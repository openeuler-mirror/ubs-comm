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

#ifdef UB_BUILD_ENABLED

#include "ub_thread_pool.h"

namespace ock {
namespace hcom {

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

    NN_LOG_INFO("UB threadpool initialized with " << mThreadCount << " threads");
}

void UBThreadPool::Stop()
{
    NN_LOG_INFO("UB threadpool begin to stop");
    if (!mIsRunning) {
        NN_LOG_INFO("UB threadpool is not running");
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
    NN_LOG_INFO("UB threadpool has been stopped");
}

void UBThreadPool::Submit(std::function<void()> task)
{
    if (!mIsRunning) {
        NN_LOG_ERROR("UB threadpool is not running");
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
            NN_LOG_ERROR("Caught error " << e.what() << " when execute a task, continue");
        } catch (...) {
            NN_LOG_ERROR("Caught unknown error when execute a task, continue");
        }
    }
}
}
}
#endif