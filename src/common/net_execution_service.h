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
#ifndef HCOM_NET_EXECUTION_SERVICE_H
#define HCOM_NET_EXECUTION_SERVICE_H

#include <thread>

#include "hcom.h"

namespace ock {
namespace hcom {
enum NetRunnableType {
    NORMAL = 0,
    STOP = 1,
};

/*
 * @brief Base class of runnable task
 */
class NetRunnable {
public:
    virtual ~NetRunnable() = default;

    virtual void Run() {}

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    inline void Type(NetRunnableType type)
    {
        mType = type;
    }

    inline NetRunnableType Type() const
    {
        return mType;
    }

private:
    NetRunnableType mType = NetRunnableType::NORMAL;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetExecutorService;
};
using NetRunnablePtr = NetRef<NetRunnable>;

constexpr uint32_t ES_MAX_THR_NUM = 256;

class NetExecutorService;
using NetExecutorServicePtr = NetRef<NetExecutorService>;


/*
 * @brief Execution service is fixed thread pool to task execution
 */
class NetExecutorService {
public:
    /*
     * @brief Create an execution service with fixed number of threads
     *
     * @param threadNum          [in] number of threads
     * @param queueCapacity      [in] capacity of inner queue to store tasks
     *
     * @return executor ptr if successfully, otherwise return null
     */
    static NetExecutorServicePtr Create(uint16_t threadNum, uint32_t queueCapacity = 10000)
    {
        if (threadNum > ES_MAX_THR_NUM || threadNum == 0) {
            NN_LOG_ERROR("The num of thread must 1-" << ES_MAX_THR_NUM);
            return nullptr;
        }

        return new (std::nothrow) NetExecutorService(threadNum, queueCapacity);
    }

public:
    ~NetExecutorService()
    {
        if (!mStopped) {
            Stop();
        }
    }

    /*
     * @brief Start the execution service, wait for all threads started
     *
     * @return true if successfully
     */
    bool Start();

    /*
     * @brief Stop the execution service, wait for all threads exited
     */
    void Stop();

    /*
     * @brief Enqueue a task to thread pool, need to ensure this has been started
     *
     * The ref count of runnable will be increased and will be decreased after executed
     *
     * @return true if enqueue successfully, otherwise the queue is full
     */
    inline bool Execute(const NetRunnablePtr &runnable)
    {
        auto tmp = runnable.Get();
        if (NN_UNLIKELY(tmp == nullptr)) {
            return false;
        }

        tmp->IncreaseRef();
        return mRunnableQueue.Enqueue(tmp);
    }

    /*
     * @brief Set the thread name prefix
     *
     * @param name             [in] prefix name of execute service working thread
     */
    inline void SetThreadName(const std::string &name)
    {
        mThreadName = name;
    }

    /*
     * @brief Bind the cpu for working threads
     *
     * @param idx              [in] starting index cpu id to bind to working threads
     */
    inline void SetCpuSetStartIndex(int16_t idx)
    {
        mCpuSetStartIdx = idx;
    }

    inline bool IsStart()
    {
        return mStarted.load();
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    NetExecutorService(uint16_t threadNum, uint32_t queueCapacity)
        : mRunnableQueue(queueCapacity),
          mThreadNum(threadNum),
          mThreads(0),
          mStarted(false),
          mStopped(false),
          mStartedThreadNum(0)
    {}

    void RunInThread(int16_t cpuId);
    void DoRunnable(bool &flag);
    void ForceStop();

private:
    NetBlockingQueue<NetRunnable *> mRunnableQueue;
    uint16_t mThreadNum = 0;
    int16_t mCpuSetStartIdx = -1;
    std::vector<std::thread *> mThreads;

    std::atomic<bool> mStarted;
    std::atomic<bool> mStopped;
    std::atomic<uint16_t> mStartedThreadNum;

    std::string mThreadName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // HCOM_NET_EXECUTION_SERVICE_H
