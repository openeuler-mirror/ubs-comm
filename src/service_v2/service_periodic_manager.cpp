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
#include <unistd.h>
#include <sys/epoll.h>

#include "hcom_service_context.h"
#include "net_trace.h"
#include "service_common.h"
#include "service_periodic_manager.h"

namespace ock {
namespace hcom {

SerResult HcomPeriodicManager::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        return SER_OK;
    }

    if (mThreadCount > M_MAX_THREAD_NUM) {
        NN_LOG_ERROR("Invalid thread count " << mThreadCount);
        return SER_INVALID_PARAM;
    }

    mNeedStop = false;
    /* create periodicManager threads */
    for (uint16_t i = 0; i < mThreadCount; i++) {
        std::thread tmpThread(&HcomPeriodicManager::RunInThread, this, i);
        if (!tmpThread.native_handle()) {
            StopInner();
            return SER_CREATE_TIMEOUT_THREAD_FAILED;
        }

        /* set thread name */
        if (pthread_setname_np(tmpThread.native_handle(), ("HcomPerMgr" + std::to_string(i)).c_str()) != 0) {
            NN_LOG_WARN("Unable to set thread name of periodic manager");
        }
        mWorkingThreads[i] = std::move(tmpThread);
    }

    while (mStartedWorkingThreads.load() != mThreadCount) {
        usleep(NN_NO10);
    }

    mStarted = true;
    return SER_OK;
}

void HcomPeriodicManager::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void HcomPeriodicManager::StopInner()
{
    mNeedStop = true;
    for (uint16_t i = 0; i < mThreadCount; i++) {
        if (mWorkingThreads[i].joinable()) {
            mWorkingThreads[i].join();
        }

        ProcessCleanUp(i);
    }
}

void HcomPeriodicManager::ProcessCleanUp(uint16_t tId)
{
    if (NN_UNLIKELY(tId >= M_MAX_THREAD_NUM)) {
        NN_LOG_WARN("tId is invalid");
        return;
    }
    UBSHcomServiceContext timeoutCtx{};
    HcomServiceGlobalObject::BuildTimeOutCtx(timeoutCtx);
    timeoutCtx.mResult = SER_STOP;
    for (uint32_t i = 0; i < M_MAX_BATCH_NUM; i++) {
        auto currentQueue = &(mQueue[tId].queue[i]);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        while (!currentQueue->empty()) {
            NN_LOG_TRACE_INFO("Process clean up seq no " << currentQueue->top()->SeqNo() << " timeout " <<
                currentQueue->top()->mTimeout << ", current time " << NetMonotonic::TimeSec());
            if (currentQueue->top()->EraseSeqNoWithRet()) {
                currentQueue->top()->TimeoutDump();
                currentQueue->top()->MarkTimeout();
                auto callback = reinterpret_cast<Callback *>(currentQueue->top()->Callback());
                timeoutCtx.mCh = currentQueue->top()->mChannel;
                callback->Run(timeoutCtx);
                currentQueue->top()->DecreaseRef();
            }
            RemoveLinkedList(currentQueue->top());
            currentQueue->top()->DecreaseRef();
            currentQueue->pop();
            timeoutCtx.mCh.Set(nullptr);
        }
    }
}

void HcomPeriodicManager::ProcessTimeOut(uint16_t tId)
{
    if (tId >= M_MAX_THREAD_NUM) {
        NN_LOG_WARN("tId is invalid");
        return;
    }
    mHandleQueue[tId].clear();
    for (int32_t i = M_MAX_BATCH_NUM - 1; i >= 0; i--) {
        auto currentQueue = &(mQueue[tId].queue[i]);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        while (!currentQueue->empty()) {
            NN_LOG_TRACE_INFO("Process time out seq no " << currentQueue->top()->SeqNo() << " timeout " <<
                currentQueue->top()->mTimeout << ", current time " << NetMonotonic::TimeSec());
            if (currentQueue->top()->IsFinished() || currentQueue->top()->IsTimeOut()) {
                mHandleQueue[tId].emplace_back(currentQueue->top());
                currentQueue->pop();
                continue;
            }

            break;
        }
    }

    UBSHcomServiceContext timeoutCtx{};
    HcomServiceGlobalObject::BuildTimeOutCtx(timeoutCtx);
    for (auto &i : mHandleQueue[tId]) {
        if (i->EraseSeqNoWithRet()) {
            i->TimeoutDump();
            i->MarkTimeout();
            auto callback = reinterpret_cast<Callback *>(i->Callback());
            timeoutCtx.mCh = i->mChannel;
            callback->Run(timeoutCtx);
            i->DecreaseRef();
        }
        RemoveLinkedList(i); /* if remove success, decrease linked list ref auto */
        i->DecreaseRef();    /* decrease periodic thread ref */
        timeoutCtx.mCh.Set(nullptr);
    }
}

void HcomPeriodicManager::RunInThread(int16_t tId)
{
    mHandleQueue[tId].reserve(NN_NO8192);
    mStartedWorkingThreads.fetch_add(1);

    if (tId >= mThreadCount) {
        NN_LOG_ERROR("Invalid tId " << tId << " to run PeriodicManager");
        return;
    }

    int eFd = epoll_create(1);
    if (eFd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("HcomPeriodic manager failed to create epoll by "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread start");
    while (!mNeedStop) {
        auto startTime = NetMonotonic::TimeMs();
        ProcessTimeOut(tId);
        auto duration = NetMonotonic::TimeMs() - startTime;

        struct epoll_event ev {};
        int waitTimeMs = 0; // wait for 500ms
        if (duration >= gMaxTimeout) {
            continue;
        } else {
            waitTimeMs = static_cast<int32_t>(gMaxTimeout - duration);
        }

        epoll_wait(eFd, &ev, 1, waitTimeMs);
    }

    NetFunc::NN_SafeCloseFd(eFd);
    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread exit");
}
}
}