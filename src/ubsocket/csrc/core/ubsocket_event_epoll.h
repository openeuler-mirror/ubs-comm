/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_EPOLL_FD_H
#define UBS_COMM_UBSOCKET_EPOLL_FD_H

#include "ubsocket_core_types.h"
#include "ubsocket_spsc_ring_queue.h"
#include "umq_errno.h"
#include "umq_pro_types.h"
#include "umq_types.h"

namespace ock {
namespace ubs {

constexpr auto MAX_READABLE_FD_COUNT = 0x10000U;
constexpr int MAX_EPOLL_WAIT_COUNT = 1024;

enum EpollEventType : uint64_t {
    EPOLL_EVENT_RAW_SOCKET = 0,
    EPOLL_EVENT_UB_SOCKET_IN,
    EPOLL_EVENT_UB_SOCKET_OUT,
    EPOLL_EVENT_BUTT
};

struct EpollEvent {
    EpollEventType event_type;
    int socket_fd = -1;
    struct epoll_event event{};
    EpollEvent *next{nullptr};
    EpollEvent(EpollEventType type, int socket, const struct epoll_event &evt) noexcept
        : event_type{type},
          socket_fd{socket},
          event{evt}
    {
    }
};

/*
 * EpollRunner注册的event中的data，总64位，高4位是类型，低60位是数值，可以是对象指针
 */
union RunnerEventData {
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
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    ~EpollMapper() {}

    void Add(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.insert(epoll_fd);
    }

    void Del(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.erase(epoll_fd);
    }

    int QueryFirst()
    {
        Locker sLock(mutex_);
        if (epoll_set_.empty()) {
            return -1;
        } else {
            return *epoll_set_.begin();
        }
    }

    void Clear() {}

private:
    int fd_;
    u_mutex_t *mutex_ = nullptr;
    std::unordered_set<int> epoll_set_;
};
EpollMapper *GetSocketEpollMapper(int socket_fd);
class EpollRunnerOps {
public:
    EpollRunnerOps() = default;
    virtual ~EpollRunnerOps() = default;

    /**
     * @brief add epoll_event to EpollRunner
     * @param sock socket fd added
     * @param epoll_fd epoll fd
     * @param event event of socket fd
     * @return int -1: failed; 0: success
     */
    virtual int AddEpollEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) = 0;

    /**
     * @brief delete epoll_event from EpollRunner
     * @param sock socket fd removed
     * @param epoll_fd epoll fd
     * @return int -1: failed; 0: success
     */
    virtual int RemoveEpollEvent(const SocketPtr &sock, int epoll_fd) = 0;

    DEFINE_REF_OPERATION_FUNC
protected:
    DECLARE_REF_COUNT_VARIABLE;
};
using EpollRunnerOpsPtr = Ref<EpollRunnerOps>;

/*
 *    (a)           (b)                                  (d)              (e)
 *  socket_fd     tx_fd                     ┌──────── share_jfr_fd       rx_fd
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 * ┌────┼────────────┼─────┐                │           ┌──┼───────────────┼──┐
 * │    AsyncEventPoll     │                │           │     EpollRunner     │
 * └───────┬───────────────┘                │           └──────┬──────────────┘
 *         │                                │                  │
 *         │                                │                  │
 *         │                                │                  │
 *         │                    (g)         │                  │
 *    readable_event_fd  <──────────────────┘              exit_event_fd
 *        (c)                                                 (f)
 *
 */

class EpollRunnerBase {
public:
    virtual ~EpollRunnerBase() = default;
    virtual int Start() = 0;
    virtual void Stop() = 0;
    virtual int AddEpollEvent(const SocketPtr &sock, struct epoll_event *event) = 0;
    virtual int DelEpollEvent(const SocketPtr &sock) = 0;
    virtual int ProcessOneEvent(const struct epoll_event &event) = 0;
};

template <SocketType T>
class EpollRunner : public EpollRunnerBase {
public:
    static EpollRunner &GetInstance()
    {
        static EpollRunner<T> instance;
        return instance;
    }

    ~EpollRunner() override
    {
        Stop();
    }

public:
    EpollRunner(const EpollRunner &) = delete;
    EpollRunner(EpollRunner &&) = delete;
    EpollRunner &operator=(const EpollRunner &) = delete;
    EpollRunner &operator=(EpollRunner &&) = delete;

    /**
     * @brief initialize resource and start a thread to epoll_wait
     */
    int Start() override;
    /**
     * @brief uninitialize resource and stop the thread
     */
    void Stop() override;

    /**
     * @brief add epoll_event to EpollRunner
     * @param socket_fd socket fd added
     * @param event event of socket fd
     * @return int -1: failed; 0: success
     */
    int AddEpollEvent(const SocketPtr &sock, struct epoll_event *event) override;

    /**
     * @brief delete epoll_event from EpollRunner
     * @param socket_fd socket fd removed
     * @return int -1: failed; 0: success
     */
    int DelEpollEvent(const SocketPtr &sock) override;

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq);

    int ProcessMainUmqRearm(uint64_t main_umq);

    std::unordered_set<Socket *> SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count);

protected:
    int epoll_fd_;                /* used by thread */
    int exit_efd_;                /* used to notify thread exit */
    uint32_t event_ack_batch = 0; /* do ack_interrupt when epoll num reaches event_ack_batch */
    u_mutex_t *mutex_;            /* mutex */
    std::once_flag flag_;
    std::thread wait_thread_;

private:
    EpollRunner() = default;
    /**
     * @brief start thread to epoll_wait
     */
    void RunInThread() noexcept;

    uint32_t event_num_{0};
};

class EpollRunnerFactory {
public:
    static EpollRunnerBase &GetInstance(SocketType type)
    {
        switch (type) {
            case SocketType::SOCK_TYPE_UMQ:
                return EpollRunner<SocketType::SOCK_TYPE_UMQ>::GetInstance();
            default:
                throw std::runtime_error("Not support type for epoll runner base");
        }
        throw std::runtime_error("Not support type for epoll runner base");
    }
};

class EventPoll {
public:
    explicit EventPoll(int epoll_fd) : epoll_fd_(epoll_fd)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        ctl_mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    virtual ~EventPoll() = default;

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    virtual int EpollCtl(int op, const SocketPtr &sock, struct epoll_event *event) = 0;

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    virtual int EpollWait(const SocketPtr &sock, struct epoll_event *events, int maxevents, int timeout) = 0;

    virtual void WakeUpEpollFd() = 0;

    DEFINE_REF_OPERATION_FUNC;

public:
    DECLARE_REF_COUNT_VARIABLE;

protected:
    int epoll_fd_;
    u_mutex_t *ctl_mutex_;
    u_mutex_t *mutex_;
};
using EventPollPtr = Ref<EventPoll>;

class AsyncEventPoll : public EventPoll {
public:
    /*
     * SPSCRingQueue需要使用cache line对齐的申请，标准的new无法进行对齐，需要重载new和delete
     */
    static void *operator new(std::size_t size) noexcept
    {
        return ::aligned_alloc(alignof(AsyncEventPoll), size);
    }

    static void operator delete(void *ptr)
    {
        free(ptr);
    }

    explicit AsyncEventPoll(int epoll_fd) noexcept : EventPoll{epoll_fd} {}

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtl(int op, const SocketPtr &sock, struct epoll_event *event);

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    int EpollWait(const SocketPtr &sock, struct epoll_event *events, int maxevents, int timeout);

    /**
     * @brief add event_data to readable socket event queue
     * @param data event_data added to event queue
     * @return 0: success; -1: failed
     */
    int AddReadableEvent(epoll_data_t data);

    int SetReadableEventFd();

    void WakeUpEpollFd() override;

private:
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_ADD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlAdd(const SocketPtr &sock, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_MOD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlMod(const SocketPtr &sock, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_DEL operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlDel(const SocketPtr &sock, struct epoll_event *event);

    /**
     * @brief add raw socket_fd and event to epoll_fd
     */
    int AddRawSocketEvent(const SocketPtr &sock, struct epoll_event *event);

    /**
     * @brief delete raw socket_fd and event to epoll_fd
     */
    int DelRawSocketEvent(const SocketPtr &sock);

    /**
     * @brief add socket_readable_fd to epoll_fd
     */
    int AddSockReadableEvent();

    /**
     * @brief add proto tx fd to epoll_fd
     */
    int AddProtoTxEvent(const SocketPtr &sock, struct epoll_event *event);

    /**
     * @brief del proto tx fd from epoll_fd
     */
    int DelProtoTxEvent(const SocketPtr &sock);

    /**
     * @brief check if socket event data exist
     */
    ALWAYS_INLINE bool IsSocketEventDataExist(int fd) noexcept
    {
        Locker sLock(mutex_);
        return socket_data_.find(fd) != socket_data_.end();
    }

    ALWAYS_INLINE bool InsertSocketEventData(int fd, EpollEvent *data) noexcept
    {
        Locker sLock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos != socket_data_.end())) {
            return false;
        }
        socket_data_.emplace(fd, data);
        return true;
    }

    ALWAYS_INLINE bool RemoveSocketEventData(int fd) noexcept
    {
        Locker sLock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return false;
        }
        auto removed = pos->second;
        socket_data_.erase(pos);
        if (removed != nullptr) {
            removed->next = removed_head_;
            removed_head_ = removed;
        }
        return true;
    }

    /**
     * @brief deal with events in the readable socket event queue
     */
    int ArrangeWakeUpEvents(struct epoll_event *events, int input_count, int max_events);

    /**
     * @brief remove all stashed EpollEvent at epoll_wait
     */
    void ReleaseRemovedEventsData();

private:
    int sock_readable_fd_ = -1;
    EpollEvent sock_readable_event_ = {EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{}};
    std::unordered_map<int, EpollEvent *> socket_data_;
    EpollEvent *removed_head_ = nullptr; // 待删除的event data列表，用wait唤醒时统一释放
    SPSCRingQueue<struct epoll_event> readable_sockets_event_queue_{MAX_READABLE_FD_COUNT};
};
using AsyncEventPollPtr = Ref<AsyncEventPoll>;

} // namespace ubs
} // namespace ock

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
