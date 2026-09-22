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

#include "common/ubsocket_link_trace.h"
#include <cstddef>
#include "umq_socket_acceptor.h"

#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_version.h"
#include "core/umq/umq_eid_table.h"
#include "core/umq/umq_backend.h"
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
            char peer_ip_buf[INET6_ADDRSTRLEN];
            UBS_VLOG_ERR("Failed to negotiate in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                         EID_ARGS(umq_conn_info_.peer_eid),
                         socketPtr->PeerIpStr(peer_ip_buf, sizeof(peer_ip_buf)), fd);
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
                     EID_ARGS(conn_eid), umq_conn_info_.peer_ip, sk->Fd());
        return UBS_UMQ_ERROR;
    }

    return UBS_OK;
}

/* 交叉 ack 依赖 RX/EPOLL_ADD 交接兜底（RX_RESCUE，UmqSocket::SetAddedEpollFd）：
 * 缺失该修复的分支上启用交叉 ack 会出现静默丢包窗口，用存在性宏在编译期拦截。 */
#ifndef UBS_HAS_RX_EPOLL_ADD_RESCUE
#error "crossed ack (UBSOCKET_EARLY_ACK) requires the RX/EPOLL_ADD rescue (RX_RESCUE)"
#endif

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
                /* 方案A' 已在协商应答前完成路检与创建，本轮免重复路检 */
                ackRet = early_prepared_ ? UBS_OK : CheckRouteDevAddForAccept(umq_conn_info_.conn_eid, umq_sk);

                std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();

                umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                               .num = static_cast<uint8_t>(used_port_vector.size())};
                const bool ack_carried = early_bound_;
                const bool ack_deferred = early_ack_deferred_;
                early_bound_ = false; /* 方案B 一次性消费：重试轮走全新经典路径 */
                early_ack_deferred_ = false;
                if (ack_carried) {
                    /* 方案B：create+bind 已在协商应答前完成，腿⑥ 已随应答送出 */
                    ackRet = early_ack_ret_;
                } else if (IsOk(ackRet)) {
                    ackRet = DoUbAccept(socketPtr, used_ports, early_prepared_);
                }
                early_prepared_ = false; // 一次性消费：重试轮走全新经典路径

                if (IsDegradable(ackRet) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
                    ackRet = ackRet - UBS_DEGRADABLE_MASK;
                }
                if (!IsOk(ackRet)) {
                    UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                }

                if (ack_carried && ack_deferred) {
                    /* 并行 bind：应答未带 bind 结果——先发本端结果（交叉 ack，无回声），
                     * 再收对端 ack；两端都先发后收，链路上交叉，不会互等。 */
                    if (SocketConnHelper::SendSocketData(fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(ackRet)) {
                        UBS_VLOG_ERR("Failed to send deferred bind ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
                    if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
                    if (IsDegradable(peerRet) && GlobalSetting::UBS_ENABLE_DEGRADE) {
                        ackRet |= UBS_DEGRADABLE_MASK; // 仅影响本端 degradable_/状态机，与交叉 ack 等价
                    }
                } else if (ack_carried) {
                    /* 方案B：腿⑥ 已随协商应答送出——只收对端 ack（腿⑤） */
                    if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
                    if (IsDegradable(peerRet) && GlobalSetting::UBS_ENABLE_DEGRADE) {
                        ackRet |= UBS_DEGRADABLE_MASK;
                    }
                } else if (peer_early_ack_) {
                    /* 交叉 ack：先发本端结果、再收对端 ack —— 两条消息在链路上交叉（与
                     * CpMsg 轮同型），客户端在本地 bind 期间即可收到应答，关键路径上省去
                     * 一个 RTT 等待；本端 executor 线程的 ack 等待也相应缩短。
                     * 应答不含回声(echo)：降级共识已在协商轮以 NEGO_CAP_DEGRADE_CONSENT
                     * 下发，客户端自行合成；收到 peerRet 后仍按原逻辑并入 ackRet，本端
                     * 状态机语义与经典路径完全一致。 */
                    if (SocketConnHelper::SendSocketData(fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(ackRet)) {
                        UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
                    if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
                    if (IsDegradable(peerRet) && GlobalSetting::UBS_ENABLE_DEGRADE) {
                        ackRet |= UBS_DEGRADABLE_MASK; // 仅影响本端 degradable_/状态机，与经典路径等价
                    }
                } else {
                    if (SocketConnHelper::RecvSocketData(fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }

                    if (IsDegradable(peerRet) && GlobalSetting::UBS_ENABLE_DEGRADE) {
                        ackRet |= UBS_DEGRADABLE_MASK;
                    }
                    if (SocketConnHelper::SendSocketData(fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(ackRet)) {
                        UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                        return UBS_TCP_EXCHANGE;
                    }
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
                                   EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
                    break;
                } else {
                    UBS_VLOG_ERR("Failed to retry accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d, err: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd, ret);
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
                             EID_ARGS(umq_conn_info_.conn_eid), umq_conn_info_.peer_ip, fd);
                return UBS_UB_ACCEPT;
            }
        }
    }
    return UBS_OK;
}

Result UmqAcceptorOps::DoUbAccept(SocketPtr socketPtr, umq_used_ports_t &used_ports, bool early_prepared)
{
    Result ret = UBS_OK;
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);

    /* 方案A'：创建/装配/bind_info 提取/CpMsg 发送已在协商应答前完成（见
     * PrepareLocalUmqEarly——bind_info 已随 NegotiateRsp 尾部送达对端） */
    if (!early_prepared) {
        // - 人工选路，使用真正的 port eid.
        // - 裸设备、bonding 设备对外均可直接使用一开始由 devname 找到的 eid.
        const umq_eid_t eid = GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_ROUTE ?
                                  umq_conn_info_.conn_eid :
                                  UmqSetting::UMQ_LOCAL_EID;
        ret = umqSocket->CreateLocalUmq(&eid, used_ports, topo_type_);

        // 校验 bind 是否成功
        if (ret != UBS_OK) {
            UBS_VLOG_ERR("Failed to create umq\n");
            return ret;
        }
        ret = SocketBase::GenerateSocketCommOps(socketPtr);
        if (ret != UBS_OK) {
            UBS_VLOG_ERR("Failed to generate socket comm ops\n");
            /* umq 已建成（id 已占）而数据面装配失败：当场销毁，不等 socket 析构（issue #49） */
            umqSocket->DestroyLocalUmq();
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
    }

    if (SocketConnHelper::RecvLengthPrefixed(fd, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive remote control message, fd: %d\n", fd);
        return UBS_ERROR;
    }
    /* 串台防御：CpMsg 槽位可能读到错位的其它控制帧（短帧被零填充、或裸 ack 污染后的
     * 残流）。魔数不符即控制面已错位，立即失败，绝不把垃圾长度喂给 umq_bind */
    if (remote_cp_msg.protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        UBS_VLOG_ERR("Receive misaligned control message, magic: 0x%llx, fd: %d\n",
                     static_cast<unsigned long long>(remote_cp_msg.protocol_negotiation), fd);
        return UBS_ERROR;
    }
    if (remote_cp_msg.queue_bind_info_size == 0 || remote_cp_msg.queue_bind_info_size > UMQ_BIND_INFO_SIZE_MAX) {
        UBS_VLOG_ERR("Receive remote invalid control message, bind info len: %lu, fd: %d\n",
                     static_cast<unsigned long>(remote_cp_msg.queue_bind_info_size), fd);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg size: %zu, bind info len: %lu\n", fd,
                   sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    return BindPeerAndFinalize(socketPtr, used_ports, remote_cp_msg.queue_bind_info,
                               remote_cp_msg.queue_bind_info_size);
}


void UmqAcceptorOps::BindPeerEarly(SocketPtr socketPtr, NegotiateReqExt &req_ext)
{
    std::vector<umq_port_id_t> early_ports = UmqBackend::GetUsedPorts();
    umq_used_ports_t early_used = {.port = early_ports.data(), .num = static_cast<uint8_t>(early_ports.size())};
    early_ack_ret_ = BindPeerAndFinalize(socketPtr, early_used, req_ext.bind_info, req_ext.bind_info_size);
    if (IsDegradable(early_ack_ret_) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
        early_ack_ret_ = early_ack_ret_ - UBS_DEGRADABLE_MASK;
    }
    early_bound_ = true;
}

/* DoUbAccept 的后半段：对端 bind_info 就绪后的 cooldown 校验 + umq_bind + prefill/
 * 注册收尾。方案B 在协商应答前提前调用（req 携带对端 bind_info）；经典/方案A'
 * 路径由 DoUbAccept 在收到 CpMsg 后调用。 */
Result UmqAcceptorOps::BindPeerAndFinalize(SocketPtr socketPtr, umq_used_ports_t &used_ports, uint8_t *info,
                                           uint64_t info_len)
{
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    // 光组网下会一次性使用所有 port，如果它出现在 cooldown 表中，则表示所有路径
    // 均已尝试过，无需再重试，可直接降级至 TCP.
    if (topo_type_ == UMQ_TOPO_TYPE_CLOS) {
        for (uint8_t i = 0; i < used_ports.num; ++i) {
            const auto &p = used_ports.port[i].bs;
            if (PortCooldownManager::IsPortInCooldown(used_ports.port[i])) {
                UBS_VLOG_WARN("used_ports[%u]: src_port(chip=%u,die=%u,port=%u) is down, skipped. Peer eid: " EID_FMT
                              ", Peer IP: %s, fd: %d\n",
                              i, p.chip_id, p.die_id, p.port_idx, EID_ARGS(umq_conn_info_.peer_eid),
                              umq_conn_info_.peer_ip, fd);
                return UBS_UMQ_BIND | UBS_DEGRADABLE_MASK;
            }
        }
    }

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    PROF_START(UMQ_BIND);
    int umq_ret =
        UmqApi::umq_bind(umqSocket->UmqHandle(), info, info_len);
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
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        umq_info_t umq_info{};
        auto ret = umq_info_get(umqSocket->UmqHandle(), &umq_info);
        UBS_VLOG_INFO("UB connection has been successfully established new fd: %d, umq id: %u \n", fd,
                       umq_info.ub.umq_id);
    }
    return UBS_OK;
}

Result UmqAcceptorOps::DoUbAcceptRetry(SocketPtr socketPtr, Result &ack_ret, Result &peer_ret)
{
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    if (peer_schedule_policy_ == dev_schedule_policy::CPU_AFFINITY) {
        UBS_VLOG_ERR("CPU_AFFINITY: %d failed, accept no need to retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     static_cast<int>(peer_schedule_policy_), EID_ARGS(umq_conn_info_.peer_eid),
                     umq_conn_info_.peer_ip, fd);
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

    std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();

    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};

    ack_ret = DoUbAccept(socketPtr, used_ports);
    if (IsDegradable(ack_ret) && !GlobalSetting::UBS_ENABLE_DEGRADE) {
        ack_ret = ack_ret - UBS_DEGRADABLE_MASK;
    }

    if (!IsOk(ack_ret)) {
        UBS_VLOG_ERR("Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd);
    }

    if (SocketConnHelper::RecvSocketData(fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to recv peer ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, peer_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd, peer_ret);
        return UBS_TCP_EXCHANGE;
    }

    if (IsDegradable(peer_ret) && GlobalSetting::UBS_ENABLE_DEGRADE) {
        ack_ret |= UBS_DEGRADABLE_MASK;
    }
    if (SocketConnHelper::SendSocketData(fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ack_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, fd, ack_ret);
        return UBS_TCP_EXCHANGE;
    }

    // 保留 kSTART 阶段的 degradable_ 标志：路由逻辑移除后无备路可选，重试使用同一路由。
    // 若重试在 CreateLocalUmq/控制信令交换等环节失败（非可降级错误码），不应丢失初始尝试
    // 的可降级判定，否则会导致故障降级 TCP 失效。
    degradable_ = degradable_ || IsDegradable(ack_ret);
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
        char peer_ip_buf[INET6_ADDRSTRLEN] = {0};
        struct sockaddr_storage peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&peer_addr), &peer_len) == 0) {
            SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<struct sockaddr *>(&peer_addr), peer_ip_buf,
                                                     sizeof(peer_ip_buf));
        }
        UBS_VLOG_WARN("Validate protocol failed, Peer IP:%s, fd: %d, ret: %zd\n", peer_ip_buf, fd,
                      protocol_negotiation_recv_size);
        return -1;
    }
    if (protocol_negotiation_recv_size != sizeof(protocol_negotiation) ||
        protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        char peer_ip_buf[INET6_ADDRSTRLEN] = {0};
        struct sockaddr_storage peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&peer_addr), &peer_len) == 0) {
            SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<struct sockaddr *>(&peer_addr), peer_ip_buf,
                                                     sizeof(peer_ip_buf));
        }
        UBS_VLOG_WARN("Validate protocol mismatch, Peer IP:%s, fd: %d, ret: %zd\n", peer_ip_buf, fd,
                      protocol_negotiation_recv_size);
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
    UBSVersion peer(peer_version);
    UBSVersion negotiated;
    VersionCheckResult result = UBS_PROTOCOL_VERSION.Negotiate(peer, negotiated);
    negotiated_version = negotiated.GetWhole();
    return result;
}

Result UmqAcceptorOps::FillLocalSocketIdsForNegotiate(uint32_t *socket_ids, uint32_t &socket_id_count)
{
    std::vector<uint32_t> ids = UmqSetting::UMQ_ALL_SOCKET_IDS;
    if (ids.empty() || ids.size() > NEGOTIATE_SOCKET_ID_MAX_NUM) {
        UBS_VLOG_ERR("Invalid local socket ids, size %zu, Peer IP:%s\n", ids.size(), umq_conn_info_.peer_ip);
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

Result UmqAcceptorOps::PrepareLocalUmqEarly(SocketPtr socketPtr, NegotiateRspExt &ext)
{
    auto umq_sk = RefStaticCast<UmqSocket>(socketPtr);
    Result ret = CheckRouteDevAddForAccept(umq_conn_info_.conn_eid, umq_sk);
    if (!IsOk(ret)) {
        return ret;
    }
    std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    auto umqSocket = RefConvert<Socket, UmqSocket>(socketPtr);
    const umq_eid_t eid = GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_ROUTE ?
                              umq_conn_info_.conn_eid :
                              UmqSetting::UMQ_LOCAL_EID;
    ret = umqSocket->CreateLocalUmq(&eid, used_ports, topo_type_);
    UBS_LINK_TRACE(fd, "S_UMQ_CREATED", "rc=%d pre=1", ret);
    if (ret != UBS_OK) {
        return ret;
    }
    if (SocketBase::GenerateSocketCommOps(socketPtr) != UBS_OK) {
        UBS_VLOG_ERR("Failed to generate comm ops (early), fd: %d\n", fd);
        umqSocket->DestroyLocalUmq();
        return UBS_ERROR;
    }
    ext.bind_info_size = UmqApi::umq_bind_info_get(umqSocket->UmqHandle(), ext.bind_info, sizeof(ext.bind_info));
    if (ext.bind_info_size == 0) {
        UBS_VLOG_ERR("umq_bind_info_get() failed (early), fd: %d\n", fd);
        umqSocket->DestroyLocalUmq();
        return UBS_UMQ_BIND_INFO_GET;
    }
    return UBS_OK;
}

Result UmqAcceptorOps::AcceptNegotiate(SocketPtr socketPtr)
{
    // 1. ValidateVersion: 读version(4B) + Major校验
    uint32_t negotiated_version = 0;
    uint32_t peer_version = 0;
    VersionCheckResult vc_result = ValidateVersion(fd, negotiated_version, peer_version);
    if (vc_result == VersionCheckResult::kMajorMismatch) {
        UBS_SLOG_WARN("Version major mismatch: peer=" << UBSVersion(peer_version) << " local=" << UBS_PROTOCOL_VERSION
                                                      << " fd=" << fd << " Peer IP:" << nego_peer_ip_
                                                      << " fallback to TCP");
        uint32_t mismatch_version = UBS_PROTOCOL_VERSION.GetWhole();
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
    /* 方案B：按扩展布局接收——新客户端尾部携带其 bind_info；老客户端短 body
     * 被零填充 ⇒ bind_info_size==0，自描述回退。 */
    NegotiateReqExt req_ext{};
    NegotiateReq &req = req_ext.req;
    if (SocketConnHelper::RecvLengthPrefixed(fd, &req_ext, sizeof(req_ext), CONTROL_PLANE_TIMEOUT_MS) < 0) {
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
    // 交叉 ack 能力协商：客户端声明支持时本端确认，并附带降级共识位
    //（老客户端的短 body 由 RecvLengthPrefixed 零填充 ⇒ cap_flags 恒 0，走经典路径）
    peer_early_ack_ = GlobalSetting::UBS_EARLY_ACK && ((req.cap_flags & NEGO_CAP_EARLY_ACK) != 0);
    if (peer_early_ack_) {
        rsp.reserved[0] |= NEGO_CAP_EARLY_ACK;
        if (GlobalSetting::UBS_ENABLE_DEGRADE) {
            rsp.reserved[0] |= NEGO_CAP_DEGRADE_CONSENT;
        }
    }
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
    } else {
        BuildNegotiateRsp(rsp);
        /* 方案A'：客户端声明可解析应答尾部 bind_info 时，提前完成路检+创建并把
         * bind_info 搭应答送出——服务端不再单独发 CpMsg，客户端免去对它的整轮
         * 等待。提前创建失败则不置能力位、发经典应答，后续 DoUbAccept 按原路径
         * 重新创建，语义零变化。 */
        peer_nego_carry_ =
            GlobalSetting::UBS_NEGO_CARRY_BINDINFO && ((req.cap_flags & NEGO_CAP_CARRY_BINDINFO) != 0);
        early_prepared_ = false;
        bool rsp_sent = false;
        /* 方案B：请求携带了客户端 bind_info、模式协商未降级（客户端按其提议模式
         * 预建）且开关开 ⇒ 应答前完成 create+bind，把 bind 结果（腿⑥）随应答
         * 送出；任一条件不满足则只走 方案A'（服务端 bind_info 随应答）或经典。 */
        const bool req_carried = GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO &&
                                 ((req.cap_flags & NEGO_CAP_REQ_CARRY_BINDINFO) != 0) &&
                                 req_ext.bind_info_size > 0 && req_ext.bind_info_size <= UMQ_BIND_INFO_SIZE_MAX &&
                                 umqSocket->GetTransMode() == req.trans_mode;
        early_bound_ = false;
        early_ack_ret_ = UBS_OK;
        early_ack_deferred_ = false;
        if (peer_nego_carry_) {
            NegotiateRspExt ext{};
            if (IsOk(PrepareLocalUmqEarly(socketPtr, ext))) {
                ext.rsp = rsp;
                ext.rsp.reserved[0] |= NEGO_CAP_CARRY_BINDINFO;
                early_prepared_ = true;
                /* 并行 bind：客户端声明可在末尾 ack 轮收 bind 结果且开关开 ⇒ 先发应答
                 * （只带本端 bind_info），应答在路上时客户端即开始它的 bind，本端随后
                 * 做自己的 bind——两端的 umq_bind 重新并行，结果在 kSTART 以交叉 ack
                 * 送出。否则按 方案B：应答前 bind，结果随应答。 */
                const bool defer_bind = req_carried && GlobalSetting::UBS_NEGO_PARALLEL_BIND &&
                                        ((req.cap_flags & NEGO_CAP_DEFER_BIND_RET) != 0);
                if (req_carried) {
                    ext.rsp.reserved[0] |= NEGO_CAP_REQ_CARRY_BINDINFO;
                    if (GlobalSetting::UBS_ENABLE_DEGRADE) {
                        ext.rsp.reserved[0] |= NEGO_CAP_DEGRADE_CONSENT;
                    }
                    if (defer_bind) {
                        ext.rsp.reserved[0] |= NEGO_CAP_DEFER_BIND_RET;
                    } else {
                        BindPeerEarly(socketPtr, req_ext);
                        ext.server_bind_ret = static_cast<int32_t>(early_ack_ret_);
                    }
                }
                const uint32_t wire_len =
                    static_cast<uint32_t>(offsetof(NegotiateRspExt, bind_info) + ext.bind_info_size);
                if (SocketConnHelper::SendLengthPrefixed(fd, &ext, wire_len, CONTROL_PLANE_TIMEOUT_MS) < 0) {
                    UBS_VLOG_ERR("Failed to send negotiate response(ext) in accept, fd: %d\n", fd);
                    return UBS_ERROR;
                }
                if (defer_bind) {
                    UBS_LINK_TRACE(fd, "S_CPMSG_SENT", "carried=1 deferred=1");
                } else {
                    UBS_LINK_TRACE(fd, "S_CPMSG_SENT", "carried=1");
                }
                rsp_sent = true;
                if (defer_bind) {
                    BindPeerEarly(socketPtr, req_ext);
                    early_ack_deferred_ = true;
                }
            }
        }
        // 4. 发送NegotiateRsp body — length-prefixed
        if (!rsp_sent && SocketConnHelper::SendLengthPrefixed(fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) < 0) {
            UBS_VLOG_ERR("Failed to send negotiate response in accept, fd: %d\n", fd);
            return UBS_ERROR;
        }
    }

    // 5. 存储版本信息
    umqSocket->SetNegotiatedVersion(negotiated_version);
    umqSocket->SetPeerVersion(peer_version);
    /* design §4.2: store peer RPC timeout for receiver ctx deadline and sender
     * pinned deadline (effective_timeout = local!=0 ? local : peer). */
    umqSocket->SetPeerRpcTimeoutMs(req.rpc_timeout_ms);

    topo_type_ = UmqBackend::GetTopoType();
    // 在选择裸设备通信时，不需要再选路
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::RAW_DEVICE) {
        umq_conn_info_.conn_eid = UmqSetting::UMQ_LOCAL_EID;
        umq_conn_info_.peer_eid = req.local_eid;
        umq_conn_info_.bonding_eid = UmqSetting::UMQ_LOCAL_EID;
        umq_conn_info_.peer_bonding_eid = req.local_eid;
        return UBS_OK;
    }

    // 使用umq_backend缓存的used_ports，取消Recv Negotiate环节
    // 复用bonding eid
    umq_conn_info_.conn_eid = UmqSetting::UMQ_LOCAL_EID;
    umq_conn_info_.peer_eid = req.local_eid;
    umq_conn_info_.peer_bonding_eid = req.local_eid;
    umq_conn_info_.bonding_eid = UmqSetting::UMQ_LOCAL_EID;
    return rsp.ret_code == 0 ? 0 : -1;
}

Result UmqAcceptorOps::AcceptExchangeSocketIDs(const SocketPtr &socketPtr, int fd)
{
    // 发送本端的all socket ids
    std::vector<uint32_t> sendAllSocketIds = UmqSetting::UMQ_ALL_SOCKET_IDS;
    uint32_t count = static_cast<uint32_t>(sendAllSocketIds.size());
    size_t dataSize = count * sizeof(uint32_t);

    char peer_ip_buf[INET6_ADDRSTRLEN];
    if (SocketConnHelper::SendSocketData(fd, &count, sizeof(count), CONTROL_PLANE_TIMEOUT_MS) != sizeof(count)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n",
                     socketPtr->PeerIpStr(peer_ip_buf, sizeof(peer_ip_buf)), fd);
        return UBS_ERROR;
    }
    if (SocketConnHelper::SendSocketData(fd, sendAllSocketIds.data(), dataSize, CONTROL_PLANE_TIMEOUT_MS) !=
        static_cast<ssize_t>(dataSize)) {
        UBS_VLOG_ERR("Failed to send local all socket ids in accept,Peer IP:%s, fd: %d\n",
                     socketPtr->PeerIpStr(peer_ip_buf, sizeof(peer_ip_buf)), fd);
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
