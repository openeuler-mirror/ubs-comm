/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_EXT_H
#define UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_EXT_H

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"

namespace ock {
namespace ubs {
namespace profiling {

constexpr const char *DEFAULT_DUMP_PATH_EXT = "/tmp/ubsocket/profiling";
constexpr const char *DUMP_FILE_PREFIX_EXT = "/ubsocket_profiling_";
constexpr const char *DUMP_FILE_SUFFIX_EXT = ".log";
constexpr const char *DUMP_ARCHIVE_SUFFIX_EXT = ".gz";
constexpr uint16_t INTERVAL_DEFAULT_MIN_EXT = 1;
constexpr uint16_t INTERVAL_MIN_MIN_EXT = 1;
constexpr uint16_t INTERVAL_MAX_MIN_EXT = 5;
constexpr int COL_WIDTH_MIN_EXT = 20;
constexpr int COL_WIDTH_MAX_EXT = 45;
constexpr int SLEEP_CHUNK_MS_EXT = 10;
constexpr int UT_SLEEP_DURATION_MS_EXT = 10;
constexpr int64_t DUMP_FILE_MAX_SIZE_EXT = 10 * 1024 * 1024; /* 10 MB */
constexpr int DUMP_MAX_ARCHIVES_EXT = 3;

class DumpThreadExt : public Referable {
public:
    DumpThreadExt() : running_(false) {}

    ~DumpThreadExt()
    {
        DumpStopExt();
    }

    DumpThreadExt(const DumpThreadExt &) = delete;
    DumpThreadExt &operator=(const DumpThreadExt &) = delete;

    // start dump thread (filePath and intervalMin are unused; DumpThreadExt reads from GlobalSetting at runtime)
    void DumpStartExt(const std::string &filePath, int intervalMin)
    {
        (void)filePath;
        (void)intervalMin;
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (running_) {
            return;
        }

        running_ = true;
        dump_thread_ = std::thread(&DumpThreadExt::DumpLoopExt, this);
    }

    // stop dump thread
    void DumpStopExt()
    {
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (!running_) {
            return;
        }

        running_ = false;
        if (dump_thread_.joinable()) {
            dump_thread_.join();
        }

        if (dump_file_.is_open()) {
            dump_file_.close();
            dir_created_ = false;
        }
    }

private:
    // thread scheduled to execute of dump data periodically
    void DumpLoopExt()
    {
        pthread_setname_np(pthread_self(), "ubs_prof_ext");

        while (running_) {
            auto sleepDuration = GetSleepDurationExt();
            auto chunkMs = std::chrono::milliseconds(SLEEP_CHUNK_MS_EXT);
            auto elapsed = std::chrono::milliseconds(0);
            while (running_ && elapsed < sleepDuration) {
                auto remaining = sleepDuration - elapsed;
                auto sleepChunk = (remaining < chunkMs) ? remaining : chunkMs;
                std::this_thread::sleep_for(sleepChunk);
                elapsed += sleepChunk;
            }
            if (!running_) {
                break;
            }
            DumpDataExt();
        }

        // Final drain before exit: guarantee the last batch of samples is
        // flushed to disk.
        DumpDataExt();
    }

    std::chrono::milliseconds GetSleepDurationExt() const
    {
#ifdef UBSOCKET_UNIT_TEST
        return std::chrono::milliseconds(UT_SLEEP_DURATION_MS_EXT);
#else
        uint16_t interval = GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN;
        if (interval < INTERVAL_MIN_MIN_EXT || interval > INTERVAL_MAX_MIN_EXT) {
            interval = INTERVAL_DEFAULT_MIN_EXT;
        }
        return std::chrono::minutes(interval);
#endif
    }

    // Truly dump the data
    void DumpDataExt() noexcept;

    void WriteDumpTitleExt(std::ostringstream &oss)
    {
        constexpr int timeBufSize = 32;
        time_t now = time(nullptr);
        char timeBuf[timeBufSize];
        struct tm timeInfo;
        if (localtime_r(&now, &timeInfo) != nullptr) {
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
        } else {
            timeBuf[0] = '\0';
            UBS_VLOG_WARN("Failed to create timeStamp.\n");
        }
        oss << "timeStamp: " << timeBuf << "\n";
        oss << std::left << std::setw(COL_WIDTH_MAX_EXT) << "[TRACE_NAME]" << std::setw(COL_WIDTH_MIN_EXT) << "SUCCESS"
            << std::setw(COL_WIDTH_MIN_EXT) << "FAILURE" << std::setw(COL_WIDTH_MIN_EXT) << "TOTAL(ns)"
            << std::setw(COL_WIDTH_MIN_EXT) << "AVG(ns)" << std::setw(COL_WIDTH_MIN_EXT) << "MAX(ns)"
            << std::setw(COL_WIDTH_MIN_EXT) << "MIN(ns)" << std::setw(COL_WIDTH_MIN_EXT) << "P99(ns)"
            << std::setw(COL_WIDTH_MIN_EXT) << "P9999(ns)" << "\n";
    }

    int CreateDirectoryExt(std::string &path)
    {
        if (dir_created_) {
            return 0;
        }

        if (path.empty()) {
            path = DEFAULT_DUMP_PATH_EXT;
        }

        constexpr mode_t DEFAULT_DIR_PERMISSION = 0750;
        std::string current_path;

        for (char c : path) {
            current_path += c;
            if (c == '/') {
                if (mkdir(current_path.c_str(), DEFAULT_DIR_PERMISSION) == -1) {
                    if (errno == EEXIST) {
                        continue;
                    }
                    UBS_VLOG_WARN("File path %s creation skipped, errno: %d, errmsg: %s.\n", current_path.c_str(),
                                  errno, Func::Error2Str(errno));
                    return -1;
                }
            }
        }

        if (mkdir(path.c_str(), DEFAULT_DIR_PERMISSION) == -1 && errno != EEXIST) {
            UBS_VLOG_WARN("File path %s creation skipped, errno: %d, errmsg: %s.\n", path.c_str(), errno,
                          Func::Error2Str(errno));
            return -1;
        }
        dir_created_ = true;
        return 0;
    }

    int WriteDumpDataExt(std::ostringstream &oss)
    {
        std::string currentPath;
        {
            std::lock_guard<std::mutex> lock(GlobalSetting::ProfDumpMutex);
            currentPath = GlobalSetting::UBS_PROF_DUMP_PATH;
        }
        if (currentPath.empty()) {
            currentPath = DEFAULT_DUMP_PATH_EXT;
        }

        if (currentPath != last_file_path_) {
            if (dump_file_.is_open()) {
                dump_file_.close();
            }
            file_name_.clear();
            dir_created_ = false;
            last_file_path_ = currentPath;
        }

        if (CreateDirectoryExt(currentPath) != 0) {
            return -1;
        }

        if (file_name_.empty()) {
            std::ostringstream ossFileName;
            ossFileName << currentPath << DUMP_FILE_PREFIX_EXT << getpid() << DUMP_FILE_SUFFIX_EXT;
            file_name_ = ossFileName.str();
        }

        if (!dump_file_.is_open()) {
            dump_file_.open(file_name_, std::ios::out | std::ios::app);
            if (!dump_file_.is_open()) {
                UBS_VLOG_WARN("File %s open skipped, errno: %d, errmsg: %s.\n", file_name_.c_str(), errno,
                              Func::Error2Str(errno));
                return -1;
            } else {
                UBS_VLOG_DEBUG("File %s open success.\n", file_name_.c_str());
            }
        }

        if (dump_file_.is_open()) {
            dump_file_ << oss.str() << std::endl;
            dump_file_.flush();
        }

        RotateDumpFileExt();
        return 0;
    }

    bool CompressFileExt(const std::string &filePath)
    {
        pid_t pid = fork();
        if (pid < 0) {
            UBS_VLOG_WARN("fork failed for compress, errno: %d.\n", errno);
            return false;
        }
        if (pid == 0) {
            execlp("gzip", "gzip", "-f", filePath.c_str(), nullptr);
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            UBS_VLOG_WARN("waitpid failed for compress, errno: %d.\n", errno);
            return false;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        }
        UBS_VLOG_WARN("gzip compress failed for %s, status: %d.\n", filePath.c_str(), status);
        return false;
    }

    void RotateDumpFileExt()
    {
        struct stat st;
        if (stat(file_name_.c_str(), &st) != 0) {
            return;
        }
        if (st.st_size < DUMP_FILE_MAX_SIZE_EXT) {
            return;
        }

        if (dump_file_.is_open()) {
            dump_file_.close();
        }

        if (!CompressFileExt(file_name_)) {
            unlink(file_name_.c_str());
        }

        std::string archivePattern =
            DUMP_FILE_PREFIX_EXT + std::to_string(getpid()) + DUMP_FILE_SUFFIX_EXT;
        std::vector<std::string> archives;
        DIR *dir = opendir(last_file_path_.c_str());
        if (dir != nullptr) {
            struct dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name.find(archivePattern) != std::string::npos &&
                    name.size() > archivePattern.size() &&
                    name.substr(archivePattern.size()) == DUMP_ARCHIVE_SUFFIX_EXT) {
                    archives.push_back(last_file_path_ + "/" + name);
                }
            }
            closedir(dir);
        }

        std::sort(archives.begin(), archives.end(), [](const std::string &a, const std::string &b) {
            struct stat sa, sb;
            if (stat(a.c_str(), &sa) != 0) {
                return true;
            }
            if (stat(b.c_str(), &sb) != 0) {
                return false;
            }
            return sa.st_mtime < sb.st_mtime;
        });

        while (static_cast<int>(archives.size()) > DUMP_MAX_ARCHIVES_EXT) {
            unlink(archives.front().c_str());
            archives.erase(archives.begin());
        }

        file_name_.clear();
    }

private:
    std::string last_file_path_;
    bool dir_created_ = false;
    std::string file_name_;
    std::ofstream dump_file_;
    std::atomic<bool> running_{false};
    std::thread dump_thread_;
    std::mutex start_mutex_;
};
using DumpThreadExtPtr = Ref<DumpThreadExt>;

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_EXT_H
