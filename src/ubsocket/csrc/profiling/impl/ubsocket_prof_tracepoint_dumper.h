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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_H
#define UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_H

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

constexpr const char *DEFAULT_DUMP_PATH = "/tmp/ubsocket/profiling";
constexpr const char *DUMP_FILE_PREFIX = "/ubsocket_profiling_";
constexpr const char *DUMP_FILE_SUFFIX = ".log";
constexpr const char *DUMP_ARCHIVE_SUFFIX = ".gz";
constexpr uint16_t INTERVAL_DEFAULT_MIN = 1;
constexpr uint16_t INTERVAL_MIN_MIN = 1;
constexpr uint16_t INTERVAL_MAX_MIN = 5;
constexpr int COL_WIDTH_MIN = 20;
constexpr int COL_WIDTH_MAX = 45;
constexpr int SLEEP_CHUNK_MS = 10;
constexpr int UT_SLEEP_DURATION_MS = 10;
constexpr int64_t DUMP_FILE_MAX_SIZE = 10 * 1024 * 1024; /* 10 MB */
constexpr int DUMP_MAX_ARCHIVES = 3;

class DumpThread : public Referable {
public:
    DumpThread() : running_(false) {}

    ~DumpThread()
    {
        DumpStop();
    }

    DumpThread(const DumpThread &) = delete;
    DumpThread &operator=(const DumpThread &) = delete;

    // start dump thread (filePath and intervalMin are unused; DumpThread reads from GlobalSetting at runtime)
    void DumpStart(const std::string &filePath, int intervalMin)
    {
        (void)filePath;
        (void)intervalMin;
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (running_) {
            return;
        }

        running_ = true;
        dump_thread_ = std::thread(&DumpThread::DumpLoop, this);
    }

    void DumpStop()
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
    void DumpLoop()
    {
        pthread_setname_np(pthread_self(), "ubs_prof");

        while (running_) {
            auto sleepDuration = GetSleepDuration();
            auto chunkMs = std::chrono::milliseconds(SLEEP_CHUNK_MS);
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
            DumpData();
        }

        // Final drain before exit: guarantee the last batch of samples is
        // flushed to disk.
        DumpData();
    }

    std::chrono::milliseconds GetSleepDuration() const
    {
#ifdef UBSOCKET_UNIT_TEST
        return std::chrono::milliseconds(UT_SLEEP_DURATION_MS);
#else
        uint16_t interval = GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN;
        if (interval < INTERVAL_MIN_MIN || interval > INTERVAL_MAX_MIN) {
            interval = INTERVAL_DEFAULT_MIN;
        }
        return std::chrono::minutes(interval);
#endif
    }

    // Truly dump the data
    void DumpData() noexcept;

    void WriteDumpTitle(std::ostringstream &oss)
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
        oss << std::left << std::setw(COL_WIDTH_MAX) << "[TRACE_NAME]" << std::setw(COL_WIDTH_MIN) << "SUCCESS"
            << std::setw(COL_WIDTH_MIN) << "FAILURE" << std::setw(COL_WIDTH_MIN) << "TOTAL(ns)"
            << std::setw(COL_WIDTH_MIN) << "AVG(ns)" << std::setw(COL_WIDTH_MIN) << "MAX(ns)"
            << std::setw(COL_WIDTH_MIN) << "MIN(ns)"
            << "\n";
    }

    int CreateDirectory(std::string &path)
    {
        if (dir_created_) {
            return 0;
        }

        if (path.empty()) {
            path = DEFAULT_DUMP_PATH;
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

    int WriteDumpData(std::ostringstream &oss)
    {
        std::string currentPath;
        {
            std::lock_guard<std::mutex> lock(GlobalSetting::ProfDumpMutex);
            currentPath = GlobalSetting::UBS_PROF_DUMP_PATH;
        }
        if (currentPath.empty()) {
            currentPath = DEFAULT_DUMP_PATH;
        }

        /* detect path change: close current file and reset state */
        if (currentPath != last_file_path_) {
            if (dump_file_.is_open()) {
                dump_file_.close();
            }
            file_name_.clear();
            dir_created_ = false;
            last_file_path_ = currentPath;
        }

        if (CreateDirectory(currentPath) != 0) {
            return -1;
        }

        if (file_name_.empty()) {
            std::ostringstream ossFileName;
            ossFileName << currentPath << DUMP_FILE_PREFIX << getpid() << DUMP_FILE_SUFFIX;
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

        RotateDumpFile();
        return 0;
    }

    /* Compress file at filePath to filePath.gz via gzip.
     * Returns true on success, false on compress failure or gzip not found.
     * On success the original file is removed by gzip (gzip replaces in-place). */
    bool CompressFile(const std::string &filePath)
    {
        pid_t pid = fork();
        if (pid < 0) {
            UBS_VLOG_WARN("fork failed for compress, errno: %d.\n", errno);
            return false;
        }
        if (pid == 0) {
            /* child */
            execlp("gzip", "gzip", "-f", filePath.c_str(), nullptr);
            /* exec failed */
            _exit(127);
        }
        /* parent */
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

    /* Rotate the dump file when it exceeds DUMP_FILE_MAX_SIZE.
     * Closes the current file, compresses it to .gz, deletes oldest archives
     * beyond DUMP_MAX_ARCHIVES, and resets file_name_ so a new file is opened
     * on the next WriteDumpData call. */
    void RotateDumpFile()
    {
        struct stat st;
        if (stat(file_name_.c_str(), &st) != 0) {
            return;
        }
        if (st.st_size < DUMP_FILE_MAX_SIZE) {
            return;
        }

        /* close current file */
        if (dump_file_.is_open()) {
            dump_file_.close();
        }

        /* compress the rotated file; on failure remove it manually so
         * subsequent dumps can start fresh */
        if (!CompressFile(file_name_)) {
            unlink(file_name_.c_str());
        }

        /* prune oldest .gz archives: keep at most DUMP_MAX_ARCHIVES */
        std::string archivePattern = DUMP_FILE_PREFIX + std::to_string(getpid()) + DUMP_FILE_SUFFIX;
        std::vector<std::string> archives;
        DIR *dir = opendir(last_file_path_.c_str());
        if (dir != nullptr) {
            struct dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name.find(archivePattern) != std::string::npos &&
                    name.size() > archivePattern.size() &&
                    name.substr(archivePattern.size()) == DUMP_ARCHIVE_SUFFIX) {
                    archives.push_back(last_file_path_ + "/" + name);
                }
            }
            closedir(dir);
        }

        /* sort by modification time ascending (oldest first) */
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

        /* remove oldest archives beyond the limit */
        while (static_cast<int>(archives.size()) > DUMP_MAX_ARCHIVES) {
            unlink(archives.front().c_str());
            archives.erase(archives.begin());
        }

        /* reset file_name_ so next WriteDumpData opens a fresh .log */
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
using DumpThreadPtr = Ref<DumpThread>;

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_H