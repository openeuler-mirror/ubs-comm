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
#include <cerrno>
#include <new>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "ubsocket_event_epoll.h"
#include "ubsocket_socket.h"
#include "ubsocket_tx_cqe_poller.h"
#include "umq/umq_share_jfr_epoll_runner_ops.h"
#include "umq/umq_tp_event_epoll_runner_ops.h"
#include "umq/umq_tp_tx_epoll_runner_ops.h"

namespace ock {
namespace ubs {

std::unordered_map<int, EpollMapper *> g_socket_epoll_mappers{};
u_rw_lock_t *g_socket_epoll_lock = nullptr;

EpollMapper *GetSocketEpollMapper(int socket_fd)
{
    if (g_socket_epoll_lock == nullptr) {
        return nullptr;
    }
    ReadLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter == g_socket_epoll_mappers.end()) {
        return nullptr;
    }
    return iter->second;
}

bool CreateSocketEpollMapper(int socket_fd, EpollMapper *&mapper)
{
    if (g_socket_epoll_lock == nullptr) {
        return false;
    }
    bool result = false;
    WriteLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter != g_socket_epoll_mappers.end()) {
        mapper = iter->second;
    } else {
        mapper = new (std::nothrow) EpollMapper(socket_fd);
        if (mapper == nullptr) {
            return false;
        }
        g_socket_epoll_mappers[socket_fd] = mapper;
        result = true;
    }
    return result;
}

void CleanSocketEpollMapper(int socket_fd)
{
    EpollMapper *mapper = GetSocketEpollMapper(socket_fd);
    if (mapper == nullptr) {
        return;
    }
    {
        if (g_socket_epoll_lock == nullptr) {
            return;
        }
        WriteLocker s_lock(g_socket_epoll_lock);
        g_socket_epoll_mappers.erase(socket_fd);
    }
    mapper->Clear();
    delete mapper;
    mapper = nullptr;
}

void ReserveSocketEpollMappers(size_t capacity)
{
    /*
     * 预留桶数组，规模建链时免除全表反复 rehash。上限 16384：40000 链路场景仅剩
     * 一次尾部扩容（一次性搬移），而小规模部署不为空表付出数百 KB 桶数组。
     */
    constexpr size_t RESERVE_CAP_MAX = 16384;
    WriteLocker s_lock(g_socket_epoll_lock);
    g_socket_epoll_mappers.reserve(std::min(capacity, RESERVE_CAP_MAX));
}

void CleanAllSocketEpollMappers()
{
    if (g_socket_epoll_lock == nullptr) {
        return;
    }

    WriteLocker s_lock(g_socket_epoll_lock);
    for (auto &mapper : g_socket_epoll_mappers) {
        if (mapper.second != nullptr) {
            delete mapper.second;
            mapper.second = nullptr;
        }
    }
    g_socket_epoll_mappers.clear();
}

template <EpollRunnerType T>
class PthreadEpollRunnerBackend : public EpollRunnerBackend {
public:
    explicit PthreadEpollRunnerBackend(EpollRunner<T> *runner) : runner_(runner) {}

    int Start() override
    {
        wait_thread_ = std::thread([this]() { runner_->RunInThread(); });
        return 0;
    }

    void Stop() override
    {
        if (!wait_thread_.joinable()) {
            UBS_VLOG_ERR("async_epoll wait thread is not joinable()\n");
            return;
        }
        wait_thread_.join();
    }

private:
    EpollRunner<T> *runner_;
    std::thread wait_thread_;
};

template <EpollRunnerType T>
class ExternalPollerEpollRunnerBackend : public EpollRunnerBackend {
public:
    explicit ExternalPollerEpollRunnerBackend(EpollRunner<T> *runner) : runner_(runner) {}

    int Start() override
    {
        ops_ = GlobalSetting::UBS_POLLER_OPS;
        if (ops_ == nullptr || ops_->add_consumer == nullptr || ops_->remove_consumer == nullptr) {
            UBS_VLOG_ERR("async_epoll external poller ops is invalid\n");
            errno = EINVAL;
            return -1;
        }

        if (ops_->add_consumer(runner_->epoll_fd_, this, DrainReadyEvents, &consumer_) != 0) {
            UBS_VLOG_ERR("async_epoll external poller add consumer failed: %d : %s\n", errno, strerror(errno));
            return -1;
        }
        started_ = true;
        return 0;
    }

    void Stop() override
    {
        if (!started_) {
            return;
        }
        ops_->remove_consumer(consumer_, runner_->epoll_fd_);
        consumer_ = nullptr;
        started_ = false;
    }

private:
    static void DrainReadyEvents(void *arg)
    {
        auto *backend = static_cast<ExternalPollerEpollRunnerBackend<T> *>(arg);
        if (UNLIKELY(backend == nullptr || backend->runner_ == nullptr)) {
            return;
        }

        bool hasEvents = false;
        do {
            hasEvents = false;
            if (backend->runner_->DrainReadyEvents(0, &hasEvents)) {
                return;
            }
        } while (hasEvents);
    }

    EpollRunner<T> *runner_;
    u_external_poller_ops_t *ops_{nullptr};
    void *consumer_{nullptr};
    bool started_{false};
};

template <EpollRunnerType T>
class ExternalDirectPollerEpollRunnerBackend : public EpollRunnerBackend {
public:
    explicit ExternalDirectPollerEpollRunnerBackend(EpollRunner<T> *runner) : runner_(runner) {}

    int Start() override
    {
        ops_ = GlobalSetting::UBS_POLLER_OPS;
        if (ops_ == nullptr || ops_->add_direct_poller == nullptr || ops_->remove_direct_poller == nullptr) {
            UBS_VLOG_ERR("async_epoll external direct poller ops is invalid\n");
            errno = EINVAL;
            return -1;
        }

        if (ops_->add_direct_poller(runner_->epoll_fd_, this, ProcessEventCb, &poller_) != 0) {
            UBS_VLOG_ERR("async_epoll external direct poller add failed: %d : %s\n", errno, strerror(errno));
            return -1;
        }
        started_ = true;
        return 0;
    }

    void Stop() override
    {
        if (!started_) {
            return;
        }
        ops_->remove_direct_poller(poller_, runner_->epoll_fd_);
        poller_ = nullptr;
        started_ = false;
    }

private:
    static int ProcessEventCb(void *arg, const struct epoll_event *event)
    {
        auto *backend = static_cast<ExternalDirectPollerEpollRunnerBackend<T> *>(arg);
        if (UNLIKELY(backend == nullptr || backend->runner_ == nullptr || event == nullptr)) {
            return 0;
        }

        RunnerEventData event_data{};
        event_data.u64 = event->data.u64;
        if (UNLIKELY(event_data.event_data.type == RUNNER_EVENT_TYPE_STOP)) {
            UBS_VLOG_DEBUG("async_epoll direct poller stop event received\n");
            return 1;
        }

        backend->runner_->ProcessOneEvent(*event);
        return 0;
    }

    EpollRunner<T> *runner_;
    u_external_poller_ops_t *ops_{nullptr};
    void *poller_{nullptr};
    bool started_{false};
};

template <EpollRunnerType T>
std::unique_ptr<EpollRunnerBackend> EpollRunner<T>::CreateBackend()
{
    if (GlobalSetting::UBS_POLLER_OPS != nullptr) {
        if (GlobalSetting::UBS_POLLER_OPS->add_direct_poller != nullptr &&
            GlobalSetting::UBS_POLLER_OPS->remove_direct_poller != nullptr) {
            return std::unique_ptr<EpollRunnerBackend>(new ExternalDirectPollerEpollRunnerBackend<T>(this));
        }
        // 统一轮询循环禁用嵌套 ExternalPoller 形态(ACTIVE 驻留会阻塞全局
        // EventDispatcher 的其他 fd 分发), 仅注册 add_consumer 时回退专职 pthread
        if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            UBS_VLOG_ERR("async_epoll nested external poller is disabled with unified poll, "
                         "fallback to pthread backend\n");
            return std::unique_ptr<EpollRunnerBackend>(new PthreadEpollRunnerBackend<T>(this));
        }
        return std::unique_ptr<EpollRunnerBackend>(new ExternalPollerEpollRunnerBackend<T>(this));
    }
    return std::unique_ptr<EpollRunnerBackend>(new PthreadEpollRunnerBackend<T>(this));
}

template <EpollRunnerType T>
int EpollRunner<T>::Start()
{
    int result = 0;
    // 与原 std::call_once 语义一致：仅首个调用者执行初始化，后续调用直接返回 0
    if (once_done_.load(std::memory_order_acquire)) {
        return result;
    }
    (void)pthread_mutex_lock(&once_mtx_);
    if (once_done_.load(std::memory_order_relaxed)) {
        (void)pthread_mutex_unlock(&once_mtx_);
        return result;
    }
    [this, &result]() {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        if (mutex_ == nullptr) {
            UBS_VLOG_ERR("async_epoll g_external_lock_ops.create(LT_EXCLUSIVE) failed.");
            result = -1;
            return -1;
        }

        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            UBS_VLOG_ERR("async_epoll epoll_create1() failed : %d : %s\n", errno, strerror(errno));
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
            result = -1;
            return -1;
        }

        // 此 exit_efd，仅用于表示退出，停止线程，释放资源
        exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (exit_efd_ < 0) {
            UBS_VLOG_ERR("async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
            close(epoll_fd_);
            epoll_fd_ = -1;
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
            result = -1;
            return -1;
        }

        RunnerEventData event_data{};
        struct epoll_event event {
        };
        event.events = EPOLLIN | EPOLLET;
        event_data.event_data.type = RUNNER_EVENT_TYPE_STOP;
        event_data.event_data.data = exit_efd_;
        event.data.u64 = event_data.u64;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, exit_efd_, &event) == -1) {
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) failed : %d : %s\n", errno, strerror(errno));
            close(exit_efd_);
            close(epoll_fd_);
            exit_efd_ = -1;
            epoll_fd_ = -1;
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
            result = -1;
            return -1;
        }

        if (T == EpollRunnerType::SHARE_JFR_RX_RUNNER) {
            ops_ = new umq::UmqShareJfrEpollRunnerOps();
        } else if (T == EpollRunnerType::TRANSPORT_POOL_TX_RUNNER) {
            ops_ = new umq::UmqTpTxEpollRunnerOps();
        } else if (T == EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER) {
            ops_ = new umq::UmqTpEventEpollRunnerOps();
        } else {
            ops_ = new EpollRunnerOps();
        }

        backend_ = CreateBackend();
        if (backend_ == nullptr || backend_->Start() != 0) {
            UBS_VLOG_ERR("async_epoll runner backend start failed\n");
            delete ops_;
            ops_ = nullptr;
            close(exit_efd_);
            close(epoll_fd_);
            exit_efd_ = -1;
            epoll_fd_ = -1;
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
            result = -1;
            return -1;
        }
        return 0;
    }();
    once_done_.store(true, std::memory_order_release);
    (void)pthread_mutex_unlock(&once_mtx_);
    return result;
}

template <EpollRunnerType T>
void EpollRunner<T>::Stop()
{
    if (exit_efd_ < 0) {
        return;
    }

    // 通过向exit_efd_写入数据，唤醒后台线程退出流程
    if (eventfd_write(exit_efd_, 1) < 0) {
        UBS_VLOG_ERR("async_epoll eventfd_write() failed : %d : %s\n", errno, strerror(errno));
        return;
    }

    if (backend_ != nullptr) {
        backend_->Stop();
        backend_.reset();
    }
    close(exit_efd_);
    close(epoll_fd_);
    exit_efd_ = -1;
    epoll_fd_ = -1;
    delete ops_;
    ops_ = nullptr;
    LockRegistry::LOCK_OPS.destroy(mutex_);
    mutex_ = nullptr;
}

template <EpollRunnerType T>
void EpollRunner<T>::RunInThread() noexcept
{
    UBS_VLOG_DEBUG("async_epoll epoll_wait_async_daemon thread started.\n");
    pthread_setname_np(pthread_self(), GetRunnerName().c_str());

    while (LIKELY(!DrainReadyEvents(10000))) {}
    UBS_VLOG_DEBUG("async_epoll epoll_wait_async_daemon thread exit.\n");
}

template <EpollRunnerType T>
bool EpollRunner<T>::DrainReadyEvents(int timeout, bool *hasEvents) noexcept
{
    struct epoll_event events[MAX_EPOLL_WAIT_COUNT];
    auto count = epoll_wait(epoll_fd_, events, MAX_EPOLL_WAIT_COUNT, timeout);
    if (hasEvents != nullptr) {
        *hasEvents = count > 0;
    }
    if (UNLIKELY(count < 0)) {
        if (errno == EINTR) {
            return false;
        }
        UBS_VLOG_ERR("async_epoll epoll_wait() failed: %d : %s\n", errno, strerror(errno));
        return true;
    }

    for (auto i = 0; i < count; i++) {
        auto event_data = (RunnerEventData *)&events[i].data;
        if (UNLIKELY(event_data->event_data.type == RUNNER_EVENT_TYPE_STOP)) {
            UBS_VLOG_DEBUG("async_epoll notify exit fd received, exit now\n");
            return true;
        }

        ProcessOneEvent(events[i]);
    }
    return false;
}

template <EpollRunnerType T>
ALWAYS_INLINE int EpollRunner<T>::AddEpollEvent(int fd, struct epoll_event *event, EpollRunnerOps::ExtContext *ctx)
{
    int ret = ops_->AddEventToRunner(epoll_fd_, fd, event, ctx);
    if (UNLIKELY(ret != 0)) {
        UBS_VLOG_ERR("add rx event to runner failed, ret:%d\n", ret);
        return -1;
    }
    return UBS_OK;
}

template <EpollRunnerType T>
ALWAYS_INLINE int EpollRunner<T>::DelEpollEvent(int fd)
{
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args efd:%d\n", epoll_fd_);
        return -1;
    }
    ops_->DelEpollEvent(epoll_fd_, fd);
    return 0;
}

template <EpollRunnerType T>
ALWAYS_INLINE int EpollRunner<T>::ProcessOneEvent(const struct epoll_event &event)
{
    return ops_->ProcessOneEvent(event);
}

template <EpollRunnerType T>
ALWAYS_INLINE std::string EpollRunner<T>::GetRunnerName()
{
    if (T == EpollRunnerType::SHARE_JFR_RX_RUNNER) {
        return "ubs_sh_jfr_rx";
    } else if (T == EpollRunnerType::TRANSPORT_POOL_TX_RUNNER) {
        return "ubs_tp_tx";
    } else if (T == EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER) {
        return "ubs_tp_evt";
    } else {
        return "ubs_runner";
    }
}

AsyncEventPoll::AsyncEventPoll(int epoll_fd) noexcept
    : EventPoll{epoll_fd},
      readable_sockets_event_queue_(ReadableRingCapacityFor(ArraySet<Socket>::GetInstance().Capacity()))
{
    /* 预留 fd->EpollEvent 表桶数组，规模建链时免除反复 rehash（上限同全局 mapper 表） */
    constexpr size_t RESERVE_CAP_MAX = 16384;
    try {
        socket_data_.reserve(
            std::min(static_cast<size_t>(ArraySet<Socket>::GetInstance().Capacity()), RESERVE_CAP_MAX));
    } catch (const std::bad_alloc &) {
        /* 预分配仅优化 rehash，失败可忽略，emplace 时会自动扩容 */
    }
}

AsyncEventPoll::~AsyncEventPoll() noexcept
{
    UBS_VLOG_INFO("async_epoll destructure invoked for fd: %d\n", epoll_fd_);
    ReleaseRemovedEventsData();
    {
        Locker sLock(mutex_);
        for (auto &socket_data : socket_data_) {
            if (socket_data.second != nullptr) {
                delete socket_data.second;
            }
        }
        socket_data_.clear();
    }
    if (epoll_fd_ < 0 || sock_readable_fd_ < 0) {
        return;
    }

    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, sock_readable_fd_, nullptr);
    close(sock_readable_fd_);
    sock_readable_fd_ = -1;
}

int AsyncEventPoll::AddSockReadableEvent()
{
    /* double check sock_readable_fd to avoid invalid lock */
    if (LIKELY(sock_readable_fd_ >= 0)) {
        return 0;
    }
    Locker sLock(mutex_);
    if (LIKELY(sock_readable_fd_ >= 0)) {
        return 0;
    }

    auto fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("async_epoll create event fd for epoll readable failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    struct epoll_event event {
    };
    // Level-triggered (no EPOLLET): UB's internal poll thread pushes events
    // to readable_sockets_event_queue_ and writes this eventfd to wake up
    // brpc's epoll_wait. With edge-triggered, if multiple writes happen
    // before epoll_wait re-arms (e.g. during ArrangeWakeUpEvents processing),
    // only one EPOLLIN fires and subsequent wakeups are lost — brpc never
    // sees new data and RPCs time out. Level-triggered ensures EPOLLIN
    // stays asserted until read() drains the counter, so no wakeup is lost.
    event.events = EPOLLIN;
    event.data.ptr = &sock_readable_event_;
    sock_readable_event_.socket_fd = fd;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll epoll_ctl add for epoll readable failed: %d : %s\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    sock_readable_fd_ = fd;
    return 0;
}

int AsyncEventPoll::EpollCtl(int op, int fd, struct epoll_event *event)
{
    int ret = -1;
    bool mapper_create = false;
    EpollMapper *mapper = nullptr;
    Locker sLock(ctl_mutex_);
    if (op == EPOLL_CTL_ADD) {
        mapper_create = CreateSocketEpollMapper(fd, mapper);
    } else {
        mapper = GetSocketEpollMapper(fd);
    }
    switch (op) {
        case EPOLL_CTL_ADD:
            ret = EpollCtlAdd(fd, event);
            if (ret == 0 && mapper != nullptr) {
                mapper->Add(epoll_fd_);
            } else if (mapper_create) {
                if (g_socket_epoll_lock == nullptr) {
                    /* 并发 uninit：CleanAllSocketEpollMappers 已释放 mapper，
                     * 不可再 delete（double-free），直接返回错误 */
                    return UBS_ERROR;
                }
                WriteLocker s_lock(g_socket_epoll_lock);
                g_socket_epoll_mappers.erase(fd);
                if (mapper != nullptr) {
                    delete mapper;
                    mapper = nullptr;
                }
            }
            break;
        case EPOLL_CTL_MOD:
            ret = EpollCtlMod(fd, event);
            break;
        case EPOLL_CTL_DEL:
            ret = EpollCtlDel(fd, event);
            if (ret == 0 && mapper != nullptr) {
                if (mapper->Del(epoll_fd_)) {
                    CleanSocketEpollMapper(fd);
                    mapper = nullptr;
                }
            }
            break;
        default:
            UBS_VLOG_ERR("Invalid op code(%d), epfd: %d, fd: %d\n", op, epoll_fd_, fd);
            errno = EINVAL;
    }
    return ret;
}

int AsyncEventPoll::EpollWait(struct epoll_event *events, int maxevents, int timeout)
{
    if (UNLIKELY(events == nullptr)) {
        UBS_VLOG_ERR("async_epoll EpollWait events is null.\n");
        errno = EFAULT;
        return -1;
    }

    if (UNLIKELY(maxevents < 0)) {
        UBS_VLOG_ERR("async_epoll EpollWait maxevents(%d) invalid.\n", maxevents);
        errno = EINVAL;
        return -1;
    }

    /* issue#43：数据面注入队列不得无限期抢占内核 epoll 集合。
     * readable_sockets_event_queue_ 由 RX 侧（share-JFR runner 等）持续灌入；
     * 原实现只要它非空就直接返回，**永不执行下面的 epoll_wait**——而监听 fd
     * 正在那个内核集合里。于是"边发包边建链"时 accept 事件永远得不到处理，
     * 内核全连接队列涨满后握手 ACK 被丢弃，客户端表现为建链失败
     * （现网实测：ListenOverflows 43 万/72 万次，用户态零日志）。
     * 护栏：连续 kMaxRingDrainStreak 次从注入队列取事件后，强制让位一次
     * 内核 epoll，使监听/裸 fd 获得确定的服务机会。 */
    const bool ring_pending = (readable_sockets_event_queue_.Size() > 0);
    if (ring_pending && ring_drain_streak_ < kMaxRingDrainStreak) {
        auto count = readable_sockets_event_queue_.MultiPop(events, maxevents);
        if (count > 0) {
            ++ring_drain_streak_;
            return (int)count;
        }
    }
    ring_drain_streak_ = 0;

    int ret = 0;
    /* 注入队列尚有存货时不得阻塞：让位只做一次非阻塞轮询，数据面时延不受影响 */
    const int wait_timeout = ring_pending ? 0 : timeout;
    if (UNLIKELY(maxevents == 0 || (ret = epoll_wait(epoll_fd_, events, maxevents, wait_timeout)) <= 0)) {
        /* 让位轮询无内核事件：回到注入队列，避免本次调用空转 */
        if (ring_pending) {
            auto count = readable_sockets_event_queue_.MultiPop(events, maxevents);
            if (count > 0) {
                return (int)count;
            }
        }
        return ret;
    }

    auto real_count = ArrangeWakeUpEvents(events, ret, maxevents);
    ReleaseRemovedEventsData();
    return real_count;
}

int AsyncEventPoll::AddReadableEvent(uint32_t events, epoll_data_t data)
{
    if (!readable_sockets_event_queue_.Push(epoll_event{.events = events, .data = data})) {
        return -1;
    }
    return 0;
}

int AsyncEventPoll::SetReadableEventFd()
{
    return eventfd_write(sock_readable_fd_, 1);
}

void AsyncEventPoll::WakeUpEpollFd()
{
    uint64_t notification = 1;
    if (eventfd_write(sock_readable_fd_, notification) < 0) {
        UBS_VLOG_ERR("Wakeup EventPoll fd: %d failed.\n", epoll_fd_);
    }
}

int AsyncEventPoll::ArrangeWakeUpEvents(struct epoll_event *events, int input_count, int max_events)
{
    bool socket_readable = false;
    int real_count = 0;
    /* One snapshot per batch; nullptr while no listener uses async accept. */
    const std::shared_ptr<const WakeupTable> wakeups = LoadWakeupTable();
    for (auto i = 0; i < input_count; ++i) {
        auto event_data = (EpollEvent *)events[i].data.ptr;
        if (UNLIKELY(event_data == nullptr)) {
            // invalid event
            UBS_VLOG_WARN("async_epoll(%d) wait get invalid event\n", epoll_fd_);
            continue;
        }

        // Check if this is the wakeup event for async accept
        // Handle ready_event wakeup
        const WakeupCallback *wakeup_cb = (wakeups != nullptr) ? FindWakeup(*wakeups, event_data) : nullptr;
        if (wakeup_cb != nullptr) {
            const int remain = max_events - real_count;
            if (remain > 0) {
                int processed = (*wakeup_cb)(events + real_count, remain, socket_data_);
                if (processed > 0) {
                    real_count += processed;
                }
            }
            // The wakeup eventfd is an internal notification channel, not a
            // socket event. ProcessReadyEvents already filled events[real_count]
            // with the listen_fd's epoll event (carrying brpc's SocketId in
            // data.u64). Skip the per-fd dispatch below for the eventfd itself
            // — its event_data is the listener's ready_event (EPOLL_EVENT_UB_SOCKET_IN,
            // socket_fd=-1) which would hit the GetItem(-1) nullptr path and
            // set socket_readable=true erroneously (both the ready_event and
            // sock_readable_event_ share EPOLL_EVENT_UB_SOCKET_IN type),
            // causing the listen_fd event in events[0] to be mis-handled.
            continue;
        }

        if (event_data->event_type == EPOLL_EVENT_RAW_SOCKET) {
            // pure socket
            if (i != real_count) {
                events[real_count].events = events[i].events;
            }
            events[real_count].data = event_data->event.data;
            real_count++;
            continue;
        }

        auto sock = ArraySet<Socket>::GetInstance().GetItem(event_data->socket_fd);
        if (event_data->event_type == EPOLL_EVENT_UB_SOCKET_OUT) {
            if (sock == nullptr) {
                UBS_VLOG_WARN("async_epoll(%d) OUT event for socket_fd %d but socket not found\n", epoll_fd_,
                              event_data->socket_fd);
                continue;
            }
            sock->ProcessEpollEvent(events[i]);
            events[real_count].events = EPOLLOUT;
            events[real_count].data = event_data->event.data;
            real_count++;
            continue;
        }

        if (event_data->event_type == EPOLL_EVENT_UB_SOCKET_IN) {
            socket_readable = true;
        }
    }

    if (LIKELY(socket_readable)) {
        uint64_t val = 0;
        if (sock_readable_fd_ >= 0 && read(sock_readable_fd_, &val, sizeof(val)) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                UBS_VLOG_ERR("Read sock_readable_fd_ failed, fd: %d, errno: %d, errmsg: %s\n", sock_readable_fd_, errno,
                             strerror(errno));
            }
        }
        auto space_size = max_events - real_count;
        if (space_size > 0) {
            real_count += (int)readable_sockets_event_queue_.MultiPop(events + real_count, space_size);
        }
    }

    return real_count;
}

void AsyncEventPoll::ReleaseRemovedEventsData()
{
    Locker sLock(ctl_mutex_);
    auto removed_head = removed_head_;
    removed_head_ = nullptr;

    while (removed_head != nullptr) {
        auto next = removed_head->next;
        delete removed_head;
        removed_head = next;
    }
}

int AsyncEventPoll::EpollCtlAdd(int fd, struct epoll_event *event)
{
    if (UNLIKELY(event == nullptr || fd < 0)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args fd:%d, event:%p\n", fd, event);
        errno = EINVAL;
        return -1;
    }

    // 1. add original socket fd to epoll fd
    if (UNLIKELY(IsSocketEventDataExist(fd))) {
        UBS_VLOG_ERR("async_epoll EpollCtlAdd(socket=%d) already added.", fd);
        errno = EEXIST;
        return -1;
    }

    // 非 ubsocket_socket API 创建的 fd 不做特殊处理
    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock == nullptr) {
        return AddRawSocketEvent(fd, event);
    }

    // brpc 在 Connect 后会监听 EPOLLOUT, 当 EPOLLOUT 发生后触发 KeepWrite. 之后 brpc 会删除对 EPOLLOUT 的关注，
    // 只关注 EPOLLIN. 不再关注 TCP fd 的 EPOLLOUT, 首次触发由 NotifyWritable() 上送.
    //
    // 附加 EPOLLRDHUP: 对端 shutdown(SHUT_WR)/close 时内核单独产生该事件，与「数据 EPOLLIN」是两个不同的
    // 边沿。UB native 模式下 FIN 只有一次 EPOLLIN 边沿；若恰逢 ubs_poll 因 UMQ 队列非空而返回数据、未走到
    // FIN 探测分支，该边沿即被消耗且不再重触发，server 侧链路永久残留（规模拆链时高概率复现）。
    // RDHUP 保证对端关闭必定再唤醒一次，由 ubs_poll 入口探测收口。brpc dispatcher 已把
    // EPOLLERR|EPOLLHUP 当作可读处理，ArrangeWakeUpEvents 对 RAW 事件原样透传，无需其他配合。
    struct epoll_event ev = *event;
    ev.events &= ~EPOLLOUT;
    ev.events |= EPOLLRDHUP;
    if (UNLIKELY(AddRawSocketEvent(fd, &ev) != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add raw socket: %d failed\n", fd);
        return -1;
    }

    // 2. add readable fd to epoll fd
    if (UNLIKELY(AddSockReadableEvent() != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add readable fd failed, raw socket: %d\n", fd);
        return -1;
    }

    // 3. set added epoll fd
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    sockBase->SetAddedEpollFd(this, event->data);
    sockBase->SetEvents(event->events);

    const bool epollout = event->events & EPOLLOUT;
    if (epollout) {
        sockBase->NotifyWritable();
    }

    // 4. add proto ex exent
    if (sock->ShouldRegisterTxEvent()) {
        int ret = AddProtoTxEvent(sock, event);
        if (ret < 0) {
            DelRawSocketEvent(fd);
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD:%d) failed(ret:%d): %d : %s\n", ret, sock->raw_socket_, errno,
                         strerror(errno));
            return -1;
        }
    }

    // Always register to the background TX CQE poller: in POOL mode it is the
    // only TX CQE drain channel for UB-native mode (brpc's ubs_post/ubs_poll
    // bypass the writev path that normally calls PollTx). Without it, SQ slots
    // are never reclaimed and status:12 (SQ full) stalls the connection.
    TxCqePoller::Instance().AddSocket(sock);

    return 0;
}

int AsyncEventPoll::AddRawSocketEvent(int fd, struct epoll_event *event)
{
    struct epoll_event raw_event {
    };
    auto event_data = new (std::nothrow) EpollEvent(EPOLL_EVENT_RAW_SOCKET, fd, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d alloc failed.\n", fd);
        return -1;
    }

    raw_event.events = event->events;
    raw_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll add pure event for socket fd: %d failed: %d : %s\n", fd, errno, strerror(errno));
        delete event_data;
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(fd, event_data))) {
        UBS_VLOG_ERR("async_epoll add pure event for socket fd: %d insert event data failed\n", fd);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int AsyncEventPoll::AddProtoTxEvent(const SocketPtr &sock, struct epoll_event *event)
{
    if (UNLIKELY(IsSocketEventDataExist(sock->GetTxFd()))) {
        return 0;
    }
    struct epoll_event add_event {
    };
    auto event_data = new (std::nothrow) EpollEvent(EPOLL_EVENT_UB_SOCKET_OUT, sock->raw_socket_, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d alloc failed.\n", sock->raw_socket_);
        return -1;
    };

    add_event.events = EPOLLIN | EPOLLET;
    add_event.data.ptr = event_data;
    int ret = sock->AddTxEvent(sock, epoll_fd_, &add_event);
    if (ret < 0) {
        delete event_data;
        UBS_VLOG_ERR("add proto tx event(ADD:%d) failed(ret:%d): %d : %s\n", ret, sock->raw_socket_, errno,
                     strerror(errno));
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(sock->GetTxFd(), event_data))) {
        delete event_data;
        UBS_VLOG_ERR("async_epoll add proto tx event for socket fd: %d insert event data failed\n", sock->raw_socket_);
        return -1;
    }
    return 0;
}

int AsyncEventPoll::DelProtoTxEvent(const SocketPtr &sock)
{
    int ret = sock->DelTxEvent(sock, epoll_fd_);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("del tx event for socket fd: %d failed\n", sock->raw_socket_);
        return -1;
    }
    RemoveSocketEventData(sock->GetTxFd());
    return 0;
}

int AsyncEventPoll::DelRawSocketEvent(int fd)
{
    if (!RemoveSocketEventData(fd)) {
        UBS_VLOG_WARN("async_epoll del pure event for socket: %d failed, RemoveSocketEventData failed\n", fd);
        return 0;
    }
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del pure event for socket: %d failed: %d : %s\n", fd, errno, strerror(errno));
        return -1;
    }

    return 0;
}

int AsyncEventPoll::EpollCtlMod(int fd, struct epoll_event *event)
{
    if (UNLIKELY(fd < 0 || event == nullptr)) {
        UBS_VLOG_ERR("async_epoll ModEvent invalid args fd:%d, event:%p\n", fd, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock == nullptr) {
        return ModRawSocketEvent(fd, event);
    }

    // 在后续通信时，brpc 只会注册 EPOLLIN (绝大多数)。如果 writev 返回 EAGAIN 则 brpc 开始关注
    // `EPOLLIN | EPOLLOUT`. 此时需去除 EPOLLOUT, 令 tcp fd 只监听 EPOLLIN, 否则会因为 tcp fd 可写而持续触发以
    // 下死循环: Write -> EAGAIN -> WaitEpollOut -> Write -> EAGAIN ...
    // EPOLL_CTL_MOD 会整体覆盖 fd 的关注集，此处同样附加 EPOLLRDHUP（理由见 EpollCtlAdd），
    // 否则 brpc 首次重新 arm 就会把断链感知能力剥掉。
    struct epoll_event ev = *event;
    ev.events &= ~EPOLLOUT;
    ev.events |= EPOLLRDHUP;
    if (UNLIKELY(ModRawSocketEvent(fd, &ev) != 0)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod(socket:%d) failed, not added\n", fd);
        errno = ENOENT;
        return -1;
    }

    auto sk_base = RefStaticCast<SocketBase>(sock);
    sk_base->SetEvents(event->events);
    sk_base->SetEpollData(event->data);
    // 如果本次关注了可写事件，且可写通知已到达，则补发 EPOLLOUT 事件
    if ((event->events & EPOLLOUT) && sk_base->ReadyAndExchange()) {
        sk_base->NotifyWritable();
    }
    return 0;
}

int AsyncEventPoll::ModRawSocketEvent(int fd, struct epoll_event *event)
{
    auto event_data = GetSocketEventData(fd);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod(socket:%d) failed, event_data null\n", fd);
        errno = EINVAL;
        return -1;
    }

    struct epoll_event raw_event {
    };
    raw_event.events = event->events;
    raw_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod(socket:%d) failed: %d : %s\n", fd, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int AsyncEventPoll::EpollCtlDel(int fd, struct epoll_event *event)
{
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("async_epoll DelEvent invalid args fd:%d\n", fd);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(!IsSocketEventDataExist(fd))) {
        UBS_VLOG_ERR("async_epoll EpollCtlDel(socket:%d) failed, not added\n", fd);
        errno = ENOENT;
        return -1;
    }

    DelRawSocketEvent(fd);
    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (UNLIKELY(sock == nullptr)) {
        UBS_VLOG_DEBUG("sock is nullptr for origin sock, socket: %d\n", fd);
        return 0;
    }

    if (sock->ShouldRegisterTxEvent()) {
        DelProtoTxEvent(sock);
    }
    TxCqePoller::Instance().DelSocket(sock);
    auto sk_base = RefStaticCast<SocketBase>(sock);
    sk_base->SetEvents(0);
    sk_base->SetEpollData({});
    return 0;
}

template class EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>;
template class EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
template class EpollRunner<EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER>;


const AsyncEventPoll::WakeupCallback *AsyncEventPoll::FindWakeup(const WakeupTable &table, const EpollEvent *event_data)
{
    for (const auto &entry : table) {
        if (entry.ready_event == event_data) {
            return &entry.cb;
        }
    }
    return nullptr;
}

void AsyncEventPoll::SetWakeupCallback(EpollEvent *ready_event, WakeupCallback cb)
{
    if (ready_event == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lk(wakeup_mutex_);
    auto next = std::make_shared<WakeupTable>();
    const std::shared_ptr<const WakeupTable> cur = LoadWakeupTable();
    if (cur != nullptr) {
        for (const auto &entry : *cur) {
            if (entry.ready_event != ready_event) {
                next->push_back(entry);
            }
        }
    }
    next->push_back(WakeupEntry{ready_event, std::move(cb)});
    std::atomic_store_explicit(&wakeup_table_, std::shared_ptr<const WakeupTable>(std::move(next)),
                               std::memory_order_release);
}

void AsyncEventPoll::RemoveWakeupCallback(EpollEvent *ready_event)
{
    std::lock_guard<std::mutex> lk(wakeup_mutex_);
    const std::shared_ptr<const WakeupTable> cur = LoadWakeupTable();
    if (cur == nullptr) {
        return;
    }
    auto next = std::make_shared<WakeupTable>();
    for (const auto &entry : *cur) {
        if (entry.ready_event != ready_event) {
            next->push_back(entry);
        }
    }
    std::shared_ptr<const WakeupTable> replacement;
    if (!next->empty()) {
        replacement = std::move(next);
    }
    std::atomic_store_explicit(&wakeup_table_, replacement, std::memory_order_release);
}

size_t AsyncEventPoll::WakeupCallbackCount() const
{
    const std::shared_ptr<const WakeupTable> cur = LoadWakeupTable();
    return (cur == nullptr) ? 0 : cur->size();
}

} // namespace ubs
} // namespace ock
