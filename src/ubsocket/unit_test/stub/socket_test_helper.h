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
#ifndef UBS_COMM_SOCKET_TEST_HELPER_H
#define UBS_COMM_SOCKET_TEST_HELPER_H

#include "fake_epoll.h"
#include "libc_api_helper.h"
#include "umq_api_helper.h"

#include "common/ubsocket_lock.h"
#include "common/ubsocket_ref.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_socket.h"
#include "core/umq/umq_data_tx_ops.h"
#include "core/umq/umq_data_rx_ops.h"

/**
 * socket_test_helper.h — SocketPtr构造与清理的快捷方式。
 *
 * 简单case直接用: SocketPtr sock = MakeTestUmqSocket();
 * 复杂case用参数定制:
 *   - state:          SOCK_STAT_INIT / SOCK_STAT_RAW_ESTABLISHED /
 *                     SOCK_STAT_ESTABLISHED(默认) / SOCK_STAT_SHUTDOWN
 *   - shareUmqHandle: 设置share JFR主handle，模拟UBS_ENABLE_SHARE_JFR=true场景
 *
 * 仍不够灵活时，可直接在test case中手写:
 *   UmqSocketPtr umqSock = MakeRef<UmqSocket>(fd);
 *   umqSock->xxx_ = ...;   // -fno-access-control可设private字段
 *   // 自定义构造tx/rx ops或跳过ops创建
 *   return RefConvert<UmqSocket, Socket>(umqSock);
 * 手写时务必在SocketPtr析构前调用DestroyTestSocketOps()清理ops+释放event_fd。
 */

namespace ock {
namespace ubs {
namespace test {

using umq::UmqSocket;
using umq::UmqSocketPtr;
using umq::UmqTxOps;
using umq::UmqRxOps;

inline SocketPtr MakeTestUmqSocket(int fd = TEST_FD, uint64_t umqHandle = TEST_UMQ_HANDLE,
                                   SocketState state = SOCK_STAT_ESTABLISHED,
                                   uint64_t shareUmqHandle = UMQ_INVALID_HANDLE)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(fd);
    if (umqSock.Get() == nullptr) {
        return SocketPtr();
    }

    umqSock->umq_handle_ = umqHandle;
    umqSock->share_umq_handle_ = shareUmqHandle;
    umqSock->event_fd_ = FakeEpollCtl::AllocFakeFd();
    umqSock->state_ = state;

    umqSock->tx_.tx_ops_ = new UmqTxOps(fd, umqHandle);
    umqSock->rx_.rx_ops_ = new UmqRxOps(fd, umqHandle);

    return RefConvert<UmqSocket, Socket>(umqSock);
}

inline void DestroyTestSocketOps(SocketPtr &sock)
{
    if (sock.Get() == nullptr) {
        return;
    }

    SocketBase *base = dynamic_cast<SocketBase *>(sock.Get());
    if (base != nullptr) {
        delete base->GetTx()->GetTxOps();
        base->GetTx()->tx_ops_ = nullptr;
        delete base->GetRx()->GetRxOps();
        base->GetRx()->rx_ops_ = nullptr;
    }

    if (sock.Get()->event_fd_ >= FakeEpollCtl::FAKE_FD_BASE) {
        FakeEpollCtl::ReleaseFakeFd(sock.Get()->event_fd_);
        sock.Get()->event_fd_ = -1;
    }
}

} // namespace test
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_SOCKET_TEST_HELPER_H