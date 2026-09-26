#include "FrontendProfile.hpp"
#include "ConfigManager.hpp"
#include "TicoCatalog.hpp"

// The Tico flavor of the frontend profile: one TU per build, the Makefile
// compiles only the flavor this build serves. Tico lays everything under one
// root directory — ROMs, per-platform saves/states and cover art — so every
// folder getter below is a small path join on the configured base dir.
namespace romm::model::frontend {

    std::string ResolvePlatformSlug(const std::string& romm_slug) {
        return ResolveTicoPlatformSlug(romm_slug);
    }

    bool IsPlatformSupported(const std::string& target_slug) {
        return IsTicoPlatformSupported(target_slug);
    }

    std::string ResolveCore(const std::string& romm_slug) {
        return ResolveTicoCore(ResolveTicoPlatformSlug(romm_slug));
    }

    std::string ResolveSaveExtension(const std::string& romm_slug) {
        return ResolveTicoSaveExtension(ResolveTicoPlatformSlug(romm_slug));
    }

    const std::vector<std::string>& PlatformList() {
        return GetTicoPlatformList();
    }

    std::string GetRomsDir(const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "roms/" + ResolvePlatformSlug(romm_slug) + "/";
    }

    std::string GetSavesDir(const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "saves/" + ResolvePlatformSlug(romm_slug) + "/";
    }

    std::string GetStatesDir(const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "states/" + ResolvePlatformSlug(romm_slug) + "/";
    }

    std::string GetCoverDir(const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "assets/covers/" + ResolvePlatformSlug(romm_slug) + "/";
    }

    std::string GetBackgroundPath(const std::string& romm_slug, const std::string& game_base) {
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "assets/backgrounds/" + ResolvePlatformSlug(romm_slug) +
               "/" + game_base + ".jpg";
    }

}
