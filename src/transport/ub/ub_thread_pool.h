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

#ifndef HCOM_UB_THREAD_POOL_H
#define HCOM_UB_THREAD_POOL_H
#ifdef UB_BUILD_ENABLED
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

#include "hcom.h"
#include "ub_common.h"

namespace ock {
namespace hcom {

class UBThreadPool {
public:
    explicit UBThreadPool(uint16_t threadCount = NN_NO16) : mThreadCount(threadCount), mIsRunning(false) {}
    ~UBThreadPool()
    {
        Stop();
    }
    UBThreadPool(const UBThreadPool&) = delete;
    UBThreadPool& operator=(const UBThreadPool&) = delete;
    void Initialize();
    void Stop();
    void Submit(std::function<void()> task);

private:
    void RunInThread();
    
    uint16_t mThreadCount;
    std::vector<std::thread> mThreads;
    std::queue<std::function<void()>> mTasks;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mIsRunning;
};

}
}
#endif
#endif // HCOM_UB_THREAD_POOL_H