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
#include <cerrno>

#include "umq_epoll_runner.h"

namespace ock {
namespace ubs {

int UmqEpollRunner::AddEpollEvent(const Socket * const socket, struct epoll_event *event)
{
    auto skt_event_fd = socekt->GetEventFd();
    if (UNLIKELY(skt_event_fd < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
        return -1;
    }
    // auto dfd = ((Brpc::SocketFd *)connect_info.socket_fd_object)->GetEventFd();
    // if (UNLIKELY(dfd < 0)) {
    //     RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
    //     return -1;
    // }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = connect_info.share_jfr_fd;

    struct epoll_event shared_jfr_event {};
    shared_jfr_event.events = EPOLLIN | EPOLLET;
    shared_jfr_event.data.u64 = event_data.u64;

    // TODO：修改为Locker
    ScopedUbExclusiveLocker sLock(mutex_);
    if (UNLIKELY(jfr_main_umq_.count(connect_info.share_jfr_fd) == 0)) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connect_info.share_jfr_fd, &shared_jfr_event) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD) jfr event failed: %d : %s\n", errno,
                strerror(errno));
            return -1;
        }
        jfr_main_umq_.emplace(connect_info.share_jfr_fd, connect_info.main_umq);
    }
    sLock.Unlock();

    struct epoll_event rx_event {
        .events = EPOLLIN | EPOLLET
    };
    event_data.event_data.type = DAEMON_EVENT_TYPE_SUB_UMQ_RX;
    event_data.event_data.data = (ptrdiff_t)(void *)connect_info.socket_fd_object;
    rx_event.data.u64 = event_data.u64;
    if (UNLIKELY(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connect_info.rx_interrupt_fd, &rx_event) < 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll epoll_ctl(ADD) rx event failed: %d : %s\n", errno,
            strerror(errno));
        return -1;
    }
    auto socket_obj = ((Brpc::SocketFd *)connect_info.socket_fd_object);
    // TODO：epoll_ctl(add)时，只传进来了epoll_fd，socket_fd，event
    // TODO：这里是把epoll_fd和对应的event data关联到socket_fd上
    // TODO：后台线程epoll_wait到shared_jfr_fd的事件以后，只知道对应的socket_fd是哪个
    // TODO：因此要根据socket_fd找对应的socket obj，然后再socket obj里面找该socket obj对应的epoll_fd是哪个
    // TODO：然后进行分发唤醒。这个逻辑可以放在上面做，不在Daemon里面做
    socket_obj->SetAddedEpollFd(&epoll_fd, event.data);
    if (LIKELY(!socket_obj->RxUseTcp())) {
        socket_obj->RearmShareJfrRxInterrupt();
        socket_obj->RearmRxInterrupt();
    }

    return skt_event_fd;
}

int UmqEpollRunner::RemoveEpollEvent(const Socket * const socket)
{
    return 0;
}

void UmqEpollRunner::ProcessOneEvent(const struct epoll_event &event)
{

}


}
}