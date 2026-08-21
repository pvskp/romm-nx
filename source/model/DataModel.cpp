#include "DataModel.hpp"
#include <iostream>

namespace romm::model {

    DataModel::DataModel()
        : platform_state(ApiState::Idle), roms_state(ApiState::Idle) {}

    const std::vector<Platform>& DataModel::GetPlatforms() const {
        return this->platforms;
    }

    void DataModel::SetPlatforms(const std::vector<Platform>& new_plats) {
        this->all_platforms.clear();

        // The sidebar lists platforms that actually carry ROMs on the server.
        // A server that doesn't report rom_count keeps every platform
        // ("unknown" is not "zero"), so an older RomM doesn't empty the list.
        for (const auto& plat : new_plats) {
            if (plat.has_rom_count && plat.rom_count <= 0) {
                std::cout << "[PLATFORMS] Skipping empty platform: " << plat.name << std::endl;
                continue;
            }
            this->all_platforms.push_back(plat);
        }

        this->platforms.clear(); // fresh server list: nothing cached to carry over

        // The sidebar shows every platform the server reports — the per-user
        // visibility selection was removed.
        RebuildVisiblePlatforms();
    }

    void DataModel::RebuildVisiblePlatforms() {
        // Park whatever the visible list has already fetched back onto the
        // master list first, so nothing is thrown away on a rebuild.
        for (auto& loaded : this->platforms) {
            for (auto& meta : this->all_platforms) {
                if (meta.id == loaded.id) {
                    meta.games = std::move(loaded.games);
                    meta.roms_state = loaded.roms_state;
                    break;
                }
            }
        }

        this->platforms = this->all_platforms;
        this->platforms_generation++;

        std::cout << "[PLATFORMS] " << this->platforms.size() << " platforms" << std::endl;
    }

    const Platform* DataModel::GetPlatformById(const std::string& id) const {
        for (const auto& plat : this->platforms) {
            if (plat.id == id) {
                return &plat;
            }
        }
        return nullptr;
    }

    const Game* DataModel::FindGameByRomId(int rom_id) const {
        if (rom_id <= 0) return nullptr;
        for (const auto* list : {&this->platforms, &this->all_platforms}) {
            for (const auto& plat : *list) {
                for (const auto& game : plat.games) {
                    if (game.id == rom_id) return &game;
                }
            }
        }
        return nullptr;
    }

    void DataModel::UpdatePlatformGames(const std::string& platform_id, const std::vector<Game>& games) {
        // Both lists mirror each other now (no visibility split), so keep them
        // in sync — lookups by FindGameByRomId walk either list.
        for (auto* list : {&this->platforms, &this->all_platforms}) {
            for (auto& plat : *list) {
                if (plat.id == platform_id) {
                    plat.games = games;
                    if (games.empty()) {
                        plat.roms_state = ApiState::NoData;
                    } else {
                        plat.roms_state = ApiState::Success;
                    }
                    break;
                }
            }
        }
    }

    void DataModel::SetPlatformRomsState(const std::string& platform_id, ApiState state) {
        for (auto* list : {&this->platforms, &this->all_platforms}) {
            for (auto& plat : *list) {
                if (plat.id == platform_id) {
                    plat.roms_state = state;
                    break;
                }
            }
        }
    }

    DetailLoadState DataModel::GetDetailState(int rom_id) const {
        auto it = detail_states.find(rom_id);
        if (it != detail_states.end()) {
            return it->second;
        }
        return DetailLoadState::NotLoaded;
    }

    void DataModel::SetDetailState(int rom_id, DetailLoadState state) {
        detail_states[rom_id] = state;
    }

    const GameDetail* DataModel::GetCachedDetail(int rom_id) const {
        auto it = cached_rom_details.find(rom_id);
        if (it != cached_rom_details.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void DataModel::SetCachedDetail(int rom_id, const GameDetail& detail) {
        cached_rom_details[rom_id] = detail;
        detail_states[rom_id] = DetailLoadState::Loaded;
    }

}
