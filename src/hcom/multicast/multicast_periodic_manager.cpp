/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <unistd.h>
#include <sys/epoll.h>

#include "net_trace.h"

#include "include/multicast_publisher.h"
#include "include/multicast_publisher_service.h"
#include "multicast_periodic_manager.h"

namespace ock {
namespace hcom {
#define BIND_CPU(cpuId, tmpThread) /* bind cpu */                                                \
    if ((cpuId) >= 0) {                                                                          \
        cpu_set_t cpuSet;                                                                        \
        CPU_ZERO(&cpuSet);                                                                       \
        CPU_SET(static_cast<uint32_t>(cpuId), &cpuSet);                                          \
        if (pthread_setaffinity_np((tmpThread).native_handle(), sizeof(cpuSet), &cpuSet) != 0) { \
            NN_LOG_WARN("Unable to bind periodic manager to cpu " << (cpuId));                   \
        }                                                                                        \
    }

SerResult MultiCastPeriodicManager::Start()
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
    /* create threads */
    for (uint16_t i = 0; i < mThreadCount; i++) {
        std::thread tmpThread(&MultiCastPeriodicManager::RunInThread, this, i);
        if (!tmpThread.native_handle()) {
            NN_LOG_ERROR("Create manager thread failed");
            StopInner();
            return SER_CREATE_TIMEOUT_THREAD_FAILED;
        }

        /* set thread name */
        if (pthread_setname_np(tmpThread.native_handle(), ("MultiPerMgr" + std::to_string(i)).c_str()) != 0) {
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

void MultiCastPeriodicManager::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void MultiCastPeriodicManager::StopInner()
{
    mNeedStop = true;
    for (uint16_t i = 0; i < mThreadCount; i++) {
        if (mWorkingThreads[i].joinable()) {
            mWorkingThreads[i].join();
        }

        ProcessCleanUp(i);
    }
}

void MultiCastPeriodicManager::ProcessCleanUp(uint16_t tId)
{
    if (NN_UNLIKELY(tId >= M_MAX_THREAD_NUM)) {
        NN_LOG_WARN("tId is invalid");
        return;
    }

    PublisherContext timeoutCtx {};
    for (uint32_t i = NN_NO0; i < M_MAX_BATCH_NUM; i++) {
        auto currentQueue = &(mQueue[tId].queue[i]);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        while (!currentQueue->empty()) {
            auto it = currentQueue->begin();
            NN_LOG_TRACE_INFO("Process clean up seq no " << (*it)->mSeqNo << " time " << (*it)->mTimeout <<
                ", current time " << NetMonotonic::TimeSec());
            if ((*it)->EraseSeqNoWithRet()) {
                (*it)->TimeoutDump();
                (*it)->MarkTimeout();
                auto callback = reinterpret_cast<MultiCastCallback *>((*it)->Callback());
                callback->Run(timeoutCtx);
                (*it)->DecreaseRef();
            }
            RemoveTimerFromList((*it));
            (*it)->DecreaseRef();
            currentQueue->pop_front();
        }
    }
}


void MultiCastPeriodicManager::FillHandleQueue(uint16_t tId)
{
    mHandleQueue[tId].clear();

    for (uint32_t i = NN_NO0; i < M_MAX_BATCH_NUM; i++) {
        auto currentQueue = &(mQueue[tId].queue[i]);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        auto it = currentQueue->begin();
        while (it != currentQueue->end()) {
            NN_LOG_TRACE_INFO("Process clean up seq no is: " << (*it)->SeqNo() << " time is: " << (*it)->mTimeout <<
                                                             ", current time is :" << NetMonotonic::TimeSec());
            if ((*it)->IsFinished() || (*it)->IsTimeOut()) {
                mHandleQueue[tId].emplace_back(*it);
                it = currentQueue->erase(it);
            } else {
                it++;
            }
        }
    }
}

void MultiCastPeriodicManager::ProcessTimeOut(uint16_t tId)
{
    if (tId >= M_MAX_THREAD_NUM) {
        NN_LOG_WARN("tId is invalid, tid:" << tId);
        return;
    }

    FillHandleQueue(tId);

    for (auto &timer : mHandleQueue[tId]) {
        if (timer->EraseSeqNoWithRet()) {
            timer->TimeoutDump();
            timer->MarkTimeout();
            auto callback = reinterpret_cast<MultiCastCallback *>(timer->Callback());

            auto publisher = timer->mPublisher;
            if (publisher == nullptr) {
                NN_LOG_ERROR("publisher is null");
                continue;
            }
            // deal with ep delay erase
            if (timer->mType == MultiCastSyncCBType::BROKEN) {
                PublisherContext pubCtx;
                callback->Run(pubCtx);
                timer->DecreaseRef();
                continue;
            }
            // deal with IO
            PublisherContext *pubCtx = nullptr;
            publisher->mPubCtxStore->GetBySeqNo(timer->SeqNo(), pubCtx);
            if (NN_UNLIKELY(pubCtx == nullptr)) {
                NN_LOG_ERROR("pubCtx is null");
                continue;
            }

            UpdateSubscriberRsp(pubCtx);
            callback->Run(*pubCtx);
            timer->DecreaseRef();
            publisher->mPubCtxStore->RemoveSeqNo(timer->SeqNo());
            publisher->mPubCtxStore->Return(pubCtx);
        }
        RemoveTimerFromList(timer); /* if remove success, decrease linked list ref auto */
        timer->DecreaseRef();       /* decrease periodic thread ref */
    }
}

void MultiCastPeriodicManager::UpdateSubscriberRsp(PublisherContext *pubCtx) const
{
    auto subscriberRsp = pubCtx->GetSubscriberRspInfo();
    for (auto &subRsp : subscriberRsp) {
        if (subRsp.mStatus != SubscriberRspStatus::SUCCESS) {
            subRsp.mStatus = SubscriberRspStatus::TIMEOUT;
        }
    }
}

SerResult MultiCastPeriodicManager::AddTimerCheck(MultiCastServiceTimer *&timer)
{
    if (NN_UNLIKELY(timer == nullptr)) {
        NN_LOG_ERROR("Failed to add timeout, timer is null");
        return SER_INVALID_PARAM;
    }

    if (NN_UNLIKELY(mNeedStop)) {
        NN_LOG_ERROR("Failed to add timeout seq no " << timer->SeqNo() << " by stop service");
        return SER_STOP;
    }

    if (NN_UNLIKELY(timer->SeqNo() == 0 || timer->Callback() == 0)) {
        NN_LOG_ERROR("Add timeout invalid seq no " << timer->SeqNo() << " or callback " << timer->Callback());
        return SER_INVALID_PARAM;
    }
    return SER_OK;
}

bool MultiCastPeriodicManager::ValidateTimer(MultiCastServiceTimer *&timer)
{
    if (NN_UNLIKELY(timer == nullptr || (timer->mType) != MultiCastSyncCBType::IO)) {
            return false;
        }

    if (NN_UNLIKELY((timer->mPublisher) == nullptr || (timer->mPublisher->mTimerList) == 0)) {
        return false;
    }
    return true;
}


void MultiCastPeriodicManager::RunInThread(int16_t tId)
{
    mHandleQueue[tId].reserve(NN_NO8192);
    mStartedWorkingThreads.fetch_add(1);

    if (tId >= mThreadCount) {
        NN_LOG_WARN("Invalid tId: " << tId << " to run PeriodicManager");
        return;
    }

    int eFd = epoll_create(1);
    if (eFd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("MultiCastPeriodicManager manager failed to create epoll by " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread start");
    while (!mNeedStop) {
        auto startTime = NetMonotonic::TimeMs();
        ProcessTimeOut(tId);
        auto spendTime = NetMonotonic::TimeMs() - startTime;

        struct epoll_event ev {};
        int waitTimeMs = 0; // wait for 500ms
        if (spendTime >= gMaxTimeout) {
            continue;
        }
        waitTimeMs = static_cast<int32_t>(gMaxTimeout - spendTime);
        epoll_wait(eFd, &ev, NN_NO1, waitTimeMs);
    }

    NetFunc::NN_SafeCloseFd(eFd);
    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread exit");
}
}
}