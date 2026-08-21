#pragma once

#include <pu/Plutonium>
#include "../navigation/NavigationManager.hpp"

namespace romm::ui {

    // Fullscreen overlay drawn on top of the Detail layout while a sync is
    // in progress (or a save conflict is waiting on a decision). Reads the
    // SyncManager snapshot every frame, so it needs no poll wiring of its
    // own — closing it (B) only hides the modal; the worker keeps going.
    class SyncModal : public pu::ui::elm::Element {
    private:
        s32 x, y, w, h;
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;

        std::string Truncate(const std::string& text, size_t max_chars) const;
        void RenderOptions(pu::ui::render::Renderer::Ref& drawer);
        void RenderProgress(pu::ui::render::Renderer::Ref& drawer);
        void RenderConflict(pu::ui::render::Renderer::Ref& drawer);

    public:
        SyncModal(std::shared_ptr<romm::navigation::NavigationManager> nav)
            : Element::Element(), x(0), y(0), w(1920), h(1080), nav_mgr(nav) {}
        ~SyncModal() {}

        s32 GetX() override { return x; }
        s32 GetY() override { return y; }
        s32 GetWidth() override { return w; }
        s32 GetHeight() override { return h; }

        void OnRender(pu::ui::render::Renderer::Ref& drawer, const s32 x, const s32 y) override;
        void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override {}

        PU_SMART_CTOR(SyncModal)
    };

}