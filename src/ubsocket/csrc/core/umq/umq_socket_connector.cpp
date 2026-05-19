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
#ifndef UBS_COMM_UMQ_SOCKET_ACCEPTOR
#define UBS_COMM_UMQ_SOCKET_ACCEPTOR

#include <netinet/tcp.h>

#include "umq_socket_connector.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqConnectorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                       const SocketPtr &sock)
{
    Result ret = UBS_OK;
    // 判断TCPI_OPT_SYN_DATA，如果已置位则复用
    NegotiateReq req{};
    if (BuildNegotiateReq(&req) != 0) {
        UBS_VLOG_ERR("Failed to send negotiate request caused by building req failure\n");
        return UBS_ERROR;
    }

    bool is_blocking = SocketConnHelper::IsBlocking(new_fd);
    if (!is_blocking) {
        SocketConnHelper::SetBlocking(new_fd);
    }

    constexpr int fast_open = 1;
    LibcApi::setsockopt(new_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
    ssize_t sendto_ret = LibcApi::sendto(new_fd, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
    ret = sendto_ret < 0 ? UBS_ERROR : UBS_OK;
    if (ret < 0 && errno != 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        UBS_VLOG_ERR("TFO sendto[1] failed, ret: %zd, errno %d, err msg: %s, fd %d\n", sendto_ret, errno,
                     NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), new_fd);
    }

    if (!SocketConnHelper::IsTfoConnection(new_fd)) {
        // 首次获取cookie，第二次发送
        UBS_VLOG_INFO("TFO Cookie not found or not used. Retrying for immediate SYN+Data.\n");
        // 创建临时socket发送
        const int tmp_fd = LibcApi::socket(AF_INET, SOCK_STREAM, 0);
        LibcApi::setsockopt(tmp_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
        sendto_ret = LibcApi::sendto(tmp_fd, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
        ret = sendto_ret < 0 ? UBS_ERROR : UBS_OK;
        if (ret < 0 && errno != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("TFO sendto[2] failed, ret: %zd, errno %d, err msg: %s, fd %d\n", sendto_ret, errno,
                         NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), tmp_fd);
        }

        int dup3_ret = dup3(tmp_fd, new_fd, O_CLOEXEC);
        LibcApi::close(tmp_fd);
        if (dup3_ret < 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("dup3 failed, ret: %d, errno %d, err msg: %s, tmp_fd %d, new_fd %d\n", dup3_ret, errno,
                         NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), tmp_fd, new_fd);
            return UBS_ERROR;
        }
    } else {
        // 已经是tfo连接，继续处理
        UBS_VLOG_INFO("TFO Cookie exists, continue...\n");
    }

    if (!is_blocking) {
        SocketConnHelper::SetNonBlocking(new_fd);
    }

    if (address != nullptr) {
        // 使用提取的接口获取IP地址
        std::string peer_ip = SocketConnHelper::ExtractIpFromSockAddr(address);
        umq_conn_info_.peer_ip = peer_ip;
        // 对端fd就是accept返回的fd
        umq_conn_info_.peer_fd = new_fd;
        umq_conn_info_.create_time = std::chrono::system_clock::now();
    }

    // TODO: m_tx_use_tcp || m_rx_use_tcp 如何处理
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED || !SocketConnHelper::IsTfoConnection(new_fd)) {
        return ret;
    }

    if (ret == UBS_OK) {
        UBS_VLOG_INFO("tcp connect succeed, fd %d\n", new_fd);
    } else {
        /* fd是非阻塞套接字
            * 1. 第一次调用connect返回-1，errno为EINPROGRESS，网络正在建连；
            * 2. 若未建连状态下，第n次对fd调用connect，n>=2，返回-1，errno为EALREADY；
            * 3. 若建连成功，且当前不是第二次调connect，返回-1，errno为EISCONN；否则返回0（非阻塞套接字）
            *
            * 若ret = 0 或者errno 是EISCONN，tcp连接已完成，则执行DoConnect，且需要等待连接完成再返回，
            * 若errno是EINPROGRESS/EALREADY，fd最终会变为连接状态，则执行DoConnect，且不需要等待ub连接完成
            * 若errno是EINTR/EADDRNOTAVAIL/EHOSTUNREACH等错误码，tcp连接失败，则不执行DoConnect，保持原错误码直接返回上层，由上层应用决定后续动作
            */
        if (errno == EINPROGRESS || errno == EALREADY) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_DEBUG("tcp connect inprogress:%s, fd %d\n",
                           NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), new_fd);
        } else if (errno != EISCONN) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("connect() failed, ret: %d, errno: %d, errmsg: %s, fd: %d\n", ret, errno,
                         NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), new_fd);
            return ret;
        }
    }

    int tcpNoDelayRet = SocketConnHelper::SetTcpNoDelay(new_fd);
    if (tcpNoDelayRet != 0) {
        UBS_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", new_fd, tcpNoDelayRet, errno);
    }
    if (!is_blocking) {
        // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
        SocketConnHelper::SetNonBlocking(new_fd);
    }

    return ret;
}

Result UmqConnectorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    return UBS_OK;
};

Result UmqConnectorOps::CreateSocketResources(int new_fd, const SocketPtr &sock)
{
    return UBS_OK;
};

void UmqConnectorOps::DestroySocketResources()
{
    return;
}

// ======================== 建链辅助方法 ========================
int UmqConnectorOps::BuildNegotiateReq(NegotiateReq *req)
{
    return 0;
}

} // namespace umq
} // namespace ubs
} // namespace ock
#endif
