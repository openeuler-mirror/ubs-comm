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
#include "umq_epoll_ops.h"

namespace ock {
namespace ubs {
namespace umq {
int UmqEventPollOps::AddTxEvent(const SocketPtr &socket, int epoll_fd)
{
#ifdef ENABLED
    struct epoll_event add_event{};
    auto event_data = new (std::nothrow) EpollEventData(EPOLL_EVENT_UB_SOCKET_OUT, socket->GetRawFD(), *event);
    if (UNLIKELY(event_data == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d alloc failed.\n",
                          socket->GetRawFD());
        return -1;
    }

    add_event.events = EPOLLOUT | EPOLLET;
    add_event.data.ptr = event_data;
    // TODO：实现socket->GetUmqTxFd
    // auto ret = epoll_ctl(socket->GetRawFD(), EPOLL_CTL_ADD, socket->GetUmqTxFd(), &add_event);
    // if (UNLIKELY(ret < 0)) {
    //     RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d failed: %d : %s\n",
    //         socket_fd, errno, strerror(errno));
    //     delete event_data;
    //     return -1;
    // }

    // auto socket_obj = (Brpc::SocketFd *)Fd<::SocketFd>::GetFdObj(socket->GetRawFD());
    // if (LIKELY(socket_obj != nullptr && !socket_obj->TxUseTcp())) {
    //     socket_obj->RearmTxInterrupt();
    // }

    // if (UNLIKELY(!InsertSocketEventData(event_fd, event_data))) {
    //     RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "async_epoll add out event for socket fd: %d insert event data failed\n",
    //         socket->GetRawFD());
    //     epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_fd, nullptr);
    //     delete event_data;
    //     return -1;
    // }
#endif
    return 0;
}
} // namespace umq
} // namespace ubs
} // namespace ock