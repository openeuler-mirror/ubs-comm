/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: Provide statistic result print class
 * Create: 2026
 */

#ifndef UBSOCKET_PRINT_STATS_MGR_H
#define UBSOCKET_PRINT_STATS_MGR_H

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "ubsocket_global_setting.h"
#include "ubsocket_statistics.h"

namespace Statistics {

class PrintStatsMgr {
public:
    static ALWAYS_INLINE PrintStatsMgr *GetPrintStatsMgr()
    {
        static PrintStatsMgr mgr;
        return &mgr;
    }

    void ProcessStats()
    {
        std::ostringstream oss;
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        StatsMgr::UpdateReTxCount(mgr->m_trans_mode);
        StatsMgr::OutputAllStats(oss, mgr->pidVal);
        mgr->OutputJSON(oss);
    }

    static void PrintStatsMgrEventLoop()
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        while (mgr->m_running) {
            mgr->ProcessStats();
            sleep(mgr->ubsocketTraceTime);
        }
    }

    static void StartStatsCollection(uint64_t traceTime, const std::string& tracePath, uint64_t traceFileSize,
        const umq_trans_mode_t trans_mode = UMQ_TRANS_MODE_UB)
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        mgr->ubsocketTraceTime = traceTime;
        mgr->ubsocketTraceFileSize = traceFileSize;
        mgr->pidVal = static_cast<uint32_t>(getpid());
        mgr->m_trans_mode = trans_mode;

        if (!tracePath.empty()) {
            mgr->ubsocketTraceFilePath = tracePath;
        } else {
            mgr->ubsocketTraceFilePath = "/tmp/ubsocket/log";
        }

        mgr->CreateDirectory(mgr->ubsocketTraceFilePath);

        if (!mgr->m_running) {
            mgr->Start();
        }
    }

    static void StopStatsCollection()
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        if (mgr->m_running) {
            sleep(mgr->ubsocketTraceTime);
            mgr->Stop();
        }
    }

private:
    PrintStatsMgr()
        : ubsocketTraceTime(ock::ubs::UBSOCKET_TRACE_TIME_DEFAULT),
          ubsocketTraceFileSize(ock::ubs::UBSOCKET_TRACE_FILE_SIZE_DEFAULT),
          m_running(false),
          m_event_loop(nullptr),
          pidVal(0)
    {
        ubsocketTraceFilePath = "/tmp/ubsocket/log";
    }

    ~PrintStatsMgr()
    {
        Stop();
    }

    void CreateDirectory(const std::string& path)
    {
        if (path.empty()) {
            return;
        }

        constexpr mode_t DEFAULT_DIR_PERMISSION = 0750;
        std::string tmpStr = path;

        for (size_t i = 1; i < tmpStr.size(); ++i) {
            if (tmpStr[i] == '/') {
                tmpStr[i] = '\0';
                mkdir(tmpStr.c_str(), DEFAULT_DIR_PERMISSION);
                tmpStr[i] = '/';
            }
        }
        mkdir(tmpStr.c_str(), DEFAULT_DIR_PERMISSION);
    }

    void ArchiveJSON(const std::string &cleanPath, const uint32_t pid, const char *filename)
    {
        struct stat st;
        if (stat(filename, &st) == 0) {
            uint64_t currentSize = static_cast<uint64_t>(st.st_size);
            uint64_t threshold = ubsocketTraceFileSize * 1024ULL * 1024ULL;

            if (currentSize > threshold) {
                constexpr mode_t DEFAULT_FILE_PERMISSION = 0440;

                constexpr int timeBufSize = 32;
                time_t now = time(nullptr);
                char timeBuf[timeBufSize];
                struct tm timeInfo;
                if (localtime_r(&now, &timeInfo) != nullptr) {
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d%H%M%S", &timeInfo);
                } else {
                    timeBuf[0] = '\0';
                    UBS_VLOG_ERR("Failed to create timeStamp.\n");
                }

                char archiveFilename[ock::ubs::UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};

                int ret = snprintf(archiveFilename, sizeof(archiveFilename), "%s/ubsocket_kpi_%s.json",
                                   cleanPath.c_str(), timeBuf);
                if (ret < 0) {
                    UBS_VLOG_ERR("Failed to create archive filename for kpi json\n");
                    return;
                }

                if (std::rename(filename, archiveFilename) != 0) {
                    UBS_VLOG_ERR("Failed to create archiveFilename\n");
                    return;
                }

                if (chmod(archiveFilename, DEFAULT_FILE_PERMISSION) != 0) {
                    UBS_VLOG_ERR("Failed to set readonly for archiveFilename\n");
                    return;
                }

                UBS_VLOG_INFO("Successfully archive ubsocket kpi json: %s -> %s (size: %ld bytes)\n", filename,
                              archiveFilename, st.st_size);
            }
        }
    }

    void OutputJSON(std::ostringstream &oss)
    {
        const uint32_t pid = pidVal;

        char filename[ock::ubs::UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};
        std::string cleanPath(ubsocketTraceFilePath);

        int ret = snprintf(filename, sizeof(filename), "%s/ubsocket_kpi.json", cleanPath.c_str());
        if (ret < 0) {
            UBS_VLOG_ERR("Failed to create ubsocket kpi json.\n");
            return;
        }

        FILE *fp = fopen(filename, "a");
        if (fp) {
            fprintf(fp, "%s\n", oss.str().c_str());
            fclose(fp);
        } else {
            UBS_VLOG_ERR("Fail to open json file: %s\n", filename);
            return;
        }

        ArchiveJSON(cleanPath, pid, filename);
    }

    void Start()
    {
        if (m_event_loop == nullptr) {
            m_event_loop = new std::thread(PrintStatsMgrEventLoop);
            m_running = true;
        }
    }

    void Stop()
    {
        m_running = false;
        if (m_event_loop != nullptr) {
            if (m_event_loop->joinable()) {
                m_event_loop->join();
            }
            delete m_event_loop;
            m_event_loop = nullptr;
        }
    }

    uint64_t ubsocketTraceTime;
    uint64_t ubsocketTraceFileSize;
    volatile bool m_running;
    std::thread *m_event_loop;
    uint32_t pidVal;
    std::string ubsocketTraceFilePath;
    umq_trans_mode_t m_trans_mode;
};

}; // namespace Statistics

#endif
