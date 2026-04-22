// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: Provide the pthread pool, etc
#include <stdexcept>
#include "brpc_context.h"
#include "rpc_adpt_vlog.h"
#include "brpc_thread_pool.h"

namespace Brpc {
ExecutorService::ExecutorService() noexcept
    : started_{false}, stopped_{false}, startedThreadNum_{0U}
{}

ExecutorService::~ExecutorService() noexcept
{
    if (!stopped_) {
        Stop();
    }
}

bool ExecutorService::Start(uint32_t queueCapacity)
{
    if (started_) {
        return true;
    }

    uint32_t threadNum = Brpc::Context::GetContext()->ThreadPoolSize();
    if (threadNum == 0) {
        RPC_ADPT_VLOG_INFO("set thread pool size is zero\n");
        return false;
    }
    threadNum_ = threadNum;
    maxWaitingTaskNum_ = queueCapacity;

    for (auto i = 0U; i < threadNum_; i++) {
        auto thr = new (std::nothrow) std::thread(&ExecutorService::RunInThread, this);
        if (thr == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create executor thread %u\n", i);
            ClearExistWorkerThread();
            return false;
        }

        workerThreads_.push_back(thr);
    }

    while (startedThreadNum_ < threadNum_) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    started_ = true;
    return true;
}

void ExecutorService::Stop()
{
    if (!started_ || stopped_) {
        return;
    }

    ClearExistWorkerThread();
    stopped_ = true;
    started_ = false;
}

bool ExecutorService::Execute(const Runnable &runnable)
{
    if (!started_) {
        return false;
    }
    std::unique_lock<std::mutex> locker{tasksMutex_};
    if (tasks_.size() >= maxWaitingTaskNum_) {
        return false;
    }
    tasks_.push(runnable);
    locker.unlock();
    tasksCond_.notify_one();
    return true;
}

bool ExecutorService::Execute(const std::function<void()> &task)
{
    return Execute(Runnable(task));
}

void ExecutorService::DoRunnable(const Runnable &runnable, bool &flag)
{
    try {
        if (runnable.Type() == RunnableType::STOP) {
            flag = false;
        } else {
            runnable.Run();
        }
    } catch (std::runtime_error &ex) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Caught error when execute a task, continue\n");
    } catch (...) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Caught unknown error when execute a task, continue\n");
    }
}

void ExecutorService::ClearExistWorkerThread()
{
    Runnable stopTask;
    stopTask.Type(RunnableType::STOP);

    std::unique_lock<std::mutex> locker{tasksMutex_};
    for (auto i = 0U; i < workerThreads_.size(); i++) {
        tasks_.push(stopTask);
    }
    locker.unlock();
    tasksCond_.notify_all();

    for (auto &thr : workerThreads_) {
        if (thr != nullptr) {
            thr->join();
        }
    }

    startedThreadNum_ = 0;
    for (auto thr : workerThreads_) {
        delete thr;
    }
    workerThreads_.clear();
}

void ExecutorService::RunInThread()
{
    bool runFlag = true;
    auto index = startedThreadNum_++;
    auto threadName = name_.empty() ? "executor" : name_;
    threadName += std::to_string(index);
    pthread_setname_np(pthread_self(), threadName.c_str());
    RPC_ADPT_VLOG_INFO("thread %s started.\n", threadName.c_str());
    while (runFlag) {
        std::unique_lock<std::mutex> locker{tasksMutex_};
        while (tasks_.empty()) {
            tasksCond_.wait(locker);
        }
        Runnable task(std::move(tasks_.front()));
        tasks_.pop();
        locker.unlock();

        DoRunnable(task, runFlag);
    }
    RPC_ADPT_VLOG_INFO("thread %s finished.\n", threadName.c_str());
}
} // namespace Brpc