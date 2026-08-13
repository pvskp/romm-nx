#include "SyncManager.hpp"
#include "ConfigManager.hpp"
#include "JsonUtil.hpp"
#include "TicoCatalog.hpp"
#include "RommApi.hpp"
#include "DownloadManager.hpp"
#include "RomPathManager.hpp"
#include "ScreenWakeManager.hpp"
#include "../i18n/I18n.hpp"
#include <switch.h>
#include <iostream>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>

namespace romm::model {

    namespace {

        // Snapshot of the job handed to the worker thread, so the UI can keep
        // mutating its own context without the worker seeing half-updated data.
        struct SyncJob {
            GameDetail detail;
            std::string platform_slug;
            std::string title;
            SyncOptions options;
        };

        struct PlatformSyncJob {
            std::string platform_slug;
            std::string platform_name;
            std::vector<SyncGameEntry> games;
            SyncOptions options;
        };

        std::string StripExtension(const std::string& filename) {
            size_t dot = filename.find_last_of('.');
            return (dot != std::string::npos) ? filename.substr(0, dot) : filename;
        }

        // Copies one small file (cover art) to another path, creating the
        // destination's parent folder. Plain FILE I/O is fine here — covers
        // are a few hundred KB at most. The parent chain is created
        // recursively (not a single mkdir): on a device where the Tico
        // assets/backgrounds folder does not exist yet, a one-level mkdir
        // silently fails and the copy is lost.
        bool CopyFile(const std::string& src, const std::string& dst) {
            FILE* in = fopen(src.c_str(), "rb");
            if (!in) return false;

            size_t slash = dst.find_last_of('/');
            if (slash != std::string::npos) {
                RomPathManager::CreateFolderIfMissing(dst.substr(0, slash));
            }

            FILE* out = fopen(dst.c_str(), "wb");
            if (!out) {
                fclose(in);
                return false;
            }

            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, n, out);
            }
            fclose(out);
            fclose(in);
            return true;
        }

        // Blocks this thread until the HTTP pool finishes a result object, or
        // the sync was cancelled (in which case we stop waiting and let the
        // pool lane finish on its own — it only writes to the result struct).
        template <typename T>
        bool WaitForCompleted(const std::shared_ptr<T>& result, std::atomic<bool>& cancel) {
            while (result && !result->completed) {
                if (cancel.load()) return false;
                svcSleepThread(10 * 1000 * 1000LL); // 10 ms
            }
            return result && !cancel.load();
        }

    } // namespace

    void* SyncManager::SyncTrampoline(void* arg) {
        std::unique_ptr<SyncJob> job(static_cast<SyncJob*>(arg));
        SyncManager::Instance().Worker(job->detail, job->platform_slug, job->title, job->options);
        return nullptr;
    }

    void* SyncManager::PlatformSyncTrampoline(void* arg) {
        std::unique_ptr<PlatformSyncJob> job(static_cast<PlatformSyncJob*>(arg));
        SyncManager::Instance().PlatformWorker(job->platform_slug, job->platform_name,
                                               job->games, job->options);
        return nullptr;
    }

    SyncManager& SyncManager::Instance() {
        static SyncManager inst;
        return inst;
    }

    SyncManager::~SyncManager() {
        CancelSync();
        if (thread_started_) {
            // The worker can be parked on the conflict prompt forever; detach
            // rather than hang the shutdown on a decision nobody can give.
            pthread_detach(worker_thread_);
            thread_started_ = false;
        }
    }

    std::string SyncManager::ShortHash(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return "0";
        uint32_t hash = 2166136261u; // FNV-1a 32-bit offset basis
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                hash ^= (unsigned char)buf[i];
                hash *= 16777619u;
            }
        }
        fclose(f);
        char out[16];
        std::snprintf(out, sizeof(out), "%08x", hash);
        return out;
    }

    std::string SyncManager::CalcFingerprint(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
            return "";
        }
        return std::to_string(st.st_size) + "-" + ShortHash(path);
    }

    bool SyncManager::IsCompressedArchive(const std::string& filename) {
        size_t dot = filename.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext;
        for (size_t i = dot; i < filename.size(); ++i) {
            ext.push_back((char)std::tolower((unsigned char)filename[i]));
        }
        static const char* kArchives[] = {".7z", ".zip", ".rar", ".gz", ".gzip",
                                          ".tar", ".bz2", ".xz", ".zst", ".lz4"};
        for (const char* a : kArchives) {
            if (ext == a) return true;
        }
        return false;
    }

    void SyncManager::LoadSyncState() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sync_state_.clear();

        std::ifstream file(kSyncStatePath);
        if (!file.is_open()) return;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        size_t pos = 0;
        while ((pos = content.find("{", pos)) != std::string::npos) {
            size_t end_pos = content.find("}", pos);
            if (end_pos == std::string::npos) break;
            std::string block = content.substr(pos, end_pos - pos + 1);

            SyncStateEntry entry;
            if (jsonExtractInt(block, "rom_id", entry.rom_id) && entry.rom_id != 0) {
                jsonExtractString(block, "platform", entry.platform);
                jsonExtractString(block, "rom_path", entry.rom_path);
                jsonExtractLongLong(block, "rom_size", entry.rom_size);
                jsonExtractString(block, "save_local_fingerprint", entry.save_local_fingerprint);
                jsonExtractInt(block, "server_save_id", entry.server_save_id);
                jsonExtractString(block, "server_save_updated_at", entry.server_save_updated_at);
                jsonExtractLongLong(block, "cover_size", entry.cover_size);
                sync_state_[entry.rom_id] = entry;
            }
            pos = end_pos + 1;
        }
        std::cout << "[SYNC] Loaded sync state entries=" << sync_state_.size() << std::endl;
    }

    void SyncManager::SaveSyncState() {
        std::lock_guard<std::mutex> lock(state_mutex_);

        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/romm-nx", 0777);

        // Every value here is a path or token, but a game folder containing a
        // quote would otherwise break the file.
        auto escape = [](const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                }
                out += c;
            }
            return out;
        };

        std::string json = "[\n";
        bool first = true;
        for (const auto& pair : sync_state_) {
            const SyncStateEntry& e = pair.second;
            if (!first) json += ",\n";
            first = false;
            json += "  {";
            json += "\"rom_id\": " + std::to_string(e.rom_id);
            json += ", \"platform\": \"" + escape(e.platform) + "\"";
            json += ", \"rom_path\": \"" + escape(e.rom_path) + "\"";
            json += ", \"rom_size\": " + std::to_string(e.rom_size);
            json += ", \"save_local_fingerprint\": \"" + escape(e.save_local_fingerprint) + "\"";
            json += ", \"server_save_id\": " + std::to_string(e.server_save_id);
            json += ", \"server_save_updated_at\": \"" + escape(e.server_save_updated_at) + "\"";
            json += ", \"cover_size\": " + std::to_string(e.cover_size);
            json += "}";
        }
        json += "\n]\n";

        FILE* f = fopen(kSyncStatePath, "w");
        if (!f) {
            std::cerr << "[SYNC] Could not write " << kSyncStatePath << std::endl;
            return;
        }
        fwrite(json.c_str(), 1, json.size(), f);
        fclose(f);
        std::cout << "[SYNC] Saved sync state entries=" << sync_state_.size() << std::endl;
    }

    void SyncManager::StartSync(const GameDetail& detail,
                                const std::string& platform_slug,
                                const std::string& title,
                                const SyncOptions& options) {
        if (worker_running_.load()) {
            std::cout << "[SYNC] Already running, ignoring StartSync for rom_id="
                      << detail.rom_id << std::endl;
            return;
        }

        LoadSyncState();
        cancel_requested_ = false;
        conflict_pending_ = false;
        conflict_skipped_ = false;

        if (thread_started_) {
            pthread_join(worker_thread_, nullptr);
            thread_started_ = false;
        }

        SyncJob* job = new SyncJob();
        job->detail = detail;
        job->platform_slug = platform_slug;
        job->title = title;
        job->options = options;

        worker_running_ = true;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 0x100000);
        const int rc = pthread_create(&worker_thread_, &attr, &SyncTrampoline, job);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            delete job;
            worker_running_ = false;
            std::cerr << "[SYNC] Failed to spawn worker thread" << std::endl;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_ = SyncSnapshot();
                snapshot_.running = false;
                snapshot_.rom_id = detail.rom_id;
                snapshot_.platform_slug = platform_slug;
                snapshot_.title = title;
                snapshot_.stages.push_back({SyncStage::Rom, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
                snapshot_.stages.push_back({SyncStage::Saves, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
                snapshot_.stages.push_back({SyncStage::Cover, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
            }
            return;
        }
        thread_started_ = true;
        std::cout << "[SYNC] Started rom_id=" << detail.rom_id << " slug=" << platform_slug << std::endl;
    }

    void SyncManager::StartPlatformSync(const std::string& platform_slug,
                                        const std::string& platform_name,
                                        const std::vector<SyncGameEntry>& games,
                                        const SyncOptions& options) {
        if (worker_running_.load()) {
            std::cout << "[SYNC] Already running, ignoring StartPlatformSync" << std::endl;
            return;
        }
        if (games.empty()) {
            std::cout << "[SYNC] Platform sync requested with no games, ignoring" << std::endl;
            return;
        }

        LoadSyncState();
        cancel_requested_ = false;
        conflict_pending_ = false;
        conflict_skipped_ = false;

        if (thread_started_) {
            pthread_join(worker_thread_, nullptr);
            thread_started_ = false;
        }

        PlatformSyncJob* job = new PlatformSyncJob();
        job->platform_slug = platform_slug;
        job->platform_name = platform_name;
        job->games = games;
        job->options = options;

        worker_running_ = true;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 0x100000);
        const int rc = pthread_create(&worker_thread_, &attr, &PlatformSyncTrampoline, job);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            delete job;
            worker_running_ = false;
            std::cerr << "[SYNC] Failed to spawn platform worker thread" << std::endl;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_ = SyncSnapshot();
                snapshot_.running = false;
                snapshot_.platform_slug = platform_slug;
                snapshot_.bulk_mode = true;
                snapshot_.bulk_total = (int)games.size();
                snapshot_.platform_name = platform_name;
                snapshot_.stages.push_back({SyncStage::Rom, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
                snapshot_.stages.push_back({SyncStage::Saves, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
                snapshot_.stages.push_back({SyncStage::Cover, SyncStageState::Failed,
                                            romm::i18n::tr("sync.error.thread")});
            }
            return;
        }
        thread_started_ = true;
        std::cout << "[SYNC] Started platform sync slug=" << platform_slug
                  << " games=" << games.size() << std::endl;
    }

    bool SyncManager::IsRunning() const {
        return worker_running_.load();
    }

    SyncSnapshot SyncManager::GetSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    void SyncManager::ResolveConflict(bool overwrite_local) {
        {
            std::lock_guard<std::mutex> lock(conflict_mutex_);
            if (!conflict_pending_) return;
            conflict_pending_ = false;
            conflict_skipped_ = false;
            conflict_overwrite_local_ = overwrite_local;
        }
        // Flip the snapshot back to progress mode immediately, so the modal
        // stops waiting while the worker carries out the upload/download.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.conflict.active = false;
        }
        conflict_cv_.notify_one();
        std::cout << "[SYNC] Conflict resolved: "
                  << (overwrite_local ? "local wins (upload)" : "server wins (download)")
                  << std::endl;
    }

    void SyncManager::SkipConflict() {
        {
            std::lock_guard<std::mutex> lock(conflict_mutex_);
            if (!conflict_pending_) return;
            conflict_pending_ = false;
            conflict_skipped_ = true;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.conflict.active = false;
        }
        conflict_cv_.notify_one();
        std::cout << "[SYNC] Conflict skipped" << std::endl;
    }

    void SyncManager::CancelSync() {
        cancel_requested_ = true;
        // Wake a worker parked on the conflict prompt so it can wind down.
        {
            std::lock_guard<std::mutex> lock(conflict_mutex_);
            if (conflict_pending_ && !conflict_skipped_) {
                conflict_pending_ = false;
                conflict_skipped_ = true;
                conflict_cv_.notify_one();
            }
        }
    }

    void SyncManager::Finish() {
        worker_running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.running = false;
            snapshot_.conflict.active = false;
        }
        ScreenWakeManager::Instance().RequestUpdate();
        std::cout << "[SYNC] Finished" << std::endl;
    }

    void SyncManager::SetStage(SyncStage stage, SyncStageState state, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t idx = StageIndex(stage);
            if (idx == (size_t)-1) {
                snapshot_.stages.push_back({stage, state, message});
            } else if (snapshot_.stages[idx].state != state || snapshot_.stages[idx].message != message) {
                snapshot_.stages[idx].state = state;
                snapshot_.stages[idx].message = message;
            }
        }
        ScreenWakeManager::Instance().RequestUpdate();
    }

    void SyncManager::SetWarning(const std::string& warning) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (snapshot_.warning != warning) {
                snapshot_.warning = warning;
            }
        }
        ScreenWakeManager::Instance().RequestUpdate();
    }

    int SyncManager::StageIndex(SyncStage stage) const {
        for (size_t i = 0; i < snapshot_.stages.size(); ++i) {
            if (snapshot_.stages[i].stage == stage) return (int)i;
        }
        return -1;
    }

    // Blocks until the user answers the conflict prompt (or the sync is
    // cancelled). The answer lands back in conflict_*.
    SyncManager::ConflictAnswer SyncManager::WaitForConflictDecision() {
        {
            std::unique_lock<std::mutex> lock(conflict_mutex_);
            conflict_cv_.wait(lock, [this] {
                return !conflict_pending_ || cancel_requested_.load();
            });
        }
        if (cancel_requested_.load()) return ConflictAnswer::Cancelled;
        if (conflict_skipped_) return ConflictAnswer::Skipped;
        return conflict_overwrite_local_ ? ConflictAnswer::OverwriteLocal
                                         : ConflictAnswer::OverwriteServer;
    }

    // Downloads one save from the server to `target` and records the result
    // in sync_state. Returns true on success.
    bool SyncManager::RunSaveDownload(const SaveEntry& save, const std::string& target,
                                      int rom_id, const std::string& tico_slug,
                                      const std::string& rom_path, long long rom_size) {
        SetStage(SyncStage::Saves, SyncStageState::Running, romm::i18n::tr("sync.saves.downloading"));

        auto& dl = DownloadManager::Instance();
        auto sr = dl.DownloadSave(save, target);
        if (!sr->success) {
            SetStage(SyncStage::Saves, SyncStageState::Failed,
                     romm::i18n::format("sync.saves.failed", {{"error", sr->error}}));
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            SyncStateEntry& e = sync_state_[rom_id];
            e.rom_id = rom_id;
            e.platform = tico_slug;
            e.rom_path = rom_path;
            e.rom_size = rom_size;
            e.save_local_fingerprint = CalcFingerprint(target);
            e.server_save_id = save.id;
            e.server_save_updated_at = save.updated_at;
        }
        SaveSyncState();

        SetStage(SyncStage::Saves, SyncStageState::Ok,
                 romm::i18n::format("sync.saves.downloaded", {{"name", save.file_name}}));
        return true;
    }

    // Uploads the local save at `target` to RomM and records the result in
    // sync_state. Returns true on success.
    bool SyncManager::RunSaveUpload(const std::string& target, int rom_id,
                                    const std::string& tico_slug, const std::string& core,
                                    const std::string& rom_path, long long rom_size) {
        SetStage(SyncStage::Saves, SyncStageState::Running, romm::i18n::tr("sync.saves.uploading"));

        size_t slash = target.find_last_of('/');
        std::string server_name = (slash != std::string::npos) ? target.substr(slash + 1) : target;

        auto res = RommApi::uploadSaveAsync(rom_id, core, true, target, server_name);
        if (!WaitForCompleted(res, cancel_requested_)) return false;
        if (!res->success) {
            SetStage(SyncStage::Saves, SyncStageState::Failed,
                     romm::i18n::format("sync.saves.failed", {{"error", res->error.empty()
                                                                         ? ("HTTP " + std::to_string(res->statusCode))
                                                                         : res->error}}));
            return false;
        }

        int new_id = 0;
        std::string new_updated;
        jsonExtractInt(res->body, "id", new_id);
        jsonExtractString(res->body, "updated_at", new_updated);

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            SyncStateEntry& e = sync_state_[rom_id];
            e.rom_id = rom_id;
            e.platform = tico_slug;
            e.rom_path = rom_path;
            e.rom_size = rom_size;
            e.save_local_fingerprint = CalcFingerprint(target);
            e.server_save_id = new_id;
            e.server_save_updated_at = new_updated;
        }
        SaveSyncState();

        SetStage(SyncStage::Saves, SyncStageState::Ok,
                 romm::i18n::format("sync.saves.uploaded", {{"name", server_name}}));
        return true;
    }

    // Full per-save decision flow (decision table in the PRD, section 5.5).
    // The worker runs this after resolving the save target path. Conflicts
    // always prompt the user — per game, single or platform-wide sync alike.
    void SyncManager::RunSavesStage(int rom_id, const std::string& tico_slug,
                                    const std::string& target, const SyncOptions& options) {
        std::string core = ResolveTicoCore(tico_slug);
        if (core.empty()) {
            SetStage(SyncStage::Saves, SyncStageState::Unsupported,
                     romm::i18n::tr("sync.saves.unsupported"));
            return;
        }

        SetStage(SyncStage::Saves, SyncStageState::Running, romm::i18n::tr("sync.saves.checking"));

        if (cancel_requested_.load()) return;

        auto fetch = RommApi::fetchSavesAsync(rom_id);
        if (!fetch) {
            SetStage(SyncStage::Saves, SyncStageState::Failed, romm::i18n::tr("sync.error.config"));
            return;
        }
        if (!WaitForCompleted(fetch, cancel_requested_)) return;
        if (!fetch->success) {
            SetStage(SyncStage::Saves, SyncStageState::Failed, romm::i18n::tr("sync.saves.fetch_failed"));
            return;
        }

        // Most recent non-missing save wins; the list order is not guaranteed.
        const SaveEntry* server_save = nullptr;
        for (const auto& s : fetch->saves) {
            if (s.missing_from_fs) continue;
            if (!server_save || s.updated_at > server_save->updated_at) {
                server_save = &s;
            }
        }

        struct stat st;
        const bool local_exists = (stat(target.c_str(), &st) == 0 && st.st_size > 0);
        const std::string local_fp = local_exists ? CalcFingerprint(target) : "";

        SyncStateEntry entry;
        bool has_entry = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = sync_state_.find(rom_id);
            if (it != sync_state_.end()) {
                entry = it->second;
                has_entry = true;
            }
        }
        // A previous sync may have recorded ROM/cover only; the save itself
        // counts as never-synced until it has both an id and a timestamp.
        const bool save_synced_before =
            has_entry && entry.server_save_id != 0 && !entry.server_save_updated_at.empty();

        const bool server_changed = server_save &&
            (entry.server_save_updated_at.empty() || server_save->updated_at != entry.server_save_updated_at);
        const bool local_changed = local_exists && local_fp != entry.save_local_fingerprint;

        // Force overrides: the user explicitly asked for a direction, so the
        // skip/conflict logic is bypassed entirely.
        if (options.force_save_upload) {
            if (local_exists) {
                RunSaveUpload(target, rom_id, tico_slug, core, entry.rom_path, entry.rom_size);
            } else {
                SetStage(SyncStage::Saves, SyncStageState::Skipped,
                         romm::i18n::tr("sync.saves.no_local"));
            }
            return;
        }
        if (options.force_save_download) {
            if (server_save) {
                RunSaveDownload(*server_save, target, rom_id, tico_slug, entry.rom_path, entry.rom_size);
            } else {
                SetStage(SyncStage::Saves, SyncStageState::Skipped,
                         romm::i18n::tr("sync.saves.no_server"));
            }
            return;
        }

        enum class SaveAction { Download, Upload, Skip, Prompt };
        SaveAction action = SaveAction::Skip;
        SaveConflictKind kind = SaveConflictKind::FirstSyncBoth;

        if (!save_synced_before) {
            // First sync of this save: never overwrite anything without asking.
            if (server_save && local_exists) {
                action = SaveAction::Prompt;
                kind = SaveConflictKind::FirstSyncBoth;
            } else if (server_save) {
                action = SaveAction::Download;
            } else if (local_exists) {
                action = SaveAction::Upload;
            } else {
                action = SaveAction::Skip;
            }
        } else {
            const bool server_gone = !server_save;
            if (server_gone) {
                action = local_changed ? SaveAction::Upload : SaveAction::Skip;
            } else if (server_changed && local_changed) {
                action = SaveAction::Prompt;
                kind = SaveConflictKind::BothNew;
            } else if (server_changed) {
                action = SaveAction::Download;
            } else if (local_changed) {
                action = SaveAction::Upload;
            } else {
                action = SaveAction::Skip;
            }
        }

        switch (action) {
            case SaveAction::Download: {
                RunSaveDownload(*server_save, target, rom_id, tico_slug, entry.rom_path, entry.rom_size);
                break;
            }
            case SaveAction::Upload: {
                RunSaveUpload(target, rom_id, tico_slug, core, entry.rom_path, entry.rom_size);
                break;
            }
            case SaveAction::Skip: {
                SetStage(SyncStage::Saves, SyncStageState::Skipped, romm::i18n::tr("sync.saves.skipped"));
                break;
            }
            case SaveAction::Prompt: {
                // The user decides every time: local or cloud wins for this
                // save, or skip it. Works in platform-wide sync too — the
                // same modal, once per conflicting game.
                SaveConflict conf;
                conf.active = true;
                conf.rom_id = rom_id;
                conf.kind = kind;
                conf.local_path = target;
                conf.local_info = local_fp;
                conf.server_info = server_save->file_name + "  (" + server_save->updated_at + ")";
                conf.server_updated_at = server_save->updated_at;
                conf.server_save_id = server_save->id;
                size_t slash = target.find_last_of('/');
                conf.target_rel_path = (slash != std::string::npos) ? target.substr(slash + 1) : target;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.conflict = conf;
                }
                {
                    std::lock_guard<std::mutex> lock(conflict_mutex_);
                    conflict_pending_ = true;
                    conflict_skipped_ = false;
                    conflict_overwrite_local_ = true;
                }

                SetStage(SyncStage::Saves, SyncStageState::WaitingConflict,
                         romm::i18n::tr("sync.conflict.waiting"));
                ScreenWakeManager::Instance().RequestUpdate();

                const ConflictAnswer answer = WaitForConflictDecision();
                if (answer == ConflictAnswer::Cancelled) return;
                if (answer == ConflictAnswer::Skipped) {
                    SetStage(SyncStage::Saves, SyncStageState::Skipped,
                             romm::i18n::tr("sync.conflict.skipped"));
                    return;
                }

                if (answer == ConflictAnswer::OverwriteLocal) {
                    RunSaveUpload(target, rom_id, tico_slug, core, entry.rom_path, entry.rom_size);
                } else {
                    RunSaveDownload(*server_save, target, rom_id, tico_slug, entry.rom_path, entry.rom_size);
                }
                break;
            }
        }
    }

    void SyncManager::ResetStages(const std::string& title, bool with_background) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.title = title;
        snapshot_.warning.clear();
        snapshot_.conflict.active = false;
        snapshot_.stages.clear();
        snapshot_.stages.push_back({SyncStage::Rom, SyncStageState::Pending, ""});
        snapshot_.stages.push_back({SyncStage::Saves, SyncStageState::Pending, ""});
        snapshot_.stages.push_back({SyncStage::Cover, SyncStageState::Pending, ""});
        // The background stage only exists when the user picked the option,
        // so the progress modal's row count matches what will actually run.
        if (with_background) {
            snapshot_.stages.push_back({SyncStage::Background, SyncStageState::Pending, ""});
        }
    }

    void SyncManager::Worker(const GameDetail& detail, const std::string& platform_slug,
                             const std::string& title, const SyncOptions& options) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = SyncSnapshot();
            snapshot_.running = true;
            snapshot_.rom_id = detail.rom_id;
            snapshot_.platform_slug = platform_slug;
            snapshot_.bulk_mode = false;
            snapshot_.bulk_index = 0;
            snapshot_.bulk_total = 1;
        }
        ResetStages(title, options.use_cover_as_background);
        ScreenWakeManager::Instance().RequestUpdate();
        RunGameSync(detail, platform_slug, title, options);
        Finish();
    }

    void SyncManager::PlatformWorker(const std::string& platform_slug,
                                     const std::string& platform_name,
                                     const std::vector<SyncGameEntry>& games,
                                     const SyncOptions& options) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = SyncSnapshot();
            snapshot_.running = true;
            snapshot_.platform_slug = platform_slug;
            snapshot_.bulk_mode = true;
            snapshot_.bulk_index = 0;
            snapshot_.bulk_total = (int)games.size();
            snapshot_.platform_name = platform_name;
        }
        ScreenWakeManager::Instance().RequestUpdate();

        for (size_t i = 0; i < games.size(); ++i) {
            if (cancel_requested_.load()) break;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.bulk_index = (int)i;
                snapshot_.rom_id = games[i].rom_id;
            }
            ResetStages(games[i].title, options.use_cover_as_background);
            ScreenWakeManager::Instance().RequestUpdate();
            std::cout << "[SYNC] Platform sync game " << (i + 1) << "/" << games.size()
                      << " rom_id=" << games[i].rom_id << " title=" << games[i].title << std::endl;

            // A batch can span several platforms (marked games collected from
            // different collections); each game resolves its folders from the
            // platform it belongs to, falling back to the batch platform.
            const std::string game_slug = games[i].platform_slug.empty()
                                              ? platform_slug
                                              : games[i].platform_slug;

            auto res = RommApi::fetchRomDetailAsync(games[i].rom_id, 0, game_slug);
            if (!res) {
                SetStage(SyncStage::Rom, SyncStageState::Failed, romm::i18n::tr("sync.error.config"));
                SetStage(SyncStage::Saves, SyncStageState::Skipped, "");
                SetStage(SyncStage::Cover, SyncStageState::Skipped, "");
                continue;
            }
            if (!WaitForCompleted(res, cancel_requested_)) {
                SetStage(SyncStage::Rom, SyncStageState::Skipped, romm::i18n::tr("sync.cancelled"));
                break;
            }
            if (!res->success) {
                SetStage(SyncStage::Rom, SyncStageState::Failed, romm::i18n::tr("sync.bulk.detail_failed"));
                SetStage(SyncStage::Saves, SyncStageState::Skipped, "");
                SetStage(SyncStage::Cover, SyncStageState::Skipped, "");
                continue;
            }
            RunGameSync(res->detail, game_slug, games[i].title, options);
        }
        Finish();
    }

    // Shared per-game pipeline (ROM -> saves -> cover) used by both the
    // single-game and the platform-wide workers. Save conflicts prompt the
    // user in both flows.
    void SyncManager::RunGameSync(const GameDetail& detail, const std::string& platform_slug,
                                  const std::string& title, const SyncOptions& options) {
        struct stat st;

        auto& config = ConfigManager::Instance();
        if (!config.IsValid()) {
            SetStage(SyncStage::Rom, SyncStageState::Failed, romm::i18n::tr("sync.error.config"));
            SetStage(SyncStage::Saves, SyncStageState::Failed, romm::i18n::tr("sync.error.config"));
            SetStage(SyncStage::Cover, SyncStageState::Failed, romm::i18n::tr("sync.error.config"));
            return;
        }

        // Multi-disc games are out of scope for v1: the whole sync is skipped.
        std::vector<RomFileEntry> files = detail.files;
        if (files.empty() && detail.file_id != 0 && !detail.file_name.empty()) {
            RomFileEntry rf;
            rf.id = detail.file_id;
            rf.file_name = detail.file_name;
            rf.file_size_bytes = detail.file_size_bytes;
            files.push_back(rf);
        }

        if (files.size() > 1) {
            SetStage(SyncStage::Rom, SyncStageState::Unsupported,
                     romm::i18n::tr("sync.multidisc"));
            SetStage(SyncStage::Saves, SyncStageState::Skipped, "");
            SetStage(SyncStage::Cover, SyncStageState::Skipped, "");
            return;
        }
        if (files.empty()) {
            SetStage(SyncStage::Rom, SyncStageState::Failed, romm::i18n::tr("sync.rom.no_files"));
            SetStage(SyncStage::Saves, SyncStageState::Skipped, "");
            SetStage(SyncStage::Cover, SyncStageState::Skipped, "");
            return;
        }

        const RomFileEntry& file = files.front();
        const std::string tico_slug = ResolveTicoPlatformSlug(platform_slug);
        auto& dl = DownloadManager::Instance();
        const std::string rom_name = dl.SanitizeFilename(file.file_name);
        const std::string rom_base = StripExtension(rom_name);
        const std::string rom_path = config.GetTicoRomPath(platform_slug) + rom_name;
        const std::string save_target = config.GetTicoSavePath(platform_slug) + rom_base +
                                        ResolveTicoSaveExtension(tico_slug);
        const std::string cover_target = config.GetTicoCoverPath(platform_slug) + rom_base + ".jpg";

        // Tico cannot run compressed archives (7z, zip, ...) — warn the user
        // to extract the ROM (e.g. with DBI) before expecting it to launch.
        if (SyncManager::IsCompressedArchive(rom_name)) {
            size_t dot = rom_name.find_last_of('.');
            std::string ext = (dot != std::string::npos) ? rom_name.substr(dot) : "";
            SetWarning(romm::i18n::format("sync.warning.compressed", {{"ext", ext}}));
        }

        // --- Stage 1: ROM -------------------------------------------------
        SetStage(SyncStage::Rom, SyncStageState::Running, romm::i18n::tr("sync.rom.downloading"));

        if (cancel_requested_.load()) {
            return;
        }

        // PSP accepts only its emulator's container formats (same whitelist as
        // the classic download pipeline).
        std::string rom_ext = "";
        {
            size_t d = rom_name.find_last_of('.');
            if (d != std::string::npos) rom_ext = rom_name.substr(d);
        }
        std::string ext_lower = rom_ext;
        for (char& c : ext_lower) c = (char)std::tolower((unsigned char)c);
        if (tico_slug == "psp" && ext_lower != ".iso" && ext_lower != ".cso" && ext_lower != ".pbp") {
            SetStage(SyncStage::Rom, SyncStageState::Unsupported,
                     romm::i18n::tr("sync.rom.unsupported_ext"));
            SetStage(SyncStage::Saves, SyncStageState::Skipped, "");
            SetStage(SyncStage::Cover, SyncStageState::Skipped, "");
            return;
        }

        bool rom_present = false;
        long long rom_present_size = 0;
        if (stat(rom_path.c_str(), &st) == 0 && st.st_size > 0) {
            rom_present = true;
            rom_present_size = (long long)st.st_size;
        }

        // Same-size ROMs are normally skipped; "force ROM" re-downloads them.
        if (!options.force_rom && rom_present && rom_present_size == file.file_size_bytes) {
            SetStage(SyncStage::Rom, SyncStageState::Skipped,
                     romm::i18n::format("sync.rom.already", {{"path", rom_path}}));
        } else {
            // Different size (or absent): overwrite via DownloadToPath, which
            // keeps the old file until the new one is fully validated.
            if (rom_present) {
                std::cout << "[SYNC] ROM exists with different size ("
                          << rom_present_size << " != " << file.file_size_bytes
                          << "), overwriting" << std::endl;
            }

            std::string url = config.GetRommHost() + "/api/roms/" + std::to_string(file.id) +
                              "/files/content/" +
                              DownloadManager::EscapeUrlComponent(file.file_name);
            std::map<std::string, std::string> headers = {
                {"Authorization", "Bearer " + config.GetApiKey()}
            };

            DownloadManager::DownloadOutcome oc = dl.DownloadToPath(url, headers, rom_path, file.file_size_bytes);
            if (!oc.success || cancel_requested_.load()) {
                if (cancel_requested_.load()) {
                    SetStage(SyncStage::Rom, SyncStageState::Skipped, romm::i18n::tr("sync.cancelled"));
                } else {
                    SetStage(SyncStage::Rom, SyncStageState::Failed,
                             romm::i18n::format("sync.rom.failed", {{"error", oc.error_message}}));
                }
            } else {
                SetStage(SyncStage::Rom, SyncStageState::Ok,
                         romm::i18n::format("sync.rom.ok", {{"path", rom_path}}));
            }
        }

        if (cancel_requested_.load()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            SyncStateEntry& e = sync_state_[detail.rom_id];
            e.rom_id = detail.rom_id;
            e.platform = tico_slug;
            e.rom_path = rom_path;
            e.rom_size = file.file_size_bytes;
        }
        SaveSyncState();

        // --- Stage 2: Saves ----------------------------------------------
        RunSavesStage(detail.rom_id, tico_slug, save_target, options);
        if (cancel_requested_.load()) {
            return;
        }

        // --- Stage 3: Cover ----------------------------------------------
        std::string raw_cover = detail.path_cover_large;
        if (raw_cover.empty()) raw_cover = detail.path_cover_small;
        if (raw_cover.empty()) {
            SetStage(SyncStage::Cover, SyncStageState::Skipped, romm::i18n::tr("sync.cover.missing"));
        } else {
            SetStage(SyncStage::Cover, SyncStageState::Running, romm::i18n::tr("sync.cover.downloading"));

            std::string cover_url = raw_cover;
            if (cover_url.find("http") != 0) {
                cover_url = config.GetRommHost() + cover_url;
            }
            size_t q = cover_url.find('?');
            if (q != std::string::npos) cover_url = cover_url.substr(0, q);

            // Skip when the cover is already on disk with the size we last
            // recorded; anything else pulls it again.
            long long prev_cover_size = 0;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto it = sync_state_.find(detail.rom_id);
                if (it != sync_state_.end()) prev_cover_size = it->second.cover_size;
            }

            bool cover_present = false;
            long long cover_present_size = 0;
            if (stat(cover_target.c_str(), &st) == 0 && st.st_size > 0) {
                cover_present = true;
                cover_present_size = (long long)st.st_size;
            }

            if (cover_present && !options.force_cover && cover_present_size == prev_cover_size) {
                SetStage(SyncStage::Cover, SyncStageState::Skipped,
                         romm::i18n::format("sync.cover.already", {{"path", cover_target}}));
            } else {
                std::map<std::string, std::string> headers = {
                    {"Authorization", "Bearer " + config.GetApiKey()}
                };
                DownloadManager::DownloadOutcome oc = dl.DownloadToPath(cover_url, headers, cover_target, 0);
                if (!oc.success || cancel_requested_.load()) {
                    if (cancel_requested_.load()) {
                        SetStage(SyncStage::Cover, SyncStageState::Skipped, romm::i18n::tr("sync.cancelled"));
                    } else {
                        SetStage(SyncStage::Cover, SyncStageState::Failed,
                                 romm::i18n::format("sync.cover.failed", {{"error", oc.error_message}}));
                    }
                } else {
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        sync_state_[detail.rom_id].cover_size = oc.final_size;
                    }
                    SaveSyncState();
                    SetStage(SyncStage::Cover, SyncStageState::Ok,
                             romm::i18n::format("sync.cover.ok", {{"path", cover_target}}));
                }
            }
        }

        // --- Stage 4: Background (optional) --------------------------------
        // Reuse this cover as the platform background (one image per platform
        // — the last synced game's cover wins). Runs for every game when the
        // option is on: even when the cover was already on disk (skipped) or
        // the server has no cover art for this game but an older cover file
        // remains on disk. In a batch the last game decides what the platform
        // shows, so gating this on any per-game condition silently lost
        // backgrounds for whole platforms.
        if (options.use_cover_as_background) {
            if (cancel_requested_.load()) {
                SetStage(SyncStage::Background, SyncStageState::Skipped,
                         romm::i18n::tr("sync.cancelled"));
            } else {
                struct stat bg_check;
                if (stat(cover_target.c_str(), &bg_check) == 0 && bg_check.st_size > 0) {
                    const std::string bg_path = config.GetTicoBackgroundPath(platform_slug);
                    SetStage(SyncStage::Background, SyncStageState::Running,
                             romm::i18n::tr("sync.background.copying"));
                    if (CopyFile(cover_target, bg_path)) {
                        SetStage(SyncStage::Background, SyncStageState::Ok,
                                 romm::i18n::format("sync.background.ok", {{"path", bg_path}}));
                    } else {
                        SetStage(SyncStage::Background, SyncStageState::Failed,
                                 romm::i18n::format("sync.background.failed", {{"path", bg_path}}));
                    }
                } else {
                    SetStage(SyncStage::Background, SyncStageState::Skipped,
                             romm::i18n::tr("sync.background.no_cover"));
                }
            }
        }
    }

}