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

#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_version.h"
#include "core/umq/umq_eid_table.h"
#include "umq_conn_helper.h"
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
    Result ret = AcceptNegotiate(socketPtr);
    if (!IsOk(ret)) {
        if (!IsDegradable(ret)) {
            UBS_VLOG_ERR("Failed to negotiate in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                         EID_ARGS(umq_conn_info_.peer_eid), conn_info.peer_ip.data(), fd);
        }
        return ret;
    }
    UBS_VLOG_DEBUG("negotiate umq topo type successfully: %d\n", topo_type_);
    return UBS_OK;
}

Result UmqAcceptorOps::CheckRouteDevAddForAccept(const umq_eid_t &conn_eid, const UmqSocketPtr &sk)
{
    // 使用 bonding 设备/裸设备连接，在初始化阶段已将其添加，无需再添加 ub dev.
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP ||
        GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::RAW_DEVICE) {
        return UBS_OK;
    }

    // 主设备
    if (sk->CheckDevAdd(conn_eid) != 0) {
        UBS_VLOG_ERR("Failed to check main dev add in accept, target eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(conn_eid), umq_conn_info_.peer_ip.c_str(), sk->Fd());
        return UBS_UMQ_ERROR;
    }

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

    auto umq_sk = RefStaticCast<UmqSocket>(socketPtr);
    while (!ok) {
        switch (retry_state_) {
            case UBHandshakeState::kOK: {
                ok = true;
                break;
            }
            case UBHandshakeState::kSTART: {
                ackRet = CheckRouteDevAddForAccept(umq_conn_info_.conn_eid, umq_sk);

                std::vector<umq_port_id_t> used_port_vector;
                if (topo_type_ == UMQ_TOPO_TYPE_CLOS && UmqSetting::UMQ_IS_BONDING) {
                    // 构造 used_ports：主路 + 所有备路端口
                    used_port_vector.push_back(conn_route_.src_port);
                    for (const auto &br : back_routes_) {
                        used_port_vector.push_back(br.src_port);
                    }
                    UBS_VLOG_DEBUG("Acceptor CreateSocketResources: used_ports num=%zu (1 main + %zu backup)\n",
                                   used_port_vector.size(), back_routes_.size());
                } else if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D && UmqSetting::UMQ_IS_BONDING) {
                    used_port_vector = {conn_route_.src_port};
                } else {
                    used_port_vector = {};
                }

                umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                               .num = static_cast<uint8_t>(used_port_vector.size())};
                if (IsOk(ackRet)) {
                    ackRet = DoUbAccept(socketPtr, used_ports);
                }

                if (IsDegradable(ackRet) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
                    ackRet = ackRet - UBS_DEGRADABLE_MASK;
                }
                if (!IsOk(ackRet)) {
                    UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                }

                if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(peerRet)) {
                    UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                    return UBS_TCP_EXCHANGE;
                }

                if (IsDegradable(peerRet) && GlobalSetting::UBS_ENABLE_DEGRADE) {
                    ackRet |= UBS_DEGRADABLE_MASK;
                }
                if (SocketConnHelper::SendSocketData(fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(ackRet)) {
                    UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
                    return UBS_TCP_EXCHANGE;
                }

                // 服务端判断是否可降级
                degradable_ = IsDegradable(ackRet);
                if (IsOk(ackRet) && IsOk(peerRet)) {
                    retry_state_ = UBHandshakeState::kOK;
                } else if ((IsRetryable(ackRet) || IsRetryable(peerRet)) &&
                           GlobalSetting::LINK_SELECTION_POLICY != LinkSelectionPolicy::RAW_DEVICE) {
                    // 裸设备不需要重试。因无法区分 1主3备 与 1主1备，两者统一重试
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

    // - 人工选路，使用真正的 port eid.
    // - 裸设备、bonding 设备对外均可直接使用一开始由 devname 找到的 eid.
    const umq_eid_t eid = GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_ROUTE ?
                              umq_conn_info_.conn_eid :
                              UmqSetting::UMQ_LOCAL_EID;
    ret = umqSocket->CreateLocalUmq(&eid, used_ports, topo_type_);

    // 校验 bind 是否成功
    if (ret != UBS_OK || SocketBase::GenerateSocketCommOps(socketPtr) != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq\n");
        return ret;
    }
    PROF_START(UMQ_BIND_INFO_GET);
    local_cp_msg.queue_bind_info_size = UmqApi::umq_bind_info_get(umqSocket->UmqHandle(), local_cp_msg.queue_bind_info,
                                                                  sizeof(local_cp_msg.queue_bind_info));
    if (local_cp_msg.queue_bind_info_size == 0) {
        PROF_END(UMQ_BIND_INFO_GET, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind_info_get() failed, ret: %lu, mapped errno: %d(%s), original errno: %d\n",
                     local_cp_msg.queue_bind_info_size, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
        return UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    PROF_END(UMQ_BIND_INFO_GET, true);

    if (SocketConnHelper::SendLengthPrefixed(fd, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to send local control message, fd: %d\n", fd);
        // return ubsocket::FromRaw(errno);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg size: %zu, bind info len: %lu\n", fd,
                   sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

    if (SocketConnHelper::RecvLengthPrefixed(fd, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive remote control message, fd: %d\n", fd);
        return UBS_ERROR;
    }
    if (remote_cp_msg.queue_bind_info_size > UMQ_BIND_INFO_SIZE_MAX) {
        UBS_VLOG_ERR("Receive remote invalid control message, fd: %d\n", fd);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg size: %zu, bind info len: %lu\n", fd,
                   sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    // 光组网下会一次性使用所有 port，如果它出现在 cooldown 表中，则表示所有路径
    // 均已尝试过，无需再重试，可直接降级至 TCP.
    if (topo_type_ == UMQ_TOPO_TYPE_CLOS) {
        for (uint8_t i = 0; i < used_ports.num; ++i) {
            const auto &p = used_ports.port[i].bs;
            if (PortCooldownManager::IsPortInCooldown(used_ports.port[i])) {
                UBS_VLOG_WARN("used_ports[%u]: src_port(chip=%u,die=%u,port=%u) is down, skipped. Peer eid: " EID_FMT
                              ", Peer IP: %s, fd: %d\n",
                              i, p.chip_id, p.die_id, p.port_idx, EID_ARGS(umq_conn_info_.peer_eid),
                              umq_conn_info_.peer_ip.c_str(), fd);
                return UBS_UMQ_BIND | UBS_DEGRADABLE_MASK;
            }
        }
    }

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    PROF_START(UMQ_BIND);
    int umq_ret =
        UmqApi::umq_bind(umqSocket->UmqHandle(), remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size);
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    long long costms = (end_tv.tv_sec - start_tv.tv_sec) * 1000LL + (end_tv.tv_usec - start_tv.tv_usec) / 1000LL;
    if (umq_ret != UMQ_SUCCESS) {
        PROF_END(UMQ_BIND, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::ACCEPT, umq_ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind() failed, ret: %d, mapped errno: %d(%s), "
                     "original errno: %d, operation duration: %lld ms.\n",
                     umq_ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::ACCEPT, umq_ret), savedErrno,
                     costms);
        return UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    PROF_END(UMQ_BIND, true);
    UBS_VLOG_DEBUG("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umqSocket->SetBindRemote(true);

    if (GlobalSetting::LINK_SELECTION_POLICY != LinkSelectionPolicy::BONDING_BACKUP) {
        // 强依赖当前实现，一个 eid 对应多 UB 传输模式不同的 umq. 如果后续逻辑有变更，需同步修改。
        auto main_umq = UmqEidTable::Instance().GetFirst(umq_conn_info_.conn_eid, umqSocket->GetTransMode());
        if (main_umq == nullptr) {
            UBS_VLOG_ERR("The main umq state is removed by other thread.\n");
            return UBS_ERROR;
        }

        const uint64_t handle = main_umq->GetUmqHandle();
        const Result ret = main_umq->EnsurePrefilled([handle]() {
            if (UmqConnHelper::PrefillRx(handle) != UBS_OK) {
                UBS_VLOG_ERR("Failed to fill rx buffer to umq\n");
                return UBS_PREFILL_RX;
            }
            if (UmqConnHelper::RegisterSharedJfrForRead(handle) != UBS_OK) {
                UBS_VLOG_ERR("Failed to register shared jfr to epoll\n");
                return UBS_PREFILL_RX;
            }
            return UBS_OK;
        });
        if (!IsOk(ret)) {
            return ret;
        }
    }
    umqSocket->UpdateRxQueueAvailNum();
#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        umq_info_t umq_info{};
        auto ret = umq_info_get(umqSocket->UmqHandle(), &umq_info);
        UBS_VLOG_INFO("UB connection has been successfully established new fd: %d, umq id: %u \n", fd,
                      umq_info.ub.umq_id);
    }
#endif
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

    umqSocket->UnbindAndFlushRemoteUmq(socketPtr.Get());
    umqSocket->DestroyLocalUmq();

    if (SocketConnHelper::RecvLengthPrefixed(fd, &other_route_message_, sizeof(other_route_message_),
                                             CONTROL_PLANE_TIMEOUT_MS) < 0) {
        return UBS_TCP_EXCHANGE;
    }

    // 客户端 CheckOtherRoute 失败
    if (other_route_message_.ub_handshake_state != UBHandshakeState::kRETRY) {
        UBS_VLOG_DEBUG("Client CheckOtherRoute failed, try to degrade to TCP.\n");
        retry_state_ = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
        return UBS_OK;
    }

    // 如果为 BONDING_ROUTE 策略，则接下来尝试这条路径
    umq_conn_info_.conn_eid = other_route_message_.other_route.dst_eid;
    umq_conn_info_.peer_eid = other_route_message_.other_route.src_eid;
    if (umqSocket->CheckDevAdd(umq_conn_info_.conn_eid) != 0) {
        UBS_VLOG_ERR("Failed to add dev in retry accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
        ack_ret = UBS_UB_DEV_ERROR | UBS_DEGRADABLE_MASK;
    } else {
        ack_ret = UBS_OK;
    }

    std::vector<umq_port_id_t> used_port_vector;
    if (topo_type_ == UMQ_TOPO_TYPE_CLOS && UmqSetting::UMQ_IS_BONDING) {
        used_port_vector = {other_route_message_.other_route.src_port, other_route_message_.other_back_route.src_port};
    } else if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D && UmqSetting::UMQ_IS_BONDING) {
        used_port_vector = {other_route_message_.other_route.src_port};
    } else {
        used_port_vector = {};
    }
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    UBS_VLOG_DEBUG("DoAccept down to back, main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_route_message_.other_route.src_port.bs.chip_id,
                   other_route_message_.other_route.src_port.bs.die_id,
                   other_route_message_.other_route.src_port.bs.port_idx);

    UBS_VLOG_DEBUG("DoAccept down to back, back route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_route_message_.other_back_route.src_port.bs.chip_id,
                   other_route_message_.other_back_route.src_port.bs.die_id,
                   other_route_message_.other_back_route.src_port.bs.port_idx);

    // 保留在 CheckDevAdd 阶段时的错误
    ack_ret = ack_ret | DoUbAccept(socketPtr, used_ports);
    if (IsDegradable(ack_ret) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
        ack_ret = ack_ret - UBS_DEGRADABLE_MASK;
    }

    if (!IsOk(ack_ret)) {
        UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd);
    }

    if (SocketConnHelper::RecvSocketData(fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to recv peer ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, peer_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd, peer_ret);
        return UBS_TCP_EXCHANGE;
    }

    if (IsDegradable(peer_ret) && GlobalSetting::UBS_ENABLE_DEGRADE) {
        ack_ret |= UBS_DEGRADABLE_MASK;
    }
    if (SocketConnHelper::SendSocketData(fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ack_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), fd, ack_ret);
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

VersionCheckResult UmqAcceptorOps::ValidateVersion(int fd, uint32_t &negotiated_version, uint32_t &peer_version)
{
    // 1. 读version(4B) — 独立于NegotiateReq body
    if (SocketConnHelper::RecvSocketData(fd, &peer_version, sizeof(peer_version), NEGOTIATE_TIMEOUT_MS) !=
        sizeof(peer_version)) {
        UBS_VLOG_ERR("ValidateVersion: failed to recv version, fd: %d\n", fd);
        return VersionCheckResult::kRecvFailed;
    }

    // 2. 校验+协商：Major不一致返回kMajorMismatch，一致则计算negotiated_version
    uint32_t local_version = UBS_PROTOCOL_VERSION;
    return NegotiateVersion(local_version, peer_version, negotiated_version);
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
    UBS_VLOG_DEBUG("%s\n", msg.str().c_str());
}

Result UmqAcceptorOps::AcceptNegotiate(SocketPtr socketPtr)
{
    // 1. ValidateVersion: 读version(4B) + Major校验
    uint32_t negotiated_version = 0;
    uint32_t peer_version = 0;
    VersionCheckResult vc_result = ValidateVersion(fd, negotiated_version, peer_version);
    if (vc_result == VersionCheckResult::kMajorMismatch) {
        UBS_VLOG_WARN("Version major mismatch: peer=%u, local=%u, fd=%d, Peer IP:%s, fallback to TCP\n",
                      UBS_PROTOCOL_VERSION_MAJOR(peer_version), UBS_PROTOCOL_VERSION_MAJOR(UBS_PROTOCOL_VERSION), fd,
                      conn_info.peer_ip.c_str());
        uint32_t mismatch_version = UBS_PROTOCOL_VERSION;
        SocketConnHelper::SendSocketData(fd, &mismatch_version, sizeof(mismatch_version), CONTROL_PLANE_TIMEOUT_MS);
        uint32_t body_len = 0;
        SocketConnHelper::RecvSocketData(fd, &body_len, sizeof(body_len), CONTROL_PLANE_TIMEOUT_MS);
        std::vector<uint8_t> discard(body_len);
        SocketConnHelper::RecvSocketData(fd, discard.data(), body_len, CONTROL_PLANE_TIMEOUT_MS);
        return UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK;
    }
    if (vc_result == VersionCheckResult::kRecvFailed) {
        return UBS_TCP_EXCHANGE;
    }

    // 2. Minor/Patch差异适配 — 本次只记录，不做具体操作

    // 3. 读取NegotiateReq body — length-prefixed
    NegotiateReq req{};
    if (SocketConnHelper::RecvLengthPrefixed(fd, &req, sizeof(req), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive negotiate request in accept, fd: %d\n", fd);
        return UBS_ERROR;
    }

    // 3. 发送negotiated_version(4B) — 独立于NegotiateRsp body
    if (SocketConnHelper::SendSocketData(fd, &negotiated_version, sizeof(negotiated_version),
                                         CONTROL_PLANE_TIMEOUT_MS) != sizeof(negotiated_version)) {
        UBS_VLOG_ERR("Failed to send negotiated version in accept, fd: %d\n", fd);
        return UBS_ERROR;
    }

    // UB 传输模式优先级协商，值越小优先级越高。例如当服务端为 RM_TP 而客户端是 RC_TP 会协商至 RC_TP.
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    auto local_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    umqSocket->SetTransMode(std::min(req.trans_mode, local_trans_mode));

    NegotiateRsp rsp{};
    // 本端、对端必须同时启用/关闭 bonding，否则建链失败
    rsp.ret_code = (UmqSetting::UMQ_IS_BONDING == (req.is_bonding != 0)) ? 0 : -1;
    rsp.local_eid = UmqSetting::UMQ_LOCAL_EID;
    if (UNLIKELY(rsp.ret_code != 0)) {
        UBS_VLOG_ERR("client bonding mode is not equal to server bonding mode, client:%d, server:%d\n", req.is_bonding,
                     UmqSetting::UMQ_IS_BONDING);
    }
    if (UNLIKELY(rsp.ret_code != 0 || req.is_bonding == 0)) {
        // 发送negotiated_version后立即发Rsp body — length-prefixed
        if (SocketConnHelper::SendLengthPrefixed(fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) < 0) {
            UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
            return UBS_ERROR;
        }
    }

    BuildNegotiateRsp(rsp);
    // 4. 发送NegotiateRsp body — length-prefixed
    if (SocketConnHelper::SendLengthPrefixed(fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
        return UBS_ERROR;
    }

    // 5. 存储版本信息
    umqSocket->SetNegotiatedVersion(negotiated_version);
    umqSocket->SetPeerVersion(peer_version);

    // 在选择裸设备通信时，不需要再选路
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::RAW_DEVICE) {
        umq_conn_info_.conn_eid = UmqSetting::UMQ_LOCAL_EID;
        umq_conn_info_.peer_eid = req.local_eid;
        umq_conn_info_.bonding_eid = UmqSetting::UMQ_LOCAL_EID;
        umq_conn_info_.peer_bonding_eid = req.local_eid;
        return UBS_OK;
    }

    NegotiateRoute negoRoute;
    if (SocketConnHelper::RecvLengthPrefixed(fd, &negoRoute, sizeof(negoRoute), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive negotiate route in accept, Peer IP:%s, fd: %d\n", conn_info.peer_ip.c_str(),
                     fd);
        return UBS_ERROR;
    }
    conn_route_ = negoRoute.master_route;
    // 视角翻转：client 发来的 master_route 是 client 视角，server 需要把 src/dst 互换
    // client 视角的 src_port = server 视角的 dst_port
    // client 视角的 dst_port = server 视角的 src_port
    {
        umq_route_t swapped = conn_route_;
        conn_route_.src_port = swapped.dst_port;
        conn_route_.dst_port = swapped.src_port;
        conn_route_.src_eid = swapped.dst_eid;
        conn_route_.dst_eid = swapped.src_eid;
    }
    // 适配一主三备：从 back_routes[] 数组读取所有备路
    back_routes_.clear();
    for (uint32_t i = 0; i < negoRoute.back_route_num; ++i) {
        // 备路组同样需要视角翻转
        umq_route_t br = negoRoute.back_routes[i];
        umq_route_t br_swapped;
        br_swapped.src_port = br.dst_port;
        br_swapped.dst_port = br.src_port;
        br_swapped.src_eid = br.dst_eid;
        br_swapped.dst_eid = br.src_eid;
        back_routes_.push_back(br_swapped);
    }
    topo_type_ = negoRoute.topo_type;

    // 日志：打印接收到的 NegotiateRoute 内容，验证一主三备
    UBS_VLOG_DEBUG("AcceptNegotiate: received back_route_num=%u\n", negoRoute.back_route_num);
    UBS_VLOG_DEBUG("  master_route: src_port(chip=%u,die=%u,port=%u) dst_port(chip=%u,die=%u,port=%u)\n",
                   conn_route_.src_port.bs.chip_id, conn_route_.src_port.bs.die_id, conn_route_.src_port.bs.port_idx,
                   conn_route_.dst_port.bs.chip_id, conn_route_.dst_port.bs.die_id, conn_route_.dst_port.bs.port_idx);
    for (size_t i = 0; i < back_routes_.size(); ++i) {
        UBS_VLOG_DEBUG("  back_routes_[%zu]: src_port(chip=%u,die=%u,port=%u) dst_port(chip=%u,die=%u,port=%u)\n", i,
                       back_routes_[i].src_port.bs.chip_id, back_routes_[i].src_port.bs.die_id,
                       back_routes_[i].src_port.bs.port_idx, back_routes_[i].dst_port.bs.chip_id,
                       back_routes_[i].dst_port.bs.die_id, back_routes_[i].dst_port.bs.port_idx);
    }

    umq_conn_info_.conn_eid = conn_route_.src_eid;
    umq_conn_info_.peer_eid = conn_route_.dst_eid;
    umq_conn_info_.peer_bonding_eid = req.local_eid;
    umq_conn_info_.bonding_eid = UmqSetting::UMQ_LOCAL_EID;
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
    UBS_VLOG_DEBUG("%s\n", msg.str().c_str());
    return UBS_OK;
}
} // namespace umq
} // namespace ubs
} // namespace ock
