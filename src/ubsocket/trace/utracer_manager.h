/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#ifndef UTRACER_MANAGER_H
#define UTRACER_MANAGER_H

#include <cerrno>
#include <sys/cdefs.h>
#include <iostream>
#include <fcntl.h>
#include <fstream>
#include "utracer.h"
#include "utracer_info.h"
#include "rpc_adpt_vlog.h"

namespace Statistics {
class UTracerManager {
public:
    static __always_inline UTracerInfo *Instance()
    {
        static UTracerInfo *tracePoints = CreateInstance();
        return tracePoints;
    }

    static __always_inline void DelayBegin(uint32_t tpId, const char *tpName)
    {
        auto instance = Instance();
        if (tpId >= MAX_TRACE_POINT_NUM) {
            return;
        }
        instance[tpId].DelayBegin(tpName);
    }

    static __always_inline void DelayEnd(uint32_t tpId, const uint64_t diff, int32_t retCode)
    {
        auto instance = Instance();
        if (tpId >= MAX_TRACE_POINT_NUM) {
            return;
        }
        if (mDumpEnable) {
            DumpTraceSplitInfo(instance[tpId].GetName(), diff, retCode);
        }
        instance[tpId].DelayEnd(diff, retCode, mLatencyQuantileEnable);
    }

    static __always_inline timespec AsyncDelayBegin(uint32_t tpId, const char *tpName)
    {
        DelayBegin(tpId, tpName);
        struct timespec tpDelay = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &tpDelay);
        return tpDelay;
    }

    static __always_inline uint64_t GetTimeNs()
    {
        struct timespec tpDelay = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &tpDelay);
        return tpDelay.tv_sec * 1000000000ULL + tpDelay.tv_nsec;
    }

    static __always_inline void SetEnable(bool enable)
    {
        mEnable = enable;
    }

    static __always_inline bool IsEnable()
    {
        return mEnable;
    }

    static __always_inline void SetLatencyQuantileEnable(bool enable)
    {
        mLatencyQuantileEnable = enable;
    }

    static __always_inline bool IsLatencyQuantileEnable()
    {
        return mLatencyQuantileEnable;
    }

    static __always_inline void SetEnableLog(bool enable, std::string logPath)
    {
        if (!logPath.empty() && !mDumpEnable) {
            GetLogPath(logPath);
        }
        if (enable && mDumpDir.empty()) {
            int32_t ret = UTracerUtils::CreateDirectory(mDefaultDir);
            if (ret != 0) {
                RPC_ADPT_VLOG_WARN("[UTRACER], prepare dum dir failed, disable dump feature!");
            }
            mDumpDir = mDefaultDir;
            mDumpEnable = false;
            return;
        }
        mDumpEnable = enable;
    }

    static __always_inline bool IsEnableLog()
    {
        return mDumpEnable;
    }

private:
    static __always_inline UTracerInfo *CreateInstance()
    {
        UTracerInfo *instance = new (std::nothrow) UTracerInfo [MAX_TRACE_POINT_NUM];
        if (instance == nullptr) {
            return nullptr;
        }
        return instance;
    }

    static __always_inline void DumpTraceSplitInfo(std::string tpName, const uint64_t diff, int32_t retCode)
    {
        std::stringstream ss;
        std::string currentTime = UTracerUtils::CurrentTime();
        ss << currentTime << "|" << tpName << "|" << retCode << "|" << diff << "(ns)" << std::endl;

        std::string dumpPath = mDumpDir + "/utrace_" + std::to_string(getpid()) + ".log";
        int fd = open(dumpPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0640);
        if (fd == -1) {
            return;
        }

        std::ofstream dump;
        dump.open(dumpPath, std::ios::out | std::ios::app);
        if (!dump.is_open()) {
            close(fd);
            return;
        }

        dump << ss.str();
        dump.flush();
        dump.close();
        close(fd);
    }

    static void GetLogPath(std::string &path)
    {
        if (!UTracerUtils::CanonicalPath(path)) {
            RPC_ADPT_VLOG_WARN("[UTRACER] Log directory is invalid, use default path. ");
            /* path is error, use old path */
            if (!mDumpDir.empty()) {
                return;
            }
            /* if path error and old path is empty, use default path */
            int32_t ret = UTracerUtils::CreateDirectory(mDefaultDir);
            if (ret != 0) {
                RPC_ADPT_VLOG_WARN("[UTRACER] prepare dump dir failed, disable dump feature!");
                mDumpEnable = false;
                return;
            }
            mDumpDir = mDefaultDir;
            return;
        }
        mDumpDir = path;
        return;
    }

private:
    static bool mEnable;
    static bool mLatencyQuantileEnable;
    static bool mDumpEnable;
    static std::string mDumpDir;
    static std::string mDefaultDir;
};
}

#endif // UTRACER_MANAGER_H