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

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <sched.h>
#include <vector>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_leaky_singleton.h"
#include "common/ubsocket_mpsc_ring_queue.h"
#include "ubsocket_core_types.h"
#include "umq_errno.h"

namespace ock {
namespace ubs {

constexpr auto MAX_READABLE_FD_COUNT = 0x10000U;
constexpr uint64_t MAX_READABLE_RING_CAPACITY = 1ULL << 18;

/*
 * 可读事件注入环的容量：随 ArraySet<Socket> 容量取整到 2 的幂，下限保持旧值 64K，上限 256K（≈3MB，
 * 覆盖 8W 链规格的 3 倍深度；再往上是底噪成本——nofile=1M 时 1M 槽要 12MB/dispatcher，见 issue #26）。
 * 环是 MPSC，满时 Push 返回 false；若环比 fd 表小，同时可读的 socket 一多，可读通知就会被丢（issue #44）。
 */
inline uint64_t ReadableRingCapacityFor(uint64_t fd_capacity) noexcept
{
    uint64_t cap = MAX_READABLE_FD_COUNT;
    while (cap < fd_capacity && cap < MAX_READABLE_RING_CAPACITY) {
        cap <<= 1U;
    }
    return cap;
}
constexpr int MAX_EPOLL_WAIT_COUNT = 128;

/*
 * 直接投递模式: 就绪事件当场交给外部 poller(如 brpc)处理,
 * 不入 readable 队列、不写 sock_readable_fd_ 再唤醒外部 epoll_wait 一次。
 * 返回 true 表示已投递; false 表示未启用, 调用方走原有队列+eventfd路径。
 *
 * 外部 poller 可能只入队不唤醒(NOSIGNAL 语义); 批量投递后必须调
 * FlushDirectDispatch() 统一唤醒。单事件生产点用 flush=true 立即唤醒。
 */
ALWAYS_INLINE void FlushDirectDispatch()
{
    auto *ops = GlobalSetting::UBS_POLLER_OPS;
    if (ops != nullptr && ops->dispatch_flush != nullptr) {
        ops->dispatch_flush();
    }
}

ALWAYS_INLINE bool TryDirectDispatchEvent(uint32_t events, epoll_data_t data, bool flush = true)
{
    auto *ops = GlobalSetting::UBS_POLLER_OPS;
    if (ops == nullptr || ops->dispatch_event == nullptr) {
        return false;
    }
    ops->dispatch_event(data.u64, events);
    if (flush) {
        FlushDirectDispatch();
    }
    return true;
}

/*
 * Cooperative scheduler yield. This helper may be called from either the
 * unified polling ACTIVE loop or synchronous teardown. A host callback must
 * not depend on poller-thread-local state and must support both worker fibers
 * and regular pthread contexts. Fall back to sched_yield when unregistered.
 */
ALWAYS_INLINE void PollerYield()
{
    auto *ops = GlobalSetting::UBS_POLLER_OPS;
    if (ops != nullptr && ops->poller_yield != nullptr) {
        ops->poller_yield();
        return;
    }
    sched_yield();
}

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
    RUNNER_EVENT_TYPE_SHARE_JFR_RETRY,
    RUNNER_EVENT_TYPE_SUB_UMQ_RX,
    RUNNER_EVENT_TYPE_TP_TX,
    RUNNER_EVENT_TYPE_TP_TX_TIMER,
    RUNNER_EVENT_TYPE_FC_TX,
    RUNNER_EVENT_TYPE_TP_EVENT,
    RUNNER_EVENT_TYPE_TX_CQE_TIMER,
    RUNNER_EVENT_TYPE_TX_WAKE,
    RUNNER_EVENT_TYPE_STOP,
    RUNNER_EVENT_TYPE_BUTT
};

extern u_rw_lock_t *g_socket_epoll_lock;

/*
 * 每 socket 一个 EpollMapper（随链路创建）。内存收敛改造：
 * 1. 不再持有实例级互斥锁（原每链路一次 LOCK_OPS.create，即一个 bthread::Mutex +
 *    池化 Butex）。并发保护复用全局 g_socket_epoll_lock：Add/Del 写锁、QueryFirst
 *    读锁。所有调用点（EpollCtl 的 ADD/DEL、Acceptor::InitWakeupEvent）在调用时
 *    均未持有该全局锁，无重入死锁风险；Add/Del 属建链/拆链冷路径，锁粒度可接受。
 * 2. epoll fd 集合由 unordered_set 改为 vector：一个 socket 通常只属于 1~2 个
 *    epoll，哈希表的桶数组与节点开销纯属浪费。
 */
class EpollMapper {
public:
    explicit EpollMapper(int fd) : fd_(fd) {}

    ~EpollMapper() = default;

    void Add(int epoll_fd)
    {
        WriteLocker sLock(g_socket_epoll_lock);
        if (std::find(epoll_fds_.begin(), epoll_fds_.end(), epoll_fd) == epoll_fds_.end()) {
            epoll_fds_.push_back(epoll_fd);
        }
    }

    bool Del(int epoll_fd)
    {
        WriteLocker sLock(g_socket_epoll_lock);
        auto iter = std::find(epoll_fds_.begin(), epoll_fds_.end(), epoll_fd);
        if (iter != epoll_fds_.end()) {
            epoll_fds_.erase(iter);
        }
        return epoll_fds_.empty();
    }

    int QueryFirst()
    {
        ReadLocker sLock(g_socket_epoll_lock);
        if (epoll_fds_.empty()) {
            return -1;
        } else {
            return epoll_fds_.front();
        }
    }

    void Clear() {}

private:
    const int fd_;
    std::vector<int> epoll_fds_;
};
EpollMapper *GetSocketEpollMapper(int socket_fd);
void CleanAllSocketEpollMappers();
/* 预留 socket->EpollMapper 全局表桶数组，避免大规模建链期间反复 rehash */
void ReserveSocketEpollMappers(size_t capacity);

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
class ExternalPollerEpollRunnerBackend;

template <EpollRunnerType T>
class ExternalDirectPollerEpollRunnerBackend;

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
    /* Start() 的一次性初始化改用 pthread 互斥 + 完成标志（bthread 上禁用
     * std::call_once，原因见 ubsocket_leaky_singleton.h::Instance 的注释） */
    pthread_mutex_t once_mtx_ = PTHREAD_MUTEX_INITIALIZER;
    std::atomic<bool> once_done_{false};
    std::unique_ptr<EpollRunnerBackend> backend_;
    EpollRunnerOps *ops_ = nullptr;

private:
    friend class PthreadEpollRunnerBackend<T>;
    friend class ExternalPollerEpollRunnerBackend<T>;
    friend class ExternalDirectPollerEpollRunnerBackend<T>;

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

    ALWAYS_INLINE int GetEpollFd() const noexcept
    {
        return epoll_fd_;
    }

    DEFINE_REF_OPERATION_FUNC;

protected:
    DECLARE_REF_COUNT_VARIABLE;

    const int epoll_fd_;
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

    static void *operator new(std::size_t size, const std::nothrow_t &) noexcept
    {
        return ::aligned_alloc(alignof(AsyncEventPoll), size);
    }

    static void operator delete(void *ptr)
    {
        free(ptr);
    }

    explicit AsyncEventPoll(int epoll_fd) noexcept;

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

    using WakeupCallback = std::function<int(struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &)>;

    /**
     * @brief Register the async-accept wakeup of one listening socket (add, or replace by ready_event).
     * One entry per listener: several listening ports may share this epoll (multi-port server),
     * each with its own ready_queue + eventfd. The former single slot let the last registration
     * overwrite the others, so only one port ever woke brpc (issue #50).
     * Copy-on-write table: writers swap it under wakeup_mutex_; the poll loop takes one atomic
     * snapshot per batch, so registration never blocks EpollWait.
     * @param ready_event Pointer to the ready_event EpollEvent (from UbsocketWakeupEvent)
     * @param cb Callback function to process ready events
     */
    void SetWakeupCallback(EpollEvent *ready_event, WakeupCallback cb);
    /** @brief Unregister one listener's wakeup (UbsocketWakeupEvent::CleanUp). Idempotent. */
    void RemoveWakeupCallback(EpollEvent *ready_event);
    /** @brief Number of registered listener wakeups (diagnostic / UT). */
    size_t WakeupCallbackCount() const;

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

    /* issue#43 饿死护栏：连续从注入队列取事件的次数。达到 kMaxRingDrainStreak
     * 时强制让位一次内核 epoll，保证监听 fd（accept）不被数据面无限期抢占。
     * 仅由 EpollWait 的单一消费者线程读写（队列为 MPSC，消费侧单线程）。 */
    static constexpr uint32_t kMaxRingDrainStreak = 8;
    uint32_t ring_drain_streak_ = 0;

    // For async accept wakeup: one entry per listening socket sharing this epoll
    struct WakeupEntry {
        EpollEvent *ready_event;
        WakeupCallback cb;
    };
    using WakeupTable = std::vector<WakeupEntry>;
    std::shared_ptr<const WakeupTable> LoadWakeupTable() const
    {
        return std::atomic_load_explicit(&wakeup_table_, std::memory_order_acquire);
    }
    static const WakeupCallback *FindWakeup(const WakeupTable &table, const EpollEvent *event_data);
    mutable std::mutex wakeup_mutex_;                 /* serializes writers only */
    std::shared_ptr<const WakeupTable> wakeup_table_; /* nullptr = no listener registered */
};
using AsyncEventPollPtr = Ref<AsyncEventPoll>;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_EPOLL_FD_H
