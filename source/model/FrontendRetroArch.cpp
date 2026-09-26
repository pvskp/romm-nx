#include "FrontendProfile.hpp"
#include "ConfigManager.hpp"
#include "RetroArchCatalog.hpp"

// The RetroArch flavor of the frontend profile (see FrontendTico.cpp for the
// counterpart). RetroArch organizes saves and states per CORE inside the cores
// folder tree — cores/savefiles/<core>/ and cores/savestates/<core>/ under the
// base dir — while ROMs live in a separate, independently configured content
// folder. Platforms without a usable core in v1 report no save/state folder at
// all (empty string).
namespace romm::model::frontend {

    std::string ResolvePlatformSlug(const std::string& romm_slug) {
        return ResolveRetroArchPlatformSlug(romm_slug);
    }

    bool IsPlatformSupported(const std::string& target_slug) {
        const auto& list = GetRetroArchPlatformList();
        for (const auto& s : list) {
            if (s == target_slug) return true;
        }
        return false;
    }

    std::string ResolveCore(const std::string& romm_slug) {
        return ResolveRetroArchCore(ResolveRetroArchPlatformSlug(romm_slug));
    }

    std::string ResolveSaveExtension(const std::string& romm_slug) {
        return ResolveRetroArchSaveExtension(ResolveRetroArchPlatformSlug(romm_slug));
    }

    const std::vector<std::string>& PlatformList() {
        return GetRetroArchPlatformList();
    }

    std::string GetRomsDir(const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        const std::string roms_root = config.GetFrontendRomsDir().empty()
                                          ? config.GetFrontendBaseDir() + "roms/"
                                          : config.GetFrontendRomsDir();
        return roms_root + ResolvePlatformSlug(romm_slug) + "/";
    }

    std::string GetSavesDir(const std::string& romm_slug) {
        const std::string core = ResolveCore(romm_slug);
        if (core.empty()) return "";
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "cores/savefiles/" + core + "/";
    }

    std::string GetStatesDir(const std::string& romm_slug) {
        const std::string core = ResolveCore(romm_slug);
        if (core.empty()) return "";
        auto& config = ConfigManager::Instance();
        return config.GetFrontendBaseDir() + "cores/savestates/" + core + "/";
    }

    // Cover sync is Tico-only (kHasCoverSync is false for this flavor); the
    // symbols stay defined so the shared code compiles, but nothing reaches
    // them.
    std::string GetCoverDir(const std::string& romm_slug) {
        return "";
    }

    std::string GetBackgroundPath(const std::string& romm_slug, const std::string& game_base) {
        return "";
    }

}
