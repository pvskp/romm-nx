#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <pthread.h>
#include "DataModel.hpp"
#include "FrontendTarget.hpp"
#include "SaveManager.hpp" // SaveVerdict

namespace romm::model {

    // One state slot on disk: which slot, and the file's fingerprint info.
    struct StateSlotInfo {
        int slot = 0;
        std::string name;     // file name on disk ("<rom>.stateN")
        long long size = 0;
        std::string hash;     // FNV-1a short hash (same as the sync)
        std::string modified; // "YYYY-MM-DD HH:MM" from mtime
    };

    // One game's save-state situation, as displayed by the State Data screen.
    struct StateGameState {
        int rom_id = 0;
        std::string title;
        std::string platform_slug; // RomM slug (paths resolve from it)
        SyncTarget target = SyncTarget::Tico; // which frontend this view reads

        // Local side (disk + sync_state.json).
        std::vector<StateSlotInfo> local_slots; // slots present on disk, sorted
        std::string state_dir;                  // states folder for this game (per target)
        // Newest recorded state updated_at across slots; empty when never synced.
        std::string last_sync_date;
        bool synced_before = false;

        // Server side (only known after a refresh).
        bool server_checked = false;
        bool server_has_states = false;
        std::vector<SaveEntry> server_states; // the server's state history

        SaveVerdict verdict = SaveVerdict::None;
    };

    // Thread-safe copy of everything the State Data screen draws.
    struct StatePlatformSnapshot {
        bool refreshing = false;
        int done = 0; // server fetches completed so far
        int total = 0;
        std::string platform_slug;
        std::vector<StateGameState> games;
    };

    // Save-state manager for the State Data screen: the per-game verdict is
    // aggregated per slot (a game has up to nine state files), everything else
    // mirrors SaveManager.
    class StateManager {
    public:
        static StateManager& Instance();

        // Which frontend's states this screen reads and writes. Switching
        // targets re-issues a Refresh from the view.
        void SetTarget(SyncTarget target) { active_target_ = target; }
        SyncTarget GetTarget() const { return active_target_; }

        // Rebuilds the platform snapshot and starts the server refresh worker.
        // While a refresh is running the call only asks the worker to park and
        // returns: re-targeting must never block the UI thread on the fetch in
        // flight, so the State Data view re-issues the request once it parks.
        void Refresh(const std::vector<Game>& games, const std::string& romm_slug);

        // Re-reads sync_state.json and the disk, keeping whatever server data
        // the last refresh fetched. Used after a state transfer.
        void RescanLocal();

        StatePlatformSnapshot GetSnapshot() const;
        bool IsRefreshing() const;

        ~StateManager();

    private:
        StateManager() = default;

        // Rebuilds the local fields + verdicts of snapshot_.games from the
        // given sync-state map. Caller holds mutex_.
        void RebuildLocal(const std::map<int, SyncStateEntry>& state);
        // Appends synthetic server entries for recorded slots the snapshot's
        // list hasn't seen yet (an upload records a state newer than the last
        // fetch). Mutates g.server_states; safe to call repeatedly.
        static void ReconcileServerStates(StateGameState& g,
                                          const SyncStateEntry* entry);
        static SaveVerdict ComputeGameVerdict(const StateGameState& g,
                                              const SyncStateEntry* entry);
        void Worker(std::vector<Game> games, std::string romm_slug,
                    std::map<int, SyncStateEntry> state);
        static void* Trampoline(void* arg);

        mutable std::mutex mutex_;
        SyncTarget active_target_ = SyncTarget::Tico;
        StatePlatformSnapshot snapshot_;
        std::atomic<bool> refreshing_{false};
        std::atomic<bool> cancel_requested_{false};
        pthread_t worker_thread_ = 0;
        bool thread_started_ = false;
    };

}
