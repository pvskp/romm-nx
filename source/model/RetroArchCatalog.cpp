#include "RetroArchCatalog.hpp"
#include "DataModel.hpp"

#include <unordered_map>

namespace romm::model {

    namespace {

        // RomM slug -> RetroArch ROM folder. RetroArch itself does not care
        // about folder names (content can live anywhere), so this only exists
        // for the "own folders" sync mode; keep the same human-readable names
        // Tico uses so a library synced to both frontends looks uniform.
        const std::unordered_map<std::string, std::string>& SlugMap() {
            static const std::unordered_map<std::string, std::string> map = {
                { "sms",     "master-system" },
                { "gamegear", "game-gear" },
                { "ngc",     "gc" },
                { "segacd",  "sega-cd" },
            };
            return map;
        }

        // Platform -> libretro core identifier. Doubles as the per-core
        // subfolder name under cores/savefiles/ and cores/savestates/. Cores without an entry cannot
        // sync saves or states in v1 (their stage shows "not supported").
        // Names follow the upstream libretro core IDs, which is what
        // RetroArch uses for its per-core save folders on Switch.
        const std::unordered_map<std::string, std::string>& CoreMap() {
            static const std::unordered_map<std::string, std::string> map = {
                { "nes",           "fceumm" },
                { "snes",          "snes9x" },
                { "gb",            "gambatte" },
                { "gbc",           "gambatte" },
                { "gba",           "mgba" },
                { "n64",           "mupen64plus-next" },
                { "genesis",       "genesis_plus_gx" },
                { "master-system", "genesis_plus_gx" },
                { "game-gear",     "genesis_plus_gx" },
                { "sega-cd",       "genesis_plus_gx" },
                { "psx",           "pcsx_rearmed" },
                { "psp",           "ppsspp" },
                { "saturn",        "yabause" },
                { "dc",            "flycast" },
                { "atomiswave",    "flycast" },
                { "naomi",         "flycast" },
                { "fbneo",         "fbneo" },
            };
            return map;
        }
    }

    std::string ResolveRetroArchPlatformSlug(const std::string& romm_slug) {
        std::string slug = NormalizePlatformSlug(romm_slug);
        const auto& map = SlugMap();
        auto it = map.find(slug);
        if (it != map.end()) {
            return it->second;
        }
        return slug;
    }

    std::string ResolveRetroArchCore(const std::string& ra_slug) {
        const auto& map = CoreMap();
        auto it = map.find(ra_slug);
        if (it != map.end()) {
            return it->second;
        }
        return "";
    }

    std::string ResolveRetroArchSaveExtension(const std::string& ra_slug) {
        (void)ra_slug;
        return ".srm";
    }

    const std::vector<std::string>& GetRetroArchPlatformList() {
        static const std::vector<std::string> platforms = {
            "atomiswave", "dc", "fbneo", "game-gear", "gb", "gba", "gbc",
            "genesis", "master-system", "naomi", "nes", "n64", "psp", "psx",
            "saturn", "sega-cd", "snes"
        };
        return platforms;
    }

}
