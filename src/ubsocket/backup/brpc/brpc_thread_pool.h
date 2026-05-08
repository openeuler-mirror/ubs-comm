// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: Provide the pthread pool, etc

#ifndef UBSOCKET_BRPC_THREAD_POOL_H
#define UBSOCKET_BRPC_THREAD_POOL_H

#include <cstdint>
#include <queue>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>

namespace Brpc {

enum class RunnableType {
    NORMAL = 0,
    STOP = 1,
};

class Runnable {
public:
    Runnable() : mTask{nullptr} {}
    explicit Runnable(const std::function<void()> &task) : mTask{task} {}
    virtual ~Runnable() = default;

    virtual void Run() const
    {
        if (mTask != nullptr) {
            mTask();
        }
    }

private:
    inline void Type(RunnableType type)
    {
        mType = type;
    }

    inline RunnableType Type() const
    {
        return mType;
    }

    RunnableType mType = RunnableType::NORMAL;
    std::function<void()> mTask;
    friend class ExecutorService;
};

class ExecutorService {
public:
    static ExecutorService *GetExecutorService()
    {
        static ExecutorService instance;
        return &instance;
    }

    explicit ExecutorService() noexcept;
    virtual ~ExecutorService() noexcept;

    bool Start(uint32_t queueCapacity = 10000U);
    void Stop();
    bool Execute(const Runnable &runnable);
    bool Execute(const std::function<void()> &task);
    inline void SetThreadName(const std::string &name)
    {
        name_ = name;
    }

private:
    void RunInThread();
    void DoRunnable(const Runnable &runnable, bool &flag);
    void ClearExistWorkerThread();

private:
    uint32_t threadNum_;
    uint32_t maxWaitingTaskNum_;
    std::atomic<bool> started_;
    std::atomic<bool> stopped_;
    std::atomic<uint32_t> startedThreadNum_;
    std::queue<Runnable> tasks_;
    std::mutex tasksMutex_;
    std::condition_variable tasksCond_;
    std::vector<std::thread *> workerThreads_;
    std::string name_;
};
} // namespace Brpc

#endif  // UBSOCKET_BRPC_THREAD_POOL_H
