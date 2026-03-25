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

#include "ub_thread_pool.h"
#include "rpc_adpt_vlog.h"

namespace Brpc {

std::unique_ptr<UBThreadPool> UBThreadPool::mPtr = nullptr;

void UBThreadPool::Initialize()
{
    if (mIsRunning) {
        return;
    }

    mIsRunning = true;
    mThreads.reserve(mThreadCount);
    
    for (int i = 0; i < mThreadCount; ++i) {
        mThreads.emplace_back(&UBThreadPool::RunInThread, this);
    }

    RPC_ADPT_VLOG_INFO("UB threadpool initialized with %d threads\n", mThreadCount);
}

void UBThreadPool::Stop()
{
    RPC_ADPT_VLOG_INFO("UB threadpool begin to stop\n");
    if (!mIsRunning) {
        RPC_ADPT_VLOG_INFO("UB threadpool is not running");
        return;
    }
    
    mIsRunning = false;
    mCondition.notify_all();
    for (auto& thread : mThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    mThreads.clear();
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mTasks.empty()) {
        mTasks.pop();
    }
    RPC_ADPT_VLOG_INFO("UB threadpool has been stopped\n");
}

void UBThreadPool::Submit(std::function<void()> task)
{
    if (!mIsRunning) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "UB threadpool is not running\n");
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mTasks.emplace(std::move(task));
    mCondition.notify_one();
}

void UBThreadPool::RunInThread()
{
    while (mIsRunning) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCondition.wait(lock, [this]() {
                return !mIsRunning || !mTasks.empty();
            });
            if (!mIsRunning) {
                return;
            }
            task = std::move(mTasks.front());
            mTasks.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Caught error %s when execute a task, continue\n", e.what());
        } catch (...) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Caught unknown error when execute a task, continue\n");
        }
    }
}
}