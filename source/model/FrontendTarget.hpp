#pragma once

#include <string>
#include <vector>

// The sync destination abstraction. A "target" is one installed frontend on
// the SD card (Tico or RetroArch); every per-frontend decision — platform
// folders, cores, save extensions, base paths — is resolved through the
// dispatchers below so neither SyncManager nor the data screens know the
// folder conventions of any specific frontend.
namespace romm::model {

    enum class SyncTarget { Tico, RetroArch };

    // Stable order used everywhere targets are iterated or listed.
    const std::vector<SyncTarget>& GetAllTargets();

    // "tico" / "retroarch" — token persisted in config.json and
    // sync_state.json. Never translated.
    const char* TargetId(SyncTarget target);
    // Display name ("Tico" / "RetroArch") — a product name, also never
    // translated, but kept separate from the token for clarity.
    const char* TargetName(SyncTarget target);
    // Parses a persisted token; returns false (leaving `out` untouched) for
    // anything unknown, so hand-edited files fall back safely.
    bool ParseTarget(const std::string& id, SyncTarget& out);

    // What the user picks for one sync run: a single target or both.
    enum class SyncDestination { Tico, RetroArch, Both };

    const char* DestinationId(SyncDestination dest);
    const char* DestinationName(SyncDestination dest);
    bool ParseDestination(const std::string& id, SyncDestination& out);
    // Expands a destination into the ordered target list to run.
    void ResolveDestination(SyncDestination dest, std::vector<SyncTarget>& out);

    // True when `dest` includes `target`.
    bool DestinationIncludes(SyncDestination dest, SyncTarget target);

    // --- catalog dispatch ------------------------------------------------

    // RomM slug -> this target's platform folder/slug.
    std::string ResolvePlatformSlugFor(SyncTarget target, const std::string& romm_slug);
    // Whether the target's catalog knows this (already-resolved) slug.
    bool IsPlatformSupportedFor(SyncTarget target, const std::string& target_slug);
    // Emulator core sent as the `emulator` field on save/state uploads, and
    // (for RetroArch) the saves/<core>/ subfolder name. Empty = unsupported.
    std::string ResolveCoreFor(SyncTarget target, const std::string& target_slug);
    // Local save file extension the frontend expects (".sav", ".srm", ...).
    std::string ResolveSaveExtensionFor(SyncTarget target, const std::string& target_slug);

    // --- path dispatch (config-driven) ------------------------------------
    // All return paths with a trailing '/'.

    // ROM folder for a platform. RetroArch reuses Tico's ROM folders when
    // configured to, so both targets point at the same files and no ROM is
    // ever copied twice.
    std::string GetRomDirFor(SyncTarget target, const std::string& romm_slug);
    // Battery-save folder. For RetroArch this is <base>/saves/<core>/ and is
    // empty when the platform has no core (unsupported).
    std::string GetSaveDirFor(SyncTarget target, const std::string& romm_slug);
    // Save-state folder. For RetroArch this is <base>/states/<core>/.
    std::string GetStateDirFor(SyncTarget target, const std::string& romm_slug);

}
