#include "SavesLayout.hpp"
#include "../navigation/NavigationManager.hpp"
#include "../model/ConfigManager.hpp"
#include "../model/RommApi.hpp"
#include "../i18n/I18n.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

namespace romm::ui {

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    std::string SavesList::SizeStr(long long bytes) {
        if (bytes >= 1024 * 1024) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
            return buf;
        }
        if (bytes >= 1024) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
            return buf;
        }
        return std::to_string(bytes) + " B";
    }

    static bool IsLocalNameMatch(const std::string& local_name, const std::string& remote_name) {
        return romm::model::SavesManager::NormalizeSaveBase(local_name) ==
               romm::model::SavesManager::NormalizeSaveBase(remote_name);
    }

    // -----------------------------------------------------------------------
    // SavesList
    // -----------------------------------------------------------------------

    SavesList::SavesList(s32 x, s32 y, s32 w, s32 h, std::shared_ptr<romm::navigation::NavigationManager> nav)
        : Element(), x(x), y(y), w(w), h(h), nav_mgr(nav) {
        empty_text = romm::i18n::tr("saves.empty");
        pu::ui::Color text_color(237, 229, 251, 255);
        empty_tex = pu::ui::render::RenderText("Ubuntu@30", empty_text, text_color);
    }

    SavesList::~SavesList() {
        ClearTextures();
        if (empty_tex) pu::ui::render::DeleteTexture(empty_tex);
    }

    void SavesList::ClearTextures() {
        for (auto& r : row_tex) {
            if (r.title_sel)   pu::ui::render::DeleteTexture(r.title_sel);
            if (r.title_unsel) pu::ui::render::DeleteTexture(r.title_unsel);
            if (r.sub_sel)     pu::ui::render::DeleteTexture(r.sub_sel);
            if (r.sub_unsel)   pu::ui::render::DeleteTexture(r.sub_unsel);
        }
        row_tex.clear();
    }

    void SavesList::BuildPlatforms() {
        platforms.clear();
        auto nav = nav_mgr.lock();
        if (!nav) return;
        auto model = nav->GetModel();
        if (!model) return;

        for (const auto& p : model->GetPlatforms()) {
            PlatRow r;
            r.id = std::atoi(p.id.c_str());
            r.name = p.name;
            r.slug = p.slug;
            platforms.push_back(r);
        }
    }

    void SavesList::BuildGames() {
        games.clear();
        unattached_files.clear();

        auto nav = nav_mgr.lock();
        if (!nav) return;
        auto model = nav->GetModel();
        if (!model) return;

        const auto* plat = model->GetPlatformById(std::to_string(platform_id));
        if (!plat) {
            load_failed = true;
            loading = false;
            return;
        }

        std::map<int, std::vector<romm::model::SaveEntry>> by_rom;
        for (const auto& s : fetched_saves) {
            by_rom[s.rom_id].push_back(s);
        }

        std::map<int, size_t> row_of;

        for (const auto& entry : by_rom) {
            const int rom_id = entry.first;
            const romm::model::Game* g = nullptr;
            for (const auto& game : plat->games) {
                if (game.id == rom_id) { g = &game; break; }
            }
            if (!g) continue;

            GameRow row;
            row.rom_id = rom_id;
            row.title = g->title;
            row.has_remote = true;
            row.remote = entry.second;
            row_of[rom_id] = games.size();
            games.push_back(std::move(row));
        }

        local_files = romm::model::SavesManager::Instance().ListLocalSaves(platform_slug);

        for (const auto& lf : local_files) {
            const std::string norm = romm::model::SavesManager::NormalizeSaveBase(lf.file_name);

            int matched_rom = -1;
            for (size_t i = 0; i < games.size() && matched_rom < 0; ++i) {
                for (const auto& sv : games[i].remote) {
                    if (IsLocalNameMatch(lf.file_name, sv.file_name)) {
                        matched_rom = (int)games[i].rom_id;
                        break;
                    }
                }
            }

            if (matched_rom < 0) {
                for (const auto& game : plat->games) {
                    if (romm::model::SavesManager::NamesMatch(norm, romm::model::SavesManager::NormalizeSaveBase(game.title))) {
                        matched_rom = game.id;
                        break;
                    }
                }
            }

            if (matched_rom < 0) {
                unattached_files.push_back(lf);
                continue;
            }

            if (row_of.count(matched_rom) == 0) {
                const romm::model::Game* g = nullptr;
                for (const auto& game : plat->games) {
                    if (game.id == matched_rom) { g = &game; break; }
                }
                if (!g) {
                    unattached_files.push_back(lf);
                    continue;
                }
                GameRow row;
                row.rom_id = matched_rom;
                row.title = g->title;
                row.local_count = 1;
                row_of[matched_rom] = games.size();
                games.push_back(std::move(row));
            } else {
                games[row_of[matched_rom]].local_count++;
            }
        }

        std::sort(games.begin(), games.end(), [](const GameRow& a, const GameRow& b) {
            return romm::model::ToLowerAscii(a.title) < romm::model::ToLowerAscii(b.title);
        });

        if (!unattached_files.empty()) {
            GameRow bucket;
            bucket.title = romm::i18n::tr("saves.unattached");
            bucket.unattached = true;
            bucket.local_count = (int)unattached_files.size();
            games.push_back(std::move(bucket));
        }

        if (active_game_idx >= games.size()) active_game_idx = games.size() - 1;
        if (selected_idx >= games.size()) selected_idx = games.size() - 1;
    }

    void SavesList::BuildSavesForGame(size_t game_idx) {
        saves.clear();
        if (game_idx >= games.size()) return;

        const auto& row = games[game_idx];
        std::vector<bool> used(local_files.size(), false);

        for (const auto& sv : row.remote) {
            SaveRow r;
            r.is_remote = true;
            r.rom_id = row.rom_id;
            r.remote = sv;
            for (size_t i = 0; i < local_files.size(); ++i) {
                if (!used[i] && IsLocalNameMatch(local_files[i].file_name, sv.file_name)) {
                    r.is_local = true;
                    used[i] = true;
                    break;
                }
            }
            saves.push_back(std::move(r));
        }

        const std::string title_norm = romm::model::SavesManager::NormalizeSaveBase(row.title);
        for (size_t i = 0; i < local_files.size(); ++i) {
            if (used[i]) continue;
            if (!romm::model::SavesManager::NamesMatch(
                    romm::model::SavesManager::NormalizeSaveBase(local_files[i].file_name), title_norm)) {
                continue;
            }
            SaveRow r;
            r.is_local = true;
            r.rom_id = row.rom_id;
            r.local = local_files[i];
            saves.push_back(std::move(r));
        }

        std::sort(saves.begin(), saves.end(), [](const SaveRow& a, const SaveRow& b) {
            std::string na = a.is_remote ? a.remote.file_name : a.local.file_name;
            std::string nb = b.is_remote ? b.remote.file_name : b.local.file_name;
            return romm::model::ToLowerAscii(na) < romm::model::ToLowerAscii(nb);
        });

        if (selected_idx >= saves.size()) selected_idx = saves.size() - 1;
    }

    void SavesList::BuildUnattachedSaves() {
        saves.clear();
        for (const auto& lf : unattached_files) {
            SaveRow r;
            r.is_local = true;
            r.rom_id = 0;
            r.local = lf;
            saves.push_back(std::move(r));
        }
        if (selected_idx >= saves.size()) selected_idx = saves.size() - 1;
    }

    void SavesList::BuildPicker() {
        picker.clear();
        auto nav = nav_mgr.lock();
        if (!nav) return;
        auto model = nav->GetModel();
        if (!model) return;

        const auto* plat = model->GetPlatformById(std::to_string(platform_id));
        if (!plat) return;

        for (const auto& game : plat->games) {
            GameRow row;
            row.rom_id = game.id;
            row.title = game.title;
            picker.push_back(std::move(row));
        }

        std::sort(picker.begin(), picker.end(), [](const GameRow& a, const GameRow& b) {
            return romm::model::ToLowerAscii(a.title) < romm::model::ToLowerAscii(b.title);
        });

        if (selected_idx >= picker.size()) selected_idx = picker.size() - 1;
    }

    void SavesList::RefreshAllData() {
        local_files = romm::model::SavesManager::Instance().ListLocalSaves(platform_slug);
        BuildGames();
        if (level == SavesLevel::Save) {
            if (active_game_idx < games.size() && games[active_game_idx].unattached) {
                BuildUnattachedSaves();
            } else {
                BuildSavesForGame(active_game_idx);
            }
        }
        dirty = true;
    }

    void SavesList::EnterPlatform(size_t plat_idx) {
        if (plat_idx >= platforms.size()) return;
        const auto& p = platforms[plat_idx];

        platform_id = p.id;
        platform_slug = romm::model::NormalizePlatformSlug(p.slug);
        platform_name = p.name;

        loading = true;
        load_failed = false;
        fetched_saves.clear();
        games.clear();
        saves.clear();
        local_files.clear();
        unattached_files.clear();
        status_msg.clear();
        delete_arm_idx = -1;
        active_game_idx = 0;
        selected_idx = 0;
        scroll_offset = 0;

        auto nav = nav_mgr.lock();
        if (nav) nav->TriggerRomsLoad(platform_id);

        fetch = romm::model::SavesManager::Instance().FetchSavesForPlatform(platform_id);
        level = SavesLevel::Game;
        dirty = true;
    }

    void SavesList::EnterGame(size_t game_idx) {
        if (game_idx >= games.size()) return;
        active_game_idx = game_idx;
        selected_idx = 0;
        scroll_offset = 0;
        delete_arm_idx = -1;
        status_msg.clear();

        if (games[game_idx].unattached) {
            BuildUnattachedSaves();
        } else {
            BuildSavesForGame(game_idx);
        }
        level = SavesLevel::Save;
        dirty = true;
    }

    void SavesList::PollAsync() {
        if (fetch && fetch->completed) {
            auto res = fetch;
            fetch.reset();
            if (res->success) {
                fetched_saves = res->saves;
                load_failed = false;

                auto nav = nav_mgr.lock();
                auto model = nav ? nav->GetModel() : nullptr;
                const auto* plat = model ? model->GetPlatformById(std::to_string(platform_id)) : nullptr;
                const bool roms_ready = !plat || plat->roms_state != romm::model::ApiState::Loading;
                if (roms_ready) {
                    loading = false;
                    if (level == SavesLevel::Game) {
                        BuildGames();
                        dirty = true;
                    } else {
                        RefreshAllData();
                    }
                }
            } else {
                load_failed = true;
                loading = false;
                dirty = true;
            }
        }

        if (loading && !load_failed && !fetch && level == SavesLevel::Game) {
            auto nav = nav_mgr.lock();
            auto model = nav ? nav->GetModel() : nullptr;
            const auto* plat = model ? model->GetPlatformById(std::to_string(platform_id)) : nullptr;
            const bool roms_ready = !plat || plat->roms_state != romm::model::ApiState::Loading;
            if (roms_ready) {
                loading = false;
                BuildGames();
                dirty = true;
            }
        }

        if (save_dl && save_dl->completed) {
            auto r = save_dl;
            save_dl.reset();
            if (r->success) {
                status_msg = romm::i18n::tr("saves.downloaded");
            } else {
                status_msg = r->error.empty() ? romm::i18n::tr("saves.download_failed") : r->error;
            }
            RefreshAllData();
        }

        if (mutation && mutation->completed) {
            auto m = mutation;
            mutation.reset();
            if (m->success) {
                status_msg = mutation_is_upload ? romm::i18n::tr("saves.uploaded") : romm::i18n::tr("saves.deleted");
            } else {
                status_msg = m->error.empty()
                    ? (mutation_is_upload ? romm::i18n::tr("saves.upload_failed") : romm::i18n::tr("saves.delete_failed"))
                    : m->error;
            }
            loading = true;
            load_failed = false;
            fetch = romm::model::SavesManager::Instance().FetchSavesForPlatform(platform_id);
        }
    }

    void SavesList::DownloadSave(size_t save_idx) {
        if (save_idx >= saves.size()) return;
        const auto& s = saves[save_idx];
        if (!s.is_remote || s.is_local) return;
        if (save_dl && !save_dl->completed) return;
        if (mutation && !mutation->completed) return;

        auto& dl_mgr = romm::model::DownloadManager::Instance();
        save_dl = dl_mgr.DownloadSave(s.remote, platform_slug);
        status_msg = romm::i18n::tr("saves.downloading");
        dirty = true;
    }

    void SavesList::StartUpload(size_t save_idx) {
        if (save_idx >= saves.size()) return;
        const auto& s = saves[save_idx];
        if (!s.is_local) return;
        if (mutation && !mutation->completed) return;
        if (save_dl && !save_dl->completed) return;

        if (s.rom_id > 0) {
            mutation_is_upload = true;
            mutation = romm::model::SavesManager::Instance().UploadSave(s.rom_id, s.local.full_path, s.local.file_name);
            status_msg = romm::i18n::tr("saves.uploading");
        } else {
            picker_pending = true;
            pending_upload = s.local;
            selected_idx = 0;
            scroll_offset = 0;
            BuildPicker();
            level = SavesLevel::Picker;
        }
        delete_arm_idx = -1;
        dirty = true;
    }

    void SavesList::ConfirmUploadTo(size_t picker_idx) {
        if (!picker_pending || picker_idx >= picker.size()) return;
        if (mutation && !mutation->completed) return;

        mutation_is_upload = true;
        mutation = romm::model::SavesManager::Instance().UploadSave(picker[picker_idx].rom_id,
                                                                     pending_upload.full_path,
                                                                     pending_upload.file_name);
        status_msg = romm::i18n::tr("saves.uploading");
        picker_pending = false;
        level = SavesLevel::Save;
        dirty = true;
    }

    void SavesList::StartDelete(size_t save_idx) {
        if (save_idx >= saves.size()) return;
        const auto& s = saves[save_idx];
        if (!s.is_remote) return;
        if (mutation && !mutation->completed) return;
        if (save_dl && !save_dl->completed) return;

        if (delete_arm_idx == (int)save_idx) {
            delete_arm_idx = -1;
            mutation_is_upload = false;
            mutation = romm::model::SavesManager::Instance().DeleteSaves({s.remote.id});
            status_msg = romm::i18n::tr("saves.deleting");
        } else {
            delete_arm_idx = (int)save_idx;
        }
        dirty = true;
    }

    void SavesList::HandleInput(const u64 keys_down, const u64 keys_held) {
        const u64 dir_mask = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_StickLUp | HidNpadButton_StickLDown;
        const u64 dir = (keys_down & dir_mask) ? (keys_down & dir_mask) : (keys_held & dir_mask);

        if (level == SavesLevel::Platform) {
            if (dir & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (selected_idx > 0) { selected_idx--; dirty = true; }
            }
            else if (dir & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (selected_idx + 1 < platforms.size()) { selected_idx++; dirty = true; }
            }
            else if (keys_down & HidNpadButton_A) {
                EnterPlatform(selected_idx);
            }
        }
        else if (level == SavesLevel::Game) {
            if (dir & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (selected_idx > 0) { selected_idx--; dirty = true; }
            }
            else if (dir & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (selected_idx + 1 < games.size()) { selected_idx++; dirty = true; }
            }
            else if (keys_down & HidNpadButton_A) {
                if (!loading && !load_failed) EnterGame(selected_idx);
            }
            else if (keys_down & HidNpadButton_B) {
                level = SavesLevel::Platform;
                selected_idx = 0;
                scroll_offset = 0;
                loading = false;
                load_failed = false;
                fetch.reset();
                status_msg.clear();
                delete_arm_idx = -1;
                dirty = true;
            }
        }
        else if (level == SavesLevel::Save) {
            if (dir & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (selected_idx > 0) { selected_idx--; delete_arm_idx = -1; dirty = true; }
            }
            else if (dir & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (selected_idx + 1 < saves.size()) { selected_idx++; delete_arm_idx = -1; dirty = true; }
            }
            else if (keys_down & HidNpadButton_A) {
                DownloadSave(selected_idx);
            }
            else if (keys_down & HidNpadButton_X) {
                StartUpload(selected_idx);
            }
            else if (keys_down & HidNpadButton_Y) {
                StartDelete(selected_idx);
            }
            else if (keys_down & HidNpadButton_B) {
                level = SavesLevel::Game;
                selected_idx = active_game_idx;
                scroll_offset = 0;
                status_msg.clear();
                delete_arm_idx = -1;
                dirty = true;
            }
            else if (keys_down) {
                delete_arm_idx = -1;
                dirty = true;
            }
        }
        else if (level == SavesLevel::Picker) {
            if (dir & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (selected_idx > 0) { selected_idx--; dirty = true; }
            }
            else if (dir & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (selected_idx + 1 < picker.size()) { selected_idx++; dirty = true; }
            }
            else if (keys_down & HidNpadButton_A) {
                ConfirmUploadTo(selected_idx);
            }
            else if (keys_down & HidNpadButton_B) {
                picker_pending = false;
                level = SavesLevel::Save;
                selected_idx = 0;
                scroll_offset = 0;
                dirty = true;
            }
        }
    }

    void SavesList::RebuildRowTextures() {
        ClearTextures();

        auto nav = nav_mgr.lock();
        if (!nav) return;

        pu::ui::Color title_sel_clr(237, 229, 251, 255);
        pu::ui::Color title_unsel_clr(190, 180, 225, 255);
        pu::ui::Color sub_sel_clr(210, 200, 235, 255);
        pu::ui::Color sub_unsel_clr(140, 130, 170, 255);

        const size_t count = (level == SavesLevel::Platform) ? platforms.size()
                            : (level == SavesLevel::Game) ? games.size()
                            : (level == SavesLevel::Save) ? saves.size()
                            : picker.size();

        for (size_t i = 0; i < count; ++i) {
            RowTex rt;
            switch (level) {
                case SavesLevel::Platform:
                    rt.title = platforms[i].name;
                    break;
                case SavesLevel::Game: {
                    const auto& g = games[i];
                    rt.title = g.title;
                    if (g.unattached) {
                        rt.sub = romm::i18n::format("saves.bucket_sub", {{"count", std::to_string(unattached_files.size())}});
                    } else {
                        std::string remote = std::to_string(g.remote.size());
                        std::string local = std::to_string(g.local_count);
                        rt.sub = romm::i18n::format("saves.game_sub", {{"remote", remote}, {"local", local}});
                    }
                    break;
                }
                case SavesLevel::Save: {
                    const auto& s = saves[i];
                    rt.title = RowTitle(s);
                    rt.sub = RowSub(s);
                    break;
                }
                case SavesLevel::Picker:
                    rt.title = picker[i].title;
                    break;
            }

            rt.title_sel = pu::ui::render::RenderText("Ubuntu@30", rt.title, title_sel_clr, w - 120);
            rt.title_unsel = pu::ui::render::RenderText("Ubuntu@30", rt.title, title_unsel_clr, w - 120);
            if (!rt.sub.empty()) {
                rt.sub_sel = pu::ui::render::RenderText("Ubuntu@20", rt.sub, sub_sel_clr, w - 120);
                rt.sub_unsel = pu::ui::render::RenderText("Ubuntu@20", rt.sub, sub_unsel_clr, w - 120);
            }
            row_tex.push_back(std::move(rt));
        }
    }

    std::string SavesList::RowTitle(const SaveRow& s) const {
        return s.is_remote ? s.remote.file_name : s.local.file_name;
    }

    std::string SavesList::RowSub(const SaveRow& s) const {
        std::string state;
        if (s.is_remote && s.is_local) state = romm::i18n::tr("saves.status.synced");
        else if (s.is_remote)          state = romm::i18n::tr("saves.status.cloud");
        else                           state = romm::i18n::tr("saves.status.local");

        std::string size = s.is_remote ? SizeStr(s.remote.file_size_bytes) : SizeStr(s.local.size_bytes);
        std::string sub = romm::i18n::format("saves.status", {{"state", state}, {"size", size}});
        if (!status_msg.empty()) {
            sub += "   |   " + status_msg;
        }
        return sub;
    }

    void SavesList::OnSelectionUpdated() {
        PollAsync();
        if (dirty) {
            RebuildRowTextures();
            dirty = false;
        }
    }

    void SavesList::ForceRefresh() {
        BuildPlatforms();
        level = SavesLevel::Platform;
        selected_idx = 0;
        scroll_offset = 0;
        loading = false;
        load_failed = false;
        fetch.reset();
        save_dl.reset();
        mutation.reset();
        status_msg.clear();
        delete_arm_idx = -1;
        games.clear();
        saves.clear();
        picker.clear();
        dirty = true;
        OnSelectionUpdated();
    }

    void SavesList::OnLeave() {
        fetch.reset();
        save_dl.reset();
        mutation.reset();
        loading = false;
        load_failed = false;
        status_msg.clear();
        delete_arm_idx = -1;
        level = SavesLevel::Platform;
        selected_idx = 0;
        scroll_offset = 0;
        games.clear();
        saves.clear();
        picker.clear();
        dirty = true;
    }

    void SavesList::RefreshTranslations() {
        empty_text = romm::i18n::tr("saves.empty");
        cached_empty_text.clear();
        if (games.size()) {
            for (auto& g : games) {
                if (g.unattached) g.title = romm::i18n::tr("saves.unattached");
            }
        }
        dirty = true;
        OnSelectionUpdated();
    }

    std::string SavesList::GetContextHint() const {
        if (delete_arm_idx >= 0) {
            return romm::i18n::tr("saves.hint.delete_confirm");
        }
        switch (level) {
            case SavesLevel::Platform: return romm::i18n::tr("saves.hint.platform");
            case SavesLevel::Game:     return romm::i18n::tr("saves.hint.game");
            case SavesLevel::Save:     return romm::i18n::tr("saves.hint.save");
            case SavesLevel::Picker:   return romm::i18n::tr("saves.hint.picker");
        }
        return "";
    }

    std::string SavesList::GetBreadcrumb() const {
        switch (level) {
            case SavesLevel::Platform:
                return romm::i18n::tr("saves.title");
            case SavesLevel::Game:
                return romm::i18n::tr("saves.title") + " > " + platform_name;
            case SavesLevel::Save: {
                std::string game = "?";
                if (active_game_idx < games.size()) game = games[active_game_idx].title;
                return romm::i18n::tr("saves.title") + " > " + platform_name + " > " + game;
            }
            case SavesLevel::Picker:
                return romm::i18n::tr("saves.title") + " > " + platform_name + " > " +
                       romm::i18n::tr("saves.picker_breadcrumb");
        }
        return "";
    }

    void SavesList::OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 x_coord, const s32 y_coord) {
        const size_t item_count = (level == SavesLevel::Platform) ? platforms.size()
                                : (level == SavesLevel::Game) ? games.size()
                                : (level == SavesLevel::Save) ? saves.size()
                                : picker.size();

        std::string display_text = empty_text;
        if (level == SavesLevel::Platform) {
            display_text = romm::i18n::tr("status.no_platforms");
        } else if (level == SavesLevel::Game && loading) {
            display_text = romm::i18n::tr("saves.loading");
        } else if (level == SavesLevel::Game && load_failed) {
            display_text = romm::i18n::tr("saves.load_failed");
        } else if (level == SavesLevel::Game) {
            display_text = romm::i18n::format("saves.no_saves_platform", {{"platform", platform_name}});
        } else if (level == SavesLevel::Save) {
            std::string title = "?";
            if (active_game_idx < games.size()) title = games[active_game_idx].title;
            display_text = romm::i18n::format("saves.no_saves_title", {{"title", title}});
        } else if (level == SavesLevel::Picker) {
            display_text = romm::i18n::tr("status.no_games");
        }

        if (item_count == 0) {
            if (display_text != cached_empty_text) {
                if (empty_tex) pu::ui::render::DeleteTexture(empty_tex);
                empty_tex = pu::ui::render::RenderText("Ubuntu@30", display_text, pu::ui::Color(237, 229, 251, 255));
                cached_empty_text = display_text;
            }

            s32 tw = pu::ui::render::GetTextureWidth(empty_tex);
            s32 th = pu::ui::render::GetTextureHeight(empty_tex);
            drawer->RenderTexture(empty_tex, x_coord + (w - tw) / 2, y_coord + (h - th) / 2);
            return;
        }

        const s32 item_h = 100;
        const s32 max_visible = h / item_h;

        if (selected_idx < (size_t)scroll_offset) {
            scroll_offset = (int)selected_idx;
        } else if (selected_idx >= (size_t)scroll_offset + max_visible) {
            scroll_offset = (int)(selected_idx - max_visible + 1);
        }

        for (s32 i = 0; i < max_visible; ++i) {
            size_t idx = (size_t)scroll_offset + (size_t)i;
            if (idx >= item_count) break;

            s32 iy = y_coord + i * item_h;
            const bool is_selected = (idx == selected_idx);

            if (is_selected) {
                drawer->RenderRoundedRectangleFill(pu::ui::Color(85, 63, 152, 255), x_coord, iy, w, item_h - 10, 10);
            } else {
                drawer->RenderRoundedRectangleFill(pu::ui::Color(45, 50, 62, 255), x_coord, iy, w, item_h - 10, 10);
                drawer->RenderRoundedRectangleFill(pu::ui::Color(30, 34, 43, 200), x_coord + 2, iy + 2, w - 4, item_h - 14, 8);
            }

            if (idx >= row_tex.size()) continue;
            const auto& rt = row_tex[idx];
            if (rt.title_sel) {
                drawer->RenderTexture(is_selected ? rt.title_sel : rt.title_unsel, x_coord + 25, iy + 14);
            }
            if (rt.sub_sel) {
                drawer->RenderTexture(is_selected ? rt.sub_sel : rt.sub_unsel, x_coord + 25, iy + 54);
            }
        }
    }

    // -----------------------------------------------------------------------
    // SavesLayout
    // -----------------------------------------------------------------------

    SavesLayout::SavesLayout(std::shared_ptr<romm::navigation::NavigationManager> nav)
        : Layout::Layout(), nav_mgr(nav) {

        this->SetBackgroundColor(pu::ui::Color(16, 18, 22, 255));

        header_text = pu::ui::elm::TextBlock::New(0, 90, romm::i18n::tr("saves.title"));
        header_text->SetFont("Orbitron@45");
        header_text->SetColor(pu::ui::Color(237, 229, 251, 255));
        header_text->SetHorizontalAlign(pu::ui::elm::HorizontalAlign::Center);
        this->Add(header_text);

        last_hint = romm::i18n::tr("saves.hint.platform");
        hint_text = pu::ui::elm::TextBlock::New(0, 1080 - 65, last_hint);
        hint_text->SetFont("Ubuntu@30");
        hint_text->SetColor(pu::ui::Color(190, 180, 225, 255));
        hint_text->SetHorizontalAlign(pu::ui::elm::HorizontalAlign::Center);
        this->Add(hint_text);

        list = SavesList::New(460, 220, 1000, 600, nav);
        this->Add(list);
    }

    void SavesLayout::UpdateHeader() {
        if (!list || !header_text) return;
        std::string bc = list->GetBreadcrumb();
        if (bc != last_breadcrumb) {
            last_breadcrumb = bc;
            header_text->SetText(bc);
        }
    }

    void SavesLayout::UpdateHint() {
        if (!list || !hint_text) return;
        std::string hint = list->GetContextHint();
        if (hint != last_hint) {
            last_hint = hint;
            hint_text->SetText(hint);
        }
    }

    void SavesLayout::OnSelectionUpdated() {
        if (list) list->OnSelectionUpdated();
        UpdateHeader();
        UpdateHint();
    }

    void SavesLayout::ForceRefresh() {
        if (list) list->ForceRefresh();
        last_hint.clear();
        last_breadcrumb.clear();
        UpdateHeader();
        UpdateHint();
    }

    void SavesLayout::RefreshTranslations() {
        if (header_text) header_text->SetText(romm::i18n::tr("saves.title"));
        if (list) list->RefreshTranslations();
        last_hint.clear();
        last_breadcrumb.clear();
        UpdateHeader();
        UpdateHint();
    }

    void SavesLayout::HandleInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
        if (list) list->HandleInput(keys_down, keys_held);
        UpdateHeader();
        UpdateHint();
    }

    void SavesLayout::OnLeave() {
        if (list) list->OnLeave();
        last_hint.clear();
        last_breadcrumb.clear();
    }

    bool SavesLayout::AtRoot() const {
        return list && list->AtRoot();
    }

    std::string SavesLayout::GetContextHint() const {
        return list ? list->GetContextHint() : std::string();
    }

}
