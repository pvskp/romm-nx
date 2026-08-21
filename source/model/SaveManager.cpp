#include "SaveManager.hpp"
#include "ConfigManager.hpp"
#include "TicoCatalog.hpp"
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
        struct SaveRefreshJob {
            std::vector<Game> games;
            std::string romm_slug;
            std::map<int, SyncStateEntry> state;
        };

        // The most recent non-missing save wins; the server list order is not
        // guaranteed (same rule as the sync pipeline).
        const SaveEntry* NewestSave(const std::vector<SaveEntry>& saves) {
            const SaveEntry* best = nullptr;
            for (const auto& s : saves) {
                if (s.missing_from_fs) continue;
                if (!best || s.updated_at > best->updated_at) best = &s;
            }
            return best;
        }

        std::string FormatModified(time_t mtime) {
            char buf[32];
            struct tm tmv;
            localtime_r(&mtime, &tmv);
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
            return std::string(buf);
        }

    } // namespace

    SaveManager& SaveManager::Instance() {
        static SaveManager inst;
        return inst;
    }

    SaveManager::~SaveManager() {
        cancel_requested_ = true;
        if (thread_started_) {
            // A refresh can be parked on a fetch; detach rather than hang the
            // shutdown on it.
            pthread_detach(worker_thread_);
            thread_started_ = false;
        }
    }

    SaveVerdict SaveManager::ComputeVerdict(const SaveGameState& g,
                                            const SyncStateEntry* entry) {
        const SaveEntry* server = NewestSave(g.server_saves);
        const bool has_server = server != nullptr;

        if (!g.local_exists && !has_server) return SaveVerdict::None;
        if (g.local_exists && !has_server) return SaveVerdict::LocalOnly;
        if (!g.local_exists && has_server) return SaveVerdict::ServerOnly;

        // First sync with saves on both sides: the sync prompts, so the list
        // shows the unresolved state.
        const bool synced_before = entry &&
                                   entry->server_save_id != 0 &&
                                   !entry->server_save_updated_at.empty();
        if (!synced_before) return SaveVerdict::Conflict;

        const std::string local_fp = std::to_string(g.local_size) + "-" + g.local_hash;
        const bool server_changed = entry->server_save_updated_at.empty() ||
                                    server->updated_at != entry->server_save_updated_at;
        const bool local_changed = local_fp != entry->save_local_fingerprint;

        if (server_changed && local_changed) return SaveVerdict::Conflict;
        if (server_changed) return SaveVerdict::ServerNewer;
        if (local_changed) return SaveVerdict::LocalNewer;
        return SaveVerdict::InSync;
    }

    void SaveManager::RebuildLocal(const std::map<int, SyncStateEntry>& state) {
        auto& config = ConfigManager::Instance();

        for (auto& g : snapshot_.games) {
            g.local_exists = false;
            g.local_path.clear();
            g.local_size = 0;
            g.local_hash.clear();
            g.local_modified.clear();
            g.last_sync_date.clear();
            g.synced_before = false;

            const SyncStateEntry* entry = nullptr;
            auto it = state.find(g.rom_id);
            if (it != state.end()) entry = &it->second;

            if (entry) {
                g.last_sync_date = entry->server_save_updated_at;
                g.synced_before = entry->server_save_id != 0 &&
                                  !entry->server_save_updated_at.empty();

                // The save target derives from the ROM name the sync recorded.
                std::string rp = entry->rom_path;
                size_t slash = rp.find_last_of('/');
                std::string rom_name = (slash != std::string::npos) ? rp.substr(slash + 1) : rp;
                size_t dot = rom_name.find_last_of('.');
                std::string rom_base = (dot != std::string::npos) ? rom_name.substr(0, dot) : rom_name;
                if (!rom_base.empty()) {
                    const std::string target = config.GetTicoSavePath(g.platform_slug) + rom_base +
                                               ResolveTicoSaveExtension(g.tico_slug);
                    struct stat st;
                    if (stat(target.c_str(), &st) == 0 && st.st_size > 0) {
                        g.local_exists = true;
                        g.local_path = target;
                        g.local_size = (long long)st.st_size;
                        g.local_hash = SyncManager::ShortHash(target);
                        g.local_modified = FormatModified(st.st_mtime);
                    }
                }

                // A transfer records the resulting server save in sync_state.
                // If that recorded save is newer than anything the snapshot
                // fetched, the snapshot predates the transfer — an upload
                // creates a brand-new server save after the last fetch, so the
                // stale list would make the verdict read ServerNewer until the
                // next refresh. Synthesise the recorded save instead: it is a
                // copy of the local file, so its name and size are known.
                const SaveEntry* newest = NewestSave(g.server_saves);
                if (g.local_exists &&
                    entry->server_save_id != 0 && !entry->server_save_updated_at.empty() &&
                    (!newest || entry->server_save_updated_at > newest->updated_at)) {
                    SaveEntry synth;
                    synth.id = entry->server_save_id;
                    synth.rom_id = g.rom_id;
                    synth.updated_at = entry->server_save_updated_at;
                    synth.missing_from_fs = false;
                    // Same name the upload pipeline sent to the server.
                    synth.file_name = SyncManager::ServerSaveName(g.local_path);
                    synth.file_size_bytes = g.local_size;
                    g.server_saves.push_back(synth);
                    g.server_checked = true;
                    g.server_has_saves = true;
                }
            }

            g.verdict = ComputeVerdict(g, entry);
        }
    }

    void SaveManager::Refresh(const std::vector<Game>& games, const std::string& romm_slug) {
        if (refreshing_.load()) {
            // A refresh is running. Parking on it via pthread_join would freeze
            // the UI thread — input included — for the duration of the worker's
            // in-flight fetch, and that fetch cannot be aborted mid-flight.
            // Ask the worker to park on its own instead and return immediately:
            // the Save Data view re-issues this call the moment the worker
            // reports idle, so the re-target is only ever delayed by one fetch.
            cancel_requested_ = true;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.games.clear();
            snapshot_.games.reserve(games.size());
            for (const auto& game : games) {
                SaveGameState g;
                g.rom_id = game.id;
                g.title = game.title;
                g.platform_slug = romm_slug;
                g.tico_slug = ResolveTicoPlatformSlug(romm_slug);
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
        sync.GetSyncState(state);
        RebuildLocal(state);

        cancel_requested_ = false;
        refreshing_ = true;

        SaveRefreshJob* job = new SaveRefreshJob();
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
            std::cerr << "[SAVES] Failed to spawn refresh thread" << std::endl;
            return;
        }
        thread_started_ = true;
        std::cout << "[SAVES] Refresh started for " << romm_slug
                  << " games=" << games.size() << std::endl;
    }

    void SaveManager::RescanLocal() {
        if (refreshing_.load()) return; // a refresh owns the snapshot

        auto& sync = SyncManager::Instance();
        sync.LoadSyncState();
        std::map<int, SyncStateEntry> state;
        sync.GetSyncState(state);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            RebuildLocal(state);
        }
        ScreenWakeManager::Instance().RequestUpdate();
    }

    void* SaveManager::Trampoline(void* arg) {
        std::unique_ptr<SaveRefreshJob> job(static_cast<SaveRefreshJob*>(arg));
        SaveManager::Instance().Worker(job->games, job->romm_slug, job->state);
        return nullptr;
    }

    void SaveManager::Worker(std::vector<Game> games, std::string romm_slug,
                             std::map<int, SyncStateEntry> state) {
        int done = 0;
        for (const auto& game : games) {
            if (cancel_requested_.load()) break;

            auto fetch = RommApi::fetchSavesAsync(game.id);
            if (fetch) {
                while (fetch && !fetch->completed) {
                    if (cancel_requested_.load()) break;
                    svcSleepThread(10 * 1000 * 1000LL); // 10 ms
                }
                if (fetch->completed && fetch->success) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& g : snapshot_.games) {
                        if (g.rom_id != game.id) continue;
                        g.server_saves = fetch->saves;
                        g.server_checked = true;
                        g.server_has_saves = !fetch->saves.empty();
                        auto it = state.find(game.id);
                        g.verdict = ComputeVerdict(g, (it != state.end()) ? &it->second : nullptr);
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
        std::cout << "[SAVES] Refresh finished for " << romm_slug
                  << " done=" << done << "/" << games.size() << std::endl;
    }

    SavePlatformSnapshot SaveManager::GetSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    bool SaveManager::IsRefreshing() const {
        return refreshing_.load();
    }

}
