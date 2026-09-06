#include "StateManager.hpp"
#include "ConfigManager.hpp"
#include "SyncManager.hpp"
#include "FrontendTarget.hpp"
#include "RommApi.hpp"
#include "ScreenWakeManager.hpp"
#include <switch.h>
#include <sys/stat.h>
#include <ctime>
#include <iostream>
#include <memory>

namespace romm::model {

    namespace {

        // Job handed to the refresh worker thread: a copy of everything it
        // needs, so the UI can keep mutating its own state without the worker
        // seeing half-updated data.
        struct StateRefreshJob {
            std::vector<Game> games;
            std::string romm_slug;
            std::map<int, SyncStateEntry> state;
        };

        std::string FormatModified(time_t mtime) {
            char buf[32];
            struct tm tmv;
            localtime_r(&mtime, &tmv);
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
            return std::string(buf);
        }

        // The ROM base name recorded in the sync-state entry ("<rom>.<ext>"
        // -> "<rom>"), which all nine slot files derive from.
        std::string RecordRomBase(const SyncStateEntry* entry) {
            if (!entry) return "";
            std::string rp = entry->rom_path;
            size_t slash = rp.find_last_of('/');
            std::string rom_name = (slash != std::string::npos) ? rp.substr(slash + 1) : rp;
            size_t dot = rom_name.find_last_of('.');
            return (dot != std::string::npos) ? rom_name.substr(0, dot) : rom_name;
        }

    } // namespace

    StateManager& StateManager::Instance() {
        static StateManager inst;
        return inst;
    }

    StateManager::~StateManager() {
        cancel_requested_ = true;
        if (thread_started_) {
            pthread_detach(worker_thread_);
            thread_started_ = false;
        }
    }

    // Per-slot decision, mirroring SaveManager::ComputeVerdict.
    SaveVerdict StateManager::ComputeGameVerdict(const StateGameState& g,
                                                 const SyncStateEntry* entry) {
        const std::string rom_base = RecordRomBase(entry);

        // Local fingerprints per slot.
        std::map<int, std::string> local_fp;
        for (const auto& s : g.local_slots) {
            local_fp[s.slot] = std::to_string(s.size) + "-" + s.hash;
        }

        // Server states matched to slots by name.
        std::map<int, const SaveEntry*> server_by_slot;
        for (const auto& s : g.server_states) {
            if (s.missing_from_fs) continue;
            const int slot = SyncManager::MatchStateSlot(s.file_name, rom_base);
            if (slot < 0) continue;
            server_by_slot[slot] = &s;
        }

        // Slots worth a verdict: anything on either side or recorded.
        std::map<int, bool> slots;
        for (const auto& lp : local_fp) slots[lp.first] = true;
        for (const auto& sp : server_by_slot) slots[sp.first] = true;
        if (entry) {
            for (const auto& sp : entry->state_slots) slots[sp.first] = true;
        }

        bool any = false, any_local = false, any_server = false;
        bool any_local_newer = false, any_server_newer = false;
        for (const auto& slot_pair : slots) {
            const int slot = slot_pair.first;
            const bool has_local = local_fp.count(slot) != 0;
            const SaveEntry* server = server_by_slot.count(slot) ? server_by_slot[slot] : nullptr;
            const StateSlotRecord* rec = nullptr;
            if (entry) {
                auto it = entry->state_slots.find(slot);
                if (it != entry->state_slots.end()) rec = &it->second;
            }

            if (!has_local && !server) continue;

            SaveVerdict v = SaveVerdict::InSync;
            if (has_local && !server) {
                v = SaveVerdict::LocalOnly;
            } else if (!has_local && server) {
                v = SaveVerdict::ServerOnly;
            } else {
                const bool synced_before = rec && rec->server_state_id != 0 &&
                                           !rec->server_state_updated_at.empty();
                if (!synced_before) {
                    v = SaveVerdict::Conflict;
                } else {
                    const bool server_changed = rec->server_state_updated_at.empty() ||
                                                server->updated_at != rec->server_state_updated_at;
                    const bool local_changed = local_fp[slot] != rec->state_local_fingerprint;
                    if (server_changed && local_changed) v = SaveVerdict::Conflict;
                    else if (server_changed) v = SaveVerdict::ServerNewer;
                    else if (local_changed) v = SaveVerdict::LocalNewer;
                    else v = SaveVerdict::InSync;
                }
            }

            any = true;
            if (v == SaveVerdict::Conflict) return v;
            if (v == SaveVerdict::LocalOnly) any_local = true;
            if (v == SaveVerdict::ServerOnly) any_server = true;
            if (v == SaveVerdict::LocalNewer) any_local_newer = true;
            if (v == SaveVerdict::ServerNewer) any_server_newer = true;
        }

        if (any_local_newer && any_server_newer) return SaveVerdict::Conflict;
        if (any_server_newer) return SaveVerdict::ServerNewer;
        if (any_local_newer) return SaveVerdict::LocalNewer;
        if (any_local && any_server) return SaveVerdict::Conflict;
        if (any_local) return SaveVerdict::LocalOnly;
        if (any_server) return SaveVerdict::ServerOnly;
        return any ? SaveVerdict::InSync : SaveVerdict::None;
    }

    void StateManager::ReconcileServerStates(StateGameState& g,
                                             const SyncStateEntry* entry) {
        if (!entry) return;
        const std::string rom_base = RecordRomBase(entry);
        if (rom_base.empty()) return;

        std::map<int, const SaveEntry*> server_by_slot;
        for (const auto& s : g.server_states) {
            if (s.missing_from_fs) continue;
            const int slot = SyncManager::MatchStateSlot(s.file_name, rom_base);
            if (slot < 0) continue;
            server_by_slot[slot] = &s;
        }

        for (const auto& sp : entry->state_slots) {
            const int slot = sp.first;
            const StateSlotRecord& rec = sp.second;
            if (rec.server_state_id == 0 || rec.server_state_updated_at.empty()) continue;

            const SaveEntry* newest = server_by_slot.count(slot) ? server_by_slot[slot] : nullptr;
            if (newest && newest->updated_at >= rec.server_state_updated_at) continue;

            // The recorded slot is newer than everything the snapshot fetched:
            // it was just transferred. Synthesise it (it is a copy of the
            // local slot file) so the verdict reads InSync right away.
            SaveEntry synth;
            synth.id = rec.server_state_id;
            synth.rom_id = g.rom_id;
            synth.updated_at = rec.server_state_updated_at;
            synth.missing_from_fs = false;
            synth.file_name = SyncManager::StateSlotName(rom_base, slot);
            for (const auto& local : g.local_slots) {
                if (local.slot == slot) {
                    synth.file_size_bytes = local.size;
                    break;
                }
            }
            g.server_states.push_back(synth);
            g.server_checked = true;
            g.server_has_states = true;
        }
    }

    void StateManager::RebuildLocal(const std::map<int, SyncStateEntry>& state) {
        for (auto& g : snapshot_.games) {
            g.local_slots.clear();
            g.state_dir.clear();
            g.last_sync_date.clear();
            g.synced_before = false;

            const SyncStateEntry* entry = nullptr;
            auto it = state.find(g.rom_id);
            if (it != state.end()) entry = &it->second;

            if (entry) {
                g.state_dir = GetStateDirFor(active_target_, g.platform_slug);
                const std::string rom_base = RecordRomBase(entry);
                if (!rom_base.empty()) {
                    // Slot 0 is the auto-slot ("<base>.state0", or plain
                    // "<base>.state" for cores that write it that way), 1..9
                    // are the numbered slots.
                    for (int slot = 0; slot <= 9; ++slot) {
                        const std::string path =
                            SyncManager::FindLocalStatePath(g.state_dir, rom_base, slot);
                        if (path.empty()) continue;
                        struct stat st;
                        if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) continue;
                        StateSlotInfo info;
                        info.slot = slot;
                        size_t slash = path.find_last_of('/');
                        info.name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
                        info.size = (long long)st.st_size;
                        info.hash = SyncManager::ShortHash(path);
                        info.modified = FormatModified(st.st_mtime);
                        g.local_slots.push_back(info);
                    }
                }

                // Newest recorded state timestamp across slots (never-synced
                // slots record nothing).
                for (const auto& sp : entry->state_slots) {
                    if (sp.second.server_state_updated_at > g.last_sync_date) {
                        g.last_sync_date = sp.second.server_state_updated_at;
                    }
                    if (sp.second.server_state_id != 0 && !sp.second.server_state_updated_at.empty()) {
                        g.synced_before = true;
                    }
                }
            }

            ReconcileServerStates(g, entry);
            g.verdict = ComputeGameVerdict(g, entry);
        }
    }

    void StateManager::Refresh(const std::vector<Game>& games, const std::string& romm_slug) {
        if (refreshing_.load()) {
            // A refresh is running. Parking on it via pthread_join would freeze
            // the UI thread — input included — for the duration of the worker's
            // in-flight fetch. Ask the worker to park on its own instead and
            // return immediately: the State Data view re-issues this call the
            // moment the worker reports idle.
            cancel_requested_ = true;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.games.clear();
            snapshot_.games.reserve(games.size());
            for (const auto& game : games) {
                StateGameState g;
                g.rom_id = game.id;
                g.title = game.title;
                g.platform_slug = romm_slug;
                g.target = active_target_;
                snapshot_.games.push_back(g);
            }
            snapshot_.platform_slug = romm_slug;
            snapshot_.refreshing = true;
            snapshot_.done = 0;
            snapshot_.total = (int)games.size();
        }

        auto& sync = SyncManager::Instance();
        sync.LoadSyncState();
        std::map<int, SyncStateEntry> state;
        sync.GetSyncState(active_target_, state);
        RebuildLocal(state);

        cancel_requested_ = false;
        refreshing_ = true;

        StateRefreshJob* job = new StateRefreshJob();
        job->games = games;
        job->romm_slug = romm_slug;
        job->state = state;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 0x100000);
        const int rc = pthread_create(&worker_thread_, &attr, &Trampoline, job);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            delete job;
            refreshing_ = false;
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.refreshing = false;
            std::cerr << "[STATES] Failed to spawn refresh thread" << std::endl;
            return;
        }
        thread_started_ = true;
        std::cout << "[STATES] Refresh started for " << romm_slug
                  << " games=" << games.size() << std::endl;
    }

    void StateManager::RescanLocal() {
        if (refreshing_.load()) return; // a refresh owns the snapshot

        auto& sync = SyncManager::Instance();
        sync.LoadSyncState();
        std::map<int, SyncStateEntry> state;
        sync.GetSyncState(active_target_, state);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            RebuildLocal(state);
        }
        ScreenWakeManager::Instance().RequestUpdate();
    }

    void* StateManager::Trampoline(void* arg) {
        std::unique_ptr<StateRefreshJob> job(static_cast<StateRefreshJob*>(arg));
        StateManager::Instance().Worker(job->games, job->romm_slug, job->state);
        return nullptr;
    }

    void StateManager::Worker(std::vector<Game> games, std::string romm_slug,
                              std::map<int, SyncStateEntry> state) {
        int done = 0;
        for (const auto& game : games) {
            if (cancel_requested_.load()) break;

            auto fetch = RommApi::fetchStatesAsync(game.id);
            if (fetch) {
                while (fetch && !fetch->completed) {
                    if (cancel_requested_.load()) break;
                    svcSleepThread(10 * 1000 * 1000LL); // 10 ms
                }
                if (fetch->completed && fetch->success) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& g : snapshot_.games) {
                        if (g.rom_id != game.id) continue;
                        g.server_states = fetch->states;
                        g.server_checked = true;
                        g.server_has_states = !fetch->states.empty();
                        auto it = state.find(game.id);
                        const SyncStateEntry* entry = (it != state.end()) ? &it->second : nullptr;
                        ReconcileServerStates(g, entry);
                        g.verdict = ComputeGameVerdict(g, entry);
                        break;
                    }
                }
            }

            done++;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.done = done;
            }
            ScreenWakeManager::Instance().RequestUpdate();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.refreshing = false;
        }
        refreshing_ = false;
        ScreenWakeManager::Instance().RequestUpdate();
        std::cout << "[STATES] Refresh finished for " << romm_slug
                  << " done=" << done << "/" << games.size() << std::endl;
    }

    StatePlatformSnapshot StateManager::GetSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    bool StateManager::IsRefreshing() const {
        return refreshing_.load();
    }

}
