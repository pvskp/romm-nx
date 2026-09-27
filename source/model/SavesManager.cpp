#include "SavesManager.hpp"
#include "ConfigManager.hpp"
#include "RomPathManager.hpp"
#include "JsonUtil.hpp"
#include "../navigation/HttpClient.hpp"
#include "../i18n/I18n.hpp"

#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <iostream>
#include <map>
#include <sstream>

namespace romm::model {

    SavesManager& SavesManager::Instance() {
        static SavesManager instance;
        return instance;
    }

    std::shared_ptr<PlatformSavesResult> SavesManager::FetchSavesForPlatform(int platform_id) {
        auto& config = ConfigManager::Instance();
        auto result = std::make_shared<PlatformSavesResult>();
        result->platform_id = platform_id;

        if (!config.IsValid()) {
            std::cerr << "[Saves] Fetch blocked: Configuration is invalid" << std::endl;
            result->completed = true;
            return result;
        }

        std::ostringstream url;
        url << config.GetRommHost() << "/api/saves?platform_id=" << platform_id;
        std::string url_str = url.str();

        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer " + config.GetApiKey()},
            {"Accept", "application/json"}
        };

        std::cout << "[Saves] Fetching saves for platform_id=" << platform_id << std::endl;

        HttpClient::runAsync([=]() {
            HttpResult http_res = HttpClient::getSync(url_str, headers);
            result->statusCode = http_res.statusCode;
            result->success = http_res.success;
            if (http_res.success) {
                if (!romm::model::jsonParseSaveItems(http_res.body, result->saves)) {
                    result->success = false;
                } else {
                    std::cout << "[Saves] Parsed saves count=" << result->saves.size() << std::endl;
                }
            }
            result->completed = true;
        }, HttpPriority::High);

        return result;
    }

    std::shared_ptr<SaveActionResult> SavesManager::UploadSave(int rom_id,
                                                               const std::string& file_path,
                                                               const std::string& file_name) {
        auto result = std::make_shared<SaveActionResult>();

        auto& config = ConfigManager::Instance();
        if (!config.IsValid()) {
            std::cerr << "[Saves] Upload blocked: Configuration is invalid" << std::endl;
            result->completed = true;
            result->error = romm::i18n::tr("save.error.config");
            return result;
        }

        struct stat st;
        if (stat(file_path.c_str(), &st) != 0 || st.st_size <= 0) {
            result->completed = true;
            result->error = romm::i18n::tr("save.error.filename");
            return result;
        }

        std::string url = config.GetRommHost() + "/api/saves?rom_id=" + std::to_string(rom_id);
        std::string auth = "Authorization: Bearer " + config.GetApiKey();

        std::cout << "[Saves] Uploading " << file_name << " to rom_id=" << rom_id << std::endl;

        HttpClient::runAsync([result, url, auth, file_path, file_name]() {
            CURL* curl = curl_easy_init();
            if (!curl) {
                std::cerr << "[Saves] curl init failed" << std::endl;
                result->completed = true;
                result->error = romm::i18n::tr("download.error.curl_init");
                return;
            }

            curl_mime* mime = curl_mime_init(curl);
            curl_mimepart* part = curl_mime_addpart(mime);
            curl_mime_name(part, "saveFile");
            curl_mime_filedata(part, file_path.c_str());

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, auth.c_str());
            headers = curl_slist_append(headers, "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);

            long status = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                result->success = (status >= 200 && status < 300);
                if (!result->success) {
                    std::cerr << "[Saves] Upload failed: HTTP " << status << std::endl;
                    result->error = "HTTP " + std::to_string(status);
                } else {
                    std::cout << "[Saves] Upload complete: " << file_name << std::endl;
                }
            } else {
                std::cerr << "[Saves] Upload failed: " << curl_easy_strerror(res) << std::endl;
                result->error = curl_easy_strerror(res);
            }

            curl_slist_free_all(headers);
            curl_mime_free(mime);
            curl_easy_cleanup(curl);
            result->completed = true;
        }, HttpPriority::High);

        return result;
    }

    std::shared_ptr<SaveActionResult> SavesManager::DeleteSaves(const std::vector<int>& ids) {
        auto result = std::make_shared<SaveActionResult>();

        auto& config = ConfigManager::Instance();
        if (!config.IsValid()) {
            std::cerr << "[Saves] Delete blocked: Configuration is invalid" << std::endl;
            result->completed = true;
            result->error = romm::i18n::tr("save.error.config");
            return result;
        }

        if (ids.empty()) {
            result->completed = true;
            return result;
        }

        std::string url = config.GetRommHost() + "/api/saves/delete";
        std::string auth = "Authorization: Bearer " + config.GetApiKey();

        std::string body = "{\"saves\":[";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) body += ",";
            body += std::to_string(ids[i]);
        }
        body += "]}";

        std::cout << "[Saves] Deleting " << ids.size() << " save(s) from server" << std::endl;

        HttpClient::runAsync([result, url, auth, body]() {
            CURL* curl = curl_easy_init();
            if (!curl) {
                std::cerr << "[Saves] curl init failed" << std::endl;
                result->completed = true;
                result->error = romm::i18n::tr("download.error.curl_init");
                return;
            }

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, auth.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);

            long status = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                result->success = (status >= 200 && status < 300);
                if (!result->success) {
                    std::cerr << "[Saves] Delete failed: HTTP " << status << std::endl;
                    result->error = "HTTP " + std::to_string(status);
                } else {
                    std::cout << "[Saves] Delete complete" << std::endl;
                }
            } else {
                std::cerr << "[Saves] Delete failed: " << curl_easy_strerror(res) << std::endl;
                result->error = curl_easy_strerror(res);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            result->completed = true;
        }, HttpPriority::High);

        return result;
    }

    std::vector<LocalSaveFile> SavesManager::ListLocalSaves(const std::string& platform_slug) {
        std::vector<LocalSaveFile> out;
        std::string dir_path = ConfigManager::Instance().GetSavePath(platform_slug);

        DIR* dir = opendir(dir_path.c_str());
        if (!dir) {
            return out;
        }

        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;

            std::string full = dir_path + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
                continue;
            }

            LocalSaveFile f;
            f.file_name = ent->d_name;
            f.full_path = full;
            f.size_bytes = st.st_size;
            out.push_back(f);
        }
        closedir(dir);

        return out;
    }

    std::string SavesManager::NormalizeSaveBase(const std::string& name) {
        std::string base = name;
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) {
            base = base.substr(0, dot);
        }

        std::string out;
        bool in_group = false;
        for (char c : base) {
            if (c == '[' || c == '(') {
                in_group = true;
                continue;
            }
            if (c == ']' || c == ')') {
                in_group = false;
                continue;
            }
            if (in_group) continue;
            out.push_back((char)std::tolower((unsigned char)c));
        }

        std::string collapsed;
        bool prev_space = false;
        for (char c : out) {
            if (c == ' ') {
                if (!prev_space) collapsed.push_back(c);
                prev_space = true;
            } else {
                collapsed.push_back(c);
                prev_space = false;
            }
        }
        while (!collapsed.empty() && collapsed.back() == ' ') {
            collapsed.pop_back();
        }

        return collapsed;
    }

    bool SavesManager::NamesMatch(const std::string& normalized_a, const std::string& normalized_b) {
        if (normalized_a.empty() || normalized_b.empty()) return false;
        if (normalized_a == normalized_b) return true;
        if (normalized_a.size() >= 4 && normalized_b.size() >= 4) {
            if (normalized_a.find(normalized_b) != std::string::npos) return true;
            if (normalized_b.find(normalized_a) != std::string::npos) return true;
        }
        return false;
    }

}
