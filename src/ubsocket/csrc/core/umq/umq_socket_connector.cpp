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

#include "umq_socket_connector.h"

#include <netinet/tcp.h>

#include "core/umq/umq_eid_table.h"
#include "umq_errno_converter.h"
#include "common/ubsocket_scope_exit.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqConnectorOps::ConnectViaHandshakeOpt(const SocketPtr &sock,
    const struct sockaddr *address, socklen_t address_len)
{
    int opt = 1;
    int ret = LibcApi::setsockopt(raw_fd_, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, sizeof(opt));
    if (ret < 0 && (errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        UBS_VLOG_WARN("UB handshake socket option not supported. Handshake mode fallback to TFO.\n");
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
        ret = ConnectViaTfo(sock, address, address_len);
    } else {
        UBS_VLOG_INFO("Connect with UB handshake socket option.\n");
    }
    ret = LibcApi::connect(raw_fd_, address, address_len);
    return ret;
}

Result UmqConnectorOps::ConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    NegotiateReq req {};
    if (BuildNegotiateReq(&req, umq_socket) != 0) {
        return -1;
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
    ssize_t sendto_ret = LibcApi::sendto(raw_fd_, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
    if (sendto_ret < 0 && errno != 0) {
        UBS_VLOG_ERR("TFO sendto[1] failed, ret: %zd, errno %d, err msg: %s\n",
                          sendto_ret, errno, Func::Error2Str(errno));
    }
    if (!SocketConnHelper::IsTfoConnection(raw_fd_)) {
        // 首次获取cookie，创建临时socket二次发送
        UBS_VLOG_INFO("TFO Cookie not found or not used. Retrying for immediate SYN+Data.\n");
        const int tmp_fd = LibcApi::socket(AF_INET, SOCK_STREAM, 0);
        LibcApi::setsockopt(tmp_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
        sendto_ret = LibcApi::sendto(tmp_fd, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
        if (sendto_ret < 0 && errno != 0) {
            UBS_VLOG_ERR("TFO sendto[2] failed, ret: %zd, errno %d, err msg: %s\n",
                sendto_ret, errno, Func::Error2Str(errno));
        }

        int dup3_ret = dup3(tmp_fd, raw_fd_, O_CLOEXEC);
        LibcApi::close(tmp_fd);
        if (dup3_ret < 0) {
            UBS_VLOG_ERR("dup3 failed, ret: %d, errno %d, err msg: %s\n",
                dup3_ret, errno, Func::Error2Str(errno));
            return -1;
        }
    } else {
        UBS_VLOG_INFO("TFO Cookie exists, continue...\n");
    }
    return sendto_ret < 0 ? -1 : 0;
}

Result UmqConnectorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                       const SocketPtr &sock)
{
    Result ret = UBS_OK;
    UBHandshakeMode handshake_mode = GlobalSetting::UBS_HAND_SHAKE_MODE;
    if (handshake_mode == UBHandshakeMode::UB_SOCK_OPT) {
        ret = ConnectViaHandshakeOpt(sock, address, address_len);
    } else if (handshake_mode == UBHandshakeMode::TFO) {
        ret = ConnectViaTfo(sock, address, address_len);
    } else {
        return LibcApi::connect(raw_fd_, address, address_len);
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
            UBS_VLOG_DEBUG("tcp connect inprogress:%s, fd %d\n", Func::Error2Str(errno), new_fd);
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

Result UmqConnectorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    route_backup_src_eid_ = {};
    if (ConnectNegotiate(umq_socket) != UBS_OK) {
        UBS_VLOG_ERR("Failed to negotiate in connect,Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip.c_str(), new_fd);
        return UBS_ERROR;
    }
    return UBS_OK;
};

Result UmqConnectorOps::CreateSocketResources(int new_fd, const SocketPtr &sock)
{
    Result ack_ret = UBS_OK;
    Result peer_ret = UBS_OK;
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    // TODO: 待补充建链时重试逻辑
    std::vector<umq_port_id_t> used_port_vector;
    used_port_vector = {conn_route_.src_port, back_route_.src_port};
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    if (ack_ret == UBS_OK) {
        ack_ret = DoUbConnect(umq_socket, used_ports);
    }
    if (ack_ret != UBS_OK) {
        UBS_VLOG_ERR("Failed to finish ub bind in connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), new_fd);
    }
    if (SocketConnHelper::SendSocketData(new_fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), new_fd);
        return UBS_ERROR;
    }

    if (SocketConnHelper::RecvSocketData(new_fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), new_fd);
        return UBS_ERROR;
    }

    umq_conn_info_.create_time = std::chrono::system_clock::now();
    UBS_VLOG_INFO("UB connection has been successfully established new fd: %d\n", new_fd);

    // PrintQbufPoolInfo();
    return UBS_OK;
};

void UmqConnectorOps::DestroySocketResources()
{
    return;
}

// ======================== 建链辅助方法 ========================
Result UmqConnectorOps::BuildNegotiateReq(NegotiateReq *req, const UmqSocketPtr &umq_socket)
{
    umq_eid_t localEid = UmqSetting::UMQ_LOCAL_EID;
    dev_schedule_policy schedulePolicy = dev_schedule_policy::CPU_AFFINITY_PRIORITY;
    req->magic_number = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    req->trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    req->is_bonding = UmqSetting::UMQ_IS_BONDING ? 1 : 0;
    req->enable_share_jfr = GlobalSetting::UBS_ENABLE_SHARE_JFR ? 1 : 0;
    req->schedule_policy = static_cast<uint8_t>(schedulePolicy);
    req->local_eid = localEid;
    return UBS_OK;
}

void UmqConnectorOps::PrintSocketsInfo()
{
    std::ostringstream oss;
    oss << "receive remote all socket ids in connect: ";
    for (size_t i = 0; i < peer_all_socket_ids_.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << peer_all_socket_ids_[i];
    }
    UBS_VLOG_INFO("%s\n", oss.str().c_str());
}

Result UmqConnectorOps::ConnectNegotiate(const UmqSocketPtr &umq_socket)
{
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::UB_SOCK_OPT) {
        // 基于内核选项的建链，需要单独发送协商请求；TFO建链随sendto发送协商请求
        NegotiateReq req {};
        if (BuildNegotiateReq(&req, umq_socket) != 0) {
            return -1;
        }
        if (SocketConnHelper::SendSocketData(raw_fd_, &req, sizeof(req), CONTROL_PLANE_TIMEOUT_MS) !=
            static_cast<int>(sizeof(req))) {
            UBS_VLOG_ERR("Failed to send negotiate request, Peer IP:%s, fd: %d\n",
                umq_conn_info_.peer_ip.c_str(), raw_fd_);
            return -1;
        }
    }
    umq_eid_t local_eid = UmqSetting::UMQ_LOCAL_EID;
    dev_schedule_policy schedule_policy = UmqSetting::UMQ_DEV_SCHEDULE_POLICY;
    umq_conn_info_.conn_eid = local_eid;
    NegotiateRsp rsp{};
    if (SocketConnHelper::RecvSocketData(raw_fd_, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<int>(sizeof(rsp))) {
        UBS_VLOG_ERR("Failed to receive negotiate response in connect,Peer IP:%s, fd: %d\n",
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    if (rsp.ret_code != 0) {
        UBS_VLOG_ERR("Failed to negotiate in connect, peer ret %d, Peer IP:%s, fd: %d\n", rsp.ret_code,
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    ub_trans_mode local_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    if (rsp.peer_trans_mode != local_trans_mode) {
        umq_socket->SetTransMode(rsp.peer_trans_mode < local_trans_mode ? rsp.peer_trans_mode : local_trans_mode);
    }
    if (schedule_policy == dev_schedule_policy::CPU_AFFINITY ||
        schedule_policy == dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
        UBS_VLOG_WARN("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                      static_cast<int>(schedule_policy), raw_fd_);
        use_round_robin_ = false;
    }

    if (UNLIKELY(!UmqSetting::UMQ_IS_BONDING)) {
        umq_conn_info_.conn_eid = local_eid;
        return UBS_OK;
    }

    peer_socket_id_ = rsp.aff_sock_id;
    if (UNLIKELY(rsp.socket_id_count == 0)) {
        UBS_VLOG_ERR("Invalid peer socket count, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }
    peer_all_socket_ids_.reserve(rsp.socket_id_count);
    for (size_t i = 0; i < rsp.socket_id_count; i++) {
        peer_all_socket_ids_.push_back(rsp.socket_ids[i]);
    }
    PrintSocketsInfo();
    umq_conn_info_.peer_eid = rsp.local_eid;

    // choose route and send to server
    if (DoRoute(&local_eid, &umq_conn_info_.peer_eid) != 0) {
        UBS_VLOG_ERR("Failed to get route list in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }

    NegotiateRoute negoRoute(topo_type_, conn_route_, back_route_);
    if (SocketConnHelper::SendSocketData(raw_fd_, &negoRoute, sizeof(NegotiateRoute), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(NegotiateRoute)) {
        UBS_VLOG_ERR("Failed to send negotiate route info in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }
    umq_conn_info_.peer_eid = conn_route_.dst_eid;

    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        umq_conn_info_.conn_eid = conn_route_.src_eid;
    } else {
        umq_conn_info_.conn_eid = local_eid;
    }

    return UBS_OK;
}

Result UmqConnectorOps::DoRoute(const umq_eid_t *src_eid, const umq_eid_t *dst_eid)
{
    umq_route_list_t filtered_list = {};
    if (GetDevRouteList(src_eid, dst_eid, filtered_list) != 0) {
        UBS_VLOG_ERR("Failed to get dev route list\n");
        return UBS_ERROR;
    }
    topo_type_ = filtered_list.topo_type;
    UBS_VLOG_INFO("Topo type's value is: %d\n", topo_type_);
    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        if (GetConnEid(filtered_list, dst_eid) != 0) {
            UBS_VLOG_ERR("Failed to get connect eid\n");
            return UBS_ERROR;
        }
        back_route_ = umq_route_t{};
    } else {
        std::vector<umq_route_t> main_routes;
        std::vector<umq_route_t> back_routes;
        umq_route_t conn_main_route;
        umq_route_t conn_back_route;
        int getAffinityRes = GetCpuAffinityUmqRoute(filtered_list, main_routes, back_routes);
        if (getAffinityRes != 0) {
            UBS_VLOG_ERR("Failed to get cpu affinity umq route\n");
            conn_route_ = umq_route_t{};
            back_route_ = umq_route_t{};
            return UBS_ERROR;
        }
        // 在客户端侧把不亲数组存起来
        back_route_list_ = back_routes;
        // 优先在mainRoutes里轮询
        RRChooseMainRoute(main_routes, dst_eid, conn_main_route, conn_back_route);
        conn_route_ = conn_main_route;
        back_route_ = conn_back_route;
        route_backup_src_eid_ = conn_back_route.src_eid;
        route_backup_dst_eid_ = conn_back_route.dst_eid;
    }
    return UBS_OK;
}

Result UmqConnectorOps::DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports)
{
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    Result ret;
    auto socket = RefConvert<UmqSocket, Socket>(umq_socket);
    // CreateLocalUmq
    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        ret = umq_socket->CreateLocalUmq(&(umq_conn_info_.conn_eid), used_ports,
            &(umq_conn_info_.conn_eid), topo_type_);
    } else {
        umq_eid_t localEid = UmqSetting::UMQ_LOCAL_EID;
        ret = umq_socket->CreateLocalUmq(&localEid, used_ports,
            &(umq_conn_info_.conn_eid), topo_type_);
    }
    if (ret != UBS_OK || SocketBase::GenerateSocketCommOps(socket) != UBS_OK) {
        UBS_VLOG_ERR("[UMQ_API] Failed to create umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return ret;
    }

    local_cp_msg.queue_bind_info_size =
        UmqApi::umq_bind_info_get(umq_socket->UmqHandle(), local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
    if (local_cp_msg.queue_bind_info_size == 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind_info_get() failed, Peer eid:" EID_FMT ",Peer IP:%s, "
                     "fd: %d, ret: %ld, mapped errno: %d(%s), original errno: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_,
                     local_cp_msg.queue_bind_info_size, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL),
                     savedErrno);
        return UBS_ERROR;
    }

    uint32_t len = sizeof(local_cp_msg) - sizeof(uint64_t);
    if (SocketConnHelper::SendSocketData(raw_fd_, &local_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) !=
        len) {
        UBS_VLOG_ERR("Failed to send local control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %ld, bind info len: %ld", raw_fd_,
                   sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

    if (SocketConnHelper::RecvSocketData(raw_fd_, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(remote_cp_msg)) {
        UBS_VLOG_ERR("Failed to receive remote control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %ld, bind info len: %ld", raw_fd_,
                   sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    int umq_ret =
        UmqApi::umq_bind(umq_socket->UmqHandle(), remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size);
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    long long costms = (end_tv.tv_sec - start_tv.tv_sec) * 1000LL + (end_tv.tv_usec - start_tv.tv_usec) / 1000LL;

    if (umq_ret != 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, umq_ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind() failed, Peer eid:" EID_FMT
                     ",Peer IP:%s, fd: %d, ret: %d, mapped errno: %d(%s), "
                     "original errno: %d, operation duration: %lld ms.\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, umq_ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, umq_ret), savedErrno, costms);
        return UBS_ERROR;
    }
    UBS_VLOG_INFO("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umq_socket->SetBindRemote(true);

    if (GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        // 强依赖当前实现，一个 eid 只对应一个主 umq. 如果后续逻辑有变更，需同步修改。
        auto main_umq = UmqEidTable::Instance().GetFirst(umq_conn_info_.conn_eid, umq_socket->GetTransMode());
        if (main_umq == nullptr) {
            UBS_VLOG_ERR("The main umq is removed by other thread.\n");
            return UBS_ERROR;
        }

        return main_umq->EnsurePrefilled([umq_socket, this]() {
            if (umq_socket->PrefillRx() != UBS_OK) {
                UBS_VLOG_ERR("Failed to fill rx buffer to main umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                             EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                return UBS_ERROR;
            }
            return UBS_OK;
        });
    }

    // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
    if (umq_socket->PrefillRx() != UBS_OK) {
        UBS_VLOG_INFO("Failed to fill rx buffer to umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                      EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_PREFILL_RX;
    }

    return UBS_OK;
}

/**
 * @brief Get device route list via umq_get_route_list with cache support
 */
Result UmqConnectorOps::GetDevRouteList(const umq_eid_t *src_eid, const umq_eid_t *dst_eid,
                                        umq_route_list_t &filtered_list)
{
    if (RouteListRegistry::Instance().GetRouteList(*dst_eid, filtered_list)) {
        if (filtered_list.route_num > 0) {
            return UBS_OK;
        }
    }

    // TODO: UmqSetting 增加 umq_trans_mode_t
    ub_trans_mode trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    umq_tp_type_t tp_type;
    if (trans_mode == RC_TP) {
        tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode == RM_TP) {
        tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode == RM_CTP) {
        tp_type = UMQ_TP_TYPE_CTP;
    } else if (trans_mode == RC_CTP) {
        tp_type = UMQ_TP_TYPE_CTP;
    } else {
        tp_type = UMQ_TP_TYPE_RTP;
    }

    umq_route_key_t route;
    (void)memcpy(&route.src_bonding_eid, src_eid, sizeof(umq_eid_t));
    (void)memcpy(&route.dst_bonding_eid, dst_eid, sizeof(umq_eid_t));
    route.tp_type = tp_type;

    umq_route_list_t route_list;
    int ret = UmqApi::umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
    if (ret != 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_route_list() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return -1;
    }

    filtered_list = route_list;
    if (filtered_list.route_num == 0) {
        UBS_VLOG_ERR("Failed to get urma topo is zero\n");
        return -1;
    }

    RouteListRegistry::Instance().RegisterOrReplaceRouteList(*dst_eid, filtered_list);
    return 0;
}

Result UmqConnectorOps::GetConnEid(umq_route_list_t &route_list, const umq_eid_t *dst_eid)
{
    if (!use_round_robin_) {
        UBS_VLOG_DEBUG("use_round_robin is false\n");
        std::set<uint32_t> unique_chip_ids;
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            unique_chip_ids.insert(route_list.routes[i].src_port.bs.chip_id);
        }
        std::vector<uint32_t> chipId_list(unique_chip_ids.begin(), unique_chip_ids.end());
        uint32_t targetChipId =
            GetTargetChipId(UmqSetting::UMQ_ALL_SOCKET_IDS, chipId_list, UmqSetting::UMQ_PROCESS_SOCKET_ID);
        if (targetChipId == UINT32_MAX) {
            return GetRoundRobinConnEid(route_list, dst_eid);
        }
        // 查找匹配的eid对
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            if (targetChipId == route_list.routes[i].src_port.bs.chip_id) {
                conn_route_ = route_list.routes[i];
                return UBS_OK;
            }
        }
        UBS_VLOG_ERR("Failed to find umq dev\n");
        return UBS_ERROR;
    } else {
        return GetRoundRobinConnEid(route_list, dst_eid);
    }
    return UBS_OK;
}

uint32_t UmqConnectorOps::GetTargetChipId(const std::vector<uint32_t> &socket_ids,
                                          const std::vector<uint32_t> &chip_id_list, int processSocketId)
{
    auto it = std::find(socket_ids.begin(), socket_ids.end(), processSocketId);
    if (it == socket_ids.end()) {
        return UINT32_MAX; // 错误标识
    }

    size_t index = std::distance(socket_ids.begin(), it);
    if (index >= chip_id_list.size()) {
        return UINT32_MAX; // 索引越界
    }

    return chip_id_list[index];
}

// Round_Robin
Result UmqConnectorOps::GetRoundRobinConnEid(umq_route_list_t &route_list, const umq_eid_t *dst_eid)
{
    // 获取起始索引
    uint32_t startIndex = 0;
    GetBondingEidMapIndex(*dst_eid, startIndex);

    // 确保索引在有效范围内
    startIndex = startIndex % route_list.route_num;

    // 从起始索引开始轮询查找
    bool found = false;
    for (uint32_t offset = 0; offset < route_list.route_num; ++offset) {
        uint32_t current_index = (startIndex + offset) % route_list.route_num;
        conn_route_ = route_list.routes[current_index];
        found = true;
        startIndex = (current_index + 1) % route_list.route_num; // 更新下次起始位置
        break;
    }

    // 更新下一个轮询位置
    EidRegistry::Instance().RegisterOrReplaceEidIndex(*dst_eid, startIndex);

    if (!found) {
        UBS_VLOG_ERR("Failed to find umq dev\n");
        return UBS_ERROR;
    }

    return UBS_OK;
}

void UmqConnectorOps::GetBondingEidMapIndex(const umq_eid_t &dst_eid, uint32_t &index)
{
    if (!EidRegistry::Instance().IsRegisteredEidIndex(dst_eid)) {
        EidRegistry::Instance().RegisterOrReplaceEidIndex(dst_eid, 0);
    }

    EidRegistry::Instance().GetEidIndex(dst_eid, index);
}

// CLOS组网 通过亲和性选择 Client端调用
Result UmqConnectorOps::GetCpuAffinityUmqRoute(umq_route_list_t &route_list, std::vector<umq_route_t> &main_routes,
                                               std::vector<umq_route_t> &back_routes)
{
    main_routes.clear();
    back_routes.clear();
    uint32_t process_chip_Id = 0;
    uint32_t peer_chip_id = 0;

    // 本端
    std::set<uint32_t> process_chip_ids;
    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        process_chip_ids.insert(route_list.routes[i].src_port.bs.chip_id);
    }
    std::vector<uint32_t> process_chip_id_list(process_chip_ids.begin(), process_chip_ids.end());
    process_chip_Id =
        GetTargetChipId(UmqSetting::UMQ_ALL_SOCKET_IDS, process_chip_id_list, UmqSetting::UMQ_PROCESS_SOCKET_ID);
    UBS_VLOG_INFO("process_chip_Id: %u\n", process_chip_Id);

    // 对端
    std::set<uint32_t> peer_chip_ids;
    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        peer_chip_ids.insert(route_list.routes[i].dst_port.bs.chip_id);
    }
    std::vector<uint32_t> peer_chip_id_list(peer_chip_ids.begin(), peer_chip_ids.end());
    peer_chip_id = GetTargetChipId(peer_all_socket_ids_, peer_chip_id_list, peer_socket_id_);
    UBS_VLOG_INFO("peer_chip_id: %u\n", peer_chip_id);

    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        if (route_list.routes[i].src_port.bs.chip_id == process_chip_Id &&
            route_list.routes[i].dst_port.bs.chip_id == peer_chip_id) {
            main_routes.push_back(route_list.routes[i]);
        }
    }

    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        if (route_list.routes[i].src_port.bs.chip_id != process_chip_Id &&
            route_list.routes[i].dst_port.bs.chip_id != peer_chip_id) {
            back_routes.push_back(route_list.routes[i]);
        }
    }

    if (!main_routes.empty() && !back_routes.empty()) {
        UBS_VLOG_INFO("Find umq route successfully\n");
        return UBS_OK;
    }

    UBS_VLOG_ERR("Failed to find umq route\n");
    return UBS_ERROR;
}

void UmqConnectorOps::RRChooseMainRoute(std::vector<umq_route_t> &main_routes, const umq_eid_t *dst_eid,
                                        umq_route_t &conn_main_route, umq_route_t &conn_back_route)
{
    // 获取起始索引
    uint32_t startIndex = 0;
    GetBondingEidMapIndex(*dst_eid, startIndex);

    // 确保索引在有效范围内
    startIndex = startIndex % main_routes.size();

    // 从起始索引开始轮询查找
    conn_main_route = main_routes[startIndex];
    startIndex = (startIndex + 1) % main_routes.size(); // 更新下次起始位置
    conn_back_route = main_routes[startIndex];

    // 更新下一个轮询位置
    EidRegistry::Instance().RegisterOrReplaceEidIndex(*dst_eid, startIndex);

    UBS_VLOG_INFO("main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n", conn_main_route.src_port.bs.chip_id,
                  conn_main_route.src_port.bs.die_id, conn_main_route.src_port.bs.port_idx);
    UBS_VLOG_INFO("back route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n", conn_back_route.src_port.bs.chip_id,
                  conn_back_route.src_port.bs.die_id, conn_back_route.src_port.bs.port_idx);
}
} // namespace umq
} // namespace ubs
} // namespace ock
