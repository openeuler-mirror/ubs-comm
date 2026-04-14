/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-01-20
 * Note:
 * History: 2026-01-20
*/

#ifndef BRPC_SHARE_JFR_H
#define BRPC_SHARE_JFR_H

#include "qbuf_queue.h"

namespace Brpc {

class ShareJfrEventFdEpollEvent : public ::EpollEvent {
public:
    ShareJfrEventFdEpollEvent(int fd, struct epoll_event *event, int event_fd)
        : EpollEvent(event_fd, event), m_origin_fd(fd)
    {
        m_event.events = EPOLLIN | EPOLLET;
    }

    virtual ~ShareJfrEventFdEpollEvent()
    {
        if (m_fd >= 0) {
            OsAPiMgr::GetOriginApi()->close(m_fd);
            m_fd = -1;
        }
    }

    virtual int AddEpollEvent(int epoll_fd);
    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                int max_events, bool use_polling = false) override;
    void WakeUp();

private:
    int m_origin_fd = -1;
};

class ShareJfrRxEpollEvent : public ::EpollEvent {
public:
    ShareJfrRxEpollEvent(int fd, struct epoll_event *event, int share_jfr_fd, int epoll_fd, uint64_t main_umq)
        : EpollEvent(share_jfr_fd, event), m_origin_fd(fd), m_epoll_fd(epoll_fd), m_main_umq(main_umq)
    {
        m_event.events = EPOLLIN | EPOLLET;
        m_fd_type = FdType::SHARE_JFR_FD;
    }

    virtual ~ShareJfrRxEpollEvent()
    {
        if (event_fd_epoll_event != nullptr) {
            delete event_fd_epoll_event;
            event_fd_epoll_event = nullptr;
        }
    }

    virtual int AddEpollEvent(int epoll_fd, bool use_polling = false) override;
    virtual int ModEpollEvent(int epoll_fd, struct epoll_event *event, bool use_polling = false) override;
    virtual int DelEpollEvent(int epoll_fd, bool use_polling = false) override;
    virtual int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event, int maxevents,
                                  bool use_polling = false) override;

    ShareJfrEventFdEpollEvent *GetEventFdEpollEvent() const
    {
        return event_fd_epoll_event;
    }

    struct epoll_event GetEpollEvent() const
    {
        return m_event;
    }

private:
    int m_origin_fd = -1;
    int m_epoll_fd = -1;
    uint64_t m_main_umq = UMQ_INVALID_HANDLE;
    uint16_t m_event_num = 0;
    ShareJfrEventFdEpollEvent *event_fd_epoll_event = nullptr;

    void AckEvent(uint16_t &event_num, uint16_t add_event_num, umq_interrupt_option_t *option);
    int GetAndAckEvent();
    int PollShareJfrAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size);
};

}

#endif
