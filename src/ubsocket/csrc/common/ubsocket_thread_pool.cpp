// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: Provide the pthread pool, etc
#include "ubsocket_thread_pool.h"
#include <stdexcept>
#include "ubsocket_global_setting.h"
#include "ubsocket_logger.h"

namespace ock {
namespace ubs {
ExecutorService::ExecutorService() noexcept : started_{false}, stopped_{false}, startedThreadNum_{0U} {}

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

    uint32_t threadNum = GlobalSetting::UBS_THREAD_POOL_SIZE;
    if (threadNum == 0) {
        UBS_VLOG_INFO("set thread pool size is zero\n");
        return false;
    }
    threadNum_ = threadNum;
    tasks_.Initialize(queueCapacity);

    for (auto i = 0U; i < threadNum_; i++) {
        auto thr = new (std::nothrow) std::thread(&ExecutorService::RunInThread, this);
        if (thr == nullptr) {
            UBS_VLOG_ERR("Failed to create executor thread %u\n", i);
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
    if (!tasks_.PushBack(runnable)) {
        return false;
    }
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
        UBS_VLOG_ERR("Caught error when execute a task, continue\n");
    } catch (...) {
        UBS_VLOG_ERR("Caught unknown error when execute a task, continue\n");
    }
}

void ExecutorService::ClearExistWorkerThread()
{
    Runnable stopTask;
    stopTask.Type(RunnableType::STOP);
    for (auto i = 0U; i < workerThreads_.size(); i++) {
        while (!tasks_.PushBack(stopTask)) {
            std::this_thread::yield();
        }
    }

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
    UBS_VLOG_INFO("thread %s started.\n", threadName.c_str());
    while (runFlag) {
        Runnable task;
        while (!tasks_.PopFront(task)) {
            std::this_thread::yield();
        }
        DoRunnable(task, runFlag);
    }
    UBS_VLOG_INFO("thread %s finished.\n", threadName.c_str());
}
} // namespace ubs
} // namespace ock
