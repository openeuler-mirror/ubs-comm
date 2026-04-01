/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026
 * Note:
 * History: 2026
*/

#ifndef PRINT_STATS_MGR_H
#define PRINT_STATS_MGR_H

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <ctime>
#include <cmath>
#include <thread>
#include <limits>
#include <sstream>
#include <iostream>
#include <mutex>
#include <string>
#include <iomanip>
#include "statistics.h"

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
        std::ostringstream m_oss;
        PrintStatsMgr* mgr = GetPrintStatsMgr();
        StatsMgr::OutputAllStats(m_oss, mgr->pidVal);
        mgr->OutputJSON(m_oss);
    }

    static void PrintStatsMgrEventLoop()
    {
        PrintStatsMgr* mgr = GetPrintStatsMgr();
        while (mgr->m_running) {
            mgr->ProcessStats();
            sleep(mgr->ubsocketTraceTime);
        }
    }

    static void StartStatsCollection(uint64_t traceTime, const char *tracePath, uint64_t traceFileSize)
    {
        PrintStatsMgr* mgr = GetPrintStatsMgr();
        mgr->ubsocketTraceTime = traceTime;
        mgr->ubsocketTraceFileSize = traceFileSize;
        mgr->pidVal = (uint32_t)getpid();

        if (tracePath) {
            int n = snprintf_s(mgr->ubsocketTraceFilePath, sizeof(mgr->ubsocketTraceFilePath),
                sizeof(mgr->ubsocketTraceFilePath) - 1, "%s", tracePath);
            if ((((int)sizeof(mgr->ubsocketTraceFilePath) - 1) < n) || (n < 0)) {
                (void)snprintf_s(mgr->ubsocketTraceFilePath, sizeof(mgr->ubsocketTraceFilePath),
                                sizeof(mgr->ubsocketTraceFilePath) - 1, "%s", "/tmp/ubsocket/log");
            }
        }
        mgr->CreateDirectory(mgr->ubsocketTraceFilePath);

        if (!mgr->m_running) {
            mgr->Start();
        }
    }

    static void StopStatsCollection()
    {
        PrintStatsMgr* mgr = GetPrintStatsMgr();
        if (mgr->m_running) {
            mgr->Stop();
        }
    }

private:
    PrintStatsMgr() : ubsocketTraceTime(UBSOCKET_TRACE_TIME_DEFAULT),
        ubsocketTraceFileSize(UBSOCKET_TRACE_FILE_SIZE_DEFAULT),
        m_running(false), m_event_loop(nullptr), pidVal(0)
    {
        mMgrLock = g_external_lock_ops.create(LT_EXCLUSIVE);
        (void)snprintf_s(ubsocketTraceFilePath, sizeof(ubsocketTraceFilePath),
                        sizeof(ubsocketTraceFilePath) - 1, "%s", "/tmp/ubsocket/log");
    }

    ~PrintStatsMgr()
    {
        Stop();
        g_external_lock_ops.destroy(mMgrLock);
    }

    void CreateDirectory(const char* path)
    {
        if (path == nullptr || path[0] == '\0') return;

        constexpr mode_t DEFAULT_DIR_PERMISSION = 0750;
        std::string tmp_str(path);

        for (size_t i = 1; i < tmp_str.size(); ++i) {
            if (tmp_str[i] == '/') {
                tmp_str[i] = '\0';
                mkdir(tmp_str.c_str(), DEFAULT_DIR_PERMISSION);
                tmp_str[i] = '/';
            }
        }
        mkdir(tmp_str.c_str(), DEFAULT_DIR_PERMISSION);
    }

    void ArchiveJSON(const std::string& cleanPath, const uint32_t pid, const char* filename)
    {
        struct stat st;
        if (stat(filename, &st) == 0) {
            uint64_t currentSize = static_cast<uint64_t>(st.st_size);
            uint64_t threshold = ubsocketTraceFileSize * 1024ULL * 1024ULL;
            RPC_ADPT_VLOG_INFO("file current size: %lu\n", currentSize);
            RPC_ADPT_VLOG_INFO("threshold size: %lu\n", threshold);

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
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create timeStamp.\n");
                }

                char archiveFilename[UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};

                int ret = snprintf_s(archiveFilename, sizeof(archiveFilename), sizeof(archiveFilename) - 1,
                                "%s/ubsocket_kpi_%s.json", cleanPath.c_str(), timeBuf);
                if (ret < 0 || ret >= (int)sizeof(archiveFilename)) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create archive filename for kpi json\n");
                    return;
                }

                if (std::rename(filename, archiveFilename) != 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create archiveFilename\n");
                    return;
                }

                if (chmod(archiveFilename, DEFAULT_FILE_PERMISSION) != 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to set readonly for archiveFilename\n");
                    return;
                }

                RPC_ADPT_VLOG_INFO(
                    "Successfully archive ubsocket kpi json: %s -> %s (size: %ld bytes)\n",
                    filename, archiveFilename, st.st_size);
            }
        }
    }

    void OutputJSON(std::ostringstream &oss)
    {
        const uint32_t pid = pidVal;

        char filename[UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};
        std::string cleanPath(ubsocketTraceFilePath);

        int ret = snprintf_s(filename, sizeof(filename), sizeof(filename) - 1,
                            "%s/ubsocket_kpi.json", cleanPath.c_str());
        if (ret < 0 || ret >= (int)sizeof(filename)) {
            throw std::runtime_error(
                std::string("Failed to create ubsocket kpi json") + std::to_string(ret));
        }

        FILE* fp = fopen(filename, "a");
        if (fp) {
            fprintf(fp, "%s\n", oss.str().c_str());
            fclose(fp);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Fail to open json file: %s\n", filename);
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
        ScopedUbExclusiveLocker sLock(mMgrLock);
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
    u_external_mutex_t* mMgrLock;
    char ubsocketTraceFilePath[UBSOCKET_TRACE_FILE_PATH_LEN_MAX];
};

};

#endif