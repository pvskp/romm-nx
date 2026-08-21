#pragma once

#include <map>
#include <memory>
#include <string>
#include <functional>

struct HttpResult {
    bool completed = false;
    bool success = false;
    long statusCode = 0;
    std::string body;
    std::string error;
};

// Lane a queued task runs in. The worker pool is shared by everything the app
// does over the network, so without lanes a single FIFO lets bulk work starve
// the things the user is actually waiting on: browsing a large library queues
// hundreds of cover downloads, and the ROM-detail fetch issued when they press
// A lands behind every one of them.
//
//   High   — the user is blocked on this right now (ROM detail / description).
//   Normal — user-initiated but not blocking a visible screen (platform and
//            ROM list fetches, update checks).
//   Low    — speculative bulk work (cover downloads and their decodes).
enum class HttpPriority {
    High = 0,
    Normal = 1,
    Low = 2
};

class HttpClient {
public:
    static void init();
    static void shutdown();

    static std::shared_ptr<HttpResult> getAsync(
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        HttpPriority priority = HttpPriority::Normal);

    static HttpResult getSync(
        const std::string& url,
        const std::map<std::string, std::string>& headers);

    static std::shared_ptr<HttpResult> downloadFileAsync(
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& outputPath,
        HttpPriority priority = HttpPriority::Normal);

    // POST multipart/form-data with one file field (e.g. RomM's "saveFile")
    // plus any number of textual fields. Runs on the HTTP pool; the returned
    // result's `completed` flips when the transfer is done.
    static std::shared_ptr<HttpResult> uploadFileAsync(
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::map<std::string, std::string>& fields,
        const std::string& file_field_name,
        const std::string& file_path,
        const std::string& file_name_on_server,
        HttpPriority priority = HttpPriority::Normal);

    // POST application/json with a raw JSON body (e.g. RomM's /api/states/delete).
    // Runs on the HTTP pool; the returned result's `completed` flips when the
    // transfer is done.
    static std::shared_ptr<HttpResult> postJsonAsync(
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& json_body,
        HttpPriority priority = HttpPriority::Normal);

    static void runAsync(std::function<void()> task, HttpPriority priority = HttpPriority::Normal);
};
