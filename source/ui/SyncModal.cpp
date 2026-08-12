#include "SyncModal.hpp"
#include "../model/SyncManager.hpp"
#include "../i18n/I18n.hpp"

namespace romm::ui {

    std::string SyncModal::Truncate(const std::string& text, size_t max_chars) const {
        if (text.size() <= max_chars) return text;
        std::string out = text.substr(0, max_chars);
        return out + "...";
    }

    void SyncModal::RenderProgress(pu::ui::render::Renderer::Ref& drawer) {
        const auto snap = romm::model::SyncManager::Instance().GetSnapshot();

        pu::ui::Color text_color(237, 229, 251, 255);
        pu::ui::Color dim_color(190, 180, 225, 255);
        pu::ui::Color state_ok(110, 200, 140, 255);
        pu::ui::Color state_fail(230, 110, 110, 255);
        pu::ui::Color state_skip(170, 170, 190, 255);

        s32 panel_w = 880;
        s32 panel_h = 560;
        s32 panel_x = (1920 - panel_w) / 2;
        s32 panel_y = (1080 - panel_h) / 2;

        drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255), panel_x, panel_y, panel_w, panel_h, 16);
        drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255), panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 12);

        // Title: sync.title, plus the game title below it (server data, raw).
        pu::sdl2::Texture tex_title = pu::ui::render::RenderText("Orbitron@37", romm::i18n::tr("sync.title"), text_color);
        if (tex_title) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_title);
            drawer->RenderTexture(tex_title, panel_x + (panel_w - tw) / 2, panel_y + 30);
            pu::ui::render::DeleteTexture(tex_title);
        }
        pu::sdl2::Texture tex_game = pu::ui::render::RenderText("Ubuntu@22", Truncate(snap.title, 52), dim_color);
        if (tex_game) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_game);
            drawer->RenderTexture(tex_game, panel_x + (panel_w - tw) / 2, panel_y + 82);
            pu::ui::render::DeleteTexture(tex_game);
        }

        static const char* kStageKeys[] = {"sync.stage.rom", "sync.stage.saves", "sync.stage.cover"};
        const s32 row_y0 = panel_y + 150;
        const s32 row_h = 118;

        for (size_t i = 0; i < snap.stages.size() && i < 3; ++i) {
            const auto& stage = snap.stages[i];
            const s32 ry = row_y0 + (s32)i * row_h;

            drawer->RenderRoundedRectangleFill(pu::ui::Color(28, 31, 38, 255),
                                               panel_x + 30, ry, panel_w - 60, row_h - 24, 10);

            pu::sdl2::Texture tex_label = pu::ui::render::RenderText("Orbitron@30", romm::i18n::tr(kStageKeys[i]), text_color);
            if (tex_label) {
                drawer->RenderTexture(tex_label, panel_x + 50, ry + 22);
                pu::ui::render::DeleteTexture(tex_label);
            }

            // State badge.
            const char* state_key = "sync.state.pending";
            pu::ui::Color state_color = dim_color;
            switch (stage.state) {
                case romm::model::SyncStageState::Running:      state_key = "sync.state.running"; state_color = dim_color; break;
                case romm::model::SyncStageState::Ok:           state_key = "sync.state.ok"; state_color = state_ok; break;
                case romm::model::SyncStageState::Skipped:      state_key = "sync.state.skipped"; state_color = state_skip; break;
                case romm::model::SyncStageState::Unsupported:  state_key = "sync.state.unsupported"; state_color = state_skip; break;
                case romm::model::SyncStageState::Failed:       state_key = "sync.state.failed"; state_color = state_fail; break;
                case romm::model::SyncStageState::WaitingConflict: state_key = "sync.state.waiting"; state_color = state_fail; break;
                default: break;
            }
            pu::sdl2::Texture tex_state = pu::ui::render::RenderText("Orbitron@24", romm::i18n::tr(state_key), state_color);
            if (tex_state) {
                s32 tw = pu::ui::render::GetTextureWidth(tex_state);
                drawer->RenderTexture(tex_state, panel_x + panel_w - 60 - tw, ry + 24);
                pu::ui::render::DeleteTexture(tex_state);
            }

            // Detail message under the label.
            if (!stage.message.empty()) {
                pu::sdl2::Texture tex_msg = pu::ui::render::RenderText("Ubuntu@22", Truncate(stage.message, 68), dim_color);
                if (tex_msg) {
                    drawer->RenderTexture(tex_msg, panel_x + 50, ry + 64);
                    pu::ui::render::DeleteTexture(tex_msg);
                }
            }
        }

        // Footer hint: B closes the modal (the sync keeps running).
        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22", romm::i18n::tr("sync.close"), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, panel_x + (panel_w - tw) / 2, panel_y + panel_h - 46);
            pu::ui::render::DeleteTexture(tex_hint);
        }
    }

    void SyncModal::RenderConflict(pu::ui::render::Renderer::Ref& drawer) {
        const auto snap = romm::model::SyncManager::Instance().GetSnapshot();
        const auto& conf = snap.conflict;

        pu::ui::Color text_color(237, 229, 251, 255);
        pu::ui::Color dim_color(190, 180, 225, 255);
        pu::ui::Color highlight(230, 199, 167, 255);

        s32 panel_w = 900;
        s32 panel_h = 620;
        s32 panel_x = (1920 - panel_w) / 2;
        s32 panel_y = (1080 - panel_h) / 2;

        drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255), panel_x, panel_y, panel_w, panel_h, 16);
        drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255), panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 12);

        pu::sdl2::Texture tex_title = pu::ui::render::RenderText("Orbitron@37", romm::i18n::tr("sync.conflict.title"), text_color);
        if (tex_title) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_title);
            drawer->RenderTexture(tex_title, panel_x + (panel_w - tw) / 2, panel_y + 30);
            pu::ui::render::DeleteTexture(tex_title);
        }

        // Local side: relative save name + fingerprint.
        pu::sdl2::Texture tex_local = pu::ui::render::RenderText("Ubuntu@24",
            romm::i18n::format("sync.conflict.local",
                               {{"name", conf.target_rel_path}, {"info", conf.local_info}}),
            dim_color);
        if (tex_local) {
            drawer->RenderTexture(tex_local, panel_x + 60, panel_y + 120);
            pu::ui::render::DeleteTexture(tex_local);
        }

        // Server side: server file name + updated_at.
        pu::sdl2::Texture tex_server = pu::ui::render::RenderText("Ubuntu@24",
            romm::i18n::format("sync.conflict.server", {{"info", conf.server_info}}),
            dim_color);
        if (tex_server) {
            drawer->RenderTexture(tex_server, panel_x + 60, panel_y + 170);
            pu::ui::render::DeleteTexture(tex_server);
        }

        // Three choices; 0 = local wins, 1 = server wins, 2 = skip.
        auto nav = nav_mgr.lock();
        const size_t sel = nav ? nav->GetSyncConflictSelectedIdx() : 0;
        static const char* kKeys[] = {"sync.conflict.overwrite_local",
                                      "sync.conflict.overwrite_server",
                                      "sync.conflict.skip"};
        for (size_t i = 0; i < 3; ++i) {
            const s32 oy = panel_y + 260 + (s32)i * 92;
            const bool focused = (i == sel);

            pu::ui::Color border = focused ? highlight : pu::ui::Color(45, 50, 62, 255);
            pu::ui::Color bg = focused ? pu::ui::Color(85, 63, 152, 255)
                                       : pu::ui::Color(28, 31, 38, 255);
            s32 bw = panel_w - 120;
            drawer->RenderRoundedRectangleFill(border, panel_x + 60, oy, bw, 76, 12);
            drawer->RenderRoundedRectangleFill(bg, panel_x + 64, oy + 4, bw - 8, 68, 10);

            pu::sdl2::Texture tex_opt = pu::ui::render::RenderText("Orbitron@30", romm::i18n::tr(kKeys[i]), text_color);
            if (tex_opt) {
                s32 tw = pu::ui::render::GetTextureWidth(tex_opt);
                drawer->RenderTexture(tex_opt, panel_x + 60 + (bw - tw) / 2, oy + 20);
                pu::ui::render::DeleteTexture(tex_opt);
            }
        }

        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22", romm::i18n::tr("sync.conflict.hint"), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, panel_x + (panel_w - tw) / 2, panel_y + panel_h - 46);
            pu::ui::render::DeleteTexture(tex_hint);
        }
    }

    void SyncModal::OnRender(pu::ui::render::Renderer::Ref& drawer, const s32 x, const s32 y) {
        auto nav = nav_mgr.lock();
        if (!nav || !nav->IsSyncModalActive()) return;

        const auto snap = romm::model::SyncManager::Instance().GetSnapshot();

        // Dim background
        drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 200), 0, 0, 1920, 1080);

        // Wait one frame's worth of snapshot before drawing progress — the
        // snapshot is empty until the worker initializes it.
        if (snap.stages.empty()) return;

        if (snap.conflict.active) {
            RenderConflict(drawer);
        } else {
            RenderProgress(drawer);
        }
    }

}