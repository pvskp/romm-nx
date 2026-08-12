#include "TicoCatalog.hpp"
#include "DataModel.hpp"

#include <unordered_map>
#include <iostream>

namespace romm::model {

    namespace {

        // RomM slug -> Tico folder. Only the platforms whose names differ
        // between the two frontends are listed; everything else uses the RomM
        // slug verbatim.
        const std::unordered_map<std::string, std::string>& SlugMap() {
            static const std::unordered_map<std::string, std::string> map = {
                { "sms",     "master-system" },
                { "gamegear", "game-gear" },
                { "ngc",     "gc" },
                { "segacd",  "sega-cd" },
            };
            return map;
        }

        // Tico platform -> emulator core sent as `emulator` on POST /api/saves.
        // Every platform with a core in v1 is here; platforms without an entry
        // cannot upload saves (their sync Saves stage is "not supported").
        // Core names match the actual cores Tico ships for each platform.
        const std::unordered_map<std::string, std::string>& CoreMap() {
            static const std::unordered_map<std::string, std::string> map = {
                { "gb",              "gambatte" },
                { "gbc",             "gambatte" },
                { "gba",             "mgba" },
                { "snes",            "snes9x" },
                { "n64",             "mupen64plus" },
                { "genesis",         "genesis_plus_gx" },
                { "master-system",   "genesis_plus_gx" },
                { "psp",             "ppsspp" },
                { "psx",             "pcsx_rearmed" },
            };
            return map;
        }

        // Tico platform -> save file extension the local emulator expects.
        // N64 is the exception (.fla memory pak); every other platform with a
        // core in v1 saves as .sav. TODO(hardware): confirm .fla semantics on
        // real Tico/N64 — the core writes .fla for the mempak, .mpk line
        // pending. .sav is the safe default for anything unlisted.
        const std::unordered_map<std::string, std::string>& ExtensionMap() {
            static const std::unordered_map<std::string, std::string> map = {
                { "n64", ".fla" },
            };
            return map;
        }
    }

    std::string ResolveTicoPlatformSlug(const std::string& romm_slug) {
        // Fold RomM aliases first so "nintendo-game-boy" and friends land on
        // the canonical Tico name without per-alias entries in the map.
        std::string slug = NormalizePlatformSlug(romm_slug);
        const auto& map = SlugMap();
        auto it = map.find(slug);
        if (it != map.end()) {
            return it->second;
        }
        return slug;
    }

    bool IsTicoPlatformSupported(const std::string& tico_slug) {
        for (const auto& s : GetTicoPlatformList()) {
            if (s == tico_slug) return true;
        }
        return false;
    }

    std::string ResolveTicoCore(const std::string& tico_slug) {
        const auto& map = CoreMap();
        auto it = map.find(tico_slug);
        if (it != map.end()) {
            return it->second;
        }
        return "";
    }

    std::string ResolveTicoSaveExtension(const std::string& tico_slug) {
        const auto& map = ExtensionMap();
        auto it = map.find(tico_slug);
        if (it != map.end()) {
            return it->second;
        }
        return ".sav";
    }

    const std::vector<std::string>& GetTicoPlatformList() {
        static const std::vector<std::string> platforms = {
            "3ds", "atomiswave", "dc", "fbneo", "game-gear",
            "gb", "gba", "gbc", "gc", "genesis",
            "master-system", "n64", "naomi", "nes", "psp",
            "psx", "saturn", "sega-cd", "snes", "wii"
        };
        return platforms;
    }

}