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
#include "service_ctx_store.h"
#include "service_timer_trace.h"

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
        auto &currentQueue = mQueue[tId].queue[i];
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        for (HcomServiceTimer *timer : currentQueue) {
            NN_LOG_TRACE_INFO("Process clean up seq no " << timer->SeqNo() << " timeout " << timer->mTimeout
                                                         << ", current time " << NetMonotonic::TimeSec());
            /* [TIMER-TRACE] 服务停止时回收一个 timer（STOP_COLLECT） */
            if (timer->mCtxStore != nullptr) {
                timer->mCtxStore->TraceMark(HcomTimerEvent::STOP_COLLECT);
            }
            if (timer->EraseSeqNoWithRet()) {
                timer->TimeoutDump();
                timer->MarkTimeout();
                auto callback = reinterpret_cast<Callback *>(timer->Callback());
                timeoutCtx.mCh = timer->mChannel;
                /* [TIMER-TRACE] callback 为空时跳过 Run，避免解引用空指针崩溃；
                   残留对象仍由后续 DecreaseRef 归还内存池 */
                if (callback != nullptr) {
                    callback->Run(timeoutCtx);
                } else if (timer->mCtxStore != nullptr) {
                    timer->mCtxStore->TraceMark(HcomTimerEvent::TIMEOUT_NULL_CB);
                }
                timer->DecreaseRef();
            }
            RemoveLinkedList(timer);
            timer->DecreaseRef();
            timeoutCtx.mCh.Set(nullptr);
        }
        currentQueue.clear();
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
        auto &currentQueue = mQueue[tId].queue[i];
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[i]);
        // 整表扫描 + 原地压缩：任意位置的已完成/已超时 timer 都可在本轮回收，
        // 消除"小顶堆队头未完成 timer 永久阻塞整条队列"的队头阻塞
        // （见 hlc_udp_multicast_mem_leak_root_cause.md §12）。
        size_t writeIdx = 0;
        for (size_t readIdx = 0; readIdx < currentQueue.size(); readIdx++) {
            HcomServiceTimer *timer = currentQueue[readIdx];
            if (timer->IsFinished() || timer->IsTimeOut()) {
                /* [TIMER-TRACE] 周期线程收集到一个 timer（TIMEOUT_COLLECT） */
                if (timer->mCtxStore != nullptr) {
                    timer->mCtxStore->TraceMark(HcomTimerEvent::TIMEOUT_COLLECT);
                }
                mHandleQueue[tId].emplace_back(timer);  // 摘走回收
                continue;
            }
            currentQueue[writeIdx++] = timer;            // 仍在途，原地前移压缩
        }
        currentQueue.resize(writeIdx);
    }

    UBSHcomServiceContext timeoutCtx{};
    HcomServiceGlobalObject::BuildTimeOutCtx(timeoutCtx);
    for (auto &i : mHandleQueue[tId]) {
        if (i->EraseSeqNoWithRet()) {
            i->TimeoutDump();
            i->MarkTimeout();
            /* [TIMER-TRACE] 周期线程真正触发了超时回调（TIMEOUT_FIRED） */
            if (i->mCtxStore != nullptr) {
                i->mCtxStore->TraceMark(HcomTimerEvent::TIMEOUT_FIRED);
            }
            auto callback = reinterpret_cast<Callback *>(i->Callback());
            timeoutCtx.mCh = i->mChannel;
            /* [TIMER-TRACE] callback 为空时跳过 Run，避免解引用空指针崩溃 */
            if (callback != nullptr) {
                callback->Run(timeoutCtx);
            } else if (i->mCtxStore != nullptr) {
                i->mCtxStore->TraceMark(HcomTimerEvent::TIMEOUT_NULL_CB);
            }
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
    if (tId == 0) {
        /* 诊断构建版本横幅：每次重新打包 timer 泄漏诊断能力时打印一次，
           用于确认线上运行的 libhcom.so 与本次源码一致（build=编译时间戳）。 */
        NN_LOG_INFO("[VERSION] hcom timer-diagnostic build=" << HcomTimerDiagVersion()
                     << ", two-side-timeout=60s, periodic-scan=" << gMaxTimeout << "ms"
                     << ", timeOutDetectThreadNum default 1"
                     << "; expect ALLOC≈RETURN (no leak), RESP_HIT/SEND_FAIL/TIMEOUT_FIRED release ref①,"
                     << " TIMEOUT_COLLECT releases ref②③. See [TIMER-TRACE] census every "
                     << HcomTimerTrace::IntervalSec() << "s");
    }
    while (!mNeedStop) {
        auto startTime = NetMonotonic::TimeMs();
        ProcessTimeOut(tId);
        /* [TIMER-TRACE] 线程 0 周期性打印全量 census，便于现网定位 timer 泄漏 */
        if (tId == 0) {
            HcomServiceCtxStore::MaybeDumpAll();
        }
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
} // namespace hcom
} // namespace ock
