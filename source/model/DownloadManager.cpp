#include "DownloadManager.hpp"
#include <switch.h>
#include "ConfigManager.hpp"
#include "RomPathManager.hpp"
#include "ScreenWakeManager.hpp"
#include <iostream>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#include "../i18n/I18n.hpp"

namespace romm::model {

    namespace {

        struct DownloadWriter {
            bool is_big_file = false;
            FILE* file_ptr = nullptr;
            FsFile fs_file = {0};
            bool fs_file_open = false;
            Result last_write_error = 0;
            s64 write_offset = 0;
            bool crossed_4GiB_logged = false;

            // BigFile writes go straight to the FS sysmodule via fsFileWrite, which is
            // a synchronous IPC round-trip. curl hands us one callback per TLS record
            // (~16 KB), so without batching a 4+ GB ISO means hundreds of thousands of
            // individual IPC calls and throughput tanks. Coalesce into large chunks
            // before touching the filesystem.
            static constexpr size_t kFlushBufferSize = 4 * 1024 * 1024; // 4 MB
            std::vector<u8> flush_buffer;
            size_t flush_buffer_used = 0;
        };

        bool FlushBigFileBuffer(DownloadWriter* writer) {
            if (writer->flush_buffer_used == 0) return true;

            s64 next_offset = writer->write_offset + (s64)writer->flush_buffer_used;
            if (!writer->crossed_4GiB_logged && next_offset > 0xFFFFFFFFLL && writer->write_offset <= 0xFFFFFFFFLL) {
                std::cout << "[BigFile] crossed_4GiB offset=" << next_offset << std::endl;
                writer->crossed_4GiB_logged = true;
            }

            Result rc = fsFileWrite(&(writer->fs_file), writer->write_offset, writer->flush_buffer.data(), writer->flush_buffer_used, FsWriteOption_None);
            if (R_FAILED(rc)) {
                writer->last_write_error = rc;
                std::cerr << "[BigFile] write failed at offset=" << writer->write_offset << " error=" << rc << std::endl;
                return false;
            }
            writer->write_offset += (s64)writer->flush_buffer_used;
            writer->flush_buffer_used = 0;
            return true;
        }

        Result DeleteLogicalFile(const std::string& stdio_path) {
            FsFileSystem* fs = nullptr;
            char fs_path[FS_MAX_PATH] = {0};
            if (fsdevTranslatePath(stdio_path.c_str(), &fs, fs_path) != 0) {
                unlink(stdio_path.c_str());
                return -1;
            }

            // 1. Try fsFsDeleteFile first
            Result rc = fsFsDeleteFile(fs, fs_path);
            if (R_SUCCEEDED(rc)) {
                return rc;
            }

            // 2. If it fails, check if it's a directory
            FsDirEntryType type;
            if (R_SUCCEEDED(fsFsGetEntryType(fs, fs_path, &type))) {
                if (type == FsDirEntryType_Dir) {
                    // Confirm that it cannot be opened as a logical file
                    FsFile f;
                    Result open_rc = fsFsOpenFile(fs, fs_path, FsOpenMode_Read, &f);
                    if (R_FAILED(open_rc)) {
                        rc = fsFsDeleteDirectoryRecursively(fs, fs_path);
                        return rc;
                    } else {
                        fsFileClose(&f);
                    }
                }
            }

            unlink(stdio_path.c_str());
            return rc;
        }

        size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            DownloadWriter* writer = static_cast<DownloadWriter*>(userp);
            size_t bytes_to_write = size * nmemb;
            if (bytes_to_write == 0) return 0;

            if (writer->is_big_file) {
                if (writer->flush_buffer.empty()) {
                    writer->flush_buffer.resize(DownloadWriter::kFlushBufferSize);
                }

                const u8* src = static_cast<const u8*>(contents);
                size_t remaining = bytes_to_write;
                while (remaining > 0) {
                    size_t space = DownloadWriter::kFlushBufferSize - writer->flush_buffer_used;
                    size_t take = std::min(space, remaining);
                    std::copy(src, src + take, writer->flush_buffer.begin() + writer->flush_buffer_used);
                    writer->flush_buffer_used += take;
                    src += take;
                    remaining -= take;

                    if (writer->flush_buffer_used == DownloadWriter::kFlushBufferSize) {
                        if (!FlushBigFileBuffer(writer)) {
                            return 0; // abort — last_write_error already set
                        }
                    }
                }
                return bytes_to_write;
            } else {
                size_t written = fwrite(contents, size, nmemb, writer->file_ptr);
                return written;
            }
        }

    } // namespace

    DownloadManager& DownloadManager::Instance() {
        static DownloadManager inst;
        return inst;
    }

    std::string DownloadManager::SanitizeFilename(const std::string& filename) {
        std::string safe = filename;
        const std::string invalid_chars = "\\/:*?\"<>|";
        for (char& c : safe) {
            if (invalid_chars.find(c) != std::string::npos || c < 32) {
                c = '_';
            }
        }
        return safe;
    }

    std::string DownloadManager::EscapeUrlComponent(const std::string& component) {
        CURL* curl = curl_easy_init();
        if (!curl) return component;
        char* encoded = curl_easy_escape(curl, component.c_str(), 0);
        std::string out = encoded ? encoded : component;
        if (encoded) curl_free(encoded);
        curl_easy_cleanup(curl);
        return out;
    }

    DownloadManager::DownloadOutcome DownloadManager::DownloadToPath(
            const std::string& url,
            const std::map<std::string, std::string>& headers,
            const std::string& final_path,
            long long expected_size) {
        DownloadOutcome outcome;

        std::string part_path = final_path + ".part";

        // Ensure the file's parent exists (recursively — on a device where
        // the frontend folder tree doesn't exist yet, a one-level mkdir
        // silently fails and the whole sync stage fails).
        size_t slash = final_path.find_last_of('/');
        if (slash != std::string::npos) {
            RomPathManager::CreateFolderIfMissing(final_path.substr(0, slash));
        }
        // Fresh-write semantics: only the stale .part from a previous attempt
        // is removed now. The current file at `final_path` is left intact and
        // only replaced by the *validated* download, right before the rename
        // below — so a power cut never loses a good copy while transferring.
        DeleteLogicalFile(part_path);

        // Big files (>4GiB, e.g. Wii ISOs) need the fs big-file API: fopen has
        // a 4GiB ceiling on the Switch. Smaller files take the plain FILE path.
        bool use_big_file = expected_size > 0xFFFFFFFFLL;
        if (use_big_file) {
            std::cout << "[BigFile] enabled expected_size=" << expected_size << std::endl;
        }

        DownloadWriter writer;
        writer.is_big_file = use_big_file;

        if (use_big_file) {
            FsFileSystem* fs = nullptr;
            char fs_path[FS_MAX_PATH] = {0};
            if (fsdevTranslatePath(part_path.c_str(), &fs, fs_path) != 0) {
                std::cerr << "[BigFile] Path translation failed: " << part_path << std::endl;
                outcome.error_message = romm::i18n::tr("download.error.path_translation");
                return outcome;
            }

            Result rc = fsFsCreateFile(fs, fs_path, expected_size, FsCreateOption_BigFile);
            if (R_FAILED(rc)) {
                outcome.error_message = romm::i18n::format("download.error.bigfile_create", {{"code", std::to_string(rc)}});
                return outcome;
            }

            rc = fsFsOpenFile(fs, fs_path, FsOpenMode_Write, &writer.fs_file);
            if (R_FAILED(rc)) {
                std::cerr << "[BigFile] Open file failed: " << rc << std::endl;
                outcome.error_message = romm::i18n::tr("download.error.bigfile_open");
                return outcome;
            }
            writer.fs_file_open = true;
            writer.write_offset = 0;
        } else {
            writer.file_ptr = fopen(part_path.c_str(), "wb");
            if (!writer.file_ptr) {
                std::cerr << "[Download] Could not open .part file for writing." << std::endl;
                outcome.error_message = romm::i18n::tr("download.error.part_file");
                return outcome;
            }
            static char file_buf[512 * 1024];
            setvbuf(writer.file_ptr, file_buf, _IOFBF, sizeof(file_buf));
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            if (use_big_file) {
                fsFileClose(&writer.fs_file);
            } else {
                fclose(writer.file_ptr);
            }
            outcome.error_message = romm::i18n::tr("download.error.curl_init");
            return outcome;
        }

        struct curl_slist* hlist = NULL;
        for (const auto& entry : headers) {
            hlist = curl_slist_append(hlist, (entry.first + ": " + entry.second).c_str());
        }
        hlist = curl_slist_append(hlist, "Expect:");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 524288L);

        ScreenWakeManager::Instance().RequestUpdate();
        std::cout << "[Download] Downloading to " << final_path << std::endl;

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(hlist);
        curl_easy_cleanup(curl);

        bool success = (res == CURLE_OK);

        if (success && use_big_file) {
            if (!FlushBigFileBuffer(&writer)) {
                success = false;
            }
            if (success && writer.write_offset != expected_size) {
                std::cerr << "[Download] Size mismatch: write_offset=" << writer.write_offset
                          << " expected=" << expected_size << std::endl;
                success = false;
            }
            fsFileClose(&writer.fs_file);
            if (success) {
                FsFileSystem* fs = nullptr;
                char fs_part_path[FS_MAX_PATH] = {0};
                char fs_final_path[FS_MAX_PATH] = {0};
                if (fsdevTranslatePath(part_path.c_str(), &fs, fs_part_path) == 0 &&
                    fsdevTranslatePath(final_path.c_str(), &fs, fs_final_path) == 0) {
                    // Validated: the original file is only replaced now, so a
                    // mid-transfer power cut cannot lose it (fsFsRenameFile
                    // refuses to overwrite an existing destination).
                    DeleteLogicalFile(final_path);
                    fsFsCommit(fs);
                    Result rename_rc = fsFsRenameFile(fs, fs_part_path, fs_final_path);
                    if (R_FAILED(rename_rc)) {
                        success = false;
                    } else {
                        fsFsCommit(fs);
                        // Verify the final logical size after reopen.
                        FsFile final_file;
                        Result open_rc = fsFsOpenFile(fs, fs_final_path, FsOpenMode_Read, &final_file);
                        if (R_SUCCEEDED(open_rc)) {
                            s64 sz = 0;
                            Result sz_rc = fsFileGetSize(&final_file, &sz);
                            fsFileClose(&final_file);
                            if (R_SUCCEEDED(sz_rc) && sz == expected_size) {
                                outcome.final_size = sz;
                            } else {
                                success = false;
                            }
                        } else {
                            success = false;
                        }
                    }
                } else {
                    success = false;
                }
            }
        } else if (success) {
            fclose(writer.file_ptr);
            struct stat check_buffer;
            if (stat(part_path.c_str(), &check_buffer) == 0 && check_buffer.st_size > 0 &&
                (expected_size <= 0 || check_buffer.st_size == expected_size)) {
                // Validated: only now replace any previous file at the target.
                DeleteLogicalFile(final_path);
                if (rename(part_path.c_str(), final_path.c_str()) != 0) {
                    success = false;
                } else {
                    outcome.final_size = check_buffer.st_size;
                }
            } else {
                success = false;
            }
        } else {
            if (use_big_file) {
                if (writer.fs_file_open) {
                    fsFileClose(&writer.fs_file);
                }
            } else if (writer.file_ptr) {
                fclose(writer.file_ptr);
            }
        }

        if (!success) {
            DeleteLogicalFile(part_path);
            if (res != CURLE_OK) {
                outcome.error_message = curl_easy_strerror(res);
            } else {
                outcome.error_message = romm::i18n::tr("download.error.verification");
            }
            return outcome;
        }

        outcome.success = true;
        return outcome;
    }

    std::shared_ptr<SaveDownloadResult> DownloadManager::DownloadSave(
            const SaveEntry& save, const std::string& final_path) {
        auto result = std::make_shared<SaveDownloadResult>();

        auto& config = ConfigManager::Instance();
        if (!config.IsValid()) {
            std::cerr << "[Save] Download blocked: Configuration is invalid" << std::endl;
            result->completed = true;
            result->error = romm::i18n::tr("save.error.config");
            return result;
        }

        std::string url = config.GetRommHost() + "/api/saves/" + std::to_string(save.id) + "/content";
        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer " + config.GetApiKey()}
        };

        // The save folder may not exist yet on a fresh frontend install
        // (RetroArch's per-core saves/<core>/ in particular) — create the
        // parent chain or the write below fails.
        {
            size_t slash = final_path.find_last_of('/');
            if (slash != std::string::npos) {
                RomPathManager::CreateFolderIfMissing(final_path.substr(0, slash));
            }
        }

        std::cout << "[Save] Downloading rom_id=" << save.rom_id << " save_id=" << save.id
                  << " -> " << final_path << std::endl;

        DownloadOutcome oc = DownloadToPath(url, headers, final_path, save.file_size_bytes);
        result->completed = true;
        result->success = oc.success;
        if (!oc.success) {
            result->error = oc.error_message.empty() ? romm::i18n::tr("save.error.write")
                                                     : oc.error_message;
        }
        return result;
    }

    std::shared_ptr<SaveDownloadResult> DownloadManager::DownloadStateEntry(
            const SaveEntry& state, const std::string& final_path) {
        auto result = std::make_shared<SaveDownloadResult>();

        auto& config = ConfigManager::Instance();
        if (!config.IsValid()) {
            std::cerr << "[State] Download blocked: Configuration is invalid" << std::endl;
            result->completed = true;
            result->error = romm::i18n::tr("save.error.config");
            return result;
        }

        std::string url = config.GetRommHost() + "/api/states/" + std::to_string(state.id) + "/content";
        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer " + config.GetApiKey()}
        };

        // Same parent-chain creation as the save path above.
        {
            size_t slash = final_path.find_last_of('/');
            if (slash != std::string::npos) {
                RomPathManager::CreateFolderIfMissing(final_path.substr(0, slash));
            }
        }

        std::cout << "[State] Downloading rom_id=" << state.rom_id << " state_id=" << state.id
                  << " -> " << final_path << std::endl;

        DownloadOutcome oc = DownloadToPath(url, headers, final_path, state.file_size_bytes);
        result->completed = true;
        result->success = oc.success;
        if (!oc.success) {
            result->error = oc.error_message.empty() ? romm::i18n::tr("save.error.write")
                                                     : oc.error_message;
        }
        return result;
    }

}
