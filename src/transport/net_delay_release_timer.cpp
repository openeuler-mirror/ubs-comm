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
#include <sys/epoll.h>
#include <unistd.h>

#include "net_delay_release_timer.h"

namespace ock {
namespace hcom {
constexpr uint32_t EPOLL_WAIT_TIMEOUT = NN_NO1000 * NN_NO10; // 10 second
NResult NetDelayReleaseTimer::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        return NN_OK;
    }

    std::thread delayReleaseThread(&NetDelayReleaseTimer::RunDelayReleaseThread, this);
    mThread = std::move(delayReleaseThread);
    std::string treadName = "DelayRelease" + std::to_string(mDriverIndex);
    if (pthread_setname_np(mThread.native_handle(), treadName.c_str()) != 0) {
        NN_LOG_WARN("Invalid to set name of NetDelayReleaseTimer working thread to " << treadName);
    }

    while (!mThreadStarted.load()) {
        usleep(NN_NO10);
    }

    mStarted = true;
    return NN_OK;
}

void NetDelayReleaseTimer::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        NN_LOG_WARN("NetDelayReleaseTimer " << mName << " has not been started");
        return;
    }

    StopInner();

    mStarted = false;
}

void NetDelayReleaseTimer::StopInner()
{
    mNeedStop = true;

    if (mThread.native_handle()) {
        mThread.join();
    }
}

void NetDelayReleaseTimer::RunDelayReleaseThread()
{
    mThreadStarted.store(true);
    NN_LOG_INFO("NetDelayReleaseTimer " << mName << "  working thread started");

    int eFd = epoll_create(1);
    if (eFd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("NetDelayReleaseTimer thread failed to create epoll by "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    while (!mNeedStop) {
        auto startTime = NetMonotonic::TimeMs();
        DequeueDelayRelease();
        auto spendTime = NetMonotonic::TimeMs() - startTime;

        struct epoll_event ev {};
        int waitTimeMs = 0; // wait for 1000ms
        if (spendTime >= EPOLL_WAIT_TIMEOUT) {
            continue;
        } else {
            waitTimeMs = static_cast<int32_t>(EPOLL_WAIT_TIMEOUT - spendTime);
        }

        epoll_wait(eFd, &ev, NN_NO1, waitTimeMs);
    }

    NetFunc::NN_SafeCloseFd(eFd);
    NN_LOG_INFO("NetDelayReleaseTimer " << mName << " working thread exiting");
}

void NetDelayReleaseTimer::DequeueDelayRelease()
{
    std::lock_guard<std::mutex> gard(mDelayReleaseMutex);
    while (!mDelayReleaseQueue.empty()) {
        auto epRes = mDelayReleaseQueue.front();
        if (epRes.IsTimeOut()) {
            if (NN_UNLIKELY(epRes.mEp != nullptr)) {
                NN_LOG_DEBUG("Destroy Ep " << epRes.mEp->Id() << ", delayed release time has come");
                epRes.mEp.Set(nullptr);
            }
            mDelayReleaseQueue.pop();
            continue;
        }
        // if the first one is not timeout ,others is not timeout too
        break;
    }
}

void NetDelayReleaseTimer::EnqueueDelayRelease(UBSHcomNetEndpointPtr &ep)
{
    std::lock_guard<std::mutex> gard(mDelayReleaseMutex);
    auto epRes = NetDelayReleaseResource(ep, NN_NO20);
    mDelayReleaseQueue.push(epRes);
}
}
}