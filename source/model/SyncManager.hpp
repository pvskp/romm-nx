#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <pthread.h>
#include "DataModel.hpp"

namespace romm::model {

    // The steps of a per-game sync, in execution order. Background only
    // exists when the "cover as platform background" option is on.
    enum class SyncStage { Rom, Saves, Cover, Background };

    enum class SyncStageState {
        Pending,
        Running,
        Ok,
        Skipped,
        Unsupported,
        Failed,
        WaitingConflict // Saves stage parked on the user's conflict decision
    };

    struct SyncStageResult {
        SyncStage stage = SyncStage::Rom;
        SyncStageState state = SyncStageState::Pending;
        std::string message; // readable detail (path, save name, error...)
    };

    enum class SaveConflictKind { ServerOnly, LocalOnly, BothNew, FirstSyncBoth };

    // A save divergence the user must decide: both sides changed since the
    // last sync (or it's the very first sync and both sides have content).
    struct SaveConflict {
        bool active = false;
        int rom_id = 0;
        SaveConflictKind kind = SaveConflictKind::FirstSyncBoth;
        std::string local_path;
        std::string local_info;   // size + short hash of the local save
        std::string server_info;  // file_name + updated_at of the server save
        std::string server_updated_at;
        int server_save_id = 0;
        std::string target_rel_path; // relative to the Tico saves folder
    };

    // Thread-safe copy of everything the sync UI needs to draw itself.
    struct SyncSnapshot {
        bool running = false;
        int rom_id = 0;
        std::string platform_slug; // RomM slug
        std::string title;         // current game title (or single-game title)
        std::vector<SyncStageResult> stages; // order: Rom, Saves, Cover
        SaveConflict conflict;
        std::string warning; // e.g. compressed-ROM notice, shown as a banner

        // Platform-wide sync (one run per game, in order).
        bool bulk_mode = false;
        int bulk_index = 0; // 0-based index of the game currently running
        int bulk_total = 0;
        std::string platform_name; // display name of the platform being synced
    };

    // One game to process inside a platform-wide sync. Carries its own
    // platform slug: a batch can span several platforms (marked games from
    // different collections), and each game must resolve its Tico folders
    // from the platform it actually belongs to.
    struct SyncGameEntry {
        int rom_id = 0;
        std::string title;
        std::string platform_slug;
    };

    // User-chosen overrides applied on this run. When any "force" flag is set
    // the corresponding stage ignores the skip/conflict logic and always
    // transfers in the requested direction. force_save_upload and
    // force_save_download are mutually exclusive (the UI is a radio).
    struct SyncOptions {
        bool force_rom = false;          // re-download the ROM even if present
        bool force_save_upload = false;  // push the local save unconditionally
        bool force_save_download = false; // pull the server save unconditionally
        bool force_cover = false;        // re-download the cover even if present
        // Also write the (downloaded) cover as this game's platform background
        // (assets/backgrounds/<platform>/<game>.jpg, one file per game).
        bool use_cover_as_background = false;
        // Save management only: skip the ROM, cover and background stages and
        // run just the saves stage. Used by the Save Data screen's per-game
        // and batch actions.
        bool saves_only = false;
    };

    // One game's record inside sync_state.json. The fingerprint pairs the
    // server's updated_at with a hash of the local file, so conflicts are
    // detected without ever trusting the console's clock.
    struct SyncStateEntry {
        int rom_id = 0;
        std::string platform;               // tico folder slug
        std::string rom_path;               // tico ROM path on the SD
        long long rom_size = 0;
        std::string save_local_fingerprint; // "<size>-<shorthash>"
        int server_save_id = 0;             // 0 = nothing uploaded/known yet
        std::string server_save_updated_at;
        long long cover_size = 0;           // size of the cover last synced
    };

    class SyncManager {
    public:
        static SyncManager& Instance();

        // Kicks off a background sync of one game (ROM -> saves -> cover).
        // No-op if a sync is already running. The UI then opens the sync modal,
        // which polls GetSnapshot() every frame.
        void StartSync(const GameDetail& detail, const std::string& platform_slug,
                       const std::string& title, const SyncOptions& options = SyncOptions());

        // Kicks off a platform-wide sync: every game in `games` is handled in
        // order (ROM -> saves -> cover each). Save conflicts prompt the user
        // exactly like a per-game sync does — the same modal, per game.
        void StartPlatformSync(const std::string& platform_slug, const std::string& platform_name,
                               const std::vector<SyncGameEntry>& games,
                               const SyncOptions& options = SyncOptions());

        // Downloads one specific server save version straight to the game's
        // save target (Save Data screen). Runs on the sync worker so the
        // progress modal shows it; stages show a single SAVES row.
        void StartSpecificSaveDownload(int rom_id, const std::string& platform_slug,
                                       const std::string& title, const SaveEntry& save);

        SyncSnapshot GetSnapshot() const;
        bool IsRunning() const;

        // Conflict prompt answers (called from the main thread's modal input).
        // overwrite_local = true  -> the local save wins (upload);
        // overwrite_local = false -> the server save wins (download).
        void ResolveConflict(bool overwrite_local);
        void SkipConflict();

        // Stops the worker between stages (in-flight transfers complete).
        void CancelSync();

        // FNV-1a 32-bit hash of a file's contents, hex-encoded (8 chars).
        static std::string ShortHash(const std::string& path);
        // "<size>-<shorthash>" for a file, "" if it doesn't exist.
        static std::string CalcFingerprint(const std::string& path);

        // True when `filename`'s extension is a compressed archive (7z, zip,
        // rar, ...). Tico can't run these directly — the UI warns the user to
        // extract (e.g. with DBI) before expecting a game to launch.
        static bool IsCompressedArchive(const std::string& filename);

        // --- sync_state.json persistence --------------------------------
        static constexpr const char* kSyncStatePath = "sdmc:/switch/romm-nx/sync_state.json";
        void LoadSyncState();
        void SaveSyncState();

        // Copy of the recorded sync-state entries, for the Save Data screen's
        // local scan (it re-reads fingerprints and server anchors per game).
        void GetSyncState(std::map<int, SyncStateEntry>& out) const;

        ~SyncManager();

    private:
        enum class ConflictAnswer { OverwriteLocal, OverwriteServer, Skipped, Cancelled };

        SyncManager() = default;

        void Worker(const GameDetail& detail, const std::string& platform_slug, const std::string& title,
                    const SyncOptions& options);
        void PlatformWorker(const std::string& platform_slug, const std::string& platform_name,
                            const std::vector<SyncGameEntry>& games, const SyncOptions& options);
        // Shared per-game pipeline (ROM -> saves -> cover), used by both the
        // single-game and the platform-wide workers.
        void RunGameSync(const GameDetail& detail, const std::string& platform_slug,
                         const std::string& title, const SyncOptions& options);
        // Rebuilds the stage list for the running game. saves_only runs keep
        // only the Saves stage (the progress modal draws whatever is there);
        // with_background adds the Background stage when the option is on.
        void ResetStages(const std::string& title, const SyncOptions& options);
        void Finish();
        void SetStage(SyncStage stage, SyncStageState state, const std::string& message);
        void SetWarning(const std::string& warning);
        int StageIndex(SyncStage stage) const;

        // Returns once the conflict prompt has been answered or the sync was
        // cancelled during the wait.
        ConflictAnswer WaitForConflictDecision();
        bool RunSaveDownload(const SaveEntry& save, const std::string& target,
                             int rom_id, const std::string& tico_slug,
                             const std::string& rom_path, long long rom_size);
        bool RunSaveUpload(const std::string& target, int rom_id,
                           const std::string& tico_slug, const std::string& core,
                           const std::string& rom_path, long long rom_size);
        void RunSavesStage(int rom_id, const std::string& tico_slug, const std::string& target,
                           const SyncOptions& options);

        static void* SyncTrampoline(void* arg);
        static void* PlatformSyncTrampoline(void* arg);
        static void* SpecificSaveTrampoline(void* arg);
        void SpecificSaveWorker(int rom_id, const std::string& platform_slug,
                                const std::string& title, const SaveEntry& save);

        mutable std::mutex mutex_;      // guards snapshot_
        SyncSnapshot snapshot_;
        std::atomic<bool> worker_running_{false};
        std::atomic<bool> cancel_requested_{false};

        // Conflict prompt signalling (worker waits, UI answers).
        std::mutex conflict_mutex_;
        std::condition_variable conflict_cv_;
        bool conflict_pending_ = false;
        bool conflict_overwrite_local_ = true;
        bool conflict_skipped_ = false;

        pthread_t worker_thread_ = 0;
        bool thread_started_ = false;

        std::map<int, SyncStateEntry> sync_state_;
        mutable std::mutex state_mutex_;
    };

}