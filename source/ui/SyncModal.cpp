#include "SyncModal.hpp"
#include "../model/SyncManager.hpp"
#include "../ui/DetailLayout.hpp"
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

        s32 panel_w = 900;
        // Height follows the stage-row count: a Both-destination run has up
        // to eight rows (ROM and Saves per target plus Tico's cover and
        // background), so rows compress and drop their message line to fit.
        const bool compact = snap.stages.size() > 4;
        const s32 row_h = compact ? 78 : 122;
        s32 panel_h = 168 + (s32)snap.stages.size() * row_h + 46;
        if (!snap.warning.empty()) panel_h += 48;
        if (panel_h > 1010) panel_h = 1010;
        s32 panel_x = (1920 - panel_w) / 2;
        s32 panel_y = (1080 - panel_h) / 2;

        drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255), panel_x, panel_y, panel_w, panel_h, 16);
        drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255), panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 12);

        // Title + subtitle. In platform-wide mode the header names the platform
        // and shows the running count; the game title sits below.
        std::string header = romm::i18n::tr("sync.title");
        if (snap.bulk_mode) {
            header = romm::i18n::format("sync.bulk.progress",
                                        {{"name", snap.platform_name},
                                         {"index", std::to_string(snap.bulk_index + 1)},
                                         {"total", std::to_string(snap.bulk_total)}});
        }
        pu::sdl2::Texture tex_title = pu::ui::render::RenderText("Orbitron@37", header, text_color);
        if (tex_title) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_title);
            drawer->RenderTexture(tex_title, panel_x + (panel_w - tw) / 2, panel_y + 26);
            pu::ui::render::DeleteTexture(tex_title);
        }
        pu::sdl2::Texture tex_game = pu::ui::render::RenderText("Ubuntu@22", Truncate(snap.title, 52), dim_color);
        if (tex_game) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_game);
            drawer->RenderTexture(tex_game, panel_x + (panel_w - tw) / 2, panel_y + 76);
            pu::ui::render::DeleteTexture(tex_game);
        }

        s32 row_y0 = panel_y + 168;

        // Warning banner (compressed ROM etc.) pushes the rows down.
        if (!snap.warning.empty()) {
            s32 warn_h = 56;
            drawer->RenderRoundedRectangleFill(pu::ui::Color(120, 70, 20, 255),
                                               panel_x + 30, row_y0 - 64, panel_w - 60, warn_h, 10);
            pu::sdl2::Texture tex_warn = pu::ui::render::RenderText("Ubuntu@20", Truncate(snap.warning, 96), pu::ui::Color(255, 220, 150, 255));
            if (tex_warn) {
                drawer->RenderTexture(tex_warn, panel_x + 46, row_y0 - 64 + 16);
                pu::ui::render::DeleteTexture(tex_warn);
            }
            row_y0 += 48;
        }

        // Labels come from each stage's own enum: a saves-only run draws a
        // single SAVES row (position 0), not a mislabeled ROM row.
        for (size_t i = 0; i < snap.stages.size(); ++i) {
            const auto& stage = snap.stages[i];
            const s32 ry = row_y0 + (s32)i * row_h;

            drawer->RenderRoundedRectangleFill(pu::ui::Color(28, 31, 38, 255),
                                               panel_x + 30, ry, panel_w - 60, row_h - 28, 10);

            const char* stage_key = "sync.stage.rom";
            switch (stage.stage) {
                case romm::model::SyncStage::Saves:      stage_key = "sync.stage.saves"; break;
                case romm::model::SyncStage::States:     stage_key = "sync.stage.states"; break;
                case romm::model::SyncStage::Cover:      stage_key = "sync.stage.cover"; break;
                case romm::model::SyncStage::Background: stage_key = "sync.stage.background"; break;
                default: break;
            }
            // Multi-target runs repeat stages per frontend: tag each row with
            // the destination name so the user knows which side it refers to.
            std::string label = romm::i18n::tr(stage_key);
            if (snap.stages.size() > 0) {
                size_t same_stage = 0;
                for (const auto& s : snap.stages) {
                    if (s.stage == stage.stage) ++same_stage;
                }
                if (same_stage > 1) {
                    label += std::string(" · ") + romm::model::TargetName(stage.target);
                }
            }
            pu::sdl2::Texture tex_label = pu::ui::render::RenderText("Orbitron@30", label, text_color);
            if (tex_label) {
                drawer->RenderTexture(tex_label, panel_x + 50, ry + 20);
                pu::ui::render::DeleteTexture(tex_label);
            }

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
                drawer->RenderTexture(tex_state, panel_x + panel_w - 60 - tw, ry + 22);
                pu::ui::render::DeleteTexture(tex_state);
            }

            if (!stage.message.empty() && !compact) {
                pu::sdl2::Texture tex_msg = pu::ui::render::RenderText("Ubuntu@22", Truncate(stage.message, 70), dim_color);
                if (tex_msg) {
                    drawer->RenderTexture(tex_msg, panel_x + 50, ry + 62);
                    pu::ui::render::DeleteTexture(tex_msg);
                }
            }
        }

        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22", romm::i18n::tr("sync.close"), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, panel_x + (panel_w - tw) / 2, panel_y + panel_h - 46);
            pu::ui::render::DeleteTexture(tex_hint);
        }
    }

    void SyncModal::RenderOptions(pu::ui::render::Renderer::Ref& drawer) {
        auto nav = nav_mgr.lock();
        if (!nav) return;

        pu::ui::Color text_color(237, 229, 251, 255);
        pu::ui::Color dim_color(190, 180, 225, 255);
        pu::ui::Color highlight(230, 199, 167, 255);

        s32 panel_w = 900;
        s32 panel_h = 900; // six option rows + warning banner + hint
        s32 panel_x = (1920 - panel_w) / 2;
        s32 panel_y = (1080 - panel_h) / 2;

        drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255), panel_x, panel_y, panel_w, panel_h, 16);
        drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255), panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 12);

        // Title + subtitle: game title (single) or platform name (bulk).
        const bool bulk = nav->IsSyncBulkPending();
        pu::sdl2::Texture tex_title = pu::ui::render::RenderText("Orbitron@37", romm::i18n::tr("sync.title"), text_color);
        if (tex_title) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_title);
            drawer->RenderTexture(tex_title, panel_x + (panel_w - tw) / 2, panel_y + 26);
            pu::ui::render::DeleteTexture(tex_title);
        }
        std::string subtitle;
        if (bulk) {
            subtitle = romm::i18n::format("sync.bulk.options_sub", {{"name", nav->GetSyncBulkPlatformName()}});
        } else if (auto lyt = nav->GetDetailLayout()) {
            subtitle = Truncate(lyt->ctx.title, 52);
        }
        if (!subtitle.empty()) {
            pu::sdl2::Texture tex_sub = pu::ui::render::RenderText("Ubuntu@22", subtitle, dim_color);
            if (tex_sub) {
                s32 tw = pu::ui::render::GetTextureWidth(tex_sub);
                drawer->RenderTexture(tex_sub, panel_x + (panel_w - tw) / 2, panel_y + 76);
                pu::ui::render::DeleteTexture(tex_sub);
            }
        }

        // Compressed-ROM warning only makes sense per game (single mode): the
        // platform-wide run shows per-game warnings in the progress view.
        std::string rom_name;
        if (!bulk) {
            if (auto lyt = nav->GetDetailLayout()) {
                const int rid = lyt->ctx.rom_id;
                if (auto model = nav->GetModel()) {
                    if (const auto* d = model->GetCachedDetail(rid)) {
                        if (!d->files.empty()) {
                            rom_name = d->files.front().file_name;
                        } else {
                            rom_name = d->file_name;
                        }
                    }
                }
            }
        }
        const bool compressed = !rom_name.empty() &&
                                romm::model::SyncManager::IsCompressedArchive(rom_name);
        s32 body_y = panel_y + 120;
        if (compressed) {
            const s32 warn_h = 64;
            drawer->RenderRoundedRectangleFill(pu::ui::Color(120, 70, 20, 255),
                                               panel_x + 40, body_y, panel_w - 80, warn_h, 10);
            size_t dot = rom_name.find_last_of('.');
            std::string ext = (dot != std::string::npos) ? rom_name.substr(dot) : "";
            pu::sdl2::Texture tex_warn = pu::ui::render::RenderText("Ubuntu@20",
                Truncate(romm::i18n::format("sync.warning.compressed", {{"ext", ext}}), 100),
                pu::ui::Color(255, 220, 150, 255));
            if (tex_warn) {
                drawer->RenderTexture(tex_warn, panel_x + 56, body_y + 20);
                pu::ui::render::DeleteTexture(tex_warn);
            }
            body_y += 84;
        }

        const size_t sel = nav->GetSyncOptionIdx();
        const bool force_rom = nav->GetSyncOptionForceRom();
        const bool force_cover = nav->GetSyncOptionForceCover();
        const bool background = nav->GetSyncOptionBackground();
        const size_t save_dir = nav->GetSyncOptionSaveDir();
        const romm::model::SyncDestination dest = nav->GetSyncOptionDestination();

        struct Row { const char* key; std::string value; };
        std::string dest_label = romm::model::DestinationName(dest);
        if (dest == romm::model::SyncDestination::Both) {
            dest_label = romm::i18n::tr("sync.destination.both");
        }
        const Row rows[6] = {
            { "sync.option.destination", dest_label },
            { "sync.option.force_rom",
              force_rom ? romm::i18n::tr("sync.option.on") : romm::i18n::tr("sync.option.off") },
            { "sync.option.force_cover",
              force_cover ? romm::i18n::tr("sync.option.on") : romm::i18n::tr("sync.option.off") },
            { "sync.option.background",
              background ? romm::i18n::tr("sync.option.on") : romm::i18n::tr("sync.option.off") },
            { "sync.option.saves",
              (save_dir == 1) ? romm::i18n::tr("sync.option.saves.upload")
              : (save_dir == 2) ? romm::i18n::tr("sync.option.saves.download")
              :                  romm::i18n::tr("sync.option.saves.auto") },
            { "sync.option.start", "" }
        };

        const s32 row_h = 92;
        for (size_t i = 0; i < 6; ++i) {
            const s32 ry = body_y + (s32)i * (row_h + 12);
            const bool focused = (i == sel);
            const bool is_start = (i == 5);

            pu::ui::Color border = focused ? highlight : pu::ui::Color(45, 50, 62, 255);
            pu::ui::Color bg = focused ? pu::ui::Color(85, 63, 152, 255)
                                       : pu::ui::Color(28, 31, 38, 255);
            s32 bw = panel_w - 100;
            s32 bx = panel_x + 50;
            drawer->RenderRoundedRectangleFill(border, bx, ry, bw, row_h - 12, 12);
            drawer->RenderRoundedRectangleFill(bg, bx + 4, ry + 4, bw - 8, row_h - 20, 10);

            std::string label = romm::i18n::tr(rows[i].key);
            if (is_start) {
                label.clear();
            }
            pu::sdl2::Texture tex_label = pu::ui::render::RenderText("Orbitron@30", label, text_color);
            if (tex_label) {
                drawer->RenderTexture(tex_label, bx + 24, ry + (row_h - 12 - pu::ui::render::GetTextureHeight(tex_label)) / 2);
                pu::ui::render::DeleteTexture(tex_label);
            }

            if (!is_start && !rows[i].value.empty()) {
                pu::sdl2::Texture tex_val = pu::ui::render::RenderText("Orbitron@24", rows[i].value, highlight);
                if (tex_val) {
                    s32 tw = pu::ui::render::GetTextureWidth(tex_val);
                    drawer->RenderTexture(tex_val, bx + bw - tw - 24, ry + 22);
                    pu::ui::render::DeleteTexture(tex_val);
                }
            } else if (is_start) {
                pu::sdl2::Texture tex_start = pu::ui::render::RenderText("Orbitron@30", romm::i18n::tr("sync.option.start"), text_color);
                if (tex_start) {
                    s32 tw = pu::ui::render::GetTextureWidth(tex_start);
                    drawer->RenderTexture(tex_start, bx + (bw - tw) / 2, ry + (row_h - 12 - pu::ui::render::GetTextureHeight(tex_start)) / 2);
                    pu::ui::render::DeleteTexture(tex_start);
                }
            }
        }

        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22", romm::i18n::tr("sync.option.hint"), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, panel_x + (panel_w - tw) / 2, panel_y + panel_h - 44);
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

        // Dim background
        drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 200), 0, 0, 1920, 1080);

        if (nav->GetSyncModalMode() == romm::navigation::SyncModalMode::Options) {
            RenderOptions(drawer);
            return;
        }

        const auto snap = romm::model::SyncManager::Instance().GetSnapshot();
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