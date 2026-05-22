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

#include "ubsocket_event_epoll.h"
#include "ubsocket_socket.h"
#include "ubsocket_socket_set.h"
#include "umq/umq_socket.h"
#include "umq/umq_data_rx_ops.h"
#include "umq/umq_backend.h"

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
    free(mapper);
    mapper = nullptr;
}

template<SocketType T>
EpollRunner<T> &EpollRunner<T>::GetInstance()
{
    static EpollRunner<T> instance;
    return instance;
}

template<SocketType T>
int EpollRunner<T>::Start()
{
    std::call_once(flag_, [this]() {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        if (mutex_ == nullptr) {
            UBS_VLOG_ERR("async_epoll g_external_lock_ops.create(LT_EXCLUSIVE) failed.");
            return -1;
        }

        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            UBS_VLOG_ERR("async_epoll epoll_create1() failed : %d : %s\n", errno, strerror(errno));
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
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
            return -1;
        }

        RunnerEventData event_data{};
        struct epoll_event event{};
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
            return -1;
        }
        wait_thread_ = std::thread([this]() { RunInThread(); });
        return 0;
    });
    return 0;
}

template<SocketType T>
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

    // 正常情况下 joinable()为真，如果不可join，可能是线程异常退出
    if (!wait_thread_.joinable()) {
        UBS_VLOG_ERR("async_epoll wait thread is not joinable()\n");
        return;
    }

    wait_thread_.join();
    close(exit_efd_);
    close(epoll_fd_);
    exit_efd_ = -1;
    epoll_fd_ = -1;
    LockRegistry::LOCK_OPS.destroy(mutex_);
    mutex_ = nullptr;
}

template<SocketType T>
void EpollRunner<T>::RunInThread() noexcept
{
    UBS_VLOG_INFO("async_epoll epoll_wait_async_daemon thread started.\n");
    pthread_setname_np(pthread_self(), "ubs_poller");

    bool stopped = false;

    std::vector<struct epoll_event> events;
    events.resize(MAX_EPOLL_WAIT_COUNT);
    while (LIKELY(!stopped)) {
        auto count = epoll_wait(epoll_fd_, events.data(), MAX_EPOLL_WAIT_COUNT, 10000);
        if (UNLIKELY(count < 0)) {
            if (errno == EINTR) {
                continue;
            }
            UBS_VLOG_ERR("async_epoll epoll_wait() failed: %d : %s\n", errno, strerror(errno));
            break;
        }

        for (auto i = 0; i < count; i++) {
            auto event_data = (RunnerEventData *)&events[i].data;
            if (UNLIKELY(event_data->event_data.type == RUNNER_EVENT_TYPE_STOP)) {
                stopped = true;
                UBS_VLOG_ERR("async_epoll notify exit fd received, exit now\n");
                break;
            }

            ProcessOneEvent(events[i]);
        }
    }
    UBS_VLOG_INFO("async_epoll epoll_wait_async_daemon thread exit.\n");
}

template<SocketType T>
ALWAYS_INLINE int EpollRunner<T>::AddEpollEvent(const SocketPtr &sock, struct epoll_event *event)
{
    if (UNLIKELY(sock == nullptr)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args efd:%d\n", epoll_fd_);
        return -1;
    }

    if (UNLIKELY(sock->event_fd_ < 0)) {
        UBS_VLOG_ERR("invalid event_fd_ of sock : %d\n", sock->event_fd_);
        return -1;
    }

    return sock->AddRxEventToRunner(sock, epoll_fd_, event);
}

template<SocketType T>
ALWAYS_INLINE int EpollRunner<T>::DelEpollEvent(const SocketPtr &sock)
{
    if (UNLIKELY(sock == nullptr)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args efd:%d\n", epoll_fd_);
        return -1;
    }
    return sock->DelRxEventToRunner(sock, epoll_fd_);
}

template<SocketType T>
ALWAYS_INLINE int EpollRunner<T>::ProcessOneEvent(const struct epoll_event &event)
{
    uint64_t main_umq = 0;
    Socket *socket_object = nullptr;
    RunnerEventData event_data{};

    event_data.u64 = event.data.u64;
    if (event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR) {
        Locker sLock(mutex_);
        auto pos = umq::UmqSocket::jfr_main_umq_.find((int)event_data.event_data.data);
        if (pos != umq::UmqSocket::jfr_main_umq_.end()) {
            main_umq = pos->second;
        }
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_SUB_UMQ_RX) {
        socket_object = (Socket *)(ptrdiff_t)event_data.event_data.data;
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
            event_data.event_data.type);
    }

    if (main_umq != 0) {
        return ProcessShareJfrEvent(event, main_umq);
    }

    if (socket_object == nullptr) {
        return 0;
    }

    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = UmqApi::umq_poll(
        dynamic_cast<umq::UmqSocket *>(socket_object)->LocalUmqHandle(), UMQ_IO_RX, buf, POLL_BATCH_MAX);
    if (UNLIKELY(poll_num <= 0)) {
        if (dynamic_cast<umq::UmqSocket *>(socket_object)->GetRx()->GetRxOps()->RearmRxInterrupt() < 0) {
            UBS_VLOG_ERR("Rearm sub umq failed, socket fd:%d, ret: %d\n",
                socket_object->raw_socket_, poll_num);
        }
        return -1;
    }
    for (int i = 0; i < poll_num; ++i) {
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                ((umq::UmqRxOps *)((umq::UmqSocket *)socket_object)->GetRx()->GetRxOps())->HandleErrorRxCqe(buf[i]);
            }
            UmqApi::umq_buf_free(buf[i]);
        }
    }

    return 0;
}

template<SocketType T>
ALWAYS_INLINE int EpollRunner<T>::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq)
{
    if (UNLIKELY(ProcessMainUmqRearm(main_umq)) < 0) {
        return -1;
    }

    umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
    auto poll_num = UmqApi::umq_poll(main_umq, UMQ_IO_RX, buf, MAX_EPOLL_WAIT_COUNT);
    if (UNLIKELY(poll_num < 0)) {
        UBS_VLOG_ERR("async_epoll umq_poll(main_umq=%lu) failed: %d\n", main_umq, poll_num);
        return -1;
    }
    if (UNLIKELY(poll_num == 0)) {
        return -1;
    }

    umq_alloc_option_t alloc_option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(ock::ubs::Block) };
    umq_buf_t *rx_buf_list = UmqApi::umq_buf_alloc(
        umq::UmqSetting::GetIOBufSize(), poll_num, UMQ_INVALID_HANDLE, &alloc_option);
    if (UNLIKELY(rx_buf_list != nullptr)) {
        umq_buf_t *bad_qbuf = nullptr;
        if (UmqApi::umq_post(main_umq, rx_buf_list, UMQ_IO_RX, &bad_qbuf) != UMQ_SUCCESS) {
            UmqApi::umq_buf_free(bad_qbuf);
        }
    }

    std::set<AsyncEventPoll *> readable_epoll_fds;
    epoll_data_t event_data{};
    auto event_reach_sockets = SiftSocketEventsWithUmqBuffers(buf, poll_num);
    for (auto obj : SiftSocketEventsWithUmqBuffers(buf, poll_num)) {
        auto socket_obj = (Socket *)obj;
        ((umq::UmqSocket *)socket_obj)->NewRxEpollIn();
        auto epoll_fd_obj = (AsyncEventPoll *)(((SocketBase *)socket_obj)->GetAddedEpollFd(event_data));
        if (UNLIKELY(epoll_fd_obj != nullptr)) {
            epoll_fd_obj->AddReadableEvent(event_data);
            readable_epoll_fds.emplace(epoll_fd_obj);
        }
    }

    for (auto epoll_fd : readable_epoll_fds) {
        epoll_fd->SetReadableEventFd();
    }

    return 0;
}

template<SocketType T>
ALWAYS_INLINE std::unordered_set<Socket *>
EpollRunner<T>::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count)
{
    std::unordered_set<Socket *> event_reach_sockets;
    for (int i = 0; i < count; ++i) {
        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        if (UNLIKELY(buf[i]->status == UMQ_FAKE_BUF_FC_ERR)) {
            UBS_VLOG_ERR("async_epoll Unreachable flow control.\n");
        }

        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        auto socket_ptr = SocketSet::Instance().GetSocket(socket_fd).Get();
        if (UNLIKELY(socket_ptr == nullptr)) {
            UBS_VLOG_WARN("async_epoll Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        if (UNLIKELY((((umq::UmqSocket *)socket_ptr)->AddQbuf(buf[i]) != 0))) {
            UBS_VLOG_ERR("async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
            continue;
        }
        
        event_reach_sockets.emplace(socket_ptr);
    }
    return event_reach_sockets;
}

template<SocketType T>
ALWAYS_INLINE int EpollRunner<T>::ProcessMainUmqRearm(uint64_t main_umq)
{
    umq_interrupt_option_t option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_EVENT };
    auto events_cnt = UmqApi::umq_get_cq_event(main_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        UBS_VLOG_ERR("async_epoll umq_get_cq_event(main_umq=%lu) failed: %d\n", main_umq,
            events_cnt);
        return events_cnt;
    }

    if (LIKELY(events_cnt > 0)) {
        UmqApi::umq_rearm_interrupt(main_umq, false, &option);
        event_num_ += events_cnt;
        if (event_num_ >= GET_PER_ACK) {
            UmqApi::umq_ack_interrupt(main_umq, event_num_, &option);
            event_num_ = 0;
        }
    }

    return events_cnt;
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

    struct epoll_event event{};
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

int AsyncEventPoll::EpollCtl(int op, const SocketPtr &sock, struct epoll_event *event)
{
    int ret = -1;
    bool mapper_create = false;
    EpollMapper *mapper = nullptr;
    Locker sLock(ctl_mutex_);
    if (op == EPOLL_CTL_ADD) {
        mapper_create = CreateSocketEpollMapper(sock->raw_socket_, mapper);
    } else {
        // TODO：补充其他实现
        // mapper = GetSocketEpollMapper(fd);
    }
    switch (op) {
        case EPOLL_CTL_ADD:
            ret = EpollCtlAdd(sock, event);
            if (ret == 0 && mapper != nullptr) {
                mapper->Add(epoll_fd_);
            } else if (mapper_create) {
                WriteLocker s_lock(g_socket_epoll_lock);
                g_socket_epoll_mappers.erase(sock->raw_socket_);
                free(mapper);
                mapper = nullptr;
            }
            break;
        // TODO：补充其他实现
        default:
            UBS_VLOG_ERR("Invalid op code(%d), epfd: %d, fd: %d\n", op, epoll_fd_, sock->raw_socket_);
            errno = EINVAL;
    }
    return ret;
}

int AsyncEventPoll::EpollWait(const SocketPtr &sock, struct epoll_event *events, int maxevents, int timeout)
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
    if (!readable_sockets_event_queue_.Push(epoll_event{
        .events = EPOLLIN,
        .data = data })) {
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

        if (event_data->event_type == EPOLL_EVENT_RAW_SOCKET) {
            // pure socket
            if (i != real_count) {
                events[real_count].events = events[i].events;
            }
            events[real_count].data = event_data->event.data;
            real_count++;
            continue;
        }

        if (event_data->event_type == EPOLL_EVENT_UB_SOCKET_OUT) {
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

int AsyncEventPoll::EpollCtlAdd(const SocketPtr &sock, struct epoll_event *event)
{
    if (UNLIKELY(event == nullptr)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args fd:%d, event:%p\n", sock->raw_socket_, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(sock == nullptr)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args sock is nullptr\n");
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        UBS_VLOG_ERR("async_epoll AddEvent must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    // 1. add readable fd to epoll fd
    if (UNLIKELY(AddSockReadableEvent() != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add readable fd failed, raw socket: %d\n", sock->raw_socket_);
        return -1;
    }

    // 2. add original socket fd to epoll fd
    if (UNLIKELY(IsSocketEventDataExist(sock->raw_socket_))) {
        UBS_VLOG_ERR("async_epoll EpollCtlAdd(socket=%d) already added.", sock->raw_socket_);
        errno = EEXIST;
        return -1;
    }
    if (UNLIKELY(AddRawSocketEvent(sock, event) != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add raw socket: %d failed\n", sock->raw_socket_);
        return -1;
    }

    if (UNLIKELY(!sock->IsBindRemote())) {  /* listen fd */
        return 0;
    }

    // 3. add epoll runner epoll fd
    if (UNLIKELY(EpollRunnerFactory::GetInstance(sock->Type()).AddEpollEvent(sock, event) != 0)) {
        UBS_VLOG_ERR("epoll runner add epoll event failed, socket fd: %d\n", sock->raw_socket_);
        return -1;
    }

    // 4. add proto ex exent
    if ((event->events & EPOLLOUT) == EPOLLOUT) {
        int ret = AddProtoTxEvent(sock, event);
        if (ret < 0) {
            DelRawSocketEvent(sock);
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD:%d) failed(ret:%d): %d : %s\n",
                    ret, sock->raw_socket_, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int AsyncEventPoll::AddRawSocketEvent(const SocketPtr &sock, struct epoll_event *event)
{
    struct epoll_event raw_event{};
    auto event_data = new (std::nothrow) EpollEvent(EPOLL_EVENT_RAW_SOCKET, sock->raw_socket_, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d alloc failed.\n", sock->raw_socket_);
        return -1;
    }

    raw_event.events = event->events;
    raw_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock->raw_socket_, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll add pure event for socket fd: %d failed: %d : %s\n", sock->raw_socket_, errno,
                     strerror(errno));
        delete event_data;
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(sock->raw_socket_, event_data))) {
        UBS_VLOG_ERR("async_epoll add pure event for socket fd: %d insert event data failed\n",
                    sock->raw_socket_);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, sock->raw_socket_, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int AsyncEventPoll::AddProtoTxEvent(const SocketPtr &sock, struct epoll_event *event)
{
    struct epoll_event add_event {};
    auto *event_data = new (std::nothrow) EpollEvent(
            EPOLL_EVENT_UB_SOCKET_OUT, sock->raw_socket_, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d alloc failed.\n",
                    sock->raw_socket_);
        return -1;
    };

    add_event.events = EPOLLOUT | EPOLLET;
    add_event.data.ptr = event_data;
    int ret = sock->AddTxEvent(sock, epoll_fd_, &add_event);
    if (ret < 0) {
        delete event_data;
        UBS_VLOG_ERR("add proto tx event(ADD:%d) failed(ret:%d): %d : %s\n",
                    ret, sock->raw_socket_, errno, strerror(errno));
        return -1;
    }
    
    if (UNLIKELY(!InsertSocketEventData(sock->GetTxFd(), event_data))) {
        delete event_data;
        UBS_VLOG_ERR("async_epoll add proto tx event for socket fd: %d insert event data failed\n",
            sock->raw_socket_);
        return -1;
    }
    return 0;
}

int AsyncEventPoll::DelProtoTxEvent(const SocketPtr &sock)
{
    int ret = sock->DelTxEvent(sock, epoll_fd_);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("del tx event for socket fd: %d failed\n",
            sock->raw_socket_);
        return -1;
    }
    RemoveSocketEventData(sock->GetTxFd());
    return 0;
}

int AsyncEventPoll::DelRawSocketEvent(const SocketPtr &sock)
{
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, sock->raw_socket_, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del pure event for socket: %d failed: %d : %s\n", sock->raw_socket_, errno,
                    strerror(errno));
        return -1;
    }

    RemoveSocketEventData(sock->raw_socket_);
    return 0;
}

int AsyncEventPoll::EpollCtlMod(const SocketPtr &sock, struct epoll_event *event)
{
    if (UNLIKELY(sock == nullptr || event == nullptr)) {
        UBS_VLOG_ERR("async_epoll ModEvent invalid args fd:%d, event:%p\n", sock->raw_socket_, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(!IsSocketEventDataExist(sock->raw_socket_))) {
        UBS_VLOG_ERR("async_epoll EpollCtlMod(socket:%d) failed, not added\n", sock->raw_socket_);
        errno = ENOENT;
        return -1;
    }

    int ret = 0;
    if (IsSocketEventDataExist(sock->GetTxFd())) {
        if ((event->events & EPOLLOUT) == 0) {
            ret = DelProtoTxEvent(sock);
        }
    } else {
        if ((event->events & EPOLLOUT) != 0) {
            ret = AddProtoTxEvent(sock, event);
        }
    }
    return ret;
}

// TODO：Del时去掉了之前的RemoveSocketEventData逻辑，不再用map记录event data，验证正确性
int AsyncEventPoll::EpollCtlDel(const SocketPtr &sock, struct epoll_event *event)
{
    if (UNLIKELY(sock->raw_socket_ < 0)) {
        UBS_VLOG_ERR("async_epoll DelEvent invalid args fd:%d\n", sock->raw_socket_);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(EpollRunnerFactory::GetInstance(sock->Type()).DelEpollEvent(sock) != 0)) {
        UBS_VLOG_ERR("epoll runner add epoll event failed, socket fd: %d\n", sock->raw_socket_);
        return -1;
    }

    if (UNLIKELY(!IsSocketEventDataExist(sock->raw_socket_))) {
        UBS_VLOG_ERR("async_epoll EpollCtlDel(socket:%d) failed, not added\n", sock->raw_socket_);
        errno = ENOENT;
        return -1;
    }

    DelRawSocketEvent(sock);
    DelProtoTxEvent(sock);
    return 0;
}

} // namespace ubs
} // namespace ock
