#pragma once

#include <pu/Plutonium>
#include <memory>
#include <vector>
#include <string>
#include "../model/SavesManager.hpp"
#include "../model/DownloadManager.hpp"

namespace romm::navigation {
    class NavigationManager;
}

namespace romm::ui {

    // Which list is on screen. The screen is one long walk: platform -> game ->
    // save, with a transient game-picker for uploading files that could not be
    // matched to a game by name.
    enum class SavesLevel {
        Platform,
        Game,
        Save,
        Picker
    };

    class SavesList : public pu::ui::elm::Element {
    public:
        struct PlatRow {
            int id = 0;
            std::string name;
            std::string slug;
        };

        struct GameRow {
            int rom_id = 0;
            std::string title;
            bool has_remote = false;
            int local_count = 0;
            bool unattached = false; // synthetic bucket for unmatched local files
            std::vector<romm::model::SaveEntry> remote;
        };

        struct SaveRow {
            bool is_remote = false;
            bool is_local = false;
            int rom_id = 0; // upload target when local-only
            romm::model::SaveEntry remote;
            romm::model::LocalSaveFile local;
        };

        SavesList(s32 x, s32 y, s32 w, s32 h, std::shared_ptr<romm::navigation::NavigationManager> nav);
        ~SavesList() override;

        void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 x, const s32 y) override;
        void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override {}

        s32 GetX() override { return x; }
        s32 GetY() override { return y; }
        s32 GetWidth() override { return w; }
        s32 GetHeight() override { return h; }

        void HandleInput(const u64 keys_down, const u64 keys_held);

        // Frame poll: settles async fetches/actions and re-renders when the
        // data changed.
        void OnSelectionUpdated();
        void ForceRefresh();
        void RefreshTranslations();
        void OnLeave();

        SavesLevel GetLevel() const { return level; }
        bool AtRoot() const { return level == SavesLevel::Platform; }
        std::string GetContextHint() const;
        std::string GetBreadcrumb() const;

        PU_SMART_CTOR(SavesList)

    private:
        s32 x, y, w, h;
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;

        SavesLevel level = SavesLevel::Platform;
        size_t selected_idx = 0;
        int scroll_offset = 0;

        std::vector<PlatRow> platforms;   // Platform level
        std::vector<GameRow> games;       // Game level
        std::vector<SaveRow> saves;       // Save level
        std::vector<GameRow> picker;      // Picker level (upload target list)

        int platform_id = 0;
        std::string platform_slug;
        std::string platform_name;
        size_t active_game_idx = 0;       // game row the Save level belongs to

        // Upload pending a picker choice.
        bool picker_pending = false;
        romm::model::LocalSaveFile pending_upload;

        // Data behind the lists.
        std::vector<romm::model::SaveEntry> fetched_saves;
        std::vector<romm::model::LocalSaveFile> local_files;
        std::vector<romm::model::LocalSaveFile> unattached_files;

        // Async state
        std::shared_ptr<romm::model::PlatformSavesResult> fetch;
        bool loading = false;
        bool load_failed = false;

        // In-flight mutation (download/upload/delete)
        std::shared_ptr<romm::model::DownloadManager::SaveDownloadResult> save_dl;
        std::shared_ptr<romm::model::SaveActionResult> mutation;
        bool mutation_is_upload = false;
        std::string status_msg;

        // Y-to-delete arming: save row index armed, or -1.
        int delete_arm_idx = -1;

        bool dirty = true;

        struct RowTex {
            std::string title;
            std::string sub;
            pu::sdl2::Texture title_sel = nullptr;
            pu::sdl2::Texture title_unsel = nullptr;
            pu::sdl2::Texture sub_sel = nullptr;
            pu::sdl2::Texture sub_unsel = nullptr;
        };
        std::vector<RowTex> row_tex;
        std::string empty_text;
        std::string cached_empty_text;
        pu::sdl2::Texture empty_tex = nullptr;

        void ClearTextures();
        void RebuildRowTextures();
        void BuildPlatforms();
        void BuildGames();
        void BuildSavesForGame(size_t game_idx);
        void BuildUnattachedSaves();
        void BuildPicker();
        void RefreshAllData();
        void EnterPlatform(size_t plat_idx);
        void EnterGame(size_t game_idx);
        void PollAsync();

        void DownloadSave(size_t save_idx);
        void StartUpload(size_t save_idx);
        void ConfirmUploadTo(size_t picker_idx);
        void StartDelete(size_t save_idx);

        static std::string SizeStr(long long bytes);
        std::string RowTitle(const SaveRow& s) const;
        std::string RowSub(const SaveRow& s) const;
    };

    class SavesLayout : public pu::ui::Layout {
    private:
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;
        std::shared_ptr<SavesList> list;
        pu::ui::elm::TextBlock::Ref header_text;
        pu::ui::elm::TextBlock::Ref hint_text;
        std::string last_hint;
        std::string last_breadcrumb;

        void UpdateHeader();
        void UpdateHint();

    public:
        SavesLayout(std::shared_ptr<romm::navigation::NavigationManager> nav);

        void OnSelectionUpdated();
        void RefreshTranslations();
        void ForceRefresh();
        void HandleInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos);
        void OnLeave();
        bool AtRoot() const;
        std::string GetContextHint() const;

        PU_SMART_CTOR(SavesLayout)
    };

}
