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
#include <unistd.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace profiling {

constexpr const char *DEFAULT_DUMP_PATH_EXT = "/tmp/ubsocket/profiling";
constexpr const char *DUMP_FILE_PREFIX_EXT = "/ubsocket_profiling_";
constexpr const char *DUMP_FILE_SUFFIX_EXT = ".log";
constexpr uint16_t INTERVAL_DEFAULT_MIN_EXT = 1;
constexpr uint16_t INTERVAL_MIN_MIN_EXT = 1;
constexpr uint16_t INTERVAL_MAX_MIN_EXT = 5;
constexpr int COL_WIDTH_MIN_EXT = 20;
constexpr int COL_WIDTH_MAX_EXT = 30;
constexpr int SLEEP_CHUNK_MS_EXT = 10;
constexpr int UT_SLEEP_DURATION_MS_EXT = 10;

class DumpThreadExt : public Referable {
public:
    DumpThreadExt() : interval_min_(INTERVAL_DEFAULT_MIN_EXT), running_(false) {}

    ~DumpThreadExt()
    {
        DumpStopExt();
    }

    DumpThreadExt(const DumpThreadExt &) = delete;
    DumpThreadExt &operator=(const DumpThreadExt &) = delete;

    // start dump thread
    void DumpStartExt(const std::string &filePath, int intervalMin)
    {
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (running_) {
            return;
        }

        interval_min_ = intervalMin;
        if (interval_min_ < INTERVAL_MIN_MIN_EXT || interval_min_ > INTERVAL_MAX_MIN_EXT) {
            interval_min_ = INTERVAL_DEFAULT_MIN_EXT;
        }
        file_path_ = filePath;
        if (file_path_.empty()) {
            file_path_ = DEFAULT_DUMP_PATH_EXT;
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
    }

    std::chrono::milliseconds GetSleepDurationExt() const
    {
#ifdef UBSOCKET_UNIT_TEST
        return std::chrono::milliseconds(UT_SLEEP_DURATION_MS_EXT);
#else
        return std::chrono::minutes(interval_min_);
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
            << std::setw(COL_WIDTH_MIN_EXT) << "MIN(ns)" << std::setw(COL_WIDTH_MIN_EXT) << "P50(ns)"
            << std::setw(COL_WIDTH_MIN_EXT) << "P90(ns)" << std::setw(COL_WIDTH_MIN_EXT) << "P95(ns)"
            << std::setw(COL_WIDTH_MIN_EXT) << "P99(ns)" << std::setw(COL_WIDTH_MIN_EXT) << "P999(ns)"
            << "\n";
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
        if (CreateDirectoryExt(file_path_) != 0) {
            return -1;
        }

        if (file_name_.empty()) {
            std::ostringstream ossFileName;
            ossFileName << file_path_ << DUMP_FILE_PREFIX_EXT << getpid() << DUMP_FILE_SUFFIX_EXT;
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
        return 0;
    }

private:
    std::string file_path_;
    bool dir_created_ = false;
    std::string file_name_;
    std::ofstream dump_file_;
    uint16_t interval_min_ = INTERVAL_DEFAULT_MIN_EXT;
    std::atomic<bool> running_{false};
    std::thread dump_thread_;
    std::mutex start_mutex_;
};
using DumpThreadExtPtr = Ref<DumpThreadExt>;

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACEPOINT_DUMPTHREAD_EXT_H
