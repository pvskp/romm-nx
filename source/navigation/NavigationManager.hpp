#pragma once

#include <pu/Plutonium>
#include "../model/DataModel.hpp"
#include "../model/SyncManager.hpp"
#include <memory>
#include <iostream>
#include <chrono>
#include <vector>
#include "../ui/SettingsLayout.hpp"

namespace romm::ui {
    class MainMenuLayout;
    class LibraryLayout;
    class DetailLayout;
    class SettingsLayout;
    class FullscreenImageLayout;
    class SaveDataLayout;
    class StateDataLayout;
}

namespace romm::navigation {

    // The sync modal opens in Options mode (force toggles + start) and moves
    // to Progress once the worker is running.
    enum class SyncModalMode {
        Options,
        Progress
    };

    enum class Screen {
        MainMenu,
        Library,
        Detail,
        Settings,
        FullscreenImage,
        SaveData,
        StateData
    };

    enum class LibraryFocus {
        Sidebar,
        Alphabet,
        Grid,
        // Detail view mode only: the right-hand panel's action row. Reached
        // with Right/A from the list, left with B.
        Panel
    };

    enum class DetailFocus {
        Actions,
        Cover
    };

    // Where the cursor sits inside the Save Data per-game view.
    enum class SaveDetailFocus {
        Local,    // the local-save card (information only)
        Server,   // the server save history list
        Actions   // SYNC / UPLOAD / DOWNLOAD
    };

    enum class CoverSize {
        Small,
        Large
    };

    struct UninstallModalPayload {
        bool active = false;
        int rom_id = 0;
        std::string platform_slug;
        std::string title;
        std::string filename;
        std::string cover_path;
        Screen source_screen;
    };

    class NavigationManager : public std::enable_shared_from_this<NavigationManager> {
    private:
        pu::ui::Application* app;
        std::shared_ptr<romm::model::DataModel> model;
        
        Screen current_screen;
        LibraryFocus library_focus;
        
        size_t selected_menu_idx;     // Main Menu active card index (0 to 6)
        size_t selected_platform_idx; // Library active platform index (focused in sidebar)
        size_t loaded_platform_idx;   // Library loaded platform index (whose ROMs are visible)
        size_t selected_game_idx;     // Library active game index
        
        size_t selected_letter_idx;   // Alphabet slider selection (0 = ALL, 1-26 = A-Z)

        // Case-insensitive title substring filter for the loaded platform.
        // Stored lowercased so the per-game comparison doesn't re-fold it;
        // search_query_display keeps what the user actually typed, for the UI.
        std::string search_query;
        std::string search_query_display;

        // Bulk-download selection, by ROM id. Scoped to the loaded platform and
        // cleared when it changes: the ids would still be valid, but the user
        // can no longer see or unselect them, and silently queueing games from
        // a platform they've left is not what R was meant to do.
        std::unordered_set<int> bulk_selection;

        // Detail-mode panel: description scroll offset in pixels, and the
        // screen the fullscreen viewer should return to. The viewer used to
        // hardcode a return to the Detail screen, which is wrong once it can be
        // opened from the library panel.
        s32 panel_desc_scroll = 0;
        Screen fullscreen_return_screen = Screen::Detail;

        // Which part of the Detail-mode panel has the cursor. Entering the
        // panel lands on the cover so A immediately opens it fullscreen;
        // Down moves to the action button.
        bool panel_on_cover = true;


        DetailFocus detail_focus;
        size_t selected_detail_action_idx; // Active detail action (0 = Sync)

        // D-pad hold-repeat state
        u64 repeat_held_button;
        std::chrono::high_resolution_clock::time_point repeat_start_time;
        std::chrono::high_resolution_clock::time_point repeat_last_time;
        std::chrono::high_resolution_clock::time_point input_cooldown_until;

        // Settings variables
        bool show_alphabet_filter = false;
        CoverSize cover_size = CoverSize::Large;
        size_t selected_settings_idx = 0;
        size_t selected_settings_category_idx = 0;
        size_t selected_settings_option_idx = 0;
        romm::ui::SettingsFocusArea settings_focus = romm::ui::SettingsFocusArea::CategoryList;

        // Library "Y-Menu" state (Search / Sort / View Mode)
        bool library_menu_active = false;
        size_t library_menu_selected_idx = 0;  // 0=Search, 1=Sort, 2=View Mode

        // Sync modal overlay (options pre-flight + progress + conflict prompt).
        bool sync_modal_active = false;
        SyncModalMode sync_modal_mode = SyncModalMode::Progress;
        size_t sync_conflict_selected_idx = 0;
        // Pre-flight options: selected row, toggles, save direction radio.
        size_t sync_option_idx = 0;
        bool sync_opt_force_rom = false;
        bool sync_opt_force_cover = false;
        bool sync_opt_background = false; // cover -> platform background
        size_t sync_opt_save_dir = 0;
        romm::model::SyncDestination sync_opt_destination = romm::model::SyncDestination::Tico;
        // Platform-wide sync intent (triggered from the library Y-Menu).
        bool sync_bulk_pending = false;
        std::string sync_bulk_platform_slug;
        std::string sync_bulk_platform_name;
        std::vector<romm::model::SyncGameEntry> sync_bulk_games;

        // --- Save Data screen state -------------------------------------
        size_t save_platform_idx = 0;    // platform under the cursor
        size_t save_game_idx = 0;        // game under the cursor
        bool save_detail_open = false;   // per-game comparison view open
        size_t save_list_focus = 0;      // 0 = game list, 1 = batch action bar
        size_t save_action_idx = 0;      // 0 = Sync all, 1 = Upload all, 2 = Download all
        int save_detail_rom_id = 0;      // the game open in the comparison view
        SaveDetailFocus save_detail_focus = SaveDetailFocus::Local;
        size_t save_detail_server_sel = 0; // selected row in the server history
        size_t save_detail_action_idx = 0; // 0 = Sync, 1 = Upload, 2 = Download
        // Which card dropped the focus into the action buttons, so Up from
        // the buttons returns to that card (Local or Server).
        SaveDetailFocus save_detail_actions_origin = SaveDetailFocus::Local;
        // Set after a transfer runs from the Save Data screen so the input
        // handler re-scans the local side once the worker finishes.
        bool save_rescan_pending = false;

        // --- State Data screen state (mirror of the Save Data screen) ----
        size_t state_platform_idx = 0;
        size_t state_game_idx = 0;
        bool state_detail_open = false;
        size_t state_list_focus = 0;     // 0 = game list, 1 = batch action bar
        size_t state_action_idx = 0;
        int state_detail_rom_id = 0;
        SaveDetailFocus state_detail_focus = SaveDetailFocus::Local;
        size_t state_detail_server_sel = 0;
        size_t state_detail_action_idx = 0;
        SaveDetailFocus state_detail_actions_origin = SaveDetailFocus::Local;
        bool state_rescan_pending = false;

        // Persistent layouts created once
        std::shared_ptr<romm::ui::MainMenuLayout> main_menu_layout;
        std::shared_ptr<romm::ui::LibraryLayout> library_layout;
        std::shared_ptr<romm::ui::DetailLayout> detail_layout;
        std::shared_ptr<romm::ui::SettingsLayout> settings_layout;
        std::shared_ptr<romm::ui::FullscreenImageLayout> fullscreen_image_layout;
        std::shared_ptr<romm::ui::SaveDataLayout> save_data_layout;
        std::shared_ptr<romm::ui::StateDataLayout> state_data_layout;

        void UpdateLayoutSelection();

    public:
        NavigationManager(pu::ui::Application* app, std::shared_ptr<romm::model::DataModel> model);
        
        void Initialize();

        // Re-applies every visible string after the UI language changes. This
        // is the one refresh path — Settings calls it, nothing else re-reads
        // translations on its own.
        void RefreshTranslations();

        void HandleInput(const u64 keys_down, const u64 keys_held);
        static std::string ShowKeyboard(const std::string& header, const std::string& subtext, const std::string& initial_text);
        
        Screen GetScreen() const { return current_screen; }
        void SetCurrentScreen(Screen s) { current_screen = s; }
        LibraryFocus GetLibraryFocus() const { return library_focus; }
        size_t GetSelectedMenuIdx() const { return selected_menu_idx; }
        size_t GetSelectedPlatformIdx() const { return selected_platform_idx; }
        size_t GetLoadedPlatformIdx() const { return loaded_platform_idx; }
        void SetLoadedPlatformIdx(size_t idx) { loaded_platform_idx = idx; }
        void SetSelectedPlatformIdx(size_t idx) { selected_platform_idx = idx; }
        size_t GetSelectedGameIdx() const { return selected_game_idx; }
        
        size_t GetSelectedLetterIdx() const { return selected_letter_idx; }
        const std::string& GetSearchQuery() const { return search_query; }
        const std::string& GetSearchQueryDisplay() const { return search_query_display; }
        // Prompts for a query and applies it. Returns true if it changed.
        bool PromptForSearch();
        void ClearSearch();

        bool IsBulkSelected(int rom_id) const { return bulk_selection.count(rom_id) > 0; }
        size_t GetBulkSelectionCount() const { return bulk_selection.size(); }
        void ToggleBulkSelection(int rom_id);
        void ClearBulkSelection() { bulk_selection.clear(); }

        bool IsPanelOnCover() const { return panel_on_cover; }
        s32 GetPanelDescScroll() const { return panel_desc_scroll; }
        void SetPanelDescScrollMax(s32 max) { if (panel_desc_scroll > max) panel_desc_scroll = max; }
        DetailFocus GetDetailFocus() const { return detail_focus; }
        size_t GetSelectedDetailActionIdx() const { return selected_detail_action_idx; }
        
        // Settings accessors
        bool ShowAlphabetFilter() const;
        void SetShowAlphabetFilter(bool show) { show_alphabet_filter = show; }
        CoverSize GetCoverSize() const { return cover_size; }
        void SetCoverSize(CoverSize size) { cover_size = size; }
        size_t GetSelectedSettingsIdx() const { return selected_settings_idx; }
        void SetSelectedSettingsIdx(size_t idx) { selected_settings_idx = idx; }
        size_t GetSelectedSettingsCategoryIdx() const { return selected_settings_category_idx; }
        void SetSelectedSettingsCategoryIdx(size_t idx) { selected_settings_category_idx = idx; }
        size_t GetSelectedSettingsOptionIdx() const { return selected_settings_option_idx; }
        void SetSelectedSettingsOptionIdx(size_t idx) { selected_settings_option_idx = idx; }
        romm::ui::SettingsFocusArea GetSettingsFocus() const { return settings_focus; }
        void SetSettingsFocus(romm::ui::SettingsFocusArea focus) { settings_focus = focus; }

        // Called after Settings > Platforms changes what's visible. Re-filters
        // the model and re-anchors the library's selection on the platforms
        // themselves rather than on stale indices, so a hidden platform can't
        // leave the cursor pointing at the wrong entry (or past the end).
        void ApplyPlatformVisibilityChange();

        std::shared_ptr<romm::ui::MainMenuLayout> GetMainMenuLayout() { return main_menu_layout; }
        std::shared_ptr<romm::ui::LibraryLayout> GetLibraryLayout() { return library_layout; }
        std::shared_ptr<romm::ui::DetailLayout> GetDetailLayout() { return detail_layout; }
        std::shared_ptr<romm::ui::SettingsLayout> GetSettingsLayout() { return settings_layout; }
        std::shared_ptr<romm::ui::FullscreenImageLayout> GetFullscreenImageLayout() { return fullscreen_image_layout; }
        std::shared_ptr<romm::model::DataModel> GetModel() { return model; }
        pu::ui::Application* GetApp() { return app; }

        // Modal Controls
        void ShowUninstallModal(const UninstallModalPayload& payload) { uninstall_modal = payload; uninstall_modal.active = true; }
        void HideUninstallModal() { uninstall_modal.active = false; }
        const UninstallModalPayload& GetUninstallModalState() const { return uninstall_modal; }
        void HandleUninstallModalInput(u64 keys_down);

        // Sync modal controls. The modal is a view over SyncManager's snapshot;
        // closing it only hides the UI, the worker keeps running. The modal
        // starts in Options mode so the user can force re-downloads/uploads
        // before the sync runs.
        bool IsSyncModalActive() const { return sync_modal_active; }
        void ShowSyncModal() { sync_modal_active = true; sync_conflict_selected_idx = 0; }
        void HideSyncModal() { sync_modal_active = false; }
        SyncModalMode GetSyncModalMode() const { return sync_modal_mode; }
        void SetSyncModalMode(SyncModalMode mode) { sync_modal_mode = mode; }
        size_t GetSyncConflictSelectedIdx() const { return sync_conflict_selected_idx; }
        void HandleSyncModalInput(u64 keys_down);

        // Force/option state of the sync pre-flight screen.
        size_t GetSyncOptionIdx() const { return sync_option_idx; }
        bool GetSyncOptionForceRom() const { return sync_opt_force_rom; }
        bool GetSyncOptionForceCover() const { return sync_opt_force_cover; }
        bool GetSyncOptionBackground() const { return sync_opt_background; }
        size_t GetSyncOptionSaveDir() const { return sync_opt_save_dir; } // 0=auto, 1=upload, 2=download
        romm::model::SyncDestination GetSyncOptionDestination() const { return sync_opt_destination; }
        void SetSyncOptionIdx(size_t idx) { sync_option_idx = idx; }

        // Platform-wide sync intent (set when opening the options from the
        // library Y-Menu; consumed when the user confirms).
        bool IsSyncBulkPending() const { return sync_bulk_pending; }
        const std::string& GetSyncBulkPlatformName() const { return sync_bulk_platform_name; }

        // Consumes a pending "mark the whole platform" request (X pressed on
        // an unloaded platform in the sidebar). Called after that platform's
        // ROMs land; marks every game and returns whether it fired. Used by
        // MainApplication once per completed ROMs fetch.
        bool ConsumePendingMarkPlatform(const std::string& platform_id,
                                        const std::vector<romm::model::Game>& games);

        // Library Y-Menu controls
        bool IsLibraryMenuActive() const { return library_menu_active; }
        size_t GetLibraryMenuSelectedIdx() const { return library_menu_selected_idx; }
        void HandleLibraryMenuInput(u64 keys_down);

        // Startup update-available popup
        bool IsUpdateModalActive() const { return update_modal_active; }
        void HandleUpdateModalInput(u64 keys_down);
        // Call once per rendered frame (works even while the user isn't
        // pressing anything, unlike HandleInput) — shows the popup the first
        // time a background update check lands on UpdateAvailable while the
        // user is on the Main Menu, once per session, unless already
        // dismissed for that exact version.
        void PollUpdateNotification();

        // --- Save Data screen ------------------------------------------
        // Opens the screen (main menu card) and kicks the first refresh.
        void OpenSaveData();
        void HandleSaveDataInput(u64 keys_down, u64 keys_effective);
        // Moves the Save Data platform cursor: fetches ROMs on demand and
        // re-targets the SaveManager refresh at the selected platform.
        void SelectSavePlatform();

        size_t GetSavePlatformIdx() const { return save_platform_idx; }
        size_t GetSaveGameIdx() const { return save_game_idx; }
        bool IsSaveDetailOpen() const { return save_detail_open; }
        size_t GetSaveListFocus() const { return save_list_focus; }
        size_t GetSaveActionIdx() const { return save_action_idx; }
        int GetSaveDetailRomId() const { return save_detail_rom_id; }
        SaveDetailFocus GetSaveDetailFocus() const { return save_detail_focus; }
        size_t GetSaveDetailServerSel() const { return save_detail_server_sel; }
        size_t GetSaveDetailActionIdx() const { return save_detail_action_idx; }
        bool GetSaveRescanPending() const { return save_rescan_pending; }

        // --- State Data screen (mirror of the Save Data screen) ----------
        void OpenStateData();
        void HandleStateDataInput(u64 keys_down, u64 keys_effective);
        void SelectStatePlatform();

        size_t GetStatePlatformIdx() const { return state_platform_idx; }
        size_t GetStateGameIdx() const { return state_game_idx; }
        bool IsStateDetailOpen() const { return state_detail_open; }
        size_t GetStateListFocus() const { return state_list_focus; }
        size_t GetStateActionIdx() const { return state_action_idx; }
        int GetStateDetailRomId() const { return state_detail_rom_id; }
        SaveDetailFocus GetStateDetailFocus() const { return state_detail_focus; }
        size_t GetStateDetailServerSel() const { return state_detail_server_sel; }
        size_t GetStateDetailActionIdx() const { return state_detail_action_idx; }
        bool GetStateRescanPending() const { return state_rescan_pending; }

    private:
        UninstallModalPayload uninstall_modal;
        bool update_modal_active = false;
        bool update_popup_shown_this_session = false;

        // Opens the sync pre-flight options for an explicit game batch
        // (marked games via ZR). The sync only starts when the user confirms.
        void OpenBulkSyncOptions(const std::string& platform_slug, const std::string& platform_name,
                                 const std::vector<romm::model::SyncGameEntry>& games);

        // X on the sidebar: mark/unmark every game of the platform under the
        // cursor. If its ROMs aren't fetched yet, loads it and auto-marks
        // everything when the fetch lands (ConsumePendingMarkPlatform).
        void MarkSelectedPlatform();

        // Sidebar hover preview: switching the cursor to another platform
        // shows its collection right away (background fetch on first hover).
        // Unlike the A handler this never moves focus off the sidebar.
        void PreviewPlatform(size_t platform_idx);

        // Pending auto-mark request (see MarkSelectedPlatform).
        bool pending_mark_platform = false;
        std::string pending_mark_platform_id;
    };

}
