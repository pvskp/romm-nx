#pragma once

#include <pu/Plutonium>
#include <memory>
#include <map>
#include "../navigation/NavigationManager.hpp"
#include "../model/StateManager.hpp"
#include "SyncModal.hpp"

namespace romm::ui {

    // Full-screen State Data manager: the save-state twin of the Save Data
    // screen. Same two-level structure (platform + game list with verdict
    // chips, then a per-game comparison view with the server state history),
    // backed by StateManager instead of SaveManager. Renders from the
    // NavigationManager cursor state plus the StateManager snapshot every
    // frame; it owns no data of its own.
    class StateDataView : public pu::ui::elm::Element {
    private:
        s32 x, y, w, h;
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;

        // Texture caches, mirroring SaveDataView: rows re-rasterize only when
        // their content key changes.
        struct RowCache {
            std::string key;
            pu::sdl2::Texture tex;
        };
        std::map<int, RowCache> row_title_texs;
        std::map<int, RowCache> row_meta_texs;
        std::map<int, RowCache> chip_texs; // per SaveVerdict
        std::map<size_t, RowCache> plat_texs;    // per platform index
        std::map<size_t, RowCache> toolbar_texs; // per action index
        std::map<std::string, RowCache> detail_texs; // level-2, content-keyed
        int cached_detail_rom_id = -1;
        std::string cached_list_id;        // platform slug + game count
        s32 list_scroll = 0;               // display-only row offset
        s32 plat_scroll = 0;               // display-only platform offset

        void ClearRowCaches();
        void RenderLevel1(pu::ui::render::Renderer::Ref& drawer);
        void RenderLevel2(pu::ui::render::Renderer::Ref& drawer);

    public:
        StateDataView(s32 x, s32 y, s32 w, s32 h,
                      std::shared_ptr<romm::navigation::NavigationManager> nav);
        ~StateDataView() override;

        s32 GetX() override { return x; }
        s32 GetY() override { return y; }
        s32 GetWidth() override { return w; }
        s32 GetHeight() override { return h; }

        void OnRender(pu::ui::render::Renderer::Ref& drawer, const s32 x_coord, const s32 y_coord) override;
        void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                     const pu::ui::TouchPoint touch_pos) override {}

        PU_SMART_CTOR(StateDataView)
    };

    class StateDataLayout : public pu::ui::Layout {
    private:
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;
        std::shared_ptr<StateDataView> view;

    public:
        StateDataLayout(std::shared_ptr<romm::navigation::NavigationManager> nav)
            : Layout::Layout(), nav_mgr(nav) {
            // Full-screen dark background, matching the other screens.
            this->SetBackgroundColor(pu::ui::Color(16, 18, 22, 255));

            view = StateDataView::New(0, 0, 1920, 1080, nav);
            this->Add(view);

            // Sync overlay: state transfers (per game and batch) run through
            // the same progress/conflict modal as the main sync.
            auto sync_modal = romm::ui::SyncModal::New(nav);
            this->Add(sync_modal);
        }

        void OnSelectionUpdated() {}

        std::shared_ptr<StateDataView> GetView() { return view; }

        PU_SMART_CTOR(StateDataLayout)
    };

}
