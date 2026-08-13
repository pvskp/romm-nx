#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <pthread.h>
#include "DataModel.hpp"
#include "SyncManager.hpp"

namespace romm::model {

    // What the Save Data screen shows for one game. Derived from the same
    // decision table the sync pipeline uses: the server's updated_at paired
    // with the recorded local fingerprint — never the console clock, which
    // can be wrong on CFW.
    enum class SaveVerdict {
        None,        // no save on either side
        Unknown,     // server not checked yet
        InSync,      // both sides agree with the last sync
        LocalOnly,   // save only on disk
        ServerOnly,  // save only on the server
        LocalNewer,  // local changed since the last sync
        ServerNewer, // server changed since the last sync
        Conflict     // both sides changed
    };

    // One game's save situation, as displayed by the Save Data screen.
    struct SaveGameState {
        int rom_id = 0;
        std::string title;
        std::string platform_slug; // RomM slug (paths resolve from it)
        std::string tico_slug;

        // Local side (disk + sync_state.json).
        bool local_exists = false;
        std::string local_path;
        long long local_size = 0;
        std::string local_hash;     // FNV-1a short hash (same as the sync)
        std::string local_modified; // "YYYY-MM-DD HH:MM" from mtime
        // The last time this game's save was synced (server updated_at of the
        // recorded save, from sync_state.json). Empty when never synced.
        std::string last_sync_date;
        bool synced_before = false;

        // Server side (only known after a refresh).
        bool server_checked = false;
        bool server_has_saves = false;
        std::vector<SaveEntry> server_saves; // the server's save history

        SaveVerdict verdict = SaveVerdict::None;
    };

    // Thread-safe copy of everything the Save Data screen draws.
    struct SavePlatformSnapshot {
        bool refreshing = false;
        int done = 0;   // server fetches completed so far
        int total = 0;
        std::string platform_slug;
        std::vector<SaveGameState> games;
    };

    // Local-first, server-second save manager for the Save Data screen.
    // Refresh() rebuilds the local side immediately and then walks the server
    // per game (fetchSavesAsync) on a worker thread, updating the snapshot as
    // results land. Transfers themselves run through SyncManager (saves_only);
    // after one completes, RescanLocal() re-reads sync_state.json + disk so
    // fingerprints and verdicts reflect what was just moved.
    class SaveManager {
    public:
        static SaveManager& Instance();

        // Rebuilds the platform snapshot and starts the server refresh worker.
        // No-op while a refresh is running (the screen's ZR refresh).
        void Refresh(const std::vector<Game>& games, const std::string& romm_slug);

        // Re-reads sync_state.json and the disk, keeping whatever server data
        // the last refresh fetched. Used after a save transfer.
        void RescanLocal();

        SavePlatformSnapshot GetSnapshot() const;
        bool IsRefreshing() const;

        ~SaveManager();

    private:
        SaveManager() = default;

        // Rebuilds the local fields + verdicts of snapshot_.games from the
        // given sync-state map. Caller holds mutex_.
        void RebuildLocal(const std::map<int, SyncStateEntry>& state);
        static SaveVerdict ComputeVerdict(const SaveGameState& g,
                                          const SyncStateEntry* entry);
        void Worker(std::vector<Game> games, std::string romm_slug,
                    std::map<int, SyncStateEntry> state);
        static void* Trampoline(void* arg);

        mutable std::mutex mutex_;
        SavePlatformSnapshot snapshot_;
        std::atomic<bool> refreshing_{false};
        std::atomic<bool> cancel_requested_{false};
        pthread_t worker_thread_ = 0;
        bool thread_started_ = false;
    };

}
