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

#include <mutex>
#include <set>
#include "htracer_utils.h"
#include "htracer_client.h"
#include "cmd_helper.h"

#define INVALID_SERVICE_ID (0xFFFF)

void ProcessInfo::UpdateLastTime()
{
    mLastUpdateTime = HTracerUtils::CurrentTime();
}

std::string ProcessInfo::ToString()
{
    std::stringstream ss;
    ss << StateToString() << std::endl;
    ss << TTraceInfo::HeaderString() << std::endl;
    time_t rawTime;
    time(&rawTime);
    for (uint32_t i = 0; i < mTraceInfo.size(); ++i) {
        ss << "\t" << mTraceInfo[i].ToString() << std::endl;
    }
    return ss.str();
}

void ProcessInfo::SetTraceInfo(std::vector<TTraceInfo> traceInfo)
{
    traceInfo.swap(mTraceInfo);
    UpdateLastTime();
}

std::string ProcessInfo::StateToString()
{
    std::stringstream ss;
    auto processName = "localhost@" + std::to_string(mPid);
    if (mActive) {
        ss << " " << processName << "(status: active) ";
    } else {
        ss << " " << processName << "(status: inactive, out of date from " << mLastUpdateTime << ") ";
    }
    return ss.str();
}

void CmdHelper::UpdateHost(double quantile)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mHostInfo.UpdateHostInfo(INVALID_SERVICE_ID, quantile);
}

void CmdHelper::EnableTrace(const HandlerConfPara &confPara)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mClient.EnableTrace(confPara);
}

HostInfo CmdHelper::GetHostInfo()
{
    return mHostInfo;
}

void CmdHelper::ResetTraceInfo()
{
    mHostInfo.ResetTraceInfo();
}

std::shared_ptr<ProcessInfo> HostInfo::GetOrCreateProcess(pid_t pid)
{
    auto processIt = mAllProcesses.find(pid);
    if (processIt != mAllProcesses.end()) {
        return processIt->second;
    }

    auto process = std::make_shared<ProcessInfo>(pid);
    if (process == nullptr) {
        LOG_ERR("failed to malloc process, pid: " << pid);
        return nullptr;
    }
    mAllProcesses[pid] = process;
    return process;
}

SerCode HostInfo::UpdateHostInfo(uint16_t serviceId, double quantile)
{
    HTracerClient client;
    pid_t pid = -1;
    std::vector<TTraceInfo> traceInfos;
    SerCode query_ret = client.Query(serviceId, quantile, traceInfos, pid);
    if (query_ret == SER_OK) {
        auto process = GetOrCreateProcess(pid);
        process->SetTraceInfo(traceInfos);
        process->SetActive(true);
        mActiveProcess = process;
    } else {
        for (const auto &processIt : mAllProcesses) {
            processIt.second->SetActive(false);
        }
        mActiveProcess = nullptr;
    }
    return SER_OK;
}

SerCode HostInfo::ResetTraceInfo()
{
    HTracerClient client;
    client.Reset();
    return SER_OK;
}

std::string HostInfo::ToString(bool detail)
{
    std::stringstream ss;
    if (detail) {
        for (const auto &processIt : mAllProcesses) {
            auto process = processIt.second;
            ss << process->ToString();
        }
    } else {
        if (IsActive()) {
            ss << "  "
               << "localHost "
               << "(status: active) ";
        } else {
            ss << "  "
               << "localHost "
               << "(status: inactive) ";
        }
    }
    return ss.str();
}