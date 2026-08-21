#pragma once

#include <string>
#include <vector>

// Central mapping between the RomM world (slugs, save contents) and the Tico
// frontend (per-emulator folder layout, cores, save file extensions).
//
// Everything the sync pipeline puts on the SD for Tico goes through these
// three tables — platfom folder names, emulator cores and save extensions —
// so a wrong folder/core/extension is fixed in exactly one place.
namespace romm::model {

    // RomM platform slug -> Tico platform folder. Platforms whose name is the
    // same on both sides pass through untouched; the few that differ are
    // remapped here. Never fails: an unknown slug falls back to (a normalized
    // form of) the slug itself so the sync can't strand a game.
    std::string ResolveTicoPlatformSlug(const std::string& romm_slug);

    // True when the slug belongs to the 20 platforms Tico actually supports
    // (v1). Informational: unknown slugs still sync via the fallback above.
    bool IsTicoPlatformSupported(const std::string& tico_slug);

    // Emulator core name Tico uses for this platform, sent as the `emulator`
    // field when uploading a save to RomM. Empty string means this platform
    // has no core in v1 and therefore cannot upload saves.
    std::string ResolveTicoCore(const std::string& tico_slug);

    // File extension the Tico emulator expects for this platform's saves,
    // including the dot (".sav"). Defaults to ".sav" — every platform in the
    // v1 core table saves as .sav; N64 is the one exception (.fla memory pak).
    std::string ResolveTicoSaveExtension(const std::string& tico_slug);

    // The 20 platforms the Tico frontend supports in v1, for tests/UI.
    const std::vector<std::string>& GetTicoPlatformList();

}