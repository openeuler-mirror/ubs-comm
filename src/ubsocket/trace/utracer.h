/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#ifndef UTRACER_H
#define UTRACER_H

#include <sstream>
#include <functional>
#include "utracer_info.h"

namespace Statistics {
enum UbSocketTracePointId : uint32_t {
    INVALID_TRACE_POINT_ID = 0,
    BRPC_CONNECT_CALL,
    BRPC_ACCEPT_CALL,
    BRPC_READV_CALL,
    BRPC_WRITEV_CALL,
};

using UTRACE_INTF = struct UTRACE_INTF_S {
    bool (*IsEnable)();
    void (*DelayBegin)(uint32_t tpId, const char *tpName);
    struct timespec (*AsyncDelayBegin)(uint32_t tpId, const char *tpName);
    void (*DelayEnd)(uint32_t tpId, const uint64_t diff, int32_t retCode);
    uint64_t (*GetCurrentTimeNs)();
};

extern int32_t UTracerInit();
extern void UTracerExit();
extern void EnableUTrace(bool);
extern std::vector<TranTraceInfo> GetTraceInfos(UbSocketTracePointId tpId, double quantile, bool enableTp);
extern void ResetTraceInfos();
extern UTRACE_INTF g_utraceIntf;

class UbsTraceDefer {
public:
    UbsTraceDefer(std::function<void()> beginFunc, std::function<void()> endFunc)
    {
        mBeginFunc = beginFunc;
        mEndFunc = endFunc;
        if (mBeginFunc) {
            mBeginFunc();
        }
    }
    ~UbsTraceDefer()
    {
        if (mEndFunc) {
            mEndFunc();
        }
    }

    // Disable copy and assignment
    UbsTraceDefer(const UbsTraceDefer &) = delete;
    UbsTraceDefer &operator = (const UbsTraceDefer &) = delete;

private:
    std::function<void()> mBeginFunc;
    std::function<void()> mEndFunc;
};

#define TRACE_DELAY_BEGIN(TP_ID)                          \
    uint64_t tpBegin##TP_ID = 0;                          \
    if (g_utraceIntf.IsEnable()) {                        \
        g_utraceIntf.DelayBegin(TP_ID, #TP_ID);           \
        tpBegin##TP_ID = g_utraceIntf.GetCurrentTimeNs(); \
    }

#define TRACE_DELAY_END(TP_ID, RET_CODE)                                                          \
    if (g_utraceIntf.IsEnable()) {                                                                \
        g_utraceIntf.DelayEnd(TP_ID, g_utraceIntf.GetCurrentTimeNs() - tpBegin##TP_ID, RET_CODE); \
    }

#define TRACE_DELAY_DEFER_BEGIN(TP_ID, TP_NAME, TP_BEGIN_TIME) \
    if (g_utraceIntf.IsEnable()) {                             \
        g_utraceIntf.DelayBegin(TP_ID, TP_NAME);               \
        TP_BEGIN_TIME = g_utraceIntf.GetCurrentTimeNs();       \
    }

#define TRACE_DELAY_DEFER_END(TP_ID, RET_CODE, TP_BEGIN_TIME)                                      \
    if (g_utraceIntf.IsEnable()) {                                                                 \
        g_utraceIntf.DelayEnd(TP_ID, g_utraceIntf.GetCurrentTimeNs() - (TP_BEGIN_TIME), RET_CODE); \
    }

#define TRACE_DELAY_AUTO(TP_ID, RET_CODE)                                                                             \
    uint64_t tpBegin##TP_ID = 0;                                                                                      \
    UbSocketTracePointId id = TP_ID;                                                                                  \
    std::string name = #TP_ID;                                                                                        \
    UbsTraceDefer defer([id, name, &tpBegin##TP_ID]() { TRACE_DELAY_DEFER_BEGIN(id, name.c_str(), tpBegin##TP_ID); }, \
        [id, &RET_CODE, &tpBegin##TP_ID]() { TRACE_DELAY_DEFER_END(id, (RET_CODE), tpBegin##TP_ID); });
}


#endif // UTRACER_H
