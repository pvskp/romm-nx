#include "SaveDataLayout.hpp"
#include "../model/ConfigManager.hpp"
#include "../i18n/I18n.hpp"
#include <algorithm>
#include <cstdio>

namespace romm::ui {

    namespace {

        // --- Verdict presentation ------------------------------------------
        pu::ui::Color VerdictColor(romm::model::SaveVerdict v) {
            switch (v) {
                case romm::model::SaveVerdict::InSync:      return pu::ui::Color(110, 200, 140, 255);
                case romm::model::SaveVerdict::LocalOnly:
                case romm::model::SaveVerdict::LocalNewer:  return pu::ui::Color(230, 199, 167, 255);
                case romm::model::SaveVerdict::ServerOnly:
                case romm::model::SaveVerdict::ServerNewer: return pu::ui::Color(190, 143, 230, 255);
                case romm::model::SaveVerdict::Conflict:    return pu::ui::Color(230, 110, 110, 255);
                default:                                    return pu::ui::Color(170, 170, 190, 255);
            }
        }

        const char* VerdictKey(romm::model::SaveVerdict v) {
            switch (v) {
                case romm::model::SaveVerdict::InSync:      return "save_data.verdict.insync";
                case romm::model::SaveVerdict::LocalOnly:   return "save_data.verdict.localonly";
                case romm::model::SaveVerdict::ServerOnly:  return "save_data.verdict.serveronly";
                case romm::model::SaveVerdict::LocalNewer:  return "save_data.verdict.localnewer";
                case romm::model::SaveVerdict::ServerNewer: return "save_data.verdict.servernewer";
                case romm::model::SaveVerdict::Conflict:    return "save_data.verdict.conflict";
                case romm::model::SaveVerdict::Unknown:     return "save_data.verdict.unknown";
                default:                                    return "save_data.verdict.none";
            }
        }

        std::string FormatBytes(long long bytes) {
            char buf[32];
            if (bytes >= 1024LL * 1024LL) std::sprintf(buf, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
            else if (bytes >= 1024LL)     std::sprintf(buf, "%.0f KB", (double)bytes / 1024.0);
            else                          std::sprintf(buf, "%lld B", bytes);
            return std::string(buf);
        }

        // The server list order is not guaranteed; newest first for display.
        std::vector<romm::model::SaveEntry> NewestFirst(std::vector<romm::model::SaveEntry> saves) {
            std::sort(saves.begin(), saves.end(), [](const romm::model::SaveEntry& a, const romm::model::SaveEntry& b) {
                return a.updated_at > b.updated_at;
            });
            return saves;
        }

        // Scanline triangle fill — the renderer has no polygon primitive.
        void FillTriangle(pu::ui::render::Renderer::Ref& drawer,
                          s32 x1, s32 y1, s32 x2, s32 y2, s32 x3, s32 y3,
                          pu::ui::Color color) {
            const s32 min_y = std::min(y1, std::min(y2, y3));
            const s32 max_y = std::max(y1, std::max(y2, y3));
            if (min_y == max_y) return;
            for (s32 y = min_y; y <= max_y; ++y) {
                s32 span_min = 0x7FFFFFFF;
                s32 span_max = -0x7FFFFFFF;
                const s32 edges[3][4] = {
                    { x1, y1, x2, y2 },
                    { x2, y2, x3, y3 },
                    { x3, y3, x1, y1 }
                };
                for (int e = 0; e < 3; ++e) {
                    const s32 ax = edges[e][0], ay = edges[e][1];
                    const s32 bx = edges[e][2], by = edges[e][3];
                    if (ay == by) continue;
                    if ((y < ay && y < by) || (y > ay && y > by)) continue;
                    const s32 ex = ax + (s32)(((int64_t)(y - ay) * (bx - ax)) / (by - ay));
                    if (ex < span_min) span_min = ex;
                    if (ex > span_max) span_max = ex;
                }
                if (span_min <= span_max) {
                    drawer->RenderRectangleFill(color, span_min, y, span_max - span_min + 1, 1);
                }
            }
        }

        void FillDiamond(pu::ui::render::Renderer::Ref& drawer, s32 cx, s32 cy,
                         s32 radius, pu::ui::Color color) {
            FillTriangle(drawer, cx, cy - radius, cx - radius, cy, cx + radius, cy, color);
            FillTriangle(drawer, cx, cy + radius, cx - radius, cy, cx + radius, cy, color);
        }

    } // namespace

    SaveDataView::SaveDataView(s32 x, s32 y, s32 w, s32 h,
                               std::shared_ptr<romm::navigation::NavigationManager> nav)
        : Element(), x(x), y(y), w(w), h(h), nav_mgr(nav) {}

    SaveDataView::~SaveDataView() {
        ClearRowCaches();
    }

    void SaveDataView::ClearRowCaches() {
        for (auto& entry : row_title_texs) {
            if (entry.second.tex) pu::ui::render::DeleteTexture(entry.second.tex);
        }
        row_title_texs.clear();
        for (auto& entry : row_meta_texs) {
            if (entry.second.tex) pu::ui::render::DeleteTexture(entry.second.tex);
        }
        row_meta_texs.clear();
        for (auto& entry : chip_texs) {
            if (entry.second.tex) pu::ui::render::DeleteTexture(entry.second.tex);
        }
        chip_texs.clear();
    }

    // --- Level 1: platform + game list ------------------------------------

    void SaveDataView::RenderLevel1(pu::ui::render::Renderer::Ref& drawer) {
        auto nav = nav_mgr.lock();
        if (!nav) return;
        auto model = nav->GetModel();
        if (!model) return;

        const auto& platforms = model->GetPlatforms();
        if (platforms.empty()) {
            pu::sdl2::Texture tex = pu::ui::render::RenderText(
                "Ubuntu@30", romm::i18n::tr("save_data.empty.platforms"),
                pu::ui::Color(190, 180, 225, 255));
            if (tex) {
                s32 tw = pu::ui::render::GetTextureWidth(tex);
                drawer->RenderTexture(tex, (1920 - tw) / 2, 500);
                pu::ui::render::DeleteTexture(tex);
            }
            return;
        }

        const size_t plat_idx = std::min(nav->GetSavePlatformIdx(), platforms.size() - 1);
        const auto& plat = platforms[plat_idx];
        const auto snap = romm::model::SaveManager::Instance().GetSnapshot();
        const size_t game_count = plat.games.size();

        // Keep the SaveManager snapshot aligned with the model's game list:
        // the ROM fetch may land after the screen opened, or a platform
        // switch may have happened. Refreshing runs are never interrupted.
        if (!romm::model::SaveManager::Instance().IsRefreshing() &&
            snap.games.size() != game_count) {
            romm::model::SaveManager::Instance().Refresh(plat.games, plat.slug);
        }

        pu::ui::Color text_color(237, 229, 251, 255);
        pu::ui::Color dim_color(190, 180, 225, 255);
        pu::ui::Color highlight(230, 199, 167, 255);

        // --- Header --------------------------------------------------------
        pu::sdl2::Texture tex_title = pu::ui::render::RenderText(
            "Orbitron@45", romm::i18n::tr("save_data.title"), text_color);
        if (tex_title) {
            drawer->RenderTexture(tex_title, 60, 40);
            pu::ui::render::DeleteTexture(tex_title);
        }
        const std::string subtitle = romm::i18n::format("save_data.platform_of", {
            {"name", plat.name},
            {"count", std::to_string(game_count)}
        });
        pu::sdl2::Texture tex_sub = pu::ui::render::RenderText("Ubuntu@24", subtitle, dim_color);
        if (tex_sub) {
            drawer->RenderTexture(tex_sub, 60, 100);
            pu::ui::render::DeleteTexture(tex_sub);
        }

        // Refresh progress (top right).
        std::string status;
        if (snap.refreshing) {
            status = romm::i18n::format("save_data.refreshing", {
                {"done", std::to_string(snap.done)},
                {"total", std::to_string(snap.total)}
            });
        } else {
            status = romm::i18n::format("save_data.refresh_done", {
                {"total", std::to_string(snap.total)}
            });
        }
        pu::sdl2::Texture tex_status = pu::ui::render::RenderText("Ubuntu@22", status, dim_color);
        if (tex_status) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_status);
            drawer->RenderTexture(tex_status, 1860 - tw, 52);
            pu::ui::render::DeleteTexture(tex_status);
        }
        if (snap.refreshing && snap.total > 0) {
            const s32 bar_w = 360;
            const s32 bar_x = 1500;
            const s32 bar_y = 90;
            drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255),
                                               bar_x, bar_y, bar_w, 10, 5);
            const s32 fill = (s32)((long long)bar_w * snap.done / snap.total);
            if (fill > 0) {
                drawer->RenderRoundedRectangleFill(pu::ui::Color(120, 85, 220, 255),
                                                   bar_x, bar_y, fill, 10, 5);
            }
        }

        // --- Batch action toolbar (top of the right column) -----------------
        // Always visible above the game list, so the batch actions are never
        // hidden behind scroll position. Focused with Down/Right from the
        // list, or Up from the list's top row.
        const bool toolbar_focused = (nav->GetSaveListFocus() == 2);
        constexpr s32 bar_y = 128;
        constexpr s32 btn_w = 280;
        constexpr s32 btn_h = 62;
        constexpr s32 btn_gap = 24;
        static const char* kActionKeys[] = {"save_data.action.sync_all",
                                            "save_data.action.upload_all",
                                            "save_data.action.download_all"};
        for (size_t i = 0; i < 3; ++i) {
            const s32 bx = 500 + (s32)i * (btn_w + btn_gap);
            const bool active = (nav->GetSaveActionIdx() == i);
            pu::ui::Color border = toolbar_focused ? highlight : pu::ui::Color(45, 50, 62, 255);
            pu::ui::Color bg(28, 31, 38, 255);
            if (toolbar_focused && active) {
                border = highlight;
                bg = pu::ui::Color(85, 63, 152, 255);
            }
            drawer->RenderRoundedRectangleFill(border, bx, bar_y, btn_w, btn_h, 12);
            drawer->RenderRoundedRectangleFill(bg, bx + 4, bar_y + 4, btn_w - 8, btn_h - 8, 10);
            pu::sdl2::Texture tex = pu::ui::render::RenderText(
                "Orbitron@24", romm::i18n::tr(kActionKeys[i]),
                (toolbar_focused && active) ? highlight : text_color);
            if (tex) {
                s32 tw = pu::ui::render::GetTextureWidth(tex);
                s32 th = pu::ui::render::GetTextureHeight(tex);
                drawer->RenderTexture(tex, bx + (btn_w - tw) / 2, bar_y + (btn_h - th) / 2);
                pu::ui::render::DeleteTexture(tex);
            }
        }

        // --- Platform list (left) -------------------------------------------
        constexpr s32 plat_x = 60;
        constexpr s32 plat_y = 210;
        constexpr s32 plat_w = 400;
        constexpr s32 plat_h = 730;
        constexpr s32 plat_row_h = 68;
        constexpr s32 plat_visible = 10;
        const bool plat_focused = (nav->GetSaveListFocus() == 0);
        if ((s32)plat_idx < plat_scroll) plat_scroll = (s32)plat_idx;
        if ((s32)plat_idx >= plat_scroll + plat_visible) {
            plat_scroll = (s32)plat_idx - plat_visible + 1;
        }
        for (size_t i = 0; i < platforms.size(); ++i) {
            const s32 row_y = plat_y + (s32)(i - plat_scroll) * plat_row_h;
            if (row_y < plat_y || row_y + plat_row_h > plat_y + plat_h) continue;
            const bool sel = (i == plat_idx);
            if (sel) {
                pu::ui::Color border = plat_focused ? highlight : pu::ui::Color(45, 50, 62, 255);
                drawer->RenderRoundedRectangleFill(border, plat_x, row_y, plat_w, plat_row_h - 6, 10);
                drawer->RenderRoundedRectangleFill(
                    pu::ui::Color(85, 63, 152, plat_focused ? 255 : 130),
                    plat_x + (plat_focused ? 4 : 0), row_y + (plat_focused ? 4 : 0),
                    plat_w - (plat_focused ? 8 : 0), plat_row_h - 6 - (plat_focused ? 8 : 0), 8);
            }
            // Long system names are truncated to the strip; unclipped they
            // would bleed into the game list's text.
            pu::sdl2::Texture tex = pu::ui::render::RenderText(
                "Ubuntu@30", platforms[i].name,
                sel ? highlight : dim_color, plat_w - 40);
            if (tex) {
                drawer->RenderTexture(tex, plat_x + 20, row_y + (plat_row_h - 6 - pu::ui::render::GetTextureHeight(tex)) / 2);
                pu::ui::render::DeleteTexture(tex);
            }
        }

        // --- Game list (right) ------------------------------------------------
        constexpr s32 list_x = 500;
        constexpr s32 list_y = 210;
        constexpr s32 list_w = 1360;
        constexpr s32 row_h = 84;
        constexpr s32 visible = 8;
        const bool list_focused = (nav->GetSaveListFocus() == 1);
        const size_t sel = std::min(nav->GetSaveGameIdx(), game_count > 0 ? game_count - 1 : 0);
        if (game_count > 0) {
            if (sel < (size_t)list_scroll) list_scroll = (s32)sel;
            if ((size_t)list_scroll + visible > game_count) {
                list_scroll = (int)game_count - visible;
                if (list_scroll < 0) list_scroll = 0;
            }
            if ((s32)sel >= list_scroll + visible) {
                list_scroll = (s32)sel - visible + 1;
            }
        } else {
            list_scroll = 0;
            pu::sdl2::Texture tex = pu::ui::render::RenderText(
                "Ubuntu@30", romm::i18n::tr("save_data.empty.games"), dim_color);
            if (tex) {
                s32 tw = pu::ui::render::GetTextureWidth(tex);
                drawer->RenderTexture(tex, list_x + (list_w - tw) / 2, list_y + 200);
                pu::ui::render::DeleteTexture(tex);
            }
        }

        // Build a lookup from rom_id -> SaveGameState for the visible rows.
        std::map<int, const romm::model::SaveGameState*> state_by_id;
        for (const auto& g : snap.games) {
            state_by_id[g.rom_id] = &g;
        }

        // Hint follows the focus zone, like the Games screen's footer.
        const char* hint_key = "save_data.hint.platform";
        if (nav->GetSaveListFocus() == 1) {
            hint_key = "save_data.hint.games";
        } else if (nav->GetSaveListFocus() == 2) {
            hint_key = "save_data.hint.toolbar";
        }
        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22",
            romm::i18n::tr(hint_key), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, 1920 - 40 - tw, 1030);
            pu::ui::render::DeleteTexture(tex_hint);
        }

        // Render only the visible rows (the list can hold hundreds of games).
        for (size_t i = 0; i < visible; ++i) {
            const size_t idx = (size_t)list_scroll + i;
            if (idx >= game_count) break;
            const auto& game = plat.games[idx];
            const auto* st = state_by_id.count(game.id) ? state_by_id[game.id] : nullptr;
            const bool selected = (idx == sel);
            const s32 ry = list_y + (s32)i * row_h;

            if (selected) {
                pu::ui::Color bg = list_focused ? pu::ui::Color(85, 63, 152, 255)
                                                : pu::ui::Color(85, 63, 152, 130);
                pu::ui::Color border = list_focused ? highlight : pu::ui::Color(45, 50, 62, 255);
                drawer->RenderRoundedRectangleFill(border, list_x, ry, list_w, row_h - 8, 12);
                drawer->RenderRoundedRectangleFill(bg, list_x + 4, ry + 4, list_w - 8, row_h - 16, 10);
            }

            // Title (cached per row; color depends on selection). The ROM's
            // name is the row's identity — without it the verdict chips and
            // meta lines are unreadable.
            const pu::ui::Color title_color = selected ? highlight : text_color;
            std::string title_key = std::to_string(game.id) + "|" + game.title + "|" +
                                    (selected ? "1" : "0");
            auto tit = row_title_texs.find(game.id);
            if (tit == row_title_texs.end() || tit->second.key != title_key) {
                if (tit != row_title_texs.end() && tit->second.tex) {
                    pu::ui::render::DeleteTexture(tit->second.tex);
                }
                pu::sdl2::Texture tex = pu::ui::render::RenderText(
                    "Ubuntu@26", game.title, title_color, list_w - 340);
                row_title_texs[game.id] = { title_key, tex };
            }
            if (row_title_texs[game.id].tex) {
                drawer->RenderTexture(row_title_texs[game.id].tex, list_x + 24, ry + 10);
            }

            // Meta line (cached): local save name + info, then server date.
            std::string meta;
            if (st && st->local_exists) {
                std::string fname = st->local_path;
                size_t slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                meta = fname + "  ·  " + FormatBytes(st->local_size) + "  ·  " + st->local_hash;
            } else {
                meta = romm::i18n::tr("save_data.meta.no_local");
            }
            if (st && st->server_checked && st->server_has_saves) {
                const romm::model::SaveEntry* newest = nullptr;
                for (const auto& s : st->server_saves) {
                    if (s.missing_from_fs) continue;
                    if (!newest || s.updated_at > newest->updated_at) newest = &s;
                }
                if (newest) {
                    meta += "  ·  " + romm::i18n::format("save_data.meta.server",
                                                         {{"date", newest->updated_at}});
                }
            }
            std::string meta_key = std::to_string(game.id) + "|" + meta;
            auto met = row_meta_texs.find(game.id);
            if (met == row_meta_texs.end() || met->second.key != meta_key) {
                if (met != row_meta_texs.end() && met->second.tex) {
                    pu::ui::render::DeleteTexture(met->second.tex);
                }
                pu::sdl2::Texture tex = pu::ui::render::RenderText(
                    "Ubuntu@20", meta, dim_color, list_w - 340);
                row_meta_texs[game.id] = { meta_key, tex };
            }
            if (row_meta_texs[game.id].tex) {
                drawer->RenderTexture(row_meta_texs[game.id].tex, list_x + 24, ry + 48);
            }

            // Verdict chip (cached per verdict).
            const romm::model::SaveVerdict verdict = st ? st->verdict : romm::model::SaveVerdict::None;
            const std::string chip_label = romm::i18n::tr(VerdictKey(verdict));
            auto chip = chip_texs.find((int)verdict);
            if (chip == chip_texs.end() || chip->second.key != chip_label) {
                if (chip != chip_texs.end() && chip->second.tex) {
                    pu::ui::render::DeleteTexture(chip->second.tex);
                }
                pu::sdl2::Texture tex = pu::ui::render::RenderText(
                    "Orbitron@24", chip_label, VerdictColor(verdict));
                chip_texs[(int)verdict] = { chip_label, tex };
            }
            if (chip_texs[(int)verdict].tex) {
                s32 tw = pu::ui::render::GetTextureWidth(chip_texs[(int)verdict].tex);
                s32 th = pu::ui::render::GetTextureHeight(chip_texs[(int)verdict].tex);
                const s32 chip_w = tw + 26;
                const s32 chip_h = 34;
                const s32 cx = list_x + list_w - chip_w - 20;
                drawer->RenderRoundedRectangleFill(VerdictColor(verdict), cx, ry + 14, chip_w, chip_h, 8);
                drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255),
                                                   cx + 2, ry + 16, chip_w - 4, chip_h - 4, 6);
                drawer->RenderTexture(chip_texs[(int)verdict].tex,
                                      cx + (chip_w - tw) / 2, ry + 14 + (chip_h - th) / 2);
            }
        }
    }

    // --- Level 2: per-game comparison ----------------------------------------

    void SaveDataView::RenderLevel2(pu::ui::render::Renderer::Ref& drawer) {
        auto nav = nav_mgr.lock();
        if (!nav) return;
        auto model = nav->GetModel();
        if (!model) return;

        const auto& platforms = model->GetPlatforms();
        if (platforms.empty()) return;
        const size_t plat_idx = std::min(nav->GetSavePlatformIdx(), platforms.size() - 1);
        const auto& plat = platforms[plat_idx];

        const auto snap = romm::model::SaveManager::Instance().GetSnapshot();
        const romm::model::SaveGameState* game = nullptr;
        for (const auto& g : snap.games) {
            if (g.rom_id == nav->GetSaveDetailRomId()) { game = &g; break; }
        }

        // Title of the open game (from the model; the snapshot may lag).
        std::string title;
        for (const auto& g : plat.games) {
            if (g.id == nav->GetSaveDetailRomId()) { title = g.title; break; }
        }

        pu::ui::Color text_color(237, 229, 251, 255);
        pu::ui::Color dim_color(190, 180, 225, 255);
        pu::ui::Color highlight(230, 199, 167, 255);

        // --- Header ------------------------------------------------------------
        const std::string header = romm::i18n::format("save_data.detail.title",
                                                      {{"title", title}});
        pu::sdl2::Texture tex_title = pu::ui::render::RenderText(
            "Orbitron@37", header, text_color);
        if (tex_title) {
            drawer->RenderTexture(tex_title, 60, 40);
            pu::ui::render::DeleteTexture(tex_title);
        }

        const romm::model::SaveVerdict verdict = game ? game->verdict
                                                      : romm::model::SaveVerdict::Unknown;
        const std::string chip_label = romm::i18n::tr(VerdictKey(verdict));
        pu::sdl2::Texture tex_chip = pu::ui::render::RenderText("Orbitron@24", chip_label,
                                                                VerdictColor(verdict));
        if (tex_chip) {
            const s32 chip_w = pu::ui::render::GetTextureWidth(tex_chip) + 26;
            const s32 chip_h = 38;
            drawer->RenderRoundedRectangleFill(VerdictColor(verdict), 1860 - chip_w, 42, chip_w, chip_h, 10);
            drawer->RenderRoundedRectangleFill(pu::ui::Color(16, 18, 22, 255),
                                               1862, 44, chip_w - 4, chip_h - 4, 8);
            drawer->RenderTexture(tex_chip, 1873, 49);
            pu::ui::render::DeleteTexture(tex_chip);
        }

        // --- Time strip ----------------------------------------------------------
        // LOCAL marker -- LAST SYNC diamond -- SERVER dot, with the dates
        // underneath. The local date is the console clock (labelled as such);
        // the server dates are the RomM timestamps, which the verdict relies on.
        constexpr s32 strip_y = 190;
        constexpr s32 strip_x1 = 560;
        constexpr s32 strip_x2 = 1360;
        constexpr s32 mid_x = (strip_x1 + strip_x2) / 2;

        drawer->RenderRectangleFill(pu::ui::Color(45, 50, 62, 255), strip_x1, strip_y, strip_x2 - strip_x1, 3);

        const std::string local_date = (game && game->local_exists && !game->local_modified.empty())
                                           ? game->local_modified
                                           : romm::i18n::tr("save_data.strip.no_local");
        drawer->RenderRectangleFill(highlight, strip_x1 - 6, strip_y - 6, 12, 12);
        pu::sdl2::Texture tex_l = pu::ui::render::RenderText("Ubuntu@20",
            romm::i18n::format("save_data.strip.local", {{"date", local_date}}), dim_color);
        if (tex_l) {
            drawer->RenderTexture(tex_l, strip_x1 - 60, strip_y + 18);
            pu::ui::render::DeleteTexture(tex_l);
        }

        const std::string sync_date = (game && !game->last_sync_date.empty())
                                          ? game->last_sync_date
                                          : romm::i18n::tr("save_data.strip.no_sync");
        FillDiamond(drawer, mid_x, strip_y, 8, dim_color);
        pu::sdl2::Texture tex_m = pu::ui::render::RenderText("Ubuntu@20",
            romm::i18n::format("save_data.strip.last_sync", {{"date", sync_date}}), dim_color);
        if (tex_m) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_m);
            drawer->RenderTexture(tex_m, mid_x - tw / 2, strip_y + 18);
            pu::ui::render::DeleteTexture(tex_m);
        }

        const std::string server_date = [&]() -> std::string {
            if (!game || !game->server_checked) return romm::i18n::tr("save_data.strip.unknown");
            if (!game->server_has_saves) return romm::i18n::tr("save_data.strip.no_server");
            const auto* newest = (const romm::model::SaveEntry*)nullptr;
            for (const auto& s : game->server_saves) {
                if (s.missing_from_fs) continue;
                if (!newest || s.updated_at > newest->updated_at) newest = &s;
            }
            return newest ? newest->updated_at : romm::i18n::tr("save_data.strip.no_server");
        }();
        drawer->RenderCircleFill(pu::ui::Color(190, 143, 230, 255), strip_x2, strip_y, 7);
        pu::sdl2::Texture tex_r = pu::ui::render::RenderText("Ubuntu@20",
            romm::i18n::format("save_data.strip.server", {{"date", server_date}}), dim_color);
        if (tex_r) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_r);
            drawer->RenderTexture(tex_r, strip_x2 - tw + 60, strip_y + 18);
            pu::ui::render::DeleteTexture(tex_r);
        }

        // --- Cards ---------------------------------------------------------------
        constexpr s32 card_y = 300;
        constexpr s32 card_h = 500;
        constexpr s32 local_w = 640;
        constexpr s32 server_w = 640;
        constexpr s32 local_x = 60;
        constexpr s32 server_x = 1220;
        constexpr s32 focus_w = 4;

        // LOCAL card
        {
            const bool focused = (nav->GetSaveDetailFocus() == romm::navigation::SaveDetailFocus::Local);
            pu::ui::Color border = focused ? highlight : pu::ui::Color(45, 50, 62, 255);
            drawer->RenderRoundedRectangleFill(border, local_x, card_y, local_w, card_h, 16);
            drawer->RenderRoundedRectangleFill(pu::ui::Color(30, 34, 43, 255),
                                               local_x + focus_w, card_y + focus_w,
                                               local_w - focus_w * 2, card_h - focus_w * 2, 14);

            pu::sdl2::Texture tex_card = pu::ui::render::RenderText(
                "Orbitron@30", romm::i18n::tr("save_data.detail.local_card"), text_color);
            if (tex_card) {
                drawer->RenderTexture(tex_card, local_x + 30, card_y + 24);
                pu::ui::render::DeleteTexture(tex_card);
            }

            if (game && game->local_exists) {
                std::string fname = game->local_path;
                size_t slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                pu::sdl2::Texture tex_f = pu::ui::render::RenderText(
                    "Ubuntu@26", fname, text_color, local_w - 60);
                if (tex_f) {
                    drawer->RenderTexture(tex_f, local_x + 30, card_y + 90);
                    pu::ui::render::DeleteTexture(tex_f);
                }
                const std::string size_line = romm::i18n::format("save_data.detail.size",
                                                                 {{"size", FormatBytes(game->local_size)}});
                pu::sdl2::Texture tex_s = pu::ui::render::RenderText("Ubuntu@24", size_line, dim_color);
                if (tex_s) {
                    drawer->RenderTexture(tex_s, local_x + 30, card_y + 150);
                    pu::ui::render::DeleteTexture(tex_s);
                }
                const std::string hash_line = romm::i18n::format("save_data.detail.hash",
                                                                 {{"hash", game->local_hash}});
                pu::sdl2::Texture tex_h = pu::ui::render::RenderText("Ubuntu@24", hash_line, dim_color);
                if (tex_h) {
                    drawer->RenderTexture(tex_h, local_x + 30, card_y + 190);
                    pu::ui::render::DeleteTexture(tex_h);
                }
                const std::string mod_line = romm::i18n::format("save_data.detail.modified",
                                                                {{"date", game->local_modified}}) +
                                             "  (" + romm::i18n::tr("save_data.detail.clock_note") + ")";
                pu::sdl2::Texture tex_m2 = pu::ui::render::RenderText("Ubuntu@24", mod_line, dim_color);
                if (tex_m2) {
                    drawer->RenderTexture(tex_m2, local_x + 30, card_y + 230);
                    pu::ui::render::DeleteTexture(tex_m2);
                }
                if (!game->last_sync_date.empty()) {
                    pu::sdl2::Texture tex_ls = pu::ui::render::RenderText(
                        "Ubuntu@22", romm::i18n::format("save_data.detail.last_sync_line",
                                                        {{"date", game->last_sync_date}}),
                        dim_color);
                    if (tex_ls) {
                        drawer->RenderTexture(tex_ls, local_x + 30, card_y + 290);
                        pu::ui::render::DeleteTexture(tex_ls);
                    }
                }
            } else {
                pu::sdl2::Texture tex_e = pu::ui::render::RenderText(
                    "Ubuntu@26", romm::i18n::tr("save_data.detail.no_local"), dim_color);
                if (tex_e) {
                    drawer->RenderTexture(tex_e, local_x + 30, card_y + 100);
                    pu::ui::render::DeleteTexture(tex_e);
                }
            }
        }

        // SERVER card (save history, newest first)
        {
            const bool focused = (nav->GetSaveDetailFocus() == romm::navigation::SaveDetailFocus::Server);
            pu::ui::Color border = focused ? highlight : pu::ui::Color(45, 50, 62, 255);
            drawer->RenderRoundedRectangleFill(border, server_x, card_y, server_w, card_h, 16);
            drawer->RenderRoundedRectangleFill(pu::ui::Color(30, 34, 43, 255),
                                               server_x + focus_w, card_y + focus_w,
                                               server_w - focus_w * 2, card_h - focus_w * 2, 14);

            pu::sdl2::Texture tex_card = pu::ui::render::RenderText(
                "Orbitron@30", romm::i18n::tr("save_data.detail.server_card"), text_color);
            if (tex_card) {
                drawer->RenderTexture(tex_card, server_x + 30, card_y + 24);
                pu::ui::render::DeleteTexture(tex_card);
            }

            if (!game) {
                pu::sdl2::Texture tex_e = pu::ui::render::RenderText(
                    "Ubuntu@26", romm::i18n::tr("save_data.detail.loading"), dim_color);
                if (tex_e) {
                    drawer->RenderTexture(tex_e, server_x + 30, card_y + 100);
                    pu::ui::render::DeleteTexture(tex_e);
                }
            } else if (!game->server_checked) {
                pu::sdl2::Texture tex_e = pu::ui::render::RenderText(
                    "Ubuntu@26", romm::i18n::tr("save_data.detail.not_checked"), dim_color);
                if (tex_e) {
                    drawer->RenderTexture(tex_e, server_x + 30, card_y + 100);
                    pu::ui::render::DeleteTexture(tex_e);
                }
            } else if (!game->server_has_saves) {
                pu::sdl2::Texture tex_e = pu::ui::render::RenderText(
                    "Ubuntu@26", romm::i18n::tr("save_data.detail.no_server"), dim_color);
                if (tex_e) {
                    drawer->RenderTexture(tex_e, server_x + 30, card_y + 100);
                    pu::ui::render::DeleteTexture(tex_e);
                }
            } else {
                const auto saves = NewestFirst(game->server_saves);
                const size_t sel_row = std::min(nav->GetSaveDetailServerSel(), saves.size() - 1);
                constexpr s32 row_h = 64;
                constexpr s32 max_rows = 6;
                s32 scroll = 0;
                if (sel_row >= max_rows) scroll = (s32)sel_row - max_rows + 1;
                for (size_t i = 0; i < saves.size() && i < max_rows; ++i) {
                    const size_t idx = (size_t)scroll + i;
                    if (idx >= saves.size()) break;
                    const auto& save = saves[idx];
                    const s32 ry = card_y + 90 + (s32)i * row_h;
                    const bool selected = (idx == sel_row);
                    if (selected) {
                        drawer->RenderRoundedRectangleFill(
                            focused ? pu::ui::Color(85, 63, 152, 255)
                                    : pu::ui::Color(85, 63, 152, 130),
                            server_x + 14, ry, server_w - 28, row_h - 8, 8);
                    }
                    pu::sdl2::Texture tex_n = pu::ui::render::RenderText(
                        "Ubuntu@24", save.file_name,
                        selected ? highlight : text_color, server_w - 60);
                    if (tex_n) {
                        drawer->RenderTexture(tex_n, server_x + 30, ry + 6);
                        pu::ui::render::DeleteTexture(tex_n);
                    }
                    const std::string meta = save.updated_at + "  ·  " + FormatBytes(save.file_size_bytes);
                    pu::sdl2::Texture tex_m = pu::ui::render::RenderText(
                        "Ubuntu@20", meta, dim_color, server_w - 60);
                    if (tex_m) {
                        drawer->RenderTexture(tex_m, server_x + 30, ry + 36);
                        pu::ui::render::DeleteTexture(tex_m);
                    }
                }
            }
        }

        // --- Action buttons ---------------------------------------------------------
        constexpr s32 act_y = 880;
        constexpr s32 act_w = 260;
        constexpr s32 act_h = 76;
        constexpr s32 act_gap = 30;
        static const char* kDetailActionKeys[] = {"save_data.action.sync",
                                                  "save_data.action.upload",
                                                  "save_data.action.download"};
        const bool actions_focused = (nav->GetSaveDetailFocus() == romm::navigation::SaveDetailFocus::Actions);
        const s32 total_w = 3 * act_w + 2 * act_gap;
        const s32 act_x0 = (1920 - total_w) / 2;
        for (size_t i = 0; i < 3; ++i) {
            const s32 bx = act_x0 + (s32)i * (act_w + act_gap);
            const bool active = (nav->GetSaveDetailActionIdx() == i);
            pu::ui::Color border = (actions_focused) ? highlight : pu::ui::Color(45, 50, 62, 255);
            pu::ui::Color bg(28, 31, 38, 255);
            if (actions_focused && active) {
                border = highlight;
                bg = pu::ui::Color(85, 63, 152, 255);
            }
            drawer->RenderRoundedRectangleFill(border, bx, act_y, act_w, act_h, 12);
            drawer->RenderRoundedRectangleFill(bg, bx + 4, act_y + 4, act_w - 8, act_h - 8, 10);
            pu::sdl2::Texture tex = pu::ui::render::RenderText(
                "Orbitron@24", romm::i18n::tr(kDetailActionKeys[i]),
                (actions_focused && active) ? highlight : text_color);
            if (tex) {
                s32 tw = pu::ui::render::GetTextureWidth(tex);
                s32 th = pu::ui::render::GetTextureHeight(tex);
                drawer->RenderTexture(tex, bx + (act_w - tw) / 2, act_y + (act_h - th) / 2);
                pu::ui::render::DeleteTexture(tex);
            }
        }

        const char* hint_key = "save_data.hint.detail.local";
        if (nav->GetSaveDetailFocus() == romm::navigation::SaveDetailFocus::Server) {
            hint_key = "save_data.hint.detail.server";
        } else if (nav->GetSaveDetailFocus() == romm::navigation::SaveDetailFocus::Actions) {
            hint_key = "save_data.hint.detail.actions";
        }
        pu::sdl2::Texture tex_hint = pu::ui::render::RenderText("Ubuntu@22",
            romm::i18n::tr(hint_key), dim_color);
        if (tex_hint) {
            s32 tw = pu::ui::render::GetTextureWidth(tex_hint);
            drawer->RenderTexture(tex_hint, (1920 - tw) / 2, 1000);
            pu::ui::render::DeleteTexture(tex_hint);
        }
    }

    void SaveDataView::OnRender(pu::ui::render::Renderer::Ref& drawer,
                                const s32 x_coord, const s32 y_coord) {
        auto nav = nav_mgr.lock();
        if (!nav) return;

        // Clear the row caches when the list identity changes (platform switch
        // or game-count change), so no stale row textures survive a refresh.
        {
            std::string list_id = "none";
            auto model = nav->GetModel();
            if (model) {
                const auto& platforms = model->GetPlatforms();
                if (!platforms.empty() && nav->GetSavePlatformIdx() < platforms.size()) {
                    const auto& plat = platforms[nav->GetSavePlatformIdx()];
                    list_id = plat.slug + "|" + std::to_string(plat.games.size());
                }
            }
            if (list_id != cached_list_id) {
                cached_list_id = list_id;
                list_scroll = 0;
                plat_scroll = 0;
                ClearRowCaches();
            }
        }

        if (nav->IsSaveDetailOpen()) {
            RenderLevel2(drawer);
        } else {
            RenderLevel1(drawer);
        }
    }

}
