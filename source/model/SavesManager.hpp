#pragma once

#include <memory>
#include <string>
#include <vector>
#include "DataModel.hpp"

namespace romm::model {

    // One save file found on disk under <base>/saves/<platform>/.
    struct LocalSaveFile {
        std::string file_name;
        std::string full_path;
        long long size_bytes = 0;
    };

    // Result of GET /api/saves?platform_id=X — every save of the current
    // user belonging to ROMs of that platform.
    struct PlatformSavesResult {
        bool completed = false;
        bool success = false;
        long statusCode = 0;
        int platform_id = 0;
        std::vector<SaveEntry> saves;
    };

    // Result of one user-initiated save mutation (upload / delete).
    struct SaveActionResult {
        bool completed = false;
        bool success = false;
        std::string error;
    };

    // Owns every remote-save operation the Saves screen needs: listing a whole
    // platform's saves in one request, uploading one local file, and deleting
    // remote saves. Local-file discovery lives here too so the screen stays a
    // pure renderer of merged data.
    class SavesManager {
    public:
        static SavesManager& Instance();

        // GET /api/saves?platform_id=<id>. Never null; poll completed/success.
        std::shared_ptr<PlatformSavesResult> FetchSavesForPlatform(int platform_id);

        // POST /api/saves?rom_id=<id> with the file as multipart field
        // "saveFile". Never null; poll completed/success/error.
        std::shared_ptr<SaveActionResult> UploadSave(int rom_id,
                                                     const std::string& file_path,
                                                     const std::string& file_name);

        // POST /api/saves/delete with {"saves": [ids]}. Never null.
        std::shared_ptr<SaveActionResult> DeleteSaves(const std::vector<int>& ids);

        // Scans <base>/saves/<slug>/ for non-hidden regular files.
        std::vector<LocalSaveFile> ListLocalSaves(const std::string& platform_slug);

        // Normalizes a save file name (or game title) down to a comparable
        // base: extension removed, [..]/(..) tag groups stripped, lowercased.
        static std::string NormalizeSaveBase(const std::string& name);

        // Whether two normalized names refer to the same save. Exact equality
        // first; failing that, one containing the other (both sides long
        // enough that a real word is implied) covers short-name saves.
        static bool NamesMatch(const std::string& normalized_a, const std::string& normalized_b);

    private:
        SavesManager() = default;
    };

}
