#pragma once

#include <pu/Plutonium>
#include <memory>
#include <map>
#include "../navigation/NavigationManager.hpp"
#include "../model/SaveManager.hpp"
#include "SyncModal.hpp"

namespace romm::ui {

    // Full-screen Save Data manager: a platform + game list with save verdict
    // chips (level 1) and a per-game comparison view with the server save
    // history (level 2). Renders from the NavigationManager cursor state plus
    // the SaveManager snapshot every frame; it owns no data of its own. The
    // renderer has no triangle primitive, so chevron/diamond shapes are drawn
    // with the scanline fill below.
    class SaveDataView : public pu::ui::elm::Element {
    private:
        s32 x, y, w, h;
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;

        // Level-1 row texture caches. Rows re-rasterize only when their
        // content key changes, so the refresh worker's verdict updates don't
        // rebuild the whole list every frame.
        struct RowCache {
            std::string key;
            pu::sdl2::Texture tex;
        };
        std::map<int, RowCache> row_title_texs;
        std::map<int, RowCache> row_meta_texs;
        std::map<int, RowCache> chip_texs; // per SaveVerdict
        std::string cached_list_id;        // platform slug + game count
        s32 list_scroll = 0;               // display-only row offset
        s32 plat_scroll = 0;               // display-only platform offset

        void ClearRowCaches();
        void RenderLevel1(pu::ui::render::Renderer::Ref& drawer);
        void RenderLevel2(pu::ui::render::Renderer::Ref& drawer);

    public:
        SaveDataView(s32 x, s32 y, s32 w, s32 h,
                     std::shared_ptr<romm::navigation::NavigationManager> nav);
        ~SaveDataView() override;

        s32 GetX() override { return x; }
        s32 GetY() override { return y; }
        s32 GetWidth() override { return w; }
        s32 GetHeight() override { return h; }

        void OnRender(pu::ui::render::Renderer::Ref& drawer, const s32 x_coord, const s32 y_coord) override;
        void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                     const pu::ui::TouchPoint touch_pos) override {}

        PU_SMART_CTOR(SaveDataView)
    };

    class SaveDataLayout : public pu::ui::Layout {
    private:
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;
        std::shared_ptr<SaveDataView> view;

    public:
        SaveDataLayout(std::shared_ptr<romm::navigation::NavigationManager> nav)
            : Layout::Layout(), nav_mgr(nav) {
            // Full-screen dark background, matching the other screens.
            this->SetBackgroundColor(pu::ui::Color(16, 18, 22, 255));

            view = SaveDataView::New(0, 0, 1920, 1080, nav);
            this->Add(view);

            // Sync overlay: save transfers (per game and batch) run through
            // the same progress/conflict modal as the main sync.
            auto sync_modal = romm::ui::SyncModal::New(nav);
            this->Add(sync_modal);
        }

        void OnSelectionUpdated() {}

        std::shared_ptr<SaveDataView> GetView() { return view; }

        PU_SMART_CTOR(SaveDataLayout)
    };

}
