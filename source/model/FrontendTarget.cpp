#include "FrontendTarget.hpp"
#include "ConfigManager.hpp"
#include "TicoCatalog.hpp"
#include "RetroArchCatalog.hpp"


namespace romm::model {

    namespace {
        const char* kTargetIds[] = { "tico", "retroarch" };
        const char* kTargetNames[] = { "Tico", "RetroArch" };
        const char* kDestinationIds[] = { "tico", "retroarch", "both" };
        const char* kDestinationNames[] = { "Tico", "RetroArch", "Both" };
    }

    const std::vector<SyncTarget>& GetAllTargets() {
        static const std::vector<SyncTarget> targets = {
            SyncTarget::Tico, SyncTarget::RetroArch
        };
        return targets;
    }

    const char* TargetId(SyncTarget target) {
        return kTargetIds[static_cast<size_t>(target)];
    }

    const char* TargetName(SyncTarget target) {
        return kTargetNames[static_cast<size_t>(target)];
    }

    bool ParseTarget(const std::string& id, SyncTarget& out) {
        for (size_t i = 0; i < 2; ++i) {
            if (id == kTargetIds[i]) {
                out = static_cast<SyncTarget>(i);
                return true;
            }
        }
        return false;
    }

    const char* DestinationId(SyncDestination dest) {
        return kDestinationIds[static_cast<size_t>(dest)];
    }

    const char* DestinationName(SyncDestination dest) {
        return kDestinationNames[static_cast<size_t>(dest)];
    }

    bool ParseDestination(const std::string& id, SyncDestination& out) {
        for (size_t i = 0; i < 3; ++i) {
            if (id == kDestinationIds[i]) {
                out = static_cast<SyncDestination>(i);
                return true;
            }
        }
        return false;
    }

    void ResolveDestination(SyncDestination dest, std::vector<SyncTarget>& out) {
        out.clear();
        switch (dest) {
            case SyncDestination::Tico:
                out.push_back(SyncTarget::Tico);
                break;
            case SyncDestination::RetroArch:
                out.push_back(SyncTarget::RetroArch);
                break;
            case SyncDestination::Both:
                out.push_back(SyncTarget::Tico);
                out.push_back(SyncTarget::RetroArch);
                break;
        }
    }

    bool DestinationIncludes(SyncDestination dest, SyncTarget target) {
        switch (dest) {
            case SyncDestination::Tico:
                return target == SyncTarget::Tico;
            case SyncDestination::RetroArch:
                return target == SyncTarget::RetroArch;
            case SyncDestination::Both:
                return true;
        }
        return false;
    }

    std::string ResolvePlatformSlugFor(SyncTarget target, const std::string& romm_slug) {
        switch (target) {
            case SyncTarget::Tico:
                return ResolveTicoPlatformSlug(romm_slug);
            case SyncTarget::RetroArch:
                return ResolveRetroArchPlatformSlug(romm_slug);
        }
        return romm_slug;
    }

    bool IsPlatformSupportedFor(SyncTarget target, const std::string& target_slug) {
        switch (target) {
            case SyncTarget::Tico:
                return IsTicoPlatformSupported(target_slug);
            case SyncTarget::RetroArch: {
                for (const auto& s : GetRetroArchPlatformList()) {
                    if (s == target_slug) return true;
                }
                return false;
            }
        }
        return false;
    }

    std::string ResolveCoreFor(SyncTarget target, const std::string& target_slug) {
        switch (target) {
            case SyncTarget::Tico:
                return ResolveTicoCore(target_slug);
            case SyncTarget::RetroArch:
                return ResolveRetroArchCore(target_slug);
        }
        return "";
    }

    std::string ResolveSaveExtensionFor(SyncTarget target, const std::string& target_slug) {
        switch (target) {
            case SyncTarget::Tico:
                return ResolveTicoSaveExtension(target_slug);
            case SyncTarget::RetroArch:
                return ResolveRetroArchSaveExtension(target_slug);
        }
        return ".sav";
    }

    std::string GetRomDirFor(SyncTarget target, const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        switch (target) {
            case SyncTarget::Tico:
                return config.GetTicoRomPath(romm_slug);
            case SyncTarget::RetroArch:
                // ROM reuse: RetroArch loads content from anywhere, so the
                // default points at the very folders the Tico sync wrote —
                // one copy of every ROM on the SD, two frontends reading it.
                if (config.RetroarchReusesTicoRoms()) {
                    return config.GetTicoRomPath(romm_slug);
                }
                return config.GetRetroArchRomPath(romm_slug);
        }
        return "";
    }

    std::string GetSaveDirFor(SyncTarget target, const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        switch (target) {
            case SyncTarget::Tico:
                return config.GetTicoSavePath(romm_slug);
            case SyncTarget::RetroArch: {
                const std::string core = ResolveCoreFor(
                    target, ResolvePlatformSlugFor(target, romm_slug));
                if (core.empty()) return "";
                return config.GetRetroArchBaseDir() + "saves/" + core + "/";
            }
        }
        return "";
    }

    std::string GetStateDirFor(SyncTarget target, const std::string& romm_slug) {
        auto& config = ConfigManager::Instance();
        switch (target) {
            case SyncTarget::Tico:
                return config.GetTicoStatePath(romm_slug);
            case SyncTarget::RetroArch: {
                const std::string core = ResolveCoreFor(
                    target, ResolvePlatformSlugFor(target, romm_slug));
                if (core.empty()) return "";
                return config.GetRetroArchBaseDir() + "states/" + core + "/";
            }
        }
        return "";
    }

}
