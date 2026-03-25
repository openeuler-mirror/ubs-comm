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

#ifndef UB_THREAD_POOL_H
#define UB_THREAD_POOL_H
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace Brpc {

constexpr const uint32_t DEFAULT_CONNECTION_THREAD_NUM = 16;

class UBThreadPool {
public:
    UBThreadPool(const UBThreadPool&) = delete;
    UBThreadPool& operator=(const UBThreadPool&) = delete;

    static UBThreadPool& GetInstance()
    {
        static std::once_flag flag;
        std::call_once(flag, []() {
            mPtr.reset(new UBThreadPool(DEFAULT_CONNECTION_THREAD_NUM));
            mPtr->Initialize();
        });
        return *mPtr;
    }

    ~UBThreadPool()
    {
        Stop();
    }

    void Initialize();
    void Stop();
    void Submit(std::function<void()> task);

private:
    explicit UBThreadPool(uint16_t threadCount) : mThreadCount(threadCount), mIsRunning(false) {}
    void RunInThread();

    static std::unique_ptr<UBThreadPool> mPtr;
    uint16_t mThreadCount;
    std::vector<std::thread> mThreads;
    std::queue<std::function<void()>> mTasks;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mIsRunning;
};

}
#endif // UB_THREAD_POOL_H