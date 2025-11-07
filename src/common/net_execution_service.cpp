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

#include "net_execution_service.h"

namespace ock {
namespace hcom {
bool NetExecutorService::Start()
{
    if (mStarted) {
        return true;
    }

    /* init ring buffer blocking queue */
    auto result = mRunnableQueue.Initialize();
    if (result != 0) {
        NN_LOG_ERROR("Failed to initialize queue, result " << result);
        return false;
    }

    for (uint16_t i = 0; i < mThreadNum; i++) {
        auto cpuId = mCpuSetStartIdx < 0 ? -1 : mCpuSetStartIdx + i;
        auto *thr = new (std::nothrow) std::thread(&NetExecutorService::RunInThread, this, cpuId);
        if (thr == nullptr) {
            ForceStop();
            NN_LOG_ERROR("Failed to create executor thread " << i);
            return false;
        }

        mThreads.push_back(thr);
    }

    while (mStartedThreadNum < mThreadNum) {
        usleep(1);
    }

    mStarted = true;
    mStopped = false;
    return true;
}

void NetExecutorService::ForceStop()
{
    for (uint32_t i = 0; i < mThreads.size(); ++i) {
        NetRunnablePtr stopTask = new (std::nothrow) NetRunnable();
        if (stopTask == nullptr) {
            NN_LOG_ERROR("Failed to new stop task, probably out of memory");
            break;
        }
        stopTask->Type(NetRunnableType::STOP);

        NetRunnable *tmp = stopTask.Get();
        tmp->IncreaseRef();
        if (!mRunnableQueue.EnqueueFirst(tmp)) {
            continue;
        }
    }

    for (auto &thr : mThreads) {
        if (thr != nullptr) {
            thr->join();
        }
    }

    mRunnableQueue.UnInitialize();

    while (!mThreads.empty()) {
        delete (mThreads.back());
        mThreads.pop_back();
    }
    mStopped = true;
    mStarted = false;
}

void NetExecutorService::Stop()
{
    if (!mStarted || mStopped) {
        return;
    }
    ForceStop();
}

void NetExecutorService::DoRunnable(bool &flag)
{
    try {
        NetRunnable *task = nullptr;
        mRunnableQueue.Dequeue(task);
        if (task != nullptr) {
            /* the ref count of `task` was manually increased when enqueue, and it will be automatically increased again
            when assignning to `runnable`, so it should be decreased explicitly after assignment to make the
            ref count = 1. */
            NetRunnablePtr runnable = task;
            task->DecreaseRef();
            if (runnable->Type() == NetRunnableType::NORMAL) {
                runnable->Run();
            } else if (runnable->Type() == NetRunnableType::STOP) {
                flag = false;
            } else {
                NN_LOG_ERROR("Un-reachable path");
            }
        } else {
            NN_LOG_ERROR("Task is null");
        }
    } catch (std::runtime_error &ex) {
        NN_LOG_ERROR("Caught error " << ex.what() << " when execute a task, continue");
    } catch (...) {
        NN_LOG_ERROR("Caught unknown error when execute a task, continue");
    }
}

void NetExecutorService::RunInThread(int16_t cpuId)
{
    bool runFlag = true;
    uint16_t threadIndex = mStartedThreadNum++;

    auto threadName = mThreadName.empty() ? "executor" : mThreadName;
    threadName += std::to_string(threadIndex);
    if (cpuId != -1) {
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(cpuId, &cpuSet);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) != 0) {
            NN_LOG_WARN("Invalid to bind executor thread" << threadName << " << to cpu " << cpuId);
        }
    }

    pthread_setname_np(pthread_self(), threadName.c_str());
    NN_LOG_INFO("Thread is started for executor service <" << threadName << "> cpuId " << cpuId);

    while (runFlag) {
        DoRunnable(runFlag);
    }

    NN_LOG_INFO("Thread for executor service <" << threadName << "> cpuId " << cpuId << " exiting");
}
}
}
