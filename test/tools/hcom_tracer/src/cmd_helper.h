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

#ifndef CMD_HELPER
#define CMD_HELPER

#include <csignal>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include "htracer_msg.h"
#include "hcom/hcom_err.h"
#include "htracer_client.h"

class ProcessInfo {
public:
    explicit ProcessInfo(pid_t pid) : mPid(pid) {}

    void SetTraceInfo(std::vector<TTraceInfo> traceInfo);

    void SetActive(bool active)
    {
        mActive = active;
    }

    bool IsActive()
    {
        return mActive;
    }

    const std::vector<TTraceInfo> &GetAllTraceInfos()
    {
        return mTraceInfo;
    }

    std::string ToString();

    std::string StateToString();

private:
    void UpdateLastTime();

private:
    bool mActive = true;
    pid_t mPid;
    std::string mLastUpdateTime = "--:--:--";
    std::vector<TTraceInfo> mTraceInfo;
};

class HostInfo {
public:
    SerCode UpdateHostInfo(uint16_t serviceId, double quantile = -1.0);
    SerCode ResetTraceInfo();

    void Inactive()
    {
        mActiveProcess = nullptr;
    }

    bool IsActive()
    {
        return mActiveProcess != nullptr;
    }

    std::string ToString(bool detail = false);

    const std::map<pid_t, std::shared_ptr<ProcessInfo>> &GetAllProcesses()
    {
        return mAllProcesses;
    }

private:
    std::shared_ptr<ProcessInfo> GetOrCreateProcess(pid_t pid);

private:
    std::shared_ptr<ProcessInfo> mActiveProcess = nullptr;
    std::map<pid_t, std::shared_ptr<ProcessInfo>> mAllProcesses;
};

class CmdHelper {
public:
    void UpdateHost(double quantile = -1.0);

    void EnableTrace(const HandlerConfPara &confPara);

    HostInfo GetHostInfo();

    void ResetTraceInfo();

private:
    std::shared_ptr<HostInfo> InsertHost(const std::string host);

    std::shared_ptr<HostInfo> GetHostInfo(const std::string host);

private:
    HostInfo mHostInfo;
    std::mutex mMutex;
    HTracerClient mClient;
};

#endif // CMD_HELPER