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
#include "core/umq/umq_eid_table.h"
#include "umq_errno_converter.h"

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
    umq_eid_t localEid = UmqSetting::UMQ_LOCAL_EID;
    connEid = localEid;
    if (AcceptNegotiate(socketPtr, connEid, dstEid) != UBS_OK) {
        UBS_VLOG_ERR("Failed to negotiate in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n", EID_ARGS(peer_eid_),
                     conn_info.peer_ip.data(), fd);
        return UBS_ERROR;
    }
    UBS_VLOG_INFO("negotiate umq topo type successfully: %d\n", topo_type_);
    peer_eid_ = dstEid;
    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        conn_eid_ = connEid;
    } else {
        conn_eid_ = localEid;
    }
    umq_conn_info_.peer_eid = peer_eid_;
    umq_conn_info_.conn_eid = conn_eid_;
    return UBS_OK;
}

Result UmqAcceptorOps::CreateSocketResources(SocketPtr socketPtr)
{
    /**
     * 1. 用户直接指定普通设备建链，失败不重试、可降级
     * 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，失败不重试、可降级
     * 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误
     *    - 优先重试，如果重试过程中失败则降级
     *    - 如果无法重试，则尝试降级
     *    - 如果无法降级，则返回失败
     */
    bool ok = false;
    Result ackRet = UBS_OK;
    Result peerRet = UBS_OK;

    // status reset
    degradable_ = false;
    retry_state_ = UBHandshakeState::kSTART;
    other_route_message_ = {};

    while (!ok) {
        switch (retry_state_) {
            case UBHandshakeState::kOK: {
                ok = true;
                break;
            }
            case UBHandshakeState::kSTART: {
                std::vector<umq_port_id_t> used_port_vector = {conn_route_.src_port, back_route_.src_port};
                umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                               .num = static_cast<uint8_t>(used_port_vector.size())};
                ackRet = DoUbAccept(socketPtr, used_ports);

                if (IsDegradable(ackRet) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
                    ackRet = ackRet - UBS_DEGRADABLE_MASK;
                }
                if (!IsOk(ackRet)) {
                    UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                }
                if (SocketConnHelper::SendSocketData(fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(ackRet)) {
                    UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                    return UBS_TCP_EXCHANGE;
                }
                if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(peerRet)) {
                    UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                    return UBS_TCP_EXCHANGE;
                }

                // 服务端判断是否可降级
                degradable_ = IsDegradable(ackRet);
                if (IsOk(ackRet) && IsOk(peerRet)) {
                    retry_state_ = UBHandshakeState::kOK;
                } else if (UmqSetting::UMQ_IS_BONDING && (IsRetryable(ackRet) || IsRetryable(peerRet)) &&
                           ((umq_conn_info_.conn_eid != UmqSetting::UMQ_LOCAL_EID &&
                             topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) ||
                            topo_type_ == UMQ_TOPO_TYPE_CLOS)) {
                    retry_state_ = UBHandshakeState::kRETRY;
                } else if (degradable_) {
                    retry_state_ = UBHandshakeState::kDEGRADE;
                } else {
                    retry_state_ = UBHandshakeState::kFAILED;
                }
                break;
            }
            case UBHandshakeState::kRETRY: {
                auto ret = DoUbAcceptRetry(socketPtr, ackRet, peerRet);
                if (ret == UBS_OK) {
                    UBS_VLOG_DEBUG("Success to retry accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                   EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                    break;
                } else {
                    UBS_VLOG_ERR("Failed to retry accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d, err: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd, ret);
                    return ret;
                }
            }
            case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE: {
                if (degradable_) {
                    retry_state_ = UBHandshakeState::kDEGRADE;
                } else {
                    retry_state_ = UBHandshakeState::kFAILED;
                }
                break;
            }
            case UBHandshakeState::kDEGRADE: {
                // 不调用 OverrideFdObj，当此连接上有请求时直接使用裸 socket API.
                UBS_VLOG_INFO("ubsocket is degraded to TCP.\n");
                return UBS_UB_ACCEPT | UBS_DEGRADABLE_MASK;
            }
            case UBHandshakeState::kFAILED: {
                UBS_VLOG_ERR("Failed to get new connect in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                             EID_ARGS(umq_conn_info_.conn_eid), umq_conn_info_.peer_ip.c_str(), fd);
                return UBS_UB_ACCEPT;
            }
        }
    }
    return UBS_OK;
}

Result UmqAcceptorOps::DoUbAccept(SocketPtr socketPtr, umq_used_ports_t &used_ports)
{
    Result ret = UBS_OK;
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);

    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        ret = umqSocket->CreateLocalUmq(&(umq_conn_info_.conn_eid), used_ports, &(umq_conn_info_.conn_eid), topo_type_);
    } else {
        umq_eid_t localEid = UmqSetting::UMQ_LOCAL_EID;
        ret = umqSocket->CreateLocalUmq(&localEid, used_ports, &(umq_conn_info_.conn_eid), topo_type_);
    }

    // 校验 bind 是否成功
    if (ret != UBS_OK || SocketBase::GenerateSocketCommOps(socketPtr) != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq\n");
        return ret;
    }
    local_cp_msg.queue_bind_info_size = UmqApi::umq_bind_info_get(umqSocket->UmqHandle(), local_cp_msg.queue_bind_info,
                                                                  sizeof(local_cp_msg.queue_bind_info));
    if (local_cp_msg.queue_bind_info_size == 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind_info_get() failed, ret: %lu, mapped errno: %d(%s), original errno: %d\n",
                     local_cp_msg.queue_bind_info_size, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
        return UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }

    size_t len = sizeof(remote_cp_msg) - sizeof(uint64_t);
    if (SocketConnHelper::RecvSocketData(fd, &remote_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) !=
        (ssize_t)len) {
        UBS_VLOG_ERR("Failed to receive remote control message, fd: %d\n", fd);
        //return ubsocket::FromRaw(errno);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %lu, bind info len: %lu\n", fd,
                   sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    if (SocketConnHelper::SendSocketData(fd, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(local_cp_msg)) {
        UBS_VLOG_ERR("Failed to send local control message, fd: %d\n", fd);
        // return ubsocket::FromRaw(errno);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %lu, bind info len: %lu\n", fd,
                   sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    int umq_ret =
        UmqApi::umq_bind(umqSocket->UmqHandle(), remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size);
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    long long costms = (end_tv.tv_sec - start_tv.tv_sec) * 1000LL + (end_tv.tv_usec - start_tv.tv_usec) / 1000LL;
    if (umq_ret != UMQ_SUCCESS) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::ACCEPT, umq_ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind() failed, ret: %d, mapped errno: %d(%s), "
                     "original errno: %d, operation duration: %lld ms.\n",
                     umq_ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::ACCEPT, umq_ret), savedErrno,
                     costms);
        return UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    UBS_VLOG_INFO("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umqSocket->SetBindRemote(true);

    if (GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        // 强依赖当前实现，一个 eid 对应多 UB 传输模式不同的 umq. 如果后续逻辑有变更，需同步修改。
        auto main_umq = UmqEidTable::Instance().GetFirst(conn_eid_, umqSocket->GetTransMode());
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

Result UmqAcceptorOps::DoUbAcceptRetry(SocketPtr socketPtr, Result &ack_ret, Result &peer_ret)
{
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    if (peer_schedule_policy_ == dev_schedule_policy::CPU_AFFINITY) {
        UBS_VLOG_ERR("CPU_AFFINITY: %d failed, accept no need to retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     static_cast<int>(peer_schedule_policy_), EID_ARGS(umq_conn_info_.peer_eid),
                     umq_conn_info_.peer_ip.c_str(), fd);
        if (degradable_) {
            retry_state_ = UBHandshakeState::kDEGRADE;
        } else {
            retry_state_ = UBHandshakeState::kFAILED;
        }
        return UBS_OK;
    }

    umqSocket->UnbindAndFlushRemoteUmq(socketPtr);
    umqSocket->DestroyLocalUmq();

    if (SocketConnHelper::RecvSocketData(fd, &other_route_message_, sizeof(other_route_message_),
                                         CONTROL_PLANE_TIMEOUT_MS) != sizeof(other_route_message_)) {
        return UBS_TCP_EXCHANGE;
    }

    // 客户端 CheckOtherRoute 失败
    if (other_route_message_.ub_handshake_state != UBHandshakeState::kRETRY) {
        UBS_VLOG_INFO("Client CheckOtherRoute failed, try to degrade to TCP.\n");
        retry_state_ = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
        return UBS_OK;
    }

    umq_conn_info_.conn_eid = other_route_message_.other_route.dst_eid;
    if (umqSocket->CheckDevAdd(umq_conn_info_.conn_eid) != 0) {
        UBS_VLOG_ERR("Failed to add dev in retry accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
        ack_ret = UBS_UB_DEV_ERROR | UBS_DEGRADABLE_MASK;
    } else {
        ack_ret = UBS_OK;
    }

    // 保留在 CheckDevAdd 阶段时的错误
    std::vector<umq_port_id_t> used_port_vector = {other_route_message_.other_route.src_port,
                                                   other_route_message_.other_back_route.src_port};
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    UBS_VLOG_INFO("DoAccept down to back, main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                  other_route_message_.other_route.src_port.bs.chip_id,
                  other_route_message_.other_route.src_port.bs.die_id,
                  other_route_message_.other_route.src_port.bs.port_idx);

    UBS_VLOG_INFO("DoAccept down to back, back route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                  other_route_message_.other_back_route.src_port.bs.chip_id,
                  other_route_message_.other_back_route.src_port.bs.die_id,
                  other_route_message_.other_back_route.src_port.bs.port_idx);
    ack_ret = ack_ret | DoUbAccept(socketPtr, used_ports);
    if (IsDegradable(ack_ret) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
        ack_ret = ack_ret - UBS_DEGRADABLE_MASK;
    }

    if (!IsOk(ack_ret)) {
        UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
    }

    if (SocketConnHelper::SendSocketData(fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ack_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd, ack_ret);
        return UBS_TCP_EXCHANGE;
    }

    if (SocketConnHelper::RecvSocketData(fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to recv peer ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, peer_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd, peer_ret);
        return UBS_TCP_EXCHANGE;
    }

    degradable_ = IsDegradable(ack_ret);
    if (IsOk(ack_ret) && IsOk(peer_ret)) {
        retry_state_ = UBHandshakeState::kOK;
    } else if (degradable_) {
        retry_state_ = UBHandshakeState::kDEGRADE;
    } else {
        retry_state_ = UBHandshakeState::kFAILED;
    }

    return UBS_OK;
}

void UmqAcceptorOps::DestroySocketResources() {}

int UmqAcceptorOps::ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size)
{
    protocol_negotiation_recv_size =
        SocketConnHelper::RecvSocketData(fd, &protocol_negotiation, sizeof(protocol_negotiation), NEGOTIATE_TIMEOUT_MS);
    if (protocol_negotiation_recv_size <= 0) {
        UBS_VLOG_ERR("Validate protocol failed, fd: %d, ret: %zd\n", fd, protocol_negotiation_recv_size);
        return -1;
    }
    if (protocol_negotiation_recv_size != sizeof(protocol_negotiation) ||
        protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        UBS_VLOG_ERR("Validate protocol mismatch, fd: %d, ret: %zd\n", fd, protocol_negotiation_recv_size);
        return protocol_negotiation_recv_size;
    }
    return 0;
}

Result UmqAcceptorOps::FillLocalSocketIdsForNegotiate(uint32_t *socket_ids, uint32_t &socket_id_count)
{
    std::vector<uint32_t> ids = UmqSetting::UMQ_ALL_SOCKET_IDS;
    if (ids.empty() || ids.size() > NEGOTIATE_SOCKET_ID_MAX_NUM) {
        UBS_VLOG_ERR("Invalid local socket ids, size %zu, Peer IP:%s\n", ids.size(), umq_conn_info_.peer_ip.c_str());
        return UBS_ERROR;
    }
    socket_id_count = static_cast<uint32_t>(ids.size());
    for (uint32_t i = 0; i < socket_id_count; ++i) {
        socket_ids[i] = ids[i];
    }
    return UBS_OK;
}

void UmqAcceptorOps::BuildNegotiateRsp(NegotiateRsp &rsp)
{
    rsp.peer_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    rsp.aff_sock_id = UmqSetting::UMQ_PROCESS_SOCKET_ID;
    FillLocalSocketIdsForNegotiate(rsp.socket_ids, rsp.socket_id_count);
    // 打印
    std::ostringstream msg;
    msg << "send local all socket ids in accept: ";
    for (size_t i = 0; i < rsp.socket_id_count; ++i) {
        if (i > 0) {
            msg << ", ";
        }
        msg << rsp.socket_ids[i];
    }
    UBS_VLOG_INFO("%s\n", msg.str().c_str());
}

Result UmqAcceptorOps::AcceptNegotiate(SocketPtr socketPtr, umq_eid_t &connEid, umq_eid_t &dstEid)
{
    NegotiateReq req{};
    NegotiateRsp rsp{};
    char *req_buf = reinterpret_cast<char *>(&req) + sizeof(req.magic_number);
    size_t req_remain_size = sizeof(req) - sizeof(req.magic_number);
    if (SocketConnHelper::RecvSocketData(fd, req_buf, req_remain_size, CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<int>(req_remain_size)) {
        UBS_VLOG_ERR("Failed to receive negotiate request in accept, fd: %d\n", fd);
        return UBS_ERROR;
    }

    // UB 传输模式优先级协商，值越小优先级越高。例如当服务端为 RM_TP 而客户端是 RC_TP 会协商至 RC_TP.
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    auto local_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    umqSocket->SetTransMode(std::min(req.trans_mode, local_trans_mode));

    // src bonding mode is different from dst bonding mode
    rsp.ret_code = (UmqSetting::UMQ_IS_BONDING == (req.is_bonding != 0)) ? 0 : -1;
    rsp.local_eid = connEid;
    if (UNLIKELY(rsp.ret_code != 0)) {
        UBS_VLOG_ERR("client bonding mode is not equal to server bonding mode, client:%d, server:%d\n", req.is_bonding,
                     UmqSetting::UMQ_IS_BONDING);
    }
    if (UNLIKELY(rsp.ret_code != 0 || req.is_bonding == 0)) {
        if (SocketConnHelper::SendSocketData(fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) !=
            static_cast<int>(sizeof(rsp))) {
            UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
            return UBS_ERROR;
        }
    }

    BuildNegotiateRsp(rsp);
    if (SocketConnHelper::SendSocketData(fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<int>(sizeof(rsp))) {
        UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
        return UBS_ERROR;
    }

    NegotiateRoute negoRoute;
    if (SocketConnHelper::RecvSocketData(fd, &negoRoute, sizeof(NegotiateRoute), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(NegotiateRoute)) {
        UBS_VLOG_ERR("Failed to receive remote negoritate route in accept, Peer IP:%s, fd: %d\n",
                     conn_info.peer_ip.c_str(), fd);
        return UBS_ERROR;
    }
    conn_route_ = negoRoute.master_route;
    back_route_ = negoRoute.back_route;
    topo_type_ = negoRoute.topo_type;

    int checkResult = umqSocket->CheckDevAdd(conn_route_.dst_eid);
    if (checkResult != 0) {
        UBS_VLOG_ERR("CheckDevAdd() failed in accept, Peer IP:%s, fd: %d, ret: %d\n", conn_info.peer_ip.c_str(), fd,
                     checkResult);
        return UBS_ERROR;
    }

    // 保存对端EID
    dstEid = conn_route_.src_eid;
    connEid = conn_route_.dst_eid;
    return rsp.ret_code == 0 ? 0 : -1;
}

Result UmqAcceptorOps::AcceptExchangeSocketIDs(int fd)
{
    // 发送本端的all socket ids
    std::vector<uint32_t> sendAllSocketIds = UmqSetting::UMQ_ALL_SOCKET_IDS;
    uint32_t count = static_cast<uint32_t>(sendAllSocketIds.size());
    size_t dataSize = count * sizeof(uint32_t);

    if (SocketConnHelper::SendSocketData(fd, &count, sizeof(count), CONTROL_PLANE_TIMEOUT_MS) != sizeof(count)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n", conn_info.peer_ip.c_str(),
                     fd);
        return UBS_ERROR;
    }
    if (SocketConnHelper::SendSocketData(fd, sendAllSocketIds.data(), dataSize, CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<ssize_t>(dataSize)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n", conn_info.peer_ip.c_str(),
                     fd);
        return UBS_ERROR;
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
    return UBS_OK;
}
} // namespace umq
} // namespace ubs
} // namespace ock
