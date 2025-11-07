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
#ifndef HCOM_NET_DELAY_RELEASE_TIMER_H
#define HCOM_NET_DELAY_RELEASE_TIMER_H

#include <queue>

#include "hcom.h"
#include "hcom_def.h"
#include "net_monotonic.h"

namespace ock {
namespace hcom {
/*
 * Delay Release of queue struct
 */
class NetDelayReleaseResource {
public:
    NetDelayReleaseResource(UBSHcomNetEndpointPtr &ep, uint64_t delayTimeSec)
    {
        mEp = ep;
        mTimeout = NetMonotonic::TimeSec() + delayTimeSec;
    }

    ~NetDelayReleaseResource() = default;

    bool IsTimeOut() const
    {
        if (NetMonotonic::TimeSec() > mTimeout) {
            return true;
        }
        return false;
    }

public:
    UBSHcomNetEndpointPtr mEp = nullptr; /* manager ep time out */
    uint64_t mTimeout = 0;        /* absolute timeout compare to current system time */
};

/*
 * Delay Release Timer
 */
class NetDelayReleaseTimer {
public:
    NetDelayReleaseTimer(const std::string &name, uint16_t driverIndex)
        : mDriverIndex(driverIndex), mName(name + std::to_string(driverIndex)) {};

    ~NetDelayReleaseTimer() = default;

    NResult Start();
    void Stop();

    void EnqueueDelayRelease(UBSHcomNetEndpointPtr &ep);

private:
    void StopInner();
    void RunDelayReleaseThread();
    void DequeueDelayRelease();

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    // hot used variables for start
    std::queue<NetDelayReleaseResource> mDelayReleaseQueue;
    std::mutex mDelayReleaseMutex;
    bool mNeedStop = false;

    uint16_t mDriverIndex = 0;
    std::string mName;
    std::mutex mMutex;
    bool mStarted = false;
    std::thread mThread;
    std::atomic_bool mThreadStarted { false };

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}
#endif // HCOM_NET_DELAY_RELEASE_TIMER_H