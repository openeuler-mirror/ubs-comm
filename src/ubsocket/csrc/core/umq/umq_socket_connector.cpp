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

#include <netinet/tcp.h>

#include "umq_socket_connector.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqConnectorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                       const SocketPtr &sock)
{
    Result ret = UBS_OK;
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    // 判断TCPI_OPT_SYN_DATA，如果已置位则复用
    NegotiateReq req{};
    if (BuildNegotiateReq(&req, umq_socket) != 0) {
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
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    route_backup_src_eid_ = {};
    if (ConnectNegotiate(umq_socket) != UBS_OK) {
        UBS_VLOG_ERR("Failed to negotiate in connect,Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip.c_str(), new_fd);
        return UBS_ERROR;
    }
    umq_topo_type_t topo_type = umq_socket->GetTopoType();
    if (SocketConnHelper::SendSocketData(new_fd, &topo_type, sizeof(umq_topo_type_t), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(umq_topo_type_t)) {
        UBS_VLOG_ERR("send umq topo type failed\n");
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
    req->trans_mode = umq_socket->GetTransMode();
    req->is_bonding = umq_socket->IsBonding() ? 1 : 0;
    req->enable_share_jfr = 0;
    req->schedule_policy = static_cast<uint8_t>(schedulePolicy);
    req->has_socket_id = ((schedulePolicy == dev_schedule_policy::CPU_AFFINITY) ||
                          (schedulePolicy == dev_schedule_policy::CPU_AFFINITY_PRIORITY)) ?
                             1 :
                             0;
    req->process_socket_id = UmqSetting::UMQ_PROCESS_SOCKET_ID;
    req->local_eid = localEid;
    if (req->is_bonding != 0 && (req->has_socket_id == 1) &&
        FillLocalSocketIdsForNegotiate(umq_socket, req->socket_ids, req->socket_id_count) != UBS_OK) {
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result UmqConnectorOps::FillLocalSocketIdsForNegotiate(const UmqSocketPtr &umq_socket, uint32_t *socket_ids,
                                                       uint32_t &socket_id_count)
{
    std::vector<uint32_t> ids = UmqSetting::UMQ_ALL_SOCKET_IDS;
    if (ids.empty() || ids.size() > NEGOTIATE_SOCKET_ID_MAX_NUM) {
        UBS_VLOG_ERR("Invalid local socket ids, size %zu, Peer IP:%s, fd: %d\n", ids.size(),
                     umq_conn_info_.peer_ip.c_str(), umq_socket->raw_socket_);
        return UBS_ERROR;
    }
    socket_id_count = static_cast<uint32_t>(ids.size());
    for (uint32_t i = 0; i < socket_id_count; ++i) {
        socket_ids[i] = ids[i];
    }
    return UBS_OK;
}

Result UmqConnectorOps::ConnectNegotiate(const UmqSocketPtr &umq_socket)
{
    // TODO: 待增加从环境变量中获取 和 亲和策略
    umq_eid_t local_eid = UmqSetting::UMQ_LOCAL_EID;
    dev_schedule_policy schedule_policy = dev_schedule_policy::ROUND_ROBIN;
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
    // TODO: 待增加从环境变量中获取 trans mode
    ub_trans_mode local_trans_mode = umq_socket->GetTransMode();
    if (rsp.peer_trans_mode != local_trans_mode) {
        umq_socket->SetTransMode(rsp.peer_trans_mode < local_trans_mode ? rsp.peer_trans_mode : local_trans_mode);
    }
    use_round_robin_ = true;
    if (schedule_policy == dev_schedule_policy::CPU_AFFINITY ||
        schedule_policy == dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
        UBS_VLOG_WARN("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                      static_cast<int>(schedule_policy), raw_fd_);
        use_round_robin_ = false;
    }

    if (umq_socket->IsBonding()) {
        // 接收服务器的id
        int receive_server_socket_id = 0;
        if (SocketConnHelper::RecvSocketData(raw_fd_, &receive_server_socket_id, sizeof(receive_server_socket_id),
                                             CONTROL_PLANE_TIMEOUT_MS) != sizeof(receive_server_socket_id)) {
            UBS_VLOG_ERR("Failed to get server socket ids in connect");
            return UBS_ERROR;
        }
        server_socket_id_for_affinity_ = receive_server_socket_id;

        if (ConnectExchangeSocketIDs() != 0) {
            UBS_VLOG_ERR("Failed to send all socket ids in DoConnect");
        }
        umq_conn_info_.peer_eid = rsp.local_eid;

        if (DoRoute(&local_eid, &umq_conn_info_.peer_eid, &conn_route_, use_round_robin_, &back_route_) != 0) {
            UBS_VLOG_ERR("Failed to get route list in connect, fd: %d\n", raw_fd_);
            return UBS_ERROR;
        }

        if (SocketConnHelper::SendSocketData(raw_fd_, &conn_route_, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) !=
            sizeof(umq_route_t)) {
            UBS_VLOG_ERR("Failed to send connect eid message in connect, fd: %d\n", raw_fd_);
            return UBS_ERROR;
        }

        if (SocketConnHelper::SendSocketData(raw_fd_, &back_route_, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) !=
            sizeof(umq_route_t)) {
            UBS_VLOG_ERR("Failed to send back connect eid message in connect, fd: %d\n", raw_fd_);
            return UBS_ERROR;
        }

        umq_conn_info_.peer_eid = conn_route_.dst_eid;

        if (umq_socket->GetTopoType() == UMQ_TOPO_TYPE_FULLMESH_1D) {
            umq_conn_info_.conn_eid = conn_route_.src_eid;
        } else {
            umq_conn_info_.conn_eid = local_eid;
        }
    } else {
        // TODO check conn_eid set
        umq_conn_info_.conn_eid = local_eid;
    }

    return UBS_OK;
}

Result UmqConnectorOps::ConnectExchangeSocketIDs(void)
{
    // 接收对端的all socket ids
    uint32_t count = 0;
    if (SocketConnHelper::RecvSocketData(raw_fd_, &count, sizeof(count), CONTROL_PLANE_TIMEOUT_MS) != sizeof(count)) {
        UBS_VLOG_ERR("Failed to receive remote all socket ids in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }

    if (count == 0) {
        UBS_VLOG_ERR("Invalid peer socket count, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }
    // 接收对端的all socket ids
    peer_all_socket_ids_.resize(count);
    size_t peer_data_size = count * sizeof(uint32_t);
    ssize_t all_socket_ret = SocketConnHelper::RecvSocketData(raw_fd_, peer_all_socket_ids_.data(), peer_data_size,
                                                              CONTROL_PLANE_TIMEOUT_MS);
    if (all_socket_ret < 0 || static_cast<size_t>(all_socket_ret) != peer_data_size) {
        UBS_VLOG_ERR("Failed to receive remote all socket ids in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }
    // 打印
    std::ostringstream oss;
    oss << "receive remote all socket ids in connect: ";
    for (size_t i = 0; i < peer_all_socket_ids_.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << peer_all_socket_ids_[i];
    }
    UBS_VLOG_INFO("%s\n", oss.str().c_str());
    return 0;
}

Result UmqConnectorOps::DoRoute(const umq_eid_t *src_eid, const umq_eid_t *dst_eid, umq_route_t *conn_route_,
                                bool use_round_robin, umq_route_t *back_route_)
{
#ifdef ENABLED
    umq_route_list_t filteredList = {};
    if (GetDevRouteList(srcEid, dstEid, filteredList) != 0) {
        UBS_VLOG_ERR(ubsocket::UBSocket, "Failed to get dev route list\n");
        return -1;
    }
    mTopoType = filteredList.topo_type;
    UBS_VLOG_INFO("Topo type's value is: %d\n", mTopoType);
    if (mTopoType == UMQ_TOPO_TYPE_FULLMESH_1D) {
        if (GetConnEid(filteredList, dstEid, conn_route_, useRoundRobin) != 0) {
            UBS_VLOG_ERR(ubsocket::UBSocket, "Failed to get connect eid\n");
            return -1;
        }
        // 清空back
        *back_route_ = umq_route_t{};
    } else {
        std::vector<umq_route_t> mainRoutes;
        std::vector<umq_route_t> backRoutes;
        umq_route_t connMainRoute;
        umq_route_t connBackRoute;
        int getAffinityRes = GetCpuAffinityUmqRoute(filteredList, mainRoutes, backRoutes);
        if (getAffinityRes != 0) {
            UBS_VLOG_ERR(ubsocket::UBSocket, "Failed to get cpu affinity umq route\n");
            // 报错时清空
            *conn_route_ = umq_route_t{};
            *back_route_ = umq_route_t{};
            return -1;
        }
        // 在客户端侧把不亲数组存起来
        mBackRoutes = backRoutes;
        // 优先在mainRoutes里轮询
        RRChooseMainRoute(mainRoutes, dstEid, connMainRoute, connBackRoute);
        *conn_route_ = connMainRoute;
        *back_route_ = connBackRoute;
        m_route_backup_src_eid = connBackRoute.src_eid;
    }
#endif
    return UBS_OK;
}

Result UmqConnectorOps::DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports)
{
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    Result ret;

    // CreateLocalUmq
    if (umq_socket->GetTopoType() == UMQ_TOPO_TYPE_FULLMESH_1D) {
        ret = umq_socket->CreateLocalUmq(&umq_conn_info_.conn_eid, used_ports);
    } else {
        // 获取 dev src eid
        // umq_eid_t localEid = umq_socket->GetDevSrcEid();
        ret = umq_socket->CreateLocalUmq(&umq_conn_info_.conn_eid, used_ports);
    }
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return ret;
    }

    local_cp_msg.queue_bind_info_size =
        UmqApi::umq_bind_info_get(umq_socket->UmqHandle(), local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
    if (local_cp_msg.queue_bind_info_size == 0) {
        UBS_VLOG_ERR("umq_bind_info_get() failed, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ret: %ld",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_,
                     local_cp_msg.queue_bind_info_size);
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
        UBS_VLOG_ERR("umq_bind() failed, Peer eid:" EID_FMT
                     ",Peer IP:%s, fd: %d, ret: %d, operation duration: %lld ms.\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, umq_ret, costms);
        return UBS_ERROR;
    }
    UBS_VLOG_INFO("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umq_socket->SetBindRemote(true);

    // TODO: EnableShareJfr

    // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
    if (umq_socket->PrefillRx() != UBS_OK) {
        UBS_VLOG_INFO("Failed to fill rx buffer to umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                      EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_PREFILL_RX;
    }

    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
