#pragma once

#include <string>
#include <vector>

// Central mapping between the RomM world and the RetroArch frontend, the
// exact counterpart of TicoCatalog. RetroArch organizes saves and states
// per CORE (saves/<core>/<rom>.srm) instead of per platform, so the core
// table is what drives the folder layout — a wrong core strands saves in
// a folder the emulator never reads.
namespace romm::model {

    // RomM platform slug -> RetroArch platform folder (used only when the
    // sync writes ROMs to RetroArch's own content folders; when reusing the
    // Tico folders this map is never consulted). Same remaps as Tico: an
    // unknown slug falls back to its normalized form.
    std::string ResolveRetroArchPlatformSlug(const std::string& romm_slug);

    // Libretro core identifier for this platform — also the subfolder name
    // under RetroArch's saves/ and states/ directories. Empty string means
    // this platform has no usable core in v1 (no save/state support).
    std::string ResolveRetroArchCore(const std::string& ra_slug);

    // RetroArch stores battery saves as .srm for every core, so unlike the
    // Tico table there is no per-platform extension.
    std::string ResolveRetroArchSaveExtension(const std::string& ra_slug);

    // Platforms with a core in v1, for tests/UI.
    const std::vector<std::string>& GetRetroArchPlatformList();

}
