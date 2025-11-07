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

#ifndef HTRACER_H
#define HTRACER_H

#include <ctime>
#include <cstdint>
#include <iostream>
namespace ock {
namespace hcom {

#define INVALID_PORT (0xFFFF)
#define TRACE_ID(SERVICE_ID_, INNER_ID_) ((SERVICE_ID_) << 16 | ((INNER_ID_) & 0xFFFF))

using HTRACE_INTF = struct HTRACE_INTF_S {
    bool (*IsEnable)();
    void (*DelayBegin)(uint32_t tpId, const char *tpName);
    struct timespec (*AsyncDelayBegin)(uint32_t tpId, const char *tpName);
    void (*DelayEnd)(uint32_t tpId, const uint64_t diff, int32_t retCode);
    uint64_t (*GetCurrentTimeNs)();
};

extern int32_t HTracerInit(const std::string &serverName);
extern void HTracerExit(void);
extern void EnableHtrace(bool);
extern HTRACE_INTF g_htraceIntf;

#define TRACE_DELAY_BEGIN(TP_ID)                          \
    uint64_t tpBegin##TP_ID = 0;                          \
    if (g_htraceIntf.IsEnable()) {                        \
        g_htraceIntf.DelayBegin(TP_ID, #TP_ID);           \
        tpBegin##TP_ID = g_htraceIntf.GetCurrentTimeNs(); \
    }

#define TRACE_DELAY_END(TP_ID, RET_CODE)                                                          \
    if (g_htraceIntf.IsEnable()) {                                                                \
        g_htraceIntf.DelayEnd(TP_ID, g_htraceIntf.GetCurrentTimeNs() - tpBegin##TP_ID, RET_CODE); \
    }

#define TRACE_DELAY_DEFER_BEGIN(TP_ID, TP_NAME, TP_BEGIN_TIME) \
    if (g_htraceIntf.IsEnable()) {                             \
        g_htraceIntf.DelayBegin(TP_ID, TP_NAME);               \
        TP_BEGIN_TIME = g_htraceIntf.GetCurrentTimeNs();       \
    }

#define TRACE_DELAY_DEFER_END(TP_ID, RET_CODE, TP_BEGIN_TIME)                                    \
    if (g_htraceIntf.IsEnable()) {                                                               \
        g_htraceIntf.DelayEnd(TP_ID, g_htraceIntf.GetCurrentTimeNs() - (TP_BEGIN_TIME), RET_CODE); \
    }

#define TRACE_DELAY_BEGIN_ASYNC(TP_ID, BEGINTIME) \
    uint64_t tpBegin##TP_ID = 0;                  \
    if (g_htraceIntf.IsEnable()) {                \
        g_htraceIntf.DelayBegin(TP_ID, #TP_ID);   \
        tpBegin##TP_ID = BEGINTIME;               \
    }

#define GET_TIME_NS()                                                 \
    ({                                                                \
        struct timespec tpDelay = { 0, 0 };                           \
        clock_gettime(CLOCK_MONOTONIC, &tpDelay);                     \
        (uint64_t)(tpDelay.tv_nsec + tpDelay.tv_sec * 1000000000ULL); \
    })

// NOTICE: will be deprecated, use TRACE_V2_DELAY_BEGIN
#define ASYNC_TRACE_DELAY_BEGIN(TP_ID) g_htraceIntf.AsyncDelayBegin(TP_ID, #TP_ID)

// NOTICE: will be deprecated, use TRACE_V2_DELAY_END
#define ASYNC_TRACE_DELAY_END(TP_ID, RET_CODE, STARTTIME)                                             \
    struct timespec tpEnd##TP_ID = { 0, 0 };                                                          \
    bool traceEnabled##TP_ID = g_htraceIntf.IsEnable();                                               \
    if (traceEnabled##TP_ID) {                                                                        \
        clock_gettime(CLOCK_MONOTONIC, &tpEnd##TP_ID);                                                \
        long tpDiff##TP_ID;                                                                           \
        long tpDiffSec##TP_ID = tpEnd##TP_ID.tv_sec - (STARTTIME).tv_sec;                               \
        if (tpDiffSec##TP_ID == 0) {                                                                  \
            tpDiff##TP_ID = tpEnd##TP_ID.tv_nsec - (STARTTIME).tv_nsec;                                 \
        } else {                                                                                      \
            tpDiff##TP_ID = tpDiffSec##TP_ID * 1000000000 + tpEnd##TP_ID.tv_nsec - (STARTTIME).tv_nsec; \
        }                                                                                             \
        g_htraceIntf.DelayEnd(TP_ID, tpDiff##TP_ID, RET_CODE);                                        \
    }

#define TRACE_V2_DELAY_BEGIN(TP_ID, P_U64_TIME_NS)            \
    if (g_htraceIntf.IsEnable()) {                            \
        g_htraceIntf.DelayBegin(TP_ID, #TP_ID);               \
        (*(P_U64_TIME_NS)) = g_htraceIntf.GetCurrentTimeNs(); \
    }

#define TRACE_V2_DELAY_END(TP_ID, U64_TIME_NS, RET_CODE)                                           \
    if (g_htraceIntf.IsEnable()) {                                                                 \
        g_htraceIntf.DelayEnd(TP_ID, (g_htraceIntf.GetCurrentTimeNs() - (U64_TIME_NS)), RET_CODE); \
    }

#define TRACE_CURRENT_TIME_NS g_htraceIntf.GetCurrentTimeNs()

#define TRACE_RECORD_DELAY(TP_ID, U64_DIFF_TIME_NS, RET_CODE)       \
    if (g_htraceIntf.IsEnable()) {                                  \
        g_htraceIntf.DelayBegin(TP_ID, #TP_ID);                     \
        g_htraceIntf.DelayEnd(TP_ID, (U64_DIFF_TIME_NS), RET_CODE); \
    }

#define TRACE_IOSIZE_BEGIN(TP_ID)               \
    if (g_htraceIntf.IsEnable()) {              \
        g_htraceIntf.DelayBegin(TP_ID, #TP_ID); \
    }


#define TRACE_IOSIZE_END(TP_ID, IOSIZE, RET_CODE)                   \
    if (g_htraceIntf.IsEnable()) {                                  \
        g_htraceIntf.DelayEnd(TP_ID, ((IOSIZE)*1000ULL), RET_CODE); \
    }
}
}
#endif

// HTRACER_H