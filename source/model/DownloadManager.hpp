#pragma once

#include <string>
#include <map>
#include <memory>
#include "DataModel.hpp"

namespace romm::model {

    // Result of downloading one save/state. The transfer (and optional rename)
    // is complete when this is returned to the caller; `success` reflects
    // whether the file landed and `error` holds a UI-readable message otherwise.
    struct SaveDownloadResult {
        bool completed = false;
        bool success = false;
        std::string error;
    };

    // Shared HTTP-to-disk helpers for the sync pipeline. The classic "download
    // games to the app's own folder" feature was removed; what remains is what
    // SyncManager (ROM/save/state/cover transfers) uses.
    class DownloadManager {
    public:
        static DownloadManager& Instance();

        // Sanitizes a file name for use on the SD card (removes path
        // separators and other characters the filesystem rejects).
        std::string SanitizeFilename(const std::string& filename);

        // URL-encodes a path component for use in a content URL
        // (/api/.../files/content/<name>). Static, no handle needed.
        static std::string EscapeUrlComponent(const std::string& component);

        // Result of a synchronous bytes-to-disk transfer.
        struct DownloadOutcome {
            bool success = false;
            long long final_size = 0;
            std::string error_message;
        };

        // Downloads `url` (with `headers`, e.g. Authorization) to `final_path`
        // using the shared .part -> verify -> rename pattern, so a power cut
        // never leaves a corrupt file at the final path. expected_size <= 0
        // accepts any non-empty file. Runs synchronously on the caller's
        // thread. Used by the sync pipeline for ROM/save/cover downloads.
        DownloadOutcome DownloadToPath(const std::string& url,
                                       const std::map<std::string, std::string>& headers,
                                       const std::string& final_path,
                                       long long expected_size);

        // Downloads one save's bytes from RomM to `final_path` (already
        // resolved by the sync pipeline, with the frontend's extension and no
        // RomM timestamp suffix). Synchronous wrapper over DownloadToPath.
        std::shared_ptr<SaveDownloadResult> DownloadSave(const SaveEntry& save,
                                                         const std::string& final_path);
        // GET /api/states/{id}/content — states share SaveEntry's shape, so
        // the download result type is the same.
        std::shared_ptr<SaveDownloadResult> DownloadStateEntry(const SaveEntry& state,
                                                               const std::string& final_path);

    private:
        DownloadManager() = default;
    };

}
