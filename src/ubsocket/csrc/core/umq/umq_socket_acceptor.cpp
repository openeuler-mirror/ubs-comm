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

#include "umq_socket_acceptor.h"
#include "core/ubsocket_socket_helper.h"
#include "umq_eid_table.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqAcceptorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                   const SocketPtr &sock)
{
    return 0;
}

Result UmqAcceptorOps::Negotiate(SocketPtr socketPtr)
{
    umq_eid_t connEid;
    umq_eid_t dstEid;
    // TODO get local eid
    umq_eid_t localEid = UmqSetting::UMQ_LOCAL_EID;
    dev_schedule_policy peerSchedulePolicy = dev_schedule_policy::ROUND_ROBIN;
    topo_type = UMQ_TOPO_TYPE_FULLMESH_1D;
    connEid = localEid;
    if (AcceptNegotiate(socketPtr, connEid, dstEid, peerSchedulePolicy) != UBS_OK) {
        UBS_VLOG_ERR("Failed to negotiate in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(peer_eid), conn_info.peer_ip.data(), fd);
        //return InnerCode::kUBSOCKET_TCP_EXCHANGE;
        return UBS_ERROR;
    }
    // 服务端收到当前topo类型
    if (SocketConnHelper::RecvSocketData(
        fd, &topo_type, sizeof(umq_topo_type_t),
        CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_topo_type_t)) {
        UBS_VLOG_ERR("receive umq topo type failed\n");
    }
    UBS_VLOG_INFO("receive umq topo type successfully: %d\n", topo_type);

    peer_eid = dstEid;
    if (topo_type == UMQ_TOPO_TYPE_FULLMESH_1D) {
        conn_eid = connEid;
    } else {
        conn_eid = localEid;
    }
    return UBS_OK;
}

Result UmqAcceptorOps::CreateSocketResources(SocketPtr socketPtr)
{
    // 1. 用户直接指定普通设备建链，失败不重试、可降级
    // 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，失败不重试、可降级
    // 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误
    //   - 优先重试，如果重试过程中失败则降级
    //   - 如果无法重试，则尝试降级
    //   - 如果无法降级，则返回失败
    // Result ackRet = UBS_OK;
    // UBHandshakeState state = UBHandshakeState::kSTART;
    umq_used_ports_t mUsedPorts = {.port = nullptr, .num = 0};
    /*while (!ok) {
        switch (state) {
            case UBHandshakeState::kOK:
                ok = true;
                break;

            case UBHandshakeState::kSTART:
                ackRet = DoUbAccept(new_fd, connEid, umq_socket_fd_obj, mUsedPorts);
                break;

            case UBHandshakeState::kRETRY:
                ackRet = ackRet | DoUbAccept(new_fd, connEid, umq_socket_fd_obj, mUsedPorts);
                break;

            case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE:
                break;

            case UBHandshakeState::kDEGRADE:
                return UBS_UB_ACCEPT;

            case UBHandshakeState::kFAILED:
                return UBS_UB_ACCEPT;
        }
    }*/
    Result ret = DoUbAccept(socketPtr, mUsedPorts);
    if (ret != UBS_OK) {
        return ret;
    }
    if (SocketConnHelper::SendSocketData(fd, &ret, sizeof(ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ret)) {
        UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(peer_eid), conn_info.peer_ip.c_str(), fd);
        return UBS_ERROR;
    }

    if (SocketConnHelper::RecvSocketData(fd, &ret, sizeof(ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(ret)) {
        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(peer_eid), conn_info.peer_ip.c_str(), fd);
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result UmqAcceptorOps::DoUbAccept(SocketPtr socketPtr, umq_used_ports_t &mUsedPorts)
{
    Result ret = UBS_OK;
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    ret = umqSocket->CreateLocalUmq(&conn_eid, mUsedPorts);
    // 校验 bind 是否成功
    if (ret != UBS_OK || SocketBase::GenerateSocketCommOps(socketPtr) != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq\n");
        return ret;
    }
    local_cp_msg.queue_bind_info_size = UmqApi::umq_bind_info_get(
        umqSocket->UmqHandle(), local_cp_msg.queue_bind_info, sizeof(local_cp_msg.queue_bind_info));
    if (local_cp_msg.queue_bind_info_size == 0) {
        UBS_VLOG_ERR("umq_bind_info_get() failed, ret: %lu\n",
                     local_cp_msg.queue_bind_info_size);
        // return ubsocket::Error::kUMQ_BIND_INFO_GET | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        return UBS_ERROR;
    }

    size_t len = sizeof(remote_cp_msg) - sizeof(uint64_t);
    if (SocketConnHelper::RecvSocketData(
        fd, &remote_cp_msg.queue_bind_info_size, len,
        CONTROL_PLANE_TIMEOUT_MS) != (ssize_t)len) {
        UBS_VLOG_ERR("Failed to receive remote control message, fd: %d\n", fd);
        //return ubsocket::FromRaw(errno);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %lu, bind info len: %lu\n",
                   fd, sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    if (SocketConnHelper::SendSocketData(
        fd, &local_cp_msg, sizeof(local_cp_msg),
        CONTROL_PLANE_TIMEOUT_MS) != sizeof(local_cp_msg)) {
        UBS_VLOG_ERR("Failed to send local control message, fd: %d\n", fd);
        // return ubsocket::FromRaw(errno);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %lu, bind info len: %lu\n",
                   fd, sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    int umq_ret = UmqApi::umq_bind(umqSocket->UmqHandle(), remote_cp_msg.queue_bind_info,
                                   remote_cp_msg.queue_bind_info_size);
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    long long costms = (end_tv.tv_sec - start_tv.tv_sec) * 1000LL +
                       (end_tv.tv_usec - start_tv.tv_usec) / 1000LL;
    if (umq_ret != UMQ_SUCCESS) {
        UBS_VLOG_ERR("umq_bind() failed, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
        // return ubsocket::Error::kUMQ_BIND | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        return UBS_ERROR;
    }
    UBS_VLOG_INFO("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umqSocket->SetBindRemote(true);

    if (GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        // 强依赖当前实现，一个 eid 对应多 UB 传输模式不同的 umq. 如果后续逻辑有变更，需同步修改。
        auto main_umq = UmqEidTable::Instance().GetFirst(conn_eid, umqSocket->GetTransMode());
        if (main_umq == nullptr) {
            UBS_VLOG_ERR("The main umq state is removed by other thread.\n");
            // return ubsocket::Error::kUBSOCKET_NO_MAIN_UMQ;
            return UBS_ERROR;
        }

        return main_umq->EnsurePrefilled([umqSocket]() {
            if (umqSocket->PrefillRx() != 0) {
                UBS_VLOG_ERR("Failed to fill rx buffer to main umq, fd: %d\n", umqSocket->raw_socket_);
                return UBS_ERROR;
            }
            return UBS_OK;
        });
    }

    // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
    if (umqSocket->PrefillRx() != 0) {
        UBS_VLOG_ERR("Failed to fill rx buffer to umq, fd: %d\n", fd);
        return UBS_PREFILL_RX;
    }
    return UBS_OK;
}

void UmqAcceptorOps::DestroySocketResources() {}

int UmqAcceptorOps::ValidateProtocol(int fd, uint64_t &protocol_negotiation,
                                     ssize_t &protocol_negotiation_recv_size)
{
    protocol_negotiation_recv_size =
        SocketConnHelper::RecvSocketData(fd, &protocol_negotiation, sizeof(protocol_negotiation), NEGOTIATE_TIMEOUT_MS);
    if (protocol_negotiation_recv_size <= 0) {
        UBS_VLOG_ERR("Validate protocol failed, fd: %d, ret: %zd\n",
                     fd, protocol_negotiation_recv_size);
        return -1;
    }
    if (protocol_negotiation_recv_size != sizeof(protocol_negotiation) ||
        protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        UBS_VLOG_ERR("Validate protocol mismatch, fd: %d, ret: %zd\n",
                     fd, protocol_negotiation_recv_size);
        return protocol_negotiation_recv_size;
    }
    return 0;
}

Result UmqAcceptorOps::AcceptNegotiate(SocketPtr socketPtr, umq_eid_t &connEid,
                                       umq_eid_t &dstEid, dev_schedule_policy &peerSchedulePolicy)
{
    NegotiateReq req {};
    NegotiateRsp rsp {};
    char *req_buf = reinterpret_cast<char *>(&req) + sizeof(req.magic_number);
    size_t req_remain_size = sizeof(req) - sizeof(req.magic_number);
    if (SocketConnHelper::RecvSocketData(fd, req_buf, req_remain_size, CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<int>(req_remain_size)) {
        UBS_VLOG_ERR("Failed to receive negotiate request in accept, fd: %d\n", fd);
        return -1;
    }

    // UB 传输模式优先级协商，值越小优先级越高。例如当服务端为 RM_TP 而客户端是 RC_TP 会协商至 RC_TP.
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    rsp.peer_trans_mode = umqSocket->GetTransMode();
    umqSocket->SetTransMode(std::min(req.trans_mode, umqSocket->GetTransMode()));

    rsp.ret_code = (umqSocket->IsBindind() == (req.is_bonding != 0)) ? 0 : -1;
    rsp.local_eid = connEid;
    if (rsp.ret_code == 0 && req.is_bonding != 0) {
        dstEid = req.local_eid;
        peerSchedulePolicy = static_cast<dev_schedule_policy>(req.schedule_policy);
        if ((peerSchedulePolicy == dev_schedule_policy::CPU_AFFINITY ||
             peerSchedulePolicy == dev_schedule_policy::CPU_AFFINITY_PRIORITY) && req.has_socket_id != 0) {
            UBS_VLOG_WARN("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                          static_cast<int>(peerSchedulePolicy), fd);
        }
        rsp.local_eid = connEid;
    }
    if (SocketConnHelper::SendSocketData(
        fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) != static_cast<int>(sizeof(rsp))) {
        UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
        return -1;
    }
    if (req.is_bonding == 1) {
        // 把服务器的套接字值给客户端，在客户端做遍历
        // TODO set UMQ_PROCESS_SOCKET_ID
        int sendServerSocketId = UmqSetting::UMQ_PROCESS_SOCKET_ID;
        if (SocketConnHelper::SendSocketData(fd, &sendServerSocketId, sizeof(sendServerSocketId),
                                             CONTROL_PLANE_TIMEOUT_MS) != sizeof(sendServerSocketId)) {
            UBS_VLOG_ERR("Failed to send server socket ids to connect");
            return -1;
        }
        // 把服务器的套接字数组给客户端，在客户端做遍历
        if (AcceptExchangeSocketIDs(fd) != 0) {
            UBS_VLOG_ERR("Failed to get all socket ids in DoAccept");
        }

        if (SocketConnHelper::RecvSocketData(
            fd, &conn_route, sizeof(umq_route_t),
            CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
            UBS_VLOG_ERR("Failed to receive remote eid message in accept, Peer IP:%s, fd: %d\n",
                         conn_info.peer_ip.c_str(), fd);
            return -1;
        }

        if (SocketConnHelper::RecvSocketData(
            fd, &back_route, sizeof(umq_route_t),
            CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
            UBS_VLOG_ERR("Failed to receive remote back eid message in accept, Peer IP:%s, fd: %d\n",
                         conn_info.peer_ip.c_str(), fd);
            return -1;
        }

        int checkResult = CheckDevAdd(conn_route.dst_eid);
        if (checkResult != 0) {
            UBS_VLOG_ERR("CheckDevAdd() failed in accept, Peer IP:%s, fd: %d, ret: %d\n",
                         conn_info.peer_ip.c_str(), fd, checkResult);
            return -1;
        }

        // 保存对端EID
        dstEid = conn_route.src_eid;
        connEid = conn_route.dst_eid;
    }
    return rsp.ret_code == 0 ? 0 : -1;
}

int UmqAcceptorOps::AcceptExchangeSocketIDs(int fd)
{
    // 发送本端的all socket ids
    // TODO set UMQ_ALL_SOCKET_IDS
    std::vector<uint32_t> sendAllSocketIds = UmqSetting::UMQ_ALL_SOCKET_IDS;

    // 发送本端的all socket ids
    uint32_t count = static_cast<uint32_t>(sendAllSocketIds.size());
    size_t dataSize = count * sizeof(uint32_t);

    if (SocketConnHelper::SendSocketData(fd, &count, sizeof(count), CONTROL_PLANE_TIMEOUT_MS) != sizeof(count)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n",
                     conn_info.peer_ip.c_str(), fd);
        return -1;
    }
    if (SocketConnHelper::SendSocketData(fd, sendAllSocketIds.data(), dataSize, CONTROL_PLANE_TIMEOUT_MS)
        != static_cast<ssize_t>(dataSize)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n",
                     conn_info.peer_ip.c_str(), fd);
        return -1;
    }

    // 打印
    std::ostringstream msg;
    msg << "send local all socket ids in accept: ";
    for (size_t i = 0; i < sendAllSocketIds.size(); ++i) {
        if (i > 0) {
            msg << ", ";
        }
        msg << sendAllSocketIds[i];
    }
    UBS_VLOG_INFO("%s\n", msg.str().c_str());
    return 0;
}

int UmqAcceptorOps::CheckDevAdd(const umq_eid_t &connEid)
{
    // TODO EidRegistry is needed
    /*if (EidRegistry::Instance().IsRegisteredEid(connEid)) {
        return 0;
    }

    umq_trans_info_t trans_info;
    trans_info.trans_mode = UMQ_TRANS_MODE_UB;
    trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
    trans_info.dev_info.eid.eid = connEid;
    int ret = umq_dev_add(&trans_info);
    if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
        UBS_VLOG_ERR("umq_dev_add() failed, ret: %d\n", ret);
        return -1;
    }

    ret = Context::GetContext()->RegisterAsyncEvent(trans_info);
    if (ret < 0) {
        UBS_VLOG_ERR("RegisterAsyncEvent() failed, conn eid:" EID_FMT ", ret: %d\n",
                     EID_ARGS(connEid), ret);
        return ret;
    }

    EidRegistry::Instance().RegisterEid(connEid);*/
    return 0;
}
} // namespace umq
} // namespace ubs
} // namespace ock
