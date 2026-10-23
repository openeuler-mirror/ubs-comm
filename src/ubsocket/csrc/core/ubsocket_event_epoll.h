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

#include <memory>
#include <atomic>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"
#include "common/ubsocket_mpsc_ring_queue.h"
#include "ubsocket_core_types.h"
#include "umq_errno.h"

namespace ock {
namespace ubs {

constexpr auto MAX_READABLE_FD_COUNT = 0x10000U;
constexpr int MAX_EPOLL_WAIT_COUNT = 128;

enum EpollEventType : uint64_t {
    EPOLL_EVENT_RAW_SOCKET = 0,
    EPOLL_EVENT_UB_SOCKET_IN,
    EPOLL_EVENT_UB_SOCKET_OUT,
    EPOLL_EVENT_JFCE_FD,
    EPOLL_EVENT_BUTT
};

struct EpollEvent {
    EpollEventType event_type;
    int socket_fd = -1;
    struct epoll_event event {};
    EpollEvent *next{nullptr};
    std::atomic<bool> oneshot_disabled{false};  // EPOLLONESHOT: disabled after triggered, cleared on MOD re-arm.
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
    RUNNER_EVENT_TYPE_SHARE_JFR_RETRY,
    RUNNER_EVENT_TYPE_SUB_UMQ_RX,
    RUNNER_EVENT_TYPE_TP_TX,
    RUNNER_EVENT_TYPE_TP_TX_TIMER,
    RUNNER_EVENT_TYPE_FC_TX,
    RUNNER_EVENT_TYPE_TP_EVENT,
    RUNNER_EVENT_TYPE_STOP,
    RUNNER_EVENT_TYPE_BUTT
};

class EpollMapper {
public:
    explicit EpollMapper(int fd) : fd_(fd)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    ~EpollMapper()
    {
        if (mutex_ != nullptr) {
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
        }
    }

    void Add(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.insert(epoll_fd);
    }

    bool Del(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.erase(epoll_fd);
        return epoll_set_.empty();
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

    /* 快照该 fd 注册过的所有 epoll fd (供 close 清理钩子使用) */
    std::vector<int> GetEpollFds()
    {
        Locker sLock(mutex_);
        return std::vector<int>(epoll_set_.begin(), epoll_set_.end());
    }

    void Clear() {}

private:
    const int fd_;
    u_mutex_t *mutex_ = nullptr;
    std::unordered_set<int> epoll_set_;
};

/* 注册表(g_socket_epoll_mappers)持有的 shared_ptr 即“基引用”;
 * Get/Create 返回的拷贝即“借出引用”, 调用方无需手工 delete,
 * 最后一个持有者释放后对象自动析构 */
using EpollMapperPtr = std::shared_ptr<EpollMapper>;
extern u_rw_lock_t *g_socket_epoll_lock;
EpollMapperPtr GetSocketEpollMapper(int socket_fd);
bool CreateSocketEpollMapper(int socket_fd, EpollMapperPtr &mapper);
/**
 * @brief 从注册表摘除 fd 的注册项(只摘“基引用”, 真正析构由最后一个持有者完成)
 * @param socket_fd socket fd
 * @param expect   非空时仅当表中仍是该对象才摘除。调用方持有借出引用时应传自身指针,
 *                 避免并发 close 后同 fd 号重新 ADD 出来的新对象被误删;
 *                 传 nullptr 表示按 fd 无条件摘除(仅 close/uninit 语义下使用)
 */
void CleanSocketEpollMapper(int socket_fd, const EpollMapper *expect = nullptr);
void CleanAllSocketEpollMappers();
void OnSocketClosed(int socket_fd);

class EpollRunnerOps {
public:
    struct ExtContext {
        uint64_t umq_handle = UMQ_INVALID_HANDLE;
        virtual ~ExtContext() = default;
    };

    EpollRunnerOps() = default;
    virtual ~EpollRunnerOps() = default;

    virtual int ProcessOneEvent(const struct epoll_event &event)
    {
        UBS_VLOG_ERR("EpollRunner EpollRunType Not Specified.\n");
        return -1;
    }

    virtual int AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx = nullptr)
    {
        UBS_VLOG_ERR("EpollRunner EpollRunType Not Specified.\n");
        return -1;
    }

    virtual int DelEpollEvent(int epoll_fd, int fd)
    {
        UBS_VLOG_ERR("EpollRunner EpollRunType Not Specified.\n");
        return -1;
    }

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
class EventPoll;

class EpollRunnerBase {
public:
    virtual ~EpollRunnerBase() = default;
    virtual int Start() = 0;
    virtual void Stop() = 0;
    virtual int AddEpollEvent(int fd, struct epoll_event *event, EpollRunnerOps::ExtContext *ctx) = 0;
    virtual int DelEpollEvent(int fd) = 0;
    virtual int ProcessOneEvent(const struct epoll_event &event) = 0;
    virtual std::string GetRunnerName() = 0;
    virtual EpollRunnerOps *GetOps() = 0;
};

class EpollRunnerBackend {
public:
    virtual ~EpollRunnerBackend() = default;
    virtual int Start() = 0;
    virtual void Stop() = 0;
};

template <EpollRunnerType T>
class PthreadEpollRunnerBackend;

template <EpollRunnerType T>
class EpollRunner
    : public EpollRunnerBase
    , public LeakySingleton<EpollRunner<T>> {
    friend LeakySingleton<EpollRunner>;

public:
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
     * @param fd  fd
     * @param event event of socket fd
     * @return int -1: failed; 0: success
     */
    int AddEpollEvent(int fd, struct epoll_event *event, EpollRunnerOps::ExtContext *ctx) override;

    /**
     * @brief delete epoll_event from EpollRunner
     * @param socket_fd socket fd removed
     * @return int -1: failed; 0: success
     */
    int DelEpollEvent(int fd) override;

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    /**
     * @brief get runner name
     */
    std::string GetRunnerName() override;

    EpollRunnerOps *GetOps() override
    {
        return ops_;
    }

protected:
    int epoll_fd_ = -1;           /* used by thread */
    int exit_efd_ = -1;           /* used to notify thread exit */
    uint32_t event_ack_batch = 0; /* do ack_interrupt when epoll num reaches event_ack_batch */
    u_mutex_t *mutex_ = nullptr;  /* mutex */
    std::once_flag flag_;
    std::unique_ptr<EpollRunnerBackend> backend_;
    EpollRunnerOps *ops_ = nullptr;

private:
    friend class PthreadEpollRunnerBackend<T>;

    EpollRunner() = default;
    /**
     * @brief start thread to epoll_wait
     */
    void RunInThread() noexcept;
    bool DrainReadyEvents(int timeout, bool *hasEvents = nullptr) noexcept;
    std::unique_ptr<EpollRunnerBackend> CreateBackend();

    uint32_t event_num_{0};
};

class EpollRunnerFactory {
public:
    static EpollRunnerBase &GetInstance(EpollRunnerType type)
    {
        switch (type) {
            case EpollRunnerType::SHARE_JFR_RX_RUNNER:
                return EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>::Instance();
            case EpollRunnerType::TRANSPORT_POOL_TX_RUNNER:
                return EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>::Instance();
            case EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER:
                return EpollRunner<EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER>::Instance();
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

    virtual ~EventPoll()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        LockRegistry::LOCK_OPS.destroy(ctl_mutex_);
    }

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    virtual int EpollCtl(int op, int fd, struct epoll_event *event) = 0;

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    virtual int EpollWait(struct epoll_event *events, int maxevents, int timeout) = 0;

    virtual void WakeUpEpollFd() = 0;

    /**
     * @brief socket fd 关闭通知: 清理本 poll 实例内该 fd 的用户态注册表残留
     *        (内核侧随 fd/eventfd 关闭自动注销, 此处仅清 socket_data_ 等映射)
     * @param fd closed socket fd
     */
    virtual void OnSocketClosed(int fd)
    {
        (void)fd;
    }

    ALWAYS_INLINE int GetEpollFd() const noexcept
    {
        return epoll_fd_;
    }

    /**
     * @brief Set wakeup callback for async accept (works for both AsyncEventPoll and UrmaEventPoll)
     * @param ready_event Pointer to the ready_event EpollEvent (from UbsocketWakeupEvent)
     * @param cb Callback function to process ready events
     */
    void SetWakeupCallback(EpollEvent *ready_event,
                           std::function<int(struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &)> cb)
    {
        ready_event_ = ready_event;
        wakeup_callback_ = cb;
    }

    DEFINE_REF_OPERATION_FUNC;

protected:
    DECLARE_REF_COUNT_VARIABLE;

    const int epoll_fd_;
    u_mutex_t *ctl_mutex_;
    u_mutex_t *mutex_;

    // For async accept wakeup
    EpollEvent *ready_event_ = nullptr;
    std::function<int(struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &)> wakeup_callback_;
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

    ~AsyncEventPoll() override;

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtl(int op, int fd, struct epoll_event *event);

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    int EpollWait(struct epoll_event *events, int maxevents, int timeout) override;

    /**
     * @brief add event_data to readable socket event queue
     * @param data event_data added to event queue
     * @return 0: success; -1: failed
     */
    int AddReadableEvent(uint32_t events, epoll_data_t data);

    int SetReadableEventFd();

    void WakeUpEpollFd() override;

private:
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_ADD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlAdd(int fd, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_MOD operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlMod(int fd, struct epoll_event *event);
    /**
     * @brief handle epoll_ctl with EPOLL_CTL_DEL operation
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    int EpollCtlDel(int fd, struct epoll_event *event);

    /**
     * @brief add raw socket_fd and event to epoll_fd
     */
    int AddRawSocketEvent(int fd, struct epoll_event *event);

    /**
     * @brief delete raw socket_fd and event to epoll_fd
     */
    int DelRawSocketEvent(int fd);

    /**
     * @brief mod raw socket_fd and event to epoll_fd
     */
    int ModRawSocketEvent(int fd, struct epoll_event *event);

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

    ALWAYS_INLINE EpollEvent *GetSocketEventData(int fd) noexcept
    {
        Locker slock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return nullptr;
        }
        auto res = pos->second;
        slock.Unlock();
        return res;
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
    MPSCRingQueue<struct epoll_event> readable_sockets_event_queue_{MAX_READABLE_FD_COUNT};
};
using AsyncEventPollPtr = Ref<AsyncEventPoll>;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_EPOLL_FD_H
