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

#include "trace/htracer.h"
#include "htracer_manager.h"
#include "htracer_service.h"

namespace ock {
namespace hcom {

static HTracerService *traceService = nullptr;
#ifdef HTRACER_ENABLED
bool TraceManager::mEnable = true;
#else
bool TraceManager::mEnable = false;
#endif

bool TraceManager::mLatencyQuantileEnable = false;

std::string TraceManager::mDumpDir = "";
std::string TraceManager::mDefaultDir = "/tmp/htrace/log";
bool TraceManager::mDumpEnable = false;
static bool HtraceEnable();

HTRACE_INTF g_htraceIntf = {HtraceEnable, NULL, NULL, NULL, NULL};
static bool g_htraceInit = false;

static bool HtraceEnable()
{
    return TraceManager::IsEnable() && g_htraceInit;
}

static void HtracerRegisterInterface(void)
{
    g_htraceIntf.DelayBegin = &TraceManager::DelayBegin;
    g_htraceIntf.AsyncDelayBegin = &TraceManager::AsyncDelayBegin;
    g_htraceIntf.DelayEnd = &TraceManager::DelayEnd;
    g_htraceIntf.GetCurrentTimeNs = &TraceManager::GetTimeNs;
}

int32_t HTracerInit(const std::string &serverName)
{
    if (traceService != nullptr) {
        return SER_OK;
    }

    HtracerRegisterInterface();

    traceService = new (std::nothrow) HTracerService();
    if (traceService == nullptr) {
        NN_LOG_WARN("[HTRACER] failed to malloc traceService");
        return SER_ERROR;
    }
    traceService->StartUp(serverName);

    auto ins = TraceManager::Instance();
    if (ins == nullptr) {
        NN_LOG_WARN("[HTRACER] init trace manager instance failed");
        return SER_ERROR;
    }
    g_htraceInit = true;
    return SER_OK;
}

void HTracerExit(void)
{
    if (traceService != nullptr) {
        traceService->ShutDown();
        delete traceService;
        traceService = nullptr;
    }
    g_htraceInit = false;
}

void EnableHtrace(bool enableTrace)
{
    TraceManager::SetEnable(enableTrace);
}

}
}