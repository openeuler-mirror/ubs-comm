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
    ReadLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter == g_socket_epoll_mappers.end()) {
        return nullptr;
    }
    return iter->second;
}

bool CreateSocketEpollMapper(int socket_fd, EpollMapper *&mapper)
{
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
        WriteLocker s_lock(g_socket_epoll_lock);
        g_socket_epoll_mappers.erase(socket_fd);
    }
    mapper->Clear();
    delete mapper;
    mapper = nullptr;
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
std::unique_ptr<EpollRunnerBackend> EpollRunner<T>::CreateBackend()
{
    if (GlobalSetting::UBS_POLLER_OPS != nullptr) {
        return std::unique_ptr<EpollRunnerBackend>(new ExternalPollerEpollRunnerBackend<T>(this));
    }
    return std::unique_ptr<EpollRunnerBackend>(new PthreadEpollRunnerBackend<T>(this));
}

template <EpollRunnerType T>
int EpollRunner<T>::Start()
{
    int result = 0;
    std::call_once(flag_, [this, &result]() {
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
    });
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

AsyncEventPoll::~AsyncEventPoll() noexcept
{
    UBS_VLOG_INFO("async_epoll destructure invoked for fd: %d\n", epoll_fd_);
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
    event.events = EPOLLIN | EPOLLET;
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
                mapper->Del(epoll_fd_);
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

    auto exist_count = readable_sockets_event_queue_.Size();
    if (UNLIKELY(exist_count > 0)) {
        auto count = readable_sockets_event_queue_.MultiPop(events, maxevents);
        if (count > 0) {
            return (int)count;
        }
    }

    int ret = 0;
    if (UNLIKELY(maxevents == 0 || (ret = epoll_wait(epoll_fd_, events, maxevents, timeout)) <= 0)) {
        return ret;
    }

    auto real_count = ArrangeWakeUpEvents(events, ret, maxevents);
    ReleaseRemovedEventsData();
    return real_count;
}

int AsyncEventPoll::AddReadableEvent(epoll_data_t data)
{
    if (!readable_sockets_event_queue_.Push(epoll_event{.events = EPOLLIN, .data = data})) {
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
    for (auto i = 0; i < input_count; ++i) {
        auto event_data = (EpollEvent *)events[i].data.ptr;
        if (UNLIKELY(event_data == nullptr)) {
            // invalid event
            UBS_VLOG_WARN("async_epoll(%d) wait get invalid event\n", epoll_fd_);
            continue;
        }

        // Check if this is the wakeup event for async accept
        // Handle ready_event wakeup
        if (ready_event_ != nullptr && event_data == ready_event_ && wakeup_callback_ != nullptr) {
            const int remain = max_events - real_count;
            if (remain > 0) {
                int processed = wakeup_callback_(events + real_count, remain, socket_data_);
                if (processed > 0) {
                    UBS_VLOG_DEBUG("async_epoll(%d) ArrangeWakeUpEvents: processed %d ready events\n", epoll_fd_,
                                   processed);
                    real_count += processed;
                }
            }
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
    if (UNLIKELY(AddRawSocketEvent(fd, event) != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add raw socket: %d failed\n", fd);
        return -1;
    }

    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (UNLIKELY(sock == nullptr || !sock->IsBindRemote())) { /* listen fd */
        UBS_VLOG_DEBUG("sock is nullptr or socket is not bind remote, socket: %d\n", fd);
        return 0;
    }

    // 2. add readable fd to epoll fd
    if (UNLIKELY(AddSockReadableEvent() != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add readable fd failed, raw socket: %d\n", fd);
        return -1;
    }

    // 3. set added epoll fd
    if ((event->events & EPOLLIN) == EPOLLIN) {
        auto sockBase = RefConvert<Socket, SocketBase>(sock);
        sockBase->SetAddedEpollFd(this, event->data);
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

        // 添加至后台 tx cqe poller
        TxCqePoller::Instance().AddSocket(sock);
    }

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

    if (UNLIKELY(ModRawSocketEvent(fd, event) != 0)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod(socket:%d) failed, not added\n", fd);
        errno = ENOENT;
        return -1;
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
        TxCqePoller::Instance().DelSocket(sock);
    }

    return 0;
}

template class EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>;
template class EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
template class EpollRunner<EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER>;

} // namespace ubs
} // namespace ock
