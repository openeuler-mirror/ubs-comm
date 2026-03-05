/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#include "utracer.h"
#include "utracer_manager.h"

namespace Statistics {
#ifdef UBSOCKET_DELAY_TRACER_ENABLED
    bool UTracerManager::mEnable = true;
#else
    bool UTracerManager::mEnable = false;
#endif

    bool UTracerManager::mLatencyQuantileEnable = false;

    std::string UTracerManager::mDumpDir = "";
    std::string UTracerManager::mDefaultDir = "/tmp/utrace/log";
    bool UTracerManager::mDumpEnable = false;
    static bool UtraceEnable();

    UTRACE_INTF g_utraceIntf = {};
    static bool g_utraceInit = false;

    static bool UtraceEnable()
    {
        return UTracerManager::IsEnable() && g_utraceInit;
    }

    static void UtracerRegisterInterface()
    {
        g_utraceIntf.IsEnable = &UtraceEnable;
        g_utraceIntf.DelayBegin = &UTracerManager::DelayBegin;
        g_utraceIntf.AsyncDelayBegin = &UTracerManager::AsyncDelayBegin;
        g_utraceIntf.DelayEnd = &UTracerManager::DelayEnd;
        g_utraceIntf.GetCurrentTimeNs = &UTracerManager::GetTimeNs;
    }

    int32_t UTracerInit()
    {
        UtracerRegisterInterface();
        auto ins = UTracerManager::Instance();
        if (ins == nullptr) {
            RPC_ADPT_VLOG_WARN("[UTRACER] init trace manager instance failed");
            return -1;
        }
        g_utraceInit = true;
        return 0;
    }

    void UTracerExit()
    {
        g_utraceInit = false;
    }

    void EnableUTrace(bool enableTrace)
    {
        UTracerManager::SetEnable(enableTrace);
    }

    std::vector<TranTraceInfo> GetTraceInfos(UbSocketTracePointId tpId, double quantile, bool enableTp)
    {
        std::vector<TranTraceInfo> retTraceInfos;
        auto traceManager = UTracerManager::Instance();
        if (tpId == INVALID_TRACE_POINT_ID) {
            for (int i = 0; i < MAX_TRACE_POINT_NUM; ++i) {
                auto &traceInfo = traceManager[i];
                if (traceInfo.Valid()) {
                    retTraceInfos.emplace_back(TranTraceInfo(traceManager[i], quantile, enableTp));
                }
            }
            return retTraceInfos;
        }

        if (tpId > MAX_TRACE_POINT_NUM) {
            return retTraceInfos;
        }

        auto &traceInfo = traceManager[tpId];
        if (traceInfo.Valid()) {
            retTraceInfos.emplace_back(TranTraceInfo(traceInfo, quantile, enableTp));
        }
        return retTraceInfos;
    }

    void ResetTraceInfos()
    {
        std::vector<TranTraceInfo> retTraceInfos;
        auto traceManager = UTracerManager::Instance();
        for (int i = 0; i < MAX_TRACE_POINT_NUM; ++i) {
            auto &traceInfo = traceManager[i];
            if (traceInfo.Valid()) {
                traceManager[i].Reset();
            }
        }
    }
}