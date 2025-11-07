/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef HTRACER_MANAGER_H
#define HTRACER_MANAGER_H

#include <cerrno>
#include <sys/cdefs.h>
#include <iostream>
#include <fcntl.h>
#include <fstream>
#include "trace/htracer.h"
#include "htracer_info.h"
#include "hcom_err.h"
#include "hcom_log.h"

namespace ock {
namespace hcom {
class TraceManager {
public:
    static __always_inline TraceInfo **Instance()
    {
        static TraceInfo **tracePoints = CreateInstance();
        return tracePoints;
    }

    static __always_inline void DelayBegin(uint32_t tpId, const char *tpName)
    {
        auto instance = Instance();
        uint16_t serviceId = SERVICE_ID(tpId);
        uint16_t innerId = INNER_ID(tpId);
        if (serviceId >= MAX_SERVICE_NUM || innerId >= MAX_INNER_ID_NUM) {
            return;
        }
        instance[serviceId][innerId].DelayBegin(tpName);
    }

    static __always_inline void DelayEnd(uint32_t tpId, const uint64_t diff, int32_t retCode)
    {
        auto instance = Instance();
        uint16_t serviceId = SERVICE_ID(tpId);
        uint16_t innerId = INNER_ID(tpId);
        if (serviceId >= MAX_SERVICE_NUM || innerId >= MAX_INNER_ID_NUM) {
            return;
        }
        if (mDumpEnable) {
            DumpTraceSplitInfo(instance[serviceId][innerId].GetName(), diff, retCode);
        }
        instance[serviceId][innerId].DelayEnd(diff, retCode, mLatencyQuantileEnable);
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

    static __always_inline void SetEnableLog(bool enable, std::string &logPath)
    {
        if (!logPath.empty() && !mDumpEnable) {
            GetLogPath(logPath);
        }
        if (enable && mDumpDir.empty()) {
            int32_t ret = HTracerUtils::CreateDirectory(mDefaultDir);
            if (ret != SER_OK) {
                NN_LOG_WARN("[htracer], prepare dum dir failed, disable dump feature!");
            }
            mDumpDir = mDefaultDir;
        }
        mDumpEnable = enable;
    }

    static __always_inline bool IsEnableLog()
    {
        return mDumpEnable;
    }

private:
    static __always_inline TraceInfo **CreateInstance()
    {
        TraceInfo **instance = new (std::nothrow) TraceInfo *[MAX_SERVICE_NUM];
        if (instance == nullptr) {
            return nullptr;
        } else {
            auto ret = memset_s(instance, sizeof(TraceInfo *) * MAX_SERVICE_NUM,
                                0x0, sizeof(TraceInfo *) * MAX_SERVICE_NUM);
            if (ret != 0) {
                NN_LOG_WARN("[HTRACER] Failed to memset_s to instance.");
                delete[] instance;
                instance = nullptr;
                return nullptr;
            }
        }

        int ret = 0;
        uint16_t i = 0;
        for (i = 0; i < MAX_SERVICE_NUM; ++i) {
            instance[i] = new (std::nothrow) TraceInfo[MAX_INNER_ID_NUM];
            if (instance[i] == nullptr) {
                ret = -1;
                break;
            }
        }

        if (ret != 0) {
            for (uint16_t j = 0; j < i; ++j) {
                delete[] instance[j];
                instance[j] = nullptr;
            }
            delete[] instance;
            instance = nullptr;
            return nullptr;
        }
        return instance;
    }

    static __always_inline void DumpTraceSplitInfo(std::string tpName, const uint64_t diff, int32_t retCode)
    {
        std::stringstream ss;
        std::string currentTime = HTracerUtils::CurrentTime();
        ss << currentTime << "|" << tpName << "|" << retCode << "|" << diff << "(ns)" << std::endl;

        std::string dumpPath = mDumpDir + "/htrace_" + std::to_string(getpid()) + ".log";
        // 创建文件并设置权限 0640 (rw-r-----)
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
        if (!HTracerUtils::CanonicalPath(path)) {
            NN_LOG_WARN("[HTRACER] Log directory is invalid, use default path. ");
            /* path is error, use old path */
            if (!mDumpDir.empty()) {
                return;
            }
            /* if path error and old path is empty, use default path */
            int32_t ret = HTracerUtils::CreateDirectory(mDefaultDir);
            if (ret != SER_OK) {
                NN_LOG_WARN("[HTRACER] prepare dump dir failed, disable dump feature!");
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
}

#endif