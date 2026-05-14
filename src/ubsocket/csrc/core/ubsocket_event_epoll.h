/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_EPOLL_FD_H
#define UBS_COMM_UBSOCKET_EPOLL_FD_H

#include "ubsocket_socket.h"

namespace ock {
namespace ubs {

enum EpollEventType : uint64_t {
    EPOLL_EVENT_RAW_SOCKET = 0,
    EPOLL_EVENT_UB_SOCKET_IN,
    EPOLL_EVENT_UB_SOCKET_OUT,
    EPOLL_EVENT_BUTT
};

// 对应之前的EpollEventData
struct EpollEvent {
    EpollEventType event_type;
    int socket_fd;
    struct epoll_event event;
    EpollEvent *next{ nullptr };
    EpollEvent(EpollEventType type, int socket, const struct epoll_event &evt) noexcept
        : event_type{ type }, socket_fd{ socket }, event{ evt } {}
};

/*
 * EpollRunner注册的event中的data，总64位，高4位是类型，低60位是数值，可以是对象指针
 */
union RunnerEventData : uint64_t {
    struct EventData {
        uint64_t type : 4;
        uint64_t data : 60;
    } event_data;
    uint64_t u64;
};

enum RunnerEventType : uint64_t {
    RUNNER_EVENT_TYPE_INVALID = 0,
    RUNNER_EVENT_TYPE_SHARE_JFR,
    RUNNER_EVENT_TYPE_SUB_UMQ_RX,
    RUNNER_EVENT_TYPE_STOP,
    RUNNER_EVENT_TYPE_BUTT
};


class EpollMapper {
public:
    explicit EpollMapper(int fd) : fd_(fd)
    {
        mutex_ = g_external_lock_ops.create(LT_EXCLUSIVE);
    }

    ~EpollMapper() {}

    void Add(int epoll_fd)
    {
        ScopedUbExclusiveLocker sLock(mutex_);
        epoll_set_.insert(epoll_fd);
    }

    void Del(int epoll_fd)
    {
        ScopedUbExclusiveLocker sLock(mutex_);
        epoll_set_.erase(epoll_fd);
    }

    int QueryFirst()
    {
        ScopedUbExclusiveLocker sLock(mutex_);
        if (epoll_set_.empty()) {
            return -1;
        } else {
            return *epoll_set_.begin();
        }
    }

    void Clear();
private:
    int fd_;
    u_external_mutex_t* mutex_ = nullptr;
    std::unordered_set<int> epoll_set_;
};

class EventPollOps {
public:
    virtual ~EventPollOps() = 0;
    virtual void AddTxEvent(const Socket * const socket, int epoll_fd) = 0;
};

class EpollRunner {
public:
    static EpollRunner &GetInstance();
    ~EpollRunner()
    {
        Stop();
    }

public:
    EpollRunner(const EpollRunner &) = delete;
    EpollRunner(EpollRunner &&) = delete;
    EpollRunner &operator = (const EpollRunner &) = delete;
    EpollRunner &operator = (EpollRunner &&) = delete;

    /**
     * @brief initialize resource and start a thread to epoll_wait
     */
    int Start();
    
    /**
     * @brief uninitialize resource and stop the thread
     */
    void Stop();

    /**
     * @brief add epoll_event to EpollRunner
     * @param socket_fd socket fd added
     * @param event event of socket fd
     * @return int -1: failed; 0: success
     */
    virtual int AddEpollEvent(const Socket * const socket, struct epoll_event *event) = 0;    // TODO：这里删了些参数，处理方式见file_descriptor_async.cpp:171行注释

    /**
     * @brief delete epoll_event from EpollRunner
     * @param socket_fd socket fd removed
     * @return int -1: failed; 0: success
     */
    virtual int RemoveEpollEvent(const Socket * const socket) = 0;

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    void ProcessOneEvent(const struct epoll_event &event) = 0;

private:
    /**
     * @brief start thread to epoll_wait
     */
    void RunnerThreadRun();

private:
    int epoll_fd_;  /* used by thread */
    int exit_efd_;  /* used to notify thread exit */
    uint32_t event_ack_batch = 0;   /* do ack_interrupt when epoll num reaches event_ack_batch */
    u_external_mutex_t *mutex_;
    std::shared_ptr<EpollOps> epoll_ops_ = nullptr; /* epoll ops implemented by different prorocol */
    std::thread wait_thread_;
};

// 合并class Fd和class EpollFd，只保留EpollFd
class EventPoll {
public:
    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    virtual int EpollCtl(int op, const Socket * const socket, struct epoll_event *event) = 0;

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    virtual int EpollWait(const Socket * const socket, struct epoll_event *events, int maxevents, int timeout) = 0;

private:
    int epoll_fd_;
};

class AsyncEventPoll : public EventPoll {
public:
    /*
     * SPSCRingQueue需要使用cache line对齐的申请，标准的new无法进行对齐，需要重载new和delete
     */
    static void *operator new(std::size_t size)
    {
        if (UNLIKELY(InitSocketReadableFd() < 0)) {
            return nullptr;
        }
        ctl_mutex_ = g_external_lock_ops.create(LT_EXCLUSIVE);
        return ::aligned_alloc(alignof(EpollFdAsync), size);
    }

    static void operator delete(void *ptr)
    {
        free(ptr);
    }

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtl(int op, const Socket * const socket, struct epoll_event *event);

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    int EpollWait(const Socket * const socket, struct epoll_event *events, int maxevents, int timeout);

private:
    /**
     * @brief socket readable fd init 
     */
    int InitSocketReadableFd();

    /**
     * @brief handle epoll_ctl with EPOLL_CTL_ADD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlAdd(const Socket * const socket, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_MOD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlMod(const Socket * const socket, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_DEL operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlDel(const Socket * const socket, struct epoll_event *event);

    /**
     * @brief add pure socket_fd and event to epoll_fd
     */ 
    int AddRawSocketEvent(const Socket * const socket, struct epoll_event *event);

    /**
     * @brief delete pure socket_fd and event to epoll_fd
     */ 
    int DelRawSocketEvent(const Socket * const socket);

private:
    // u_external_mutex_t* mutex_;
    u_external_mutex_t* ctl_mutex_;
}

}   // namespace ubs
}   // namespace ock

#endif // UBS_COMM_UBSOCKET_EPOLL_FD_H


/**
 * TODO：问题记录
 * 1. 当前async的方式不开共享JFR跑不起来，后续是否还需要适配
 * 2. AddSocketReadableEvent：socket_fd无用入参
 * 3. EnsureReadableEventFdReady：m_socket_readable_event_fd重复判断，无用锁
 * 4. bug：ProcessShareJfrEvent中SiftSocketEventsWithUmqBuffers重复2次
 * 5. bug：EpollCtlAdd中ret = AddSocketOutEvent(socket_fd, connect_info.tx_interrupt_fd, event);为tx的
 * 6. bug：ProcessOneEvent中流控处理时候没有wait就直接poll了
 * 7. 性能：ProcessUmqRxEvent中umq_get_cq_event如果为0的话，不需要umq_poll了
 * 8. AddPureSocketEvent中以前有添加重复的socket_fd到epoll_fd中会报错的逻辑，当前依赖原生epoll_ctl报错，需确认是否可以
 */
