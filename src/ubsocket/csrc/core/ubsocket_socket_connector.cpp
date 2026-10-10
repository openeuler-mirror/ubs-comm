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
#include "ubsocket_socket_connector.h"

#include <netinet/tcp.h>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_scope_exit.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "ubsocket_raw_socket.h"
#include "ubsocket_socket.h"
#include "ubsocket_socket_helper.h"

namespace ock {
namespace ubs {

// ======================== ConnectorOps 基类共用方法 ========================

Result ConnectorOps::DoTcpConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                  const SocketPtr &sock)
{
    Result ret;
    UBHandshakeMode handshake_mode = GlobalSetting::UBS_HAND_SHAKE_MODE;
    if (handshake_mode == UBHandshakeMode::UB_SOCK_OPT) {
        ret = DoConnectViaHandshakeOpt(sock, address, address_len);
    } else if (handshake_mode == UBHandshakeMode::TFO) {
        ret = DoConnectViaTfo(sock, address, address_len);
    } else {
        return LibcApi::connect(raw_fd_, address, address_len);
    }

    if (address != nullptr) {
        std::string peer_ip = SocketConnHelper::ExtractIpFromSockAddr(address);
        conn_info.peer_ip = peer_ip;
        conn_info.peer_fd = new_fd;
        conn_info.create_time = std::chrono::system_clock::now();
    }

    // TODO: m_tx_use_tcp || m_rx_use_tcp 如何处理
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED || !SocketConnHelper::IsUbsConnection(new_fd)) {
        return ret;
    }

    if (ret == UBS_OK) {
        UBS_VLOG_DEBUG("tcp connect succeed, ip %s port %d fd %d\n", conn_info.peer_ip.c_str(),
                       SocketConnHelper::ExtractPortFromSockAddr(address), new_fd);
    } else {
        /* fd是非阻塞套接字
         * 1. 第一次调用connect返回-1，errno为EINPROGRESS，网络正在建连；
         * 2. 若未建连状态下，第n次对fd调用connect，n>=2，返回-1，errno为EALREADY；
         * 3. 若建连成功，且当前不是第二次调connect，返回-1，errno为EISCONN；否则返回0（非阻塞套接字）
         *
         * 若ret = 0 或者errno 是EISCONN，tcp连接已完成，则执行DoConnect，且需要等待连接完成再返回，
         * 若errno是EINPROGRESS/EALREADY，fd最终会变为连接状态，则执行DoConnect，且不需要等待ub连接完成
         * 若errno是EINTR/EADDRNOTAVAIL/EHOSTUNREACH等错误码，tcp连接失败，则不执行DoConnect，保持原错误码直接返回上层
         */
        if (errno == EINPROGRESS || errno == EALREADY) {
            UBS_VLOG_DEBUG("tcp connect inprogress:%s, fd %d\n", Func::Error2Str(errno), new_fd);
            // 内核选项建链场景：fd是非阻塞套接字，调用connect返回-1，errno为EINPROGRESS，网络正在建连，需修正ret
            ret = UBS_OK;
        } else if (errno != EISCONN) {
            UBS_VLOG_ERR("connect() failed, ret: %d, errno: %d, errmsg: %s, fd: %d\n", ret, errno,
                         Func::Error2Str(errno), new_fd);
            return ret;
        }
    }

    int tcpNoDelayRet = SocketConnHelper::SetTcpNoDelay(new_fd);
    if (tcpNoDelayRet != 0) {
        UBS_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", new_fd, tcpNoDelayRet, errno);
    }
    bool is_blocking = SocketConnHelper::IsBlocking(raw_fd_);
    if (!is_blocking) {
        // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
        SocketConnHelper::SetNonBlocking(new_fd);
    }

    return ret;
}

Result ConnectorOps::DoConnectViaHandshakeOpt(const SocketPtr &sock, const struct sockaddr *address,
                                              socklen_t address_len)
{
    int opt = 1;
    int ret = LibcApi::setsockopt(raw_fd_, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, sizeof(opt));
    if (ret < 0 && (errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        UBS_VLOG_WARN("UB handshake socket option not supported. Handshake mode fallback to TFO.\n");
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
        ret = DoConnectViaTfo(sock, address, address_len);
    } else {
        ret = LibcApi::connect(raw_fd_, address, address_len);
        UBS_VLOG_INFO("Connect with UB handshake socket option.\n");
    }
    return ret;
}

Result ConnectorOps::DoConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    uint8_t send_buf[NEGOTIATE_REQ_BUFFER_SIZE];
    int buf_len = 0;
    if (BuildTfoPayload(send_buf, buf_len, sock) != UBS_OK) {
        return UBS_ERROR;
    }

    bool is_blocking = SocketConnHelper::IsBlocking(raw_fd_);
    if (!is_blocking) {
        SocketConnHelper::SetBlocking(raw_fd_);
    }
    int reset_fd = raw_fd_;
    auto blocking_reset = MakeScopeExit([is_blocking, reset_fd]() {
        if (!is_blocking) {
            SocketConnHelper::SetNonBlocking(reset_fd);
        }
    });

    constexpr int fast_open = 1;
    LibcApi::setsockopt(raw_fd_, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
    ssize_t sendto_ret = LibcApi::sendto(raw_fd_, send_buf, buf_len, MSG_FASTOPEN, address, address_len);
    if (sendto_ret < 0 && errno != 0) {
        UBS_VLOG_ERR("TFO sendto[1] failed, ret: %zd, errno %d, err msg: %s\n", sendto_ret, errno,
                     Func::Error2Str(errno));
    }
    if (!SocketConnHelper::IsUbsConnection(raw_fd_)) {
        UBS_VLOG_INFO("TFO Cookie not found or not used. Retrying for immediate SYN+Data.\n");
        const int tmp_fd = LibcApi::socket(AF_INET, SOCK_STREAM, 0);
        LibcApi::setsockopt(tmp_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
        sendto_ret = LibcApi::sendto(tmp_fd, send_buf, buf_len, MSG_FASTOPEN, address, address_len);
        if (sendto_ret < 0 && errno != 0) {
            UBS_VLOG_ERR("TFO sendto[2] failed, ret: %zd, errno %d, err msg: %s\n", sendto_ret, errno,
                         Func::Error2Str(errno));
        }

        int dup3_ret = dup3(tmp_fd, raw_fd_, O_CLOEXEC);
        LibcApi::close(tmp_fd);
        if (dup3_ret < 0) {
            UBS_VLOG_ERR("dup3 failed, ret: %d, errno %d, err msg: %s\n", dup3_ret, errno, Func::Error2Str(errno));
            return UBS_ERROR;
        }
    } else {
        UBS_VLOG_INFO("TFO Cookie exists, continue...\n");
    }
    return sendto_ret < 0 ? UBS_ERROR : UBS_OK;
}

// ======================== Connector 基础方法 ========================
int Connector::Connect(SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    if (UNLIKELY(connector_ops_ == nullptr)) {
        UBS_VLOG_INFO("Un-reachable path, connector ops is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(sock.Get() == nullptr)) {
        UBS_VLOG_INFO("Un-reachable path, param socket is null");
        return UBS_ERROR;
    }

    PROF_START(CORE_CONNECT);
    auto sockBase = RefStaticCastToRaw<SocketBase>(sock);
    bool is_blocking = SocketConnHelper::IsBlocking(raw_fd_);

    Result ret = connector_ops_->PrepareConnect(raw_fd_, address, address_len, sock);
    if (ret != UBS_OK) {
        PROF_END(CORE_CONNECT, false);
        return ret;
    }
    sock->conn_add_id = SocketConnHelper::PackSockAddrToId(address);

    ret = connector_ops_->Negotiate(raw_fd_, sock);
    if (ret == UBS_OK) {
        ret = connector_ops_->CreateSocketResources(sock);
    }

    if (is_blocking) {
        SocketConnHelper::SetBlocking(raw_fd_);
    }

    if (ret != UBS_OK && IsDegradable(ret)) {
        ret = RawSocket::SetRawSocket(raw_fd_, SOCK_CREATE_TYPE_CONNECT);
        if (ret != UBS_OK) {
            return ret;
        }
        UBS_VLOG_INFO("ubsocket is degraded to TCP.\n");
        return UBS_OK;
    }

    connector_ops_->conn_info.type_fd = 1;
    sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;

    if (GlobalSetting::UBS_TRACE_ENABLED) {
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::ACTIVE_OPEN_COUNT, 1);
    }

    PROF_END(CORE_CONNECT, !ret);
    return ret;
}

Connector::~Connector()
{
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        Statistics::StatsMgr::SubMConnCount();
        Statistics::StatsMgr::SubMActiveConnCount();
    }
}
} // namespace ubs
} // namespace ock
