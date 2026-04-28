/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-04-20
 * Note:
 * History: 2026-04-20
 */
#include <cerrno>
#include <cstring>

#include "brpc/brpc_file_descriptor.h"
#include "file_descriptor_async.h"

namespace Brpc::async {
constexpr int MAX_EPOLL_WAIT_COUNT = 1024; // 一次epoll读取最event数量, 这个值用于后台线程epoll时使用

/*
 * DaemonEpoll注册的event中的data，总64位，高4位是类型，低60位是数值，可以是对象指针
 */
union DaemonEventData {
    struct EventData {
        uint64_t type : 4;
        uint64_t data : 60;
    } event_data;
    uint64_t u64;
};

enum DaemonEventType : uint64_t {
    DAEMON_EVENT_TYPE_INVALID = 0,
    DAEMON_EVENT_TYPE_SHARE_JFR,
    DAEMON_EVENT_TYPE_SUB_UMQ_RX,
    DAEMON_EVENT_TYPE_STOP,
    DAEMON_EVENT_TYPE_BUTT
};

EpollDaemon &EpollDaemon::GetInstance() noexcept
{
    static EpollDaemon instance;
    return instance;
}

int EpollDaemon::Start() noexcept
{
    if (!Context::GetContext()->UseAsyncEpollWait()) {
        return 0;
    }

    m_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    if (m_mutex == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll g_external_lock_ops.create(LT_EXCLUSIVE) failed.");
        return -1;
    }

    m_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epoll_fd < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_create1() failed : %d : %s\n", errno, strerror(errno));
        g_external_lock_ops.destroy(m_mutex);
        m_mutex = nullptr;
        return -1;
    }

    // 此 notify_fd，仅用于表示退出，停止线程，释放资源
    m_notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_notify_fd < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
        close(m_epoll_fd);
        m_epoll_fd = -1;
        g_external_lock_ops.destroy(m_mutex);
        m_mutex = nullptr;
        return -1;
    }

    DaemonEventData event_data{};
    struct epoll_event event {};
    event.events = EPOLLIN | EPOLLET;
    event_data.event_data.type = DAEMON_EVENT_TYPE_STOP;
    event_data.event_data.data = m_notify_fd;
    event.data.u64 = event_data.u64;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_notify_fd, &event) == -1) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD) failed : %d : %s\n", errno, strerror(errno));
        close(m_notify_fd);
        close(m_epoll_fd);
        m_notify_fd = -1;
        m_epoll_fd = -1;
        g_external_lock_ops.destroy(m_mutex);
        m_mutex = nullptr;
        return -1;
    }

    m_wait_thread = std::thread([this]() { DaemonThreadRun(); });
    return 0;
}

void EpollDaemon::Stop() noexcept
{
    if (m_notify_fd < 0) {
        return;
    }

    // 通过向notify_fd写入数据，唤醒后台线程退出流程
    if (eventfd_write(m_notify_fd, 1) < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll eventfd_write() failed : %d : %s\n", errno, strerror(errno));
        return;
    }

    // 正常情况下 joinable()为真，如果不可join，可能是线程异常退出
    if (!m_wait_thread.joinable()) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll wait thread is not joinable()\n");
        return;
    }

    m_wait_thread.join();
    close(m_notify_fd);
    close(m_epoll_fd);
    m_notify_fd = -1;
    m_epoll_fd = -1;
    g_external_lock_ops.destroy(m_mutex);
    m_mutex = nullptr;
}

int EpollDaemon::AddEvent(::EpollFd &epoll_fd, int socket_fd, const struct epoll_event &event,
    SocketConnectInfo &connect_info) noexcept
{
    if (UNLIKELY(socket_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll AddEvent invalid args efd:%d, sfd:%d\n", epoll_fd.GetFd(),
            socket_fd);
        return -1;
    }

    if (UNLIKELY(GetSocketConnectInfo(socket_fd, connect_info) != 0)) {
        return -1;
    }

    auto dfd = ((Brpc::SocketFd *)connect_info.socket_fd_object)->GetEventFd();
    if (UNLIKELY(dfd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
        return -1;
    }

    DaemonEventData event_data{};
    event_data.event_data.type = DAEMON_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = connect_info.share_jfr_fd;

    struct epoll_event shared_jfr_event {};
    shared_jfr_event.events = EPOLLIN | EPOLLET;
    shared_jfr_event.data.u64 = event_data.u64;

    ScopedUbExclusiveLocker sLock(m_mutex);
    if (UNLIKELY(m_jfr_main_umq.count(connect_info.share_jfr_fd) == 0)) {
        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, connect_info.share_jfr_fd, &shared_jfr_event) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD) jfr event failed: %d : %s\n", errno,
                strerror(errno));
            return -1;
        }
        m_jfr_main_umq.emplace(connect_info.share_jfr_fd, connect_info.main_umq);
    }
    sLock.Unlock();

    struct epoll_event rx_event {
        .events = EPOLLIN | EPOLLET
    };
    event_data.event_data.type = DAEMON_EVENT_TYPE_SUB_UMQ_RX;
    event_data.event_data.data = (ptrdiff_t)(void *)connect_info.socket_fd_object;
    rx_event.data.u64 = event_data.u64;
    if (UNLIKELY(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, connect_info.rx_interrupt_fd, &rx_event) < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD) rx event failed: %d : %s\n", errno,
            strerror(errno));
        return -1;
    }
    auto socket_obj = ((Brpc::SocketFd *)connect_info.socket_fd_object);
    socket_obj->SetAddedEpollFd(&epoll_fd, event.data);
    if (LIKELY(!socket_obj->RxUseTcp())) {
        socket_obj->RearmShareJfrRxInterrupt();
        socket_obj->RearmRxInterrupt();
    }

    return dfd;
}

int EpollDaemon::RemoveEvent(::EpollFd &epoll_fd, int socket_fd) noexcept
{
    if (UNLIKELY(socket_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll RemoveEvent invalid args efd:%d, sfd:%d\n", epoll_fd.GetFd(),
            socket_fd);
        return -1;
    }

    SocketConnectInfo connect_info;
    if (UNLIKELY(GetSocketConnectInfo(socket_fd, connect_info) != 0)) {
        return -1;
    }

    auto ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, connect_info.rx_interrupt_fd, nullptr);
    ((Brpc::SocketFd *)connect_info.socket_fd_object)->SetAddedEpollFd(nullptr);

    return ret;
}

EpollDaemon::EpollDaemon() noexcept : m_epoll_fd{ -1 }, m_notify_fd{ -1 }, m_mutex{ nullptr } {}

void EpollDaemon::DaemonThreadRun() noexcept
{
    RPC_ADPT_VLOG_INFO("async_epoll epoll_wait_async_daemon thread started.\n");
    pthread_setname_np(pthread_self(), "ubs_poller");
    bool stopped = false;
    m_events.resize(MAX_EPOLL_WAIT_COUNT);
    while (LIKELY(!stopped)) {
        auto count = epoll_wait(m_epoll_fd, m_events.data(), MAX_EPOLL_WAIT_COUNT, 10000);
        if (UNLIKELY(count < 0)) {
            if (errno == EINTR) {
                continue;
            }
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_wait() failed: %d : %s\n", errno, strerror(errno));
            break;
        }

        for (auto i = 0; i < count; i++) {
            auto event_data = (DaemonEventData *)&m_events[i].data;
            if (UNLIKELY(event_data->event_data.type == DAEMON_EVENT_TYPE_STOP)) {
                stopped = true;
                RPC_ADPT_VLOG_INFO("async_epoll notify exit fd received, exit now\n");
                break;
            }

            ProcessOneEvent(m_events[i]);
        }
    }

    RPC_ADPT_VLOG_INFO("async_epoll epoll_wait_async_daemon thread exit.\n");
}

void EpollDaemon::ProcessOneEvent(const struct epoll_event &event) noexcept
{
    uint64_t main_umq = 0;
    Brpc::SocketFd *socket_object = nullptr;
    DaemonEventData event_data{};

    event_data.u64 = event.data.u64;
    if (event_data.event_data.type == DAEMON_EVENT_TYPE_SHARE_JFR) {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto pos = m_jfr_main_umq.find((int)event_data.event_data.data);
        if (pos != m_jfr_main_umq.end()) {
            main_umq = pos->second;
        }
    } else if (event_data.event_data.type == DAEMON_EVENT_TYPE_SUB_UMQ_RX) {
        socket_object = (Brpc::SocketFd *)(void *)(ptrdiff_t)event_data.event_data.data;
    } else {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
            event_data.event_data.type);
    }

    if (main_umq != 0) {
        ProcessShareJfrEvent(event, main_umq);
        return;
    }

    if (socket_object == nullptr) {
        return;
    }

    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = umq_poll(socket_object->GetLocalUmqHandle(), UMQ_IO_RX, buf, POLL_BATCH_MAX);
    if (UNLIKELY(poll_num <= 0)) {
        if (socket_object->RearmRxInterrupt() < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Rearm sub umq failed, socket fd:%d, ret: %d\n",
                socket_object->GetFd(), poll_num);
        }
        return;
    }
    for (int i = 0; i < poll_num; ++i) {
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                socket_object->HandleErrorRxCqe(buf[i]);
            }
            umq_buf_free(buf[i]);
        }
    }
}

void EpollDaemon::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq) noexcept
{
    if (UNLIKELY(ProcessMainUmqRearm(main_umq)) < 0) {
        return;
    }

    umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
    auto poll_num = umq_poll(main_umq, UMQ_IO_RX, buf, MAX_EPOLL_WAIT_COUNT);
    if (UNLIKELY(poll_num < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "async_epoll umq_poll(main_umq=%lu) failed: %d\n", main_umq, poll_num);
        return;
    }
    if (UNLIKELY(poll_num == 0)) {
        return;
    }

    umq_alloc_option_t alloc_option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(Brpc::IOBuf::Block) };
    umq_buf_t *rx_buf_list = umq_buf_alloc(Brpc::BrpcIOBufSize(), poll_num, UMQ_INVALID_HANDLE, &alloc_option);
    if (UNLIKELY(rx_buf_list != nullptr)) {
        umq_buf_t *bad_qbuf = nullptr;
        if (umq_post(main_umq, rx_buf_list, UMQ_IO_RX, &bad_qbuf) != UMQ_SUCCESS) {
            umq_buf_free(bad_qbuf);
        }
    }

    std::set<EpollFdAsync *> readable_epoll_fds;
    epoll_data_t event_data{};
    auto event_reach_sockets = SiftSocketEventsWithUmqBuffers(buf, poll_num);
    for (auto obj : SiftSocketEventsWithUmqBuffers(buf, poll_num)) {
        auto socket_obj = (Brpc::SocketFd *)obj;
        socket_obj->NewRxEpollIn();
        auto epoll_fd_obj = (EpollFdAsync *)socket_obj->GetAddedEpollFd(event_data);
        if (UNLIKELY(epoll_fd_obj != nullptr)) {
            epoll_fd_obj->AddSocketReadableEvent(socket_obj->GetFd(), event_data);
            readable_epoll_fds.emplace(epoll_fd_obj);
        }
    }

    for (auto epoll_fd : readable_epoll_fds) {
        epoll_fd->SetSocketsReadable();
    }
}

std::unordered_set<::SocketFd *> EpollDaemon::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count) noexcept
{
    std::unordered_set<::SocketFd *> event_reach_sockets;
    for (int i = 0; i < count; ++i) {
        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        if (UNLIKELY(buf[i]->status == UMQ_FAKE_BUF_FC_ERR)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "async_epoll Unreachable flow control.\n");
        }

        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        auto socket_fd_obj = (Brpc::SocketFd *)Fd<::SocketFd>::GetFdObj(socket_fd);
        if (UNLIKELY(socket_fd_obj == nullptr)) {
            RPC_ADPT_VLOG_WARN("async_epoll Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        if (UNLIKELY(socket_fd_obj->AddQbuf(buf[i]) != 0)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
            continue;
        }
        event_reach_sockets.emplace(socket_fd_obj);
    }
    return event_reach_sockets;
}

int EpollDaemon::ProcessMainUmqRearm(uint64_t main_umq) noexcept
{
    umq_interrupt_option_t option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
    auto events_cnt = umq_get_cq_event(main_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "async_epoll umq_get_cq_event(main_umq=%lu) failed: %d\n", main_umq,
            events_cnt);
        return events_cnt;
    }

    if (LIKELY(events_cnt > 0)) {
        umq_rearm_interrupt(main_umq, false, &option);
        m_event_num += events_cnt;
        if (m_event_num >= GET_PER_ACK) {
            umq_ack_interrupt(main_umq, m_event_num, &option);
            m_event_num = 0;
        }
    }

    return events_cnt;
}

int EpollDaemon::GetSocketConnectInfo(int socket_fd, async::SocketConnectInfo &info) noexcept
{
    auto socket_fd_obj = dynamic_cast<Brpc::SocketFd *>(Fd<::SocketFd>::GetFdObj(socket_fd));
    if (UNLIKELY(socket_fd_obj == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll get fd object with socket fd: %d\n", socket_fd);
        return -1;
    }

    auto main_umq = socket_fd_obj->GetMainUmqHandle();
    if (UNLIKELY(main_umq == UMQ_INVALID_HANDLE)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll Get main umq handle failed, socket fd: %d\n", socket_fd);
        return -1;
    }

    umq_interrupt_option_t main_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
    auto share_jfr_fd = umq_interrupt_fd_get(main_umq, &main_option);
    if (UNLIKELY(share_jfr_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "async_epoll umq_interrupt_fd_get() failed, socket fd: %d, ret: %d\n",
            socket_fd, share_jfr_fd);
        return -1;
    }

    umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
    int tx_interrupt_fd = umq_interrupt_fd_get(socket_fd_obj->GetLocalUmqHandle(), &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll Failed to get TX interrupt fd for umq, socket fd: %d\n",
            socket_fd);
        return -1;
    }

    umq_interrupt_option_t rx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
    int rx_interrupt_fd = umq_interrupt_fd_get(socket_fd_obj->GetLocalUmqHandle(), &rx_option);
    if (UNLIKELY(rx_interrupt_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll Failed to get RX interrupt fd for umq, socket fd: %d\n",
            socket_fd);
        return -1;
    }

    auto event_fd = socket_fd_obj->GetEventFd();
    info.socket_fd_object = socket_fd_obj;
    info.main_umq = main_umq;
    info.share_jfr_fd = share_jfr_fd;
    info.tx_interrupt_fd = tx_interrupt_fd;
    info.rx_interrupt_fd = rx_interrupt_fd;
    info.rd_event_fd = event_fd;

    return 0;
}

EpollFdAsync::~EpollFdAsync() noexcept
{
    RPC_ADPT_VLOG_INFO("async_epoll destructure invoked for fd: %d\n", m_fd);
    if (m_fd < 0 || m_socket_readable_event_fd < 0) {
        return;
    }

    epoll_ctl(m_fd, EPOLL_CTL_DEL, m_socket_readable_event_fd, nullptr);
    close(m_socket_readable_event_fd);
    m_socket_readable_event_fd = -1;
}

int EpollFdAsync::EpollCtlAdd(int socket_fd, struct epoll_event *event, bool use_polling)
{
    if (UNLIKELY(socket_fd < 0 || event == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll AddEvent invalid args fd:%d, event:%p\n", socket_fd, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll AddEvent must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(EnsureReadableEventFdReady() != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll prepare event readable failed for socket: %d.\n", socket_fd);
        errno = ENOMEM;
        return -1;
    }

    if (UNLIKELY(IsSocketEventDataExist(socket_fd))) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollCtlAdd(socket=%d) already added.", socket_fd);
        errno = EEXIST;
        return -1;
    }

    auto ret = AddPureSocketEvent(socket_fd, event);
    if (UNLIKELY(ret != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll ctl add raw socket: %d failed\n", socket_fd);
        return -1;
    }

    auto socket_fd_object = (Brpc::SocketFd *)Fd<::SocketFd>::GetFdObj(socket_fd);
    if (socket_fd_object == nullptr || !socket_fd_object->GetBindRemote()) { // raw socket or listen socket
        return 0;
    }

    SocketConnectInfo connect_info;
    auto event_fd = EpollDaemon::GetInstance().AddEvent(*this, socket_fd, *event, connect_info);
    if (UNLIKELY(event_fd < 0)) {
        DelPureSocketEvent(socket_fd);
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollDaemon add event failed, socket fd: %d\n", socket_fd);
        errno = EINVAL;
        return -1;
    }

    if ((event->events & EPOLLOUT) == EPOLLOUT) {
        ret = AddSocketOutEvent(socket_fd, connect_info.rx_interrupt_fd, event);
        if (ret < 0) {
            DelPureSocketEvent(socket_fd);
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD:%d) failed: %d : %s\n", socket_fd, errno,
                strerror(errno));
            return -1;
        }
    }

    return 0;
}

int EpollFdAsync::EpollCtlMod(int socket_fd, struct epoll_event *event, bool use_polling)
{
    if (UNLIKELY(socket_fd < 0 || event == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll ModEvent invalid args fd:%d, event:%p\n", socket_fd, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollCtlMod must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    EpollSocketInOutFds socket_fds;
    if (UNLIKELY(GetEpollSocketInOutFds(socket_fd, socket_fds) != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll GetSocketConnectInfo failed, socket fd: %d\n", socket_fd);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(!IsSocketEventDataExist(socket_fd))) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollCtlMod(socket:%d) failed, not added\n", socket_fd);
        errno = ENOENT;
        return -1;
    }

    int ret = 0;
    if (IsSocketEventDataExist(socket_fds.epoll_out_fd)) {
        if ((event->events & EPOLLOUT) == 0) {
            ret = DelSocketOutEvent(socket_fd, socket_fds.epoll_out_fd);
        }
    } else {
        if ((event->events & EPOLLOUT) != 0) {
            ret = AddSocketOutEvent(socket_fd, socket_fds.epoll_out_fd, event);
        }
    }

    return ret;
}

int EpollFdAsync::EpollCtlDel(int socket_fd, struct epoll_event *event, bool use_polling)
{
    if (UNLIKELY(socket_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll DelEvent invalid args fd:%d\n", socket_fd);
        errno = EINVAL;
        return -1;
    }

    EpollSocketInOutFds socket_fds;
    if (UNLIKELY(GetEpollSocketInOutFds(socket_fd, socket_fds) != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll GetSocketConnectInfo failed, socket fd: %d\n", socket_fd);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(EpollDaemon::GetInstance().RemoveEvent(*this, socket_fd) != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll RemoveEvent epoll_fd:%d, socket fd: %d failed\n", m_fd,
            socket_fd);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY(!IsSocketEventDataExist(socket_fd))) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollCtlDel(socket:%d) failed, not added\n", socket_fd);
        errno = ENOENT;
        return -1;
    }

    DelPureSocketEvent(socket_fd);
    DelSocketOutEvent(socket_fd, socket_fds.epoll_out_fd);
    return 0;
}

int EpollFdAsync::EpollWait(struct epoll_event *events, int max_events, int timeout, bool use_polling)
{
    if (UNLIKELY(events == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollWait events is null\n");
        errno = EFAULT;
        return -1;
    }

    if (UNLIKELY(max_events < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll EpollWait max_events(%d) invalid.\n", max_events);
        errno = EINVAL;
        return -1;
    }

    auto exist_count = m_readable_sockets_queue.Size();
    if (UNLIKELY(exist_count > 0)) {
        auto count = m_readable_sockets_queue.MultiPop(events, max_events);
        if (count > 0) {
            return (int)count;
        }
    }

    int ret = 0;
    if (UNLIKELY(max_events == 0 || (ret = epoll_wait(m_fd, events, max_events, timeout)) <= 0)) {
        return ret;
    }

    auto real_count = ArrangeWakeupEvents(events, ret, max_events);
    ReleaseRemovedEventsData();
    return real_count;
}

int EpollFdAsync::AddSocketReadableEvent(int socket_fd, epoll_data_t data) noexcept
{
    if (!m_readable_sockets_queue.Push(epoll_event{
        .events = EPOLLIN,
        .data = data })) {
        return -1;
    }
    return 0;
}

int EpollFdAsync::SetSocketsReadable() noexcept
{
    return eventfd_write(m_socket_readable_event_fd, 1);
}

int EpollFdAsync::ArrangeWakeupEvents(struct epoll_event *events, int input_count, int max_events) noexcept
{
    bool socket_readable = false;
    int real_count = 0;
    for (auto i = 0; i < input_count; i++) {
        auto event_data = (EpollEventData *)events[i].data.ptr;
        if (UNLIKELY(event_data == nullptr)) {
            // invalid event
            RPC_ADPT_VLOG_WARN("async_epoll(%d) wait get invalid event\n", m_fd);
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
            real_count += (int)m_readable_sockets_queue.MultiPop(events + real_count, space_size);
        }
    }

    return real_count;
}

void EpollFdAsync::ReleaseRemovedEventsData() noexcept
{
    ScopedUbExclusiveLocker sLock(m_mutex);
    auto removed_head = m_removed_head;
    m_removed_head = nullptr;
    sLock.Unlock();

    while (removed_head != nullptr) {
        auto next = removed_head->next;
        delete removed_head;
        removed_head = next;
    }
}

int EpollFdAsync::EnsureReadableEventFdReady() noexcept
{
    if (LIKELY(m_socket_readable_event_fd >= 0)) {
        return 0;
    }

    ScopedUbExclusiveLocker sLock(m_mutex);
    if (UNLIKELY(m_socket_readable_event_fd >= 0)) {
        return 0;
    }

    auto fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (UNLIKELY(fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll create event fd for epoll readable failed: %d : %s\n", errno,
            strerror(errno));
        return -1;
    }

    struct epoll_event event {};
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &m_readable_event_data;
    m_readable_event_data.socket_fd = fd;

    auto ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event);
    if (UNLIKELY(ret < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl add for epoll readable failed: %d : %s\n", errno,
            strerror(errno));
        close(fd);
        return -1;
    }

    m_socket_readable_event_fd = fd;
    return 0;
}

int EpollFdAsync::AddPureSocketEvent(int socket_fd, struct epoll_event *event) noexcept
{
    struct epoll_event pure_event {};
    auto event_data = new (std::nothrow) EpollEventData(EPOLL_EVENT_RAW_SOCKET, socket_fd, *event);
    if (UNLIKELY(event_data == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d alloc failed.\n", socket_fd);
        return -1;
    }

    pure_event.events = event->events;
    pure_event.data.ptr = event_data;
    auto ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, socket_fd, &pure_event);
    if (UNLIKELY(ret < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add pure event for socket fd: %d failed: %d : %s\n",
            socket_fd, errno, strerror(errno));
        delete event_data;
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(socket_fd, event_data))) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add pure event for socket fd: %d insert event data failed\n",
            socket_fd);
        epoll_ctl(m_fd, EPOLL_CTL_DEL, socket_fd, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int EpollFdAsync::DelPureSocketEvent(int socket_fd) noexcept
{
    auto ret = epoll_ctl(m_fd, EPOLL_CTL_DEL, socket_fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll del pure event for socket: %d failed: %d : %s\n", socket_fd,
            errno, strerror(errno));
        return -1;
    }

    RemoveSocketEventData(socket_fd);
    return 0;
}

int EpollFdAsync::AddSocketOutEvent(int socket_fd, int event_fd, struct epoll_event *event) noexcept
{
    struct epoll_event add_event {};
    auto event_data = new (std::nothrow) EpollEventData(EPOLL_EVENT_UB_SOCKET_OUT, socket_fd, *event);
    if (UNLIKELY(event_data == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d alloc failed.\n", socket_fd);
        return -1;
    }

    add_event.events = EPOLLOUT | EPOLLET;
    add_event.data.ptr = event_data;
    auto ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, event_fd, &add_event);
    if (UNLIKELY(ret < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d failed: %d : %s\n",
            socket_fd, errno, strerror(errno));
        delete event_data;
        return -1;
    }

    auto socket_obj = (Brpc::SocketFd *)Fd<::SocketFd>::GetFdObj(socket_fd);
    if (LIKELY(socket_obj != nullptr && !socket_obj->TxUseTcp())) {
        socket_obj->RearmTxInterrupt();
    }

    if (UNLIKELY(!InsertSocketEventData(event_fd, event_data))) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d insert event data failed\n",
            socket_fd);
        epoll_ctl(m_fd, EPOLL_CTL_DEL, event_fd, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int EpollFdAsync::DelSocketOutEvent(int socket_fd, int event_fd) noexcept
{
    auto ret = epoll_ctl(m_fd, EPOLL_CTL_DEL, event_fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll del out event for socket event fd: %d failed: %d : %s\n",
            event_fd, errno, strerror(errno));
        return -1;
    }

    RemoveSocketEventData(event_fd);
    return 0;
}

int EpollFdAsync::GetEpollSocketInOutFds(int socket_fd, EpollSocketInOutFds &fds) noexcept
{
    auto socket_fd_obj = dynamic_cast<Brpc::SocketFd *>(Fd<::SocketFd>::GetFdObj(socket_fd));
    if (UNLIKELY(socket_fd_obj == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll get fd object with socket fd: %d\n", socket_fd);
        return -1;
    }

    umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
    int tx_interrupt_fd = umq_interrupt_fd_get(socket_fd_obj->GetLocalUmqHandle(), &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll Failed to get TX interrupt fd for umq, socket fd: %d\n",
            socket_fd);
        return -1;
    }

    fds.epoll_in_fd = socket_fd_obj->GetEventFd();
    fds.epoll_out_fd = tx_interrupt_fd;
    return 0;
}
}
