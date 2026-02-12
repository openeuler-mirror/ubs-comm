/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-01-20
 * Note:
 * History: 2026-01-20
*/

#include "brpc_file_descriptor.h"
#include "brpc_share_jfr.h"

template <>
Brpc::EpollFd *Fd<Brpc::EpollFd>::m_fd_obj_map[RPC_ADPT_FD_MAX] = {0};
template <>
pthread_rwlock_t Fd<Brpc::EpollFd>::m_rwlock = PTHREAD_RWLOCK_INITIALIZER;

namespace Brpc {

int ShareJfrEventFdEpollEvent::AddEpollEvent(int epoll_fd)
{
    struct epoll_event tmp_event;
    tmp_event.events = EPOLLIN;
    tmp_event.data.ptr = (void *)(uintptr_t)this;

    int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_fd, &tmp_event);
    if (ret != 0) {
        RPC_ADPT_VLOG_ERR("Origin epoll control add share jfr event fd failed, epfd: %d, share jfr event fd: %d\n",
                          epoll_fd, m_fd);
        return ret;
    }

    RPC_ADPT_VLOG_DEBUG("Origin epoll control add share jfr event fd successful, epfd: %d, share jfr event fd: %d\n",
                        epoll_fd, m_fd);
    m_add_epoll_event = true;

    return 0;
}

int ShareJfrEventFdEpollEvent::ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                 bool use_polling)
{
    uint64_t cnt;
    if (eventfd_read(m_fd, &cnt) == -1) {
        RPC_ADPT_VLOG_WARN("read share jfr event fd %d failed, errno %d\n", m_fd, errno);
    }

    SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
    if (socket_fd_obj != nullptr) {
        socket_fd_obj->NewRxEpollIn();
    }

    return ::EpollEvent::ProcessEpollEvent(input_event, output_event, use_polling);
}

void ShareJfrEventFdEpollEvent::WakeUp()
{
    uint64_t notification = 1;
    if (eventfd_write(m_fd, notification) < 0) {
        RPC_ADPT_VLOG_ERR("Wakeup sub-umq (socket fd %d) failed\n", m_fd);
    }
}

int ShareJfrRxEpollEvent::AddEpollEvent(int epoll_fd, bool use_polling)
{
    if (!SocketFdEpollTable::Contains(m_fd)) {
        if (::EpollEvent::AddEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR("Failed to add share jfr rx fd to epoll, epfd: %d, share jfr rx fd: %d\n", epoll_fd,
                              m_fd);
            return -1;
        }

        SocketFdEpollTable::Set(m_fd, epoll_fd);
        return 0;
    }

    int old_epoll_fd;
    if (SocketFdEpollTable::Get(m_fd, old_epoll_fd) && old_epoll_fd == epoll_fd) {
        return 0;
    }

    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    try {
        event_fd_epoll_event = new ShareJfrEventFdEpollEvent(m_origin_fd, &m_event, event_fd);
    } catch (std::exception &e) {
        RPC_ADPT_VLOG_ERR("%s\n", e.what());
        return -1;
    }

    if (event_fd_epoll_event->AddEpollEvent(epoll_fd) < 0) {
        RPC_ADPT_VLOG_ERR("Failed to add share jfr event fd to epoll, epfd: %d, share jfr event fd: %d\n", epoll_fd,
                          event_fd);
        return -1;
    }

    return 0;
}

int ShareJfrRxEpollEvent::ModEpollEvent(int epoll_fd, struct epoll_event *event, bool use_polling)
{
    if (!IsAddEpollEvent() && (event->events & EPOLLIN)) {
        if (AddEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to modify(add) share jfr RX interrupt fd(%d) for main umq, fd: %d\n", m_fd,
                               m_origin_fd);
        } else {
            RPC_ADPT_VLOG_DEBUG("Modify(add) share jfr RX interrupt fd(%d) for main umq successful, fd: %d\n", m_fd,
                                m_origin_fd);
        }
    }

    return 0;
}

int ShareJfrRxEpollEvent::DelEpollEvent(int epoll_fd, bool use_polling)
{
    if (::EpollEvent::DelEpollEvent(epoll_fd, use_polling) < 0) {
        return -1;
    }

    if (event_fd_epoll_event != nullptr && event_fd_epoll_event->IsAddEpollEvent() &&
        event_fd_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
        RPC_ADPT_VLOG_WARN("Failed to delete share jfr event fd(%d) for umq, fd: %d\n", event_fd_epoll_event->GetFd(),
                           m_fd);
    }

    return 0;
}

int ShareJfrRxEpollEvent::ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                            bool use_polling)
{
    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = PollShareJfrAndRefillRx(buf, POLL_BATCH_MAX);
    if (poll_num < 0) {
        RPC_ADPT_VLOG_ERR("Failed to poll umq of share jfr, share jfr fd: %d\n", m_fd);
        return -1;
    }

    if (poll_num == 0) {
        return 0;
    }

    std::set<ShareJfrEventFdEpollEvent *> need_wake_up_events;
    std::set<int> socket_fds;
    for (int i = 0; i < poll_num; ++i) {
        umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        int socket_fd = buf_pro->umq_ctx;
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(socket_fd);
        if (socket_fd_obj == nullptr) {
            RPC_ADPT_VLOG_WARN("Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        socket_fd_obj->AddQbuf(buf[i]);

        if (socket_fd_obj->GetShareJfrRxEpollEvent() != nullptr &&
            socket_fd_obj->GetShareJfrRxEpollEvent()->GetEventFdEpollEvent() != nullptr) {
            need_wake_up_events.insert(socket_fd_obj->GetShareJfrRxEpollEvent()->GetEventFdEpollEvent());
            continue;
        }

        socket_fds.insert(socket_fd);
    }

    for (Brpc::ShareJfrEventFdEpollEvent *event : need_wake_up_events) {
        event->WakeUp();
    }

    if (socket_fds.empty()) {
        return 0;
    }

    EpollFd *epoll_fd_obj = (EpollFd *)Fd<::EpollFd>::GetFdObj(m_epoll_fd);
    if (epoll_fd_obj == nullptr) {
        RPC_ADPT_VLOG_ERR("Failed to get epoll fd object, epoll fd: %d \n", m_epoll_fd);
        return -1;
    }

    int total = 0;
    for (int socket_fd : socket_fds) {
        EpollEvent *ev = epoll_fd_obj->Find(socket_fd);
        if (ev == nullptr) {
            RPC_ADPT_VLOG_WARN("Get epoll event failed, socket fd:%d \n", socket_fd);
            continue;
        }

        total += ev->::EpollEvent::ProcessEpollEvent(output_event + total);

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(socket_fd);
        if (socket_fd_obj == nullptr) {
            RPC_ADPT_VLOG_WARN("Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        socket_fd_obj->NewRxEpollIn();
    }

    return total;
}

void ShareJfrRxEpollEvent::AckEvent(uint16_t &event_num, uint16_t add_event_num, umq_interrupt_option_t *option)
{
    if ((event_num += add_event_num) >= GET_PER_ACK) {
        umq_ack_interrupt(m_main_umq, event_num, option);
        event_num = 0;
    }
}

int ShareJfrRxEpollEvent::GetAndAckEvent()
{
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX};
    int events = umq_get_cq_event(m_main_umq, &option);
    if (events == 0) {
        return 0;
    } else if (events < 0) {
        return -1;
    }

    umq_rearm_interrupt(m_main_umq, false, &option);

    AckEvent(m_event_num, events, &option);
    return 0;
}

int ShareJfrRxEpollEvent::PollShareJfrAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size)
{
    if (GetAndAckEvent() < 0) {
        return -1;
    }

    int poll_num = umq_poll(m_main_umq, UMQ_IO_RX, buf, max_buf_size);
    if (poll_num < 0) {
        return -1;
    }

    if (poll_num == 0) {
        return 0;
    }

    umq_alloc_option_t option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(IOBuf::Block)};
    umq_buf_t *rx_buf_list = umq_buf_alloc(BrpcIOBufSize(), poll_num, UMQ_INVALID_HANDLE, &option);
    if (rx_buf_list != nullptr) {
        umq_buf_t *bad_qbuf = nullptr;
        if (umq_post(m_main_umq, rx_buf_list, UMQ_IO_RX, &bad_qbuf) != UMQ_SUCCESS) {
            umq_buf_free(bad_qbuf);
        }
    }

    return poll_num;
}
}  // namespace Brpc
