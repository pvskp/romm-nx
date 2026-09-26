#pragma once

#include <string>
#include <vector>

// Compile-time identity of the one frontend this build serves. The Makefile
// selects a flavor with exactly one of -DROMM_FRONTEND_TICO /
// -DROMM_FRONTEND_RETROARCH; every per-frontend decision — folder layout,
// cores, save extensions, app identity — flows from the constants and
// functions below, so no other file needs to know which frontend exists.
//
// The two builds are fully independent apps that can live side by side on
// the same SD card (separate install folders, config files, sync-state files
// and update URLs).
namespace romm::model::frontend {

#if defined(ROMM_FRONTEND_TICO) == defined(ROMM_FRONTEND_RETROARCH)
#error "Define exactly one of ROMM_FRONTEND_TICO / ROMM_FRONTEND_RETROARCH (see the Makefile FRONTEND variable)"
#endif

#ifdef ROMM_FRONTEND_TICO
    constexpr bool kIsTico = true;
#else
    constexpr bool kIsTico = false;
#endif

    // --- identity --------------------------------------------------------

    // Stable token; tags nothing user-visible, never translated.
    constexpr const char* Id() { return kIsTico ? "tico" : "retroarch"; }
    // Display name — a product name, never translated.
    constexpr const char* Name() { return kIsTico ? "Tico" : "RetroArch"; }

    // Install/app folder on the SD card, no trailing slash. Separate per
    // flavor so config.json, sync_state.json and the update machinery of the
    // two builds never collide.
    constexpr const char* AppDir() {
        return kIsTico ? "sdmc:/switch/romm-nx-tico" : "sdmc:/switch/romm-nx-retroarch";
    }
    // File name this build's NRO is installed/updated under (homebrew menu
    // convention: one folder per app, same-named NRO inside it).
    constexpr const char* NroFileName() {
        return kIsTico ? "romm-nx-tico.nro" : "romm-nx-retroarch.nro";
    }

    // --- config keys & defaults ------------------------------------------

    // Key holding the frontend's root directory inside config.json.
    constexpr const char* ConfigKeyBaseDir() {
        return kIsTico ? "tico_base_dir" : "retroarch_base_dir";
    }
    constexpr const char* DefaultBaseDir() {
        // The RetroArch Switch distribution installs under sdmc:/retroarch/
        // (lowercase); its per-core save/state trees live inside the cores/ folder.
        return kIsTico ? "sdmc:/tico/" : "sdmc:/retroarch/";
    }

    // RetroArch keeps ROMs in a content folder of its own, separate from the
    // base dir (RetroArch keeps them under cores/, Tico under the base itself); Tico lays everything out
    // under one root. kHasRomsDirSetting tells whether the build has a
    // second, ROMs-only directory setting.
    constexpr bool kHasRomsDirSetting = !kIsTico;
    // Empty for Tico — ROMs live under the base dir there.
    constexpr const char* ConfigKeyRomsDir() {
        return kIsTico ? "" : "retroarch_roms_dir";
    }
    constexpr const char* DefaultRomsDir() {
        return kIsTico ? "" : "sdmc:/retroarch/roms/";
    }

    // --- feature flags -----------------------------------------------------

    // Cover-art download and per-game platform backgrounds write into Tico's
    // assets/ folders — a Tico-only feature. RetroArch thumbnails follow a
    // different naming scheme, so the RetroArch build has no such stages.
    constexpr bool kHasCoverSync = kIsTico;

    // OTA update layout: each flavor tracks its own manifest channel tree.
    constexpr const char* DefaultUpdateBaseUrl() {
        return kIsTico
                   ? "https://romm-nx.aaaoz.fr/romm-nx-tico/"
                   : "https://romm-nx.aaaoz.fr/romm-nx-retroarch/";
    }

    // --- catalog & path dispatch ------------------------------------------
    // Implemented per flavor (FrontendTico.cpp / FrontendRetroArch.cpp), only
    // the active flavor's implementation is compiled.

    // RomM slug -> this frontend's platform folder/slug. Unknown slugs fall
    // back to a normalized form of the slug itself.
    std::string ResolvePlatformSlug(const std::string& romm_slug);
    // Whether the frontend's catalog knows this (already-resolved) slug.
    bool IsPlatformSupported(const std::string& target_slug);
    // Emulator core sent as the `emulator` field on save/state uploads and
    // (for RetroArch) the per-core subfolder name. Empty = unsupported.
    std::string ResolveCore(const std::string& romm_slug);
    // Local save file extension the frontend expects (".sav", ".srm", ...).
    std::string ResolveSaveExtension(const std::string& romm_slug);
    // Platforms this frontend supports, for tests/UI.
    const std::vector<std::string>& PlatformList();

    // --- SD folder helpers (all take a RomM slug) -------------------------
    // All return paths with a trailing '/', or "" when the frontend has no
    // usable folder for the platform.

    std::string GetRomsDir(const std::string& romm_slug);
    std::string GetSavesDir(const std::string& romm_slug);
    std::string GetStatesDir(const std::string& romm_slug);
    // Tico-only (behind kHasCoverSync): folder covers are downloaded into
    // and the full path of one game's background file.
    std::string GetCoverDir(const std::string& romm_slug);
    std::string GetBackgroundPath(const std::string& romm_slug, const std::string& game_base);

}
