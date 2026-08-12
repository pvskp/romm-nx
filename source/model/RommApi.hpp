#pragma once

#include "../navigation/HttpClient.hpp"
#include "DataModel.hpp"
#include <memory>
#include <string>
#include <vector>

namespace romm::model {

    struct PlatformFetchResult {
        bool completed = false;
        bool success = false;
        long statusCode = 0;
        std::vector<Platform> platforms;
    };

    struct RomFetchResult {
        bool completed = false;
        bool success = false;
        long statusCode = 0;
        int platform_id = 0;
        int request_id = 0;
        std::vector<Game> games;
    };

    struct RomDetailFetchResult {
        bool completed = false;
        bool success = false;
        long statusCode = 0;
        int rom_id = 0;
        uint64_t generation = 0;
        std::string platform_slug;
        GameDetail detail;
    };

    struct SaveFetchResult {
        bool completed = false;
        bool success = false;
        long statusCode = 0;
        int rom_id = 0;
        std::vector<SaveEntry> saves;
    };

    class RommApi {
    public:
        static std::shared_ptr<PlatformFetchResult> fetchPlatformsAsync();
        static std::shared_ptr<RomFetchResult> fetchRomsAsync(int platformId, int requestId);
        static std::shared_ptr<RomDetailFetchResult> fetchRomDetailAsync(int romId, uint64_t generation = 0, const std::string& platform_slug = "");
        static std::shared_ptr<SaveFetchResult> fetchSavesAsync(int romId);
        // POST /api/saves?rom_id=<id>&emulator=<core>&overwrite=<bool> with a
        // multipart saveFile upload. Runs on the HTTP pool; the caller blocks
        // on result->completed (the sync worker thread, not a pool lane).
        static std::shared_ptr<HttpResult> uploadSaveAsync(
            int rom_id, const std::string& emulator, bool overwrite,
            const std::string& local_path, const std::string& file_name_on_server);
    };

}
