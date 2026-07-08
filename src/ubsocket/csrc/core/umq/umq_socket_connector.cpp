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
#include <random>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_scope_exit.h"
#include "common/ubsocket_version.h"
#include "core/umq/umq_eid_table.h"
#include "umq/include/umq/umq_dfx_types.h"
#include "umq_conn_helper.h"
#include "umq_errno_converter.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqConnectorOps::ConnectViaHandshakeOpt(const SocketPtr &sock, const struct sockaddr *address,
                                               socklen_t address_len)
{
    int opt = 1;
    int ret = LibcApi::setsockopt(raw_fd_, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, sizeof(opt));
    if (ret < 0 && (errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        UBS_VLOG_WARN("UB handshake socket option not supported. Handshake mode fallback to TFO.\n");
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
        ret = ConnectViaTfo(sock, address, address_len);
    } else {
        ret = LibcApi::connect(raw_fd_, address, address_len);
        UBS_VLOG_DEBUG("Connect with UB handshake socket option.\n");
    }
    return ret;
}

Result UmqConnectorOps::ConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);

    uint8_t send_buf[NEGOTIATE_REQ_BUFFER_SIZE];
    int buf_len = 0;
    if (BuildNegotiateReqBuffer(send_buf, umq_socket, buf_len) != UBS_OK) {
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
        UBS_VLOG_DEBUG("TFO Cookie not found or not used. Retrying for immediate SYN+Data.\n");
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
        UBS_VLOG_DEBUG("TFO Cookie exists, continue...\n");
    }
    return sendto_ret < 0 ? UBS_ERROR : UBS_OK;
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
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED || !SocketConnHelper::IsUbsConnection(new_fd)) {
        return ret;
    }

    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        // 多打一和多打多的trace需要关联socket之间的关系
        struct sockaddr_storage local_addr;
        socklen_t local_addr_len = sizeof(local_addr);
        if (getsockname(raw_fd_, (struct sockaddr *)&local_addr, &local_addr_len) == 0) {
            std::string local_ip = SocketConnHelper::ExtractIpFromSockAddr((struct sockaddr *)&local_addr);
            int local_port = SocketConnHelper::ExtractPortFromSockAddr((struct sockaddr *)&local_addr);

            UBS_VLOG_INFO("tcp connect, local ip %s port %d, peer ip %s port %d, fd %d\n", local_ip.c_str(), local_port,
                          umq_conn_info_.peer_ip.c_str(), SocketConnHelper::ExtractPortFromSockAddr(address), new_fd);
        }
    }
    if (ret == UBS_OK) {
        UBS_VLOG_DEBUG("tcp connect succeed, ip %s port %d fd %d\n", umq_conn_info_.peer_ip.c_str(),
                       SocketConnHelper::ExtractPortFromSockAddr(address), new_fd);

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
            // 内核选项建链场景：fd是非阻塞套接字，调用connect返回-1，errno为EINPROGRESS，网络正在建连，需修正ret，确保后续正常协商
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

Result UmqConnectorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    Result ret = ConnectNegotiate(umq_socket);
    if (!IsOk(ret) && !IsDegradable(ret)) {
        UBS_VLOG_ERR("Failed to negotiate in connect,Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip.c_str(), new_fd);
    }
    return ret;
};

Result UmqConnectorOps::CreateSocketResources(const SocketPtr &sock)
{
    /**
     * 1. 用户直接指定普通设备建链，不重试、可降级
     * 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，不重试、可降级
     * 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误，优先重试，如果重试仍旧失败则降级
     */
    bool ok = false;
    Result ack_ret = UBS_OK;
    Result peer_ret = UBS_OK;
    // status reset
    degradable_ = false;
    retry_state_ = UBHandshakeState::kSTART;
    other_route_message_ = {};
    other_conn_route = {};
    other_back_conn_route = {};

    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    while (!ok) {
        switch (retry_state_) {
            case UBHandshakeState::kOK: {
                ok = true;
                break;
            }
            case UBHandshakeState::kSTART: {
                // 作为客户端，它的 Degradable 属性对于是否降级不生效. Degradable 仅当角色为服务端时生效
                ack_ret = CheckRouteDevAddForConnect(umq_conn_info_.conn_eid, umq_socket);

                std::vector<umq_port_id_t> used_port_vector;
                if (topo_type_ == UMQ_TOPO_TYPE_CLOS && UmqSetting::UMQ_IS_BONDING) {
                    used_port_vector.push_back(conn_route_.src_port);
                    for (const auto &br : back_routes_) {
                        used_port_vector.push_back(br.src_port);
                    }
                    UBS_VLOG_DEBUG("CreateSocketResources: used_ports num=%zu (1 main + %zu backup)\n",
                                   used_port_vector.size(), back_routes_.size());
                } else if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D && UmqSetting::UMQ_IS_BONDING) {
                    used_port_vector = {conn_route_.src_port};
                } else {
                    used_port_vector = {};
                }

                umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                               .num = static_cast<uint8_t>(used_port_vector.size())};
                if (ack_ret == UBS_OK) {
                    ack_ret = DoUbConnect(umq_socket, used_ports);
                }
                if (ack_ret != UBS_OK) {
                    UBS_VLOG_ERR("Failed to finish ub bind in connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                }
                if (SocketConnHelper::SendSocketData(raw_fd_, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(ack_ret)) {
                    UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                    return UBS_TCP_EXCHANGE;
                }

                if (SocketConnHelper::RecvSocketData(raw_fd_, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(peer_ret)) {
                    UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                    return UBS_TCP_EXCHANGE;
                }

                // 如果服务端支持降级则客户端需要配合
                degradable_ = IsDegradable(peer_ret);
                if (IsOk(ack_ret) && IsOk(peer_ret)) {
                    retry_state_ = UBHandshakeState::kOK;
                } else if ((IsRetryable(ack_ret) || IsRetryable(peer_ret)) &&
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
                auto ret = DoUbConnectRetry(sock, ack_ret, peer_ret);
                if (ret == UBS_OK) {
                    UBS_VLOG_DEBUG("Success to retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                   EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                    break;
                } else {
                    UBS_VLOG_ERR("Failed to retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d, err:%d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, ret);
                    return ret;
                }
            }
            case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE: {
                // 客户端在 kRETRY 错误时会进入 kRETRY_FAILED_CHECK_OTHER_ROUTE，但是服务端仍处于 kRETRY 阶段，
                // 需要发送信令通知服务端，此种情况下 other_route 字段不可用
                other_route_message_.ub_handshake_state = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
                if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &other_route_message_, sizeof(other_route_message_),
                                                         CONTROL_PLANE_TIMEOUT_MS) < 0) {
                    UBS_VLOG_ERR("Failed to send connect eid message in retry connect,Peer eid:" EID_FMT
                                 ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                }

                if (degradable_) {
                    retry_state_ = UBHandshakeState::kDEGRADE;
                } else {
                    retry_state_ = UBHandshakeState::kFAILED;
                }
                break;
            }

            case UBHandshakeState::kDEGRADE: {
                ArraySet<Socket>::GetInstance().OverrideItem(raw_fd_, nullptr);
                UBS_VLOG_INFO("ubsocket is degraded to TCP.\n");
                return UBS_OK;
            }

            case UBHandshakeState::kFAILED: {
                UBS_VLOG_ERR("Failed to get new connect in connect, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                             EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
                return UBS_CONN_RETRY_FAILED;
            }
        }
    }

    umq_conn_info_.create_time = std::chrono::system_clock::now();
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        umq_info_t umq_info{};
        auto ret = umq_info_get(umq_socket->UmqHandle(), &umq_info);
        UBS_VLOG_INFO("UB connection has been successfully established new fd: %d, umq id: %u \n", raw_fd_,
                      umq_info.ub.umq_id);
        return UBS_OK;
    }
    UBS_VLOG_INFO("UB connection has been successfully established new fd: %d\n", raw_fd_);

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
    dev_schedule_policy schedulePolicy = UmqSetting::UMQ_DEV_SCHEDULE_POLICY;
    req->trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    req->is_bonding = UmqSetting::UMQ_IS_BONDING ? 1 : 0;
    req->enable_share_jfr = GlobalSetting::UBS_ENABLE_SHARE_JFR ? 1 : 0;
    req->schedule_policy = static_cast<uint8_t>(schedulePolicy);
    req->local_eid = localEid;
    return UBS_OK;
}

Result UmqConnectorOps::BuildNegotiateReqBuffer(uint8_t *buf, const UmqSocketPtr &umq_socket, int &buf_len)
{
    int offset = 0;

    uint64_t magic = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    memcpy(buf + offset, &magic, sizeof(magic));
    offset += sizeof(magic);

    uint32_t version = UBS_PROTOCOL_VERSION;
    memcpy(buf + offset, &version, sizeof(version));
    offset += sizeof(version);

    NegotiateReq req{};
    BuildNegotiateReq(&req, umq_socket);

    uint32_t body_len = static_cast<uint32_t>(sizeof(req));
    memcpy(buf + offset, &body_len, sizeof(body_len));
    offset += sizeof(body_len);
    memcpy(buf + offset, &req, sizeof(req));
    offset += sizeof(req);

    buf_len = offset;
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
    UBS_VLOG_DEBUG("%s\n", oss.str().c_str());
}

Result UmqConnectorOps::ConnectNegotiate(const UmqSocketPtr &umq_socket)
{
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::UB_SOCK_OPT) {
        uint8_t send_buf[NEGOTIATE_REQ_BUFFER_SIZE];
        int buf_len = 0;
        if (BuildNegotiateReqBuffer(send_buf, umq_socket, buf_len) != UBS_OK) {
            return UBS_ERROR;
        }
        if (SocketConnHelper::SendSocketData(raw_fd_, send_buf, buf_len, CONTROL_PLANE_TIMEOUT_MS) != buf_len) {
            UBS_VLOG_ERR("Failed to send negotiate request, Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip.c_str(),
                         raw_fd_);
            return UBS_ERROR;
        }
    }
    // TFO模式: SYN包携带send_buf内容(见ConnectViaTfo，需同步改造)

    // 接收negotiated_version(4B) — 独立于Rsp body
    uint32_t negotiated_version = 0;
    if (SocketConnHelper::RecvSocketData(raw_fd_, &negotiated_version, sizeof(negotiated_version),
                                         CONTROL_PLANE_TIMEOUT_MS) != sizeof(negotiated_version)) {
        UBS_VLOG_ERR("Failed to receive negotiated version in connect, Peer IP:%s, fd: %d\n",
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }

    // 校验协商结果：Major必须一致
    VersionCheckResult vc_result = ValidateNegotiatedVersion(UBS_PROTOCOL_VERSION, negotiated_version);
    if (vc_result == VersionCheckResult::kMajorMismatch) {
        UBS_VLOG_WARN("Version major mismatch: negotiated=%u, local=%u, fd=%d, Peer IP:%s, fallback to TCP\n",
                      UBS_PROTOCOL_VERSION_MAJOR(negotiated_version), UBS_PROTOCOL_VERSION_MAJOR(UBS_PROTOCOL_VERSION),
                      raw_fd_, umq_conn_info_.peer_ip.c_str());
        return UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK;
    }

    // Minor/Patch差异 — 不一致时处理方式待确认
    if (UBS_PROTOCOL_VERSION_MINOR(negotiated_version) != UBS_PROTOCOL_VERSION_MINOR(UBS_PROTOCOL_VERSION)) {
        UBS_VLOG_DEBUG("Minor diff: negotiated=%u, local=%u\n", UBS_PROTOCOL_VERSION_MINOR(negotiated_version),
                       UBS_PROTOCOL_VERSION_MINOR(UBS_PROTOCOL_VERSION));
    }

    umq_socket->SetNegotiatedVersion(negotiated_version);

    // 接收NegotiateRsp body — length-prefixed
    NegotiateRsp rsp{};
    if (SocketConnHelper::RecvLengthPrefixed(raw_fd_, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive negotiate response in connect,Peer IP:%s, fd: %d\n",
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    if (rsp.ret_code != 0) {
        UBS_VLOG_ERR("Failed to negotiate in connect, peer ret %d, Peer IP:%s, fd: %d\n", rsp.ret_code,
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }

    // UB 传输模式优先级协商，值越小优先级越高。例如当服务端为 RM_TP 而客户端是 RC_TP 会协商至 RC_TP.
    ub_trans_mode local_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    umq_socket->SetTransMode(std::min(rsp.peer_trans_mode, local_trans_mode));

    const dev_schedule_policy schedule_policy = UmqSetting::UMQ_DEV_SCHEDULE_POLICY;
    if (schedule_policy == dev_schedule_policy::CPU_AFFINITY ||
        schedule_policy == dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
        UBS_VLOG_DEBUG("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                       static_cast<int>(schedule_policy), raw_fd_);
        use_round_robin_ = false;
    }

    const umq_eid_t local_eid = UmqSetting::UMQ_LOCAL_EID; // 客户端的 local_eid
    const umq_eid_t peer_eid = rsp.local_eid;              // 服务端的 local_eid

    // 在选择裸设备通信时，不需要再选路
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::RAW_DEVICE) {
        umq_conn_info_.conn_eid = local_eid;
        umq_conn_info_.peer_eid = peer_eid;
        return UBS_OK;
    }

    peer_socket_id_ = rsp.aff_sock_id;
    if (UNLIKELY(rsp.socket_id_count == 0 || (rsp.socket_id_count > NEGOTIATE_SOCKET_ID_MAX_NUM))) {
        UBS_VLOG_ERR("Invalid peer socket count, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }
    peer_all_socket_ids_.reserve(rsp.socket_id_count);
    for (size_t i = 0; i < rsp.socket_id_count; i++) {
        peer_all_socket_ids_.push_back(rsp.socket_ids[i]);
    }
    PrintSocketsInfo();

    // BONDING_BACKUP 或者 BONDING_ROUTE 策略都依赖 bonding 设备选路。只不过前者需要 ubsocket 来显式提供主
    // port、备 port. 而后者是直接通过选出的设备通信.
    // 此处 local_eid, peer_eid 保证必定是 bonding 设备的 eid.
    if (DoRoute(&local_eid, &peer_eid) != 0) {
        UBS_VLOG_ERR("Failed to get route list in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }

    // 日志：打印即将发送的 NegotiateRoute 内容
    UBS_VLOG_DEBUG("Send NegotiateRoute: topo_type=%u, back_route_num=%zu\n", topo_type_, back_routes_.size());
    UBS_VLOG_DEBUG("  master_route: src_port(chip=%u,die=%u,port=%u) dst_port(chip=%u,die=%u,port=%u)\n",
                   conn_route_.src_port.bs.chip_id, conn_route_.src_port.bs.die_id, conn_route_.src_port.bs.port_idx,
                   conn_route_.dst_port.bs.chip_id, conn_route_.dst_port.bs.die_id, conn_route_.dst_port.bs.port_idx);
    for (size_t i = 0; i < back_routes_.size(); ++i) {
        UBS_VLOG_DEBUG("  back_routes[%zu]: src_port(chip=%u,die=%u,port=%u) dst_port(chip=%u,die=%u,port=%u)\n", i,
                       back_routes_[i].src_port.bs.chip_id, back_routes_[i].src_port.bs.die_id,
                       back_routes_[i].src_port.bs.port_idx, back_routes_[i].dst_port.bs.chip_id,
                       back_routes_[i].dst_port.bs.die_id, back_routes_[i].dst_port.bs.port_idx);
    }

    NegotiateRoute negoRoute(topo_type_, conn_route_, back_routes_);
    if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &negoRoute, sizeof(negoRoute), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to send negotiate route info in connect, fd: %d\n", raw_fd_);
        return UBS_ERROR;
    }

    umq_conn_info_.conn_eid = conn_route_.src_eid;
    umq_conn_info_.peer_eid = conn_route_.dst_eid;
    umq_conn_info_.peer_bonding_eid = peer_eid;
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
    UBS_VLOG_DEBUG("Topo type's value is: %d\n", topo_type_);
    if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D) {
        if (GetConnEid(filtered_list, dst_eid) != 0) {
            UBS_VLOG_ERR("Failed to get connect eid\n");
            return UBS_ERROR;
        }
        // 电组网无备路，清空以防 CLOS→FULLMESH 切换时残留
        back_routes_.clear();
    } else {                                       //光组网
        std::vector<umq_route_t> main_routes;      //亲和组
        std::vector<umq_route_t> back_routes;      //不亲和组
        umq_route_t conn_main_route;               //主路
        std::vector<umq_route_t> conn_back_routes; //备路组（最多3条）

        int getAffinityRes = GetCpuAffinityUmqRoute(filtered_list, main_routes, back_routes);
        if (getAffinityRes != 0) {
            UBS_VLOG_ERR("Failed to get cpu affinity umq route\n");
            conn_route_ = umq_route_t{};
            back_routes_.clear();
            return UBS_ERROR;
        }
        // 在客户端侧把不亲数组存起来（用于降级，不消耗）
        non_aff_route_list_ = back_routes;

        // 一主三备：合并亲和组和不亲和组，统一RR轮询（不区分亲和/不亲和）
        std::vector<umq_route_t> all_routes = main_routes;
        all_routes.insert(all_routes.end(), back_routes.begin(), back_routes.end());

        // 日志：打印所有路由大小
        UBS_VLOG_DEBUG("DoRoute(CLOS): main_routes.size()=%zu, back_routes.size()=%zu, all_routes.size()=%zu\n",
                       main_routes.size(), back_routes.size(), all_routes.size());
        uint32_t main_route_size = all_routes.size();
        if (UmqSetting::UMQ_DEV_SCHEDULE_POLICY != dev_schedule_policy::ROUND_ROBIN) {
            main_route_size = main_routes.size();
        }
        RRChooseMainRoute(all_routes, main_route_size, dst_eid, conn_main_route, conn_back_routes);
        conn_route_ = conn_main_route;

        // 备路组：直接使用RR选择的备路（已包含亲和+不亲和的统一排序）
        back_routes_.clear();
        for (const auto &br : conn_back_routes) {
            back_routes_.push_back(br);
        }
    }
    return UBS_OK;
}

Result UmqConnectorOps::DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports)
{
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    Result ret;
    auto socket = RefConvert<UmqSocket, Socket>(umq_socket);

    // - 人工选路，使用真正的 port eid.
    // - 裸设备、bonding 设备对外均可直接使用一开始由 devname 找到的 eid.
    const umq_eid_t eid = GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_ROUTE ?
                              umq_conn_info_.conn_eid :
                              UmqSetting::UMQ_LOCAL_EID;
    ret = umq_socket->CreateLocalUmq(&eid, used_ports, topo_type_);

    if (ret != UBS_OK || SocketBase::GenerateSocketCommOps(socket) != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return ret;
    }

    PROF_START(UMQ_BIND_INFO_GET);
    local_cp_msg.queue_bind_info_size =
        UmqApi::umq_bind_info_get(umq_socket->UmqHandle(), local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
    if (local_cp_msg.queue_bind_info_size == 0) {
        PROF_END(UMQ_BIND_INFO_GET, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind_info_get() failed, Peer eid:" EID_FMT ",Peer IP:%s, "
                     "fd: %d, ret: %ld, mapped errno: %d(%s), original errno: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_,
                     local_cp_msg.queue_bind_info_size, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
        return UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    PROF_END(UMQ_BIND_INFO_GET, true);

    if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) <
        0) {
        UBS_VLOG_ERR("Failed to send local control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg size: %zu, bind info len: %lu", raw_fd_,
                   sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

    if (SocketConnHelper::RecvLengthPrefixed(raw_fd_, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) <
        0) {
        UBS_VLOG_ERR("Failed to receive remote control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    if (remote_cp_msg.queue_bind_info_size > UMQ_BIND_INFO_SIZE_MAX) {
        UBS_VLOG_ERR("Receive remote invalid control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_ERROR;
    }
    UBS_VLOG_DEBUG("recv remote control message, fd: %d, cp msg size: %zu, bind info len: %lu", raw_fd_,
                   sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

    // 光组网下会一次性使用所有 port，如果它出现在 cooldown 表中，则表示所有路径
    // 均已尝试过，无需再重试，可直接降级至 TCP.
    if (topo_type_ == UMQ_TOPO_TYPE_CLOS) {
        for (uint8_t i = 0; i < used_ports.num; ++i) {
            const auto &p = used_ports.port[i].bs;
            if (PortCooldownManager::IsPortInCooldown(used_ports.port[i])) {
                UBS_VLOG_WARN("used_ports[%u]: src_port(chip=%u,die=%u,port=%u) is down, skipped. Peer eid: " EID_FMT
                              ", Peer IP: %s, fd: %d\n",
                              i, p.chip_id, p.die_id, p.port_idx, EID_ARGS(umq_conn_info_.peer_bonding_eid),
                              umq_conn_info_.peer_ip.c_str(), raw_fd_);
                return UBS_UMQ_BIND | UBS_DEGRADABLE_MASK;
            }
        }
    }

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    PROF_START(UMQ_BIND);
    int umq_ret =
        UmqApi::umq_bind(umq_socket->UmqHandle(), remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size);
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    long long costms = (end_tv.tv_sec - start_tv.tv_sec) * 1000LL + (end_tv.tv_usec - start_tv.tv_usec) / 1000LL;

    if (umq_ret != 0) {
        PROF_END(UMQ_BIND, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, umq_ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_bind() failed, Peer eid:" EID_FMT
                     ",Peer IP:%s, fd: %d, ret: %d, mapped errno: %d(%s), "
                     "original errno: %d, operation duration: %lld ms.\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, umq_ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, umq_ret), savedErrno, costms);
        return UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    PROF_END(UMQ_BIND, true);
    UBS_VLOG_DEBUG("umq_bind success, ret: %d, operation duration: %lld ms.\n", umq_ret, costms);
    umq_socket->SetBindRemote(true);

    if (GlobalSetting::LINK_SELECTION_POLICY != LinkSelectionPolicy::BONDING_BACKUP) {
        // 强依赖当前实现，一个 eid 对应多 UB 传输模式不同的 umq. 如果后续逻辑有变更，需同步修改。
        auto main_umq = UmqEidTable::Instance().GetFirst(umq_conn_info_.conn_eid, umq_socket->GetTransMode());
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
    umq_socket->UpdateRxQueueAvailNum();
    return UBS_OK;
}

Result UmqConnectorOps::DoUbConnectRetry(SocketPtr socket_ptr, Result &ack_ret, Result &peer_ret)
{
    auto umq_socket = RefConvert<Socket, UmqSocket>(socket_ptr);
    // ub降级后检查other链路时，是否检查成功的ret值
    int checkOtherRet = 0;
    if (UmqSetting::UMQ_DEV_SCHEDULE_POLICY == dev_schedule_policy::CPU_AFFINITY) {
        UBS_VLOG_ERR("CPU_AFFINITY:%d failed, connect no need to retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     static_cast<int>(UmqSetting::UMQ_DEV_SCHEDULE_POLICY), EID_ARGS(umq_conn_info_.peer_eid),
                     umq_conn_info_.peer_ip.c_str(), raw_fd_);

        if (degradable_) {
            retry_state_ = UBHandshakeState::kDEGRADE;
        } else {
            retry_state_ = UBHandshakeState::kFAILED;
        }
        return UBS_OK;
    }
    umq_socket->UnbindAndFlushRemoteUmq(socket_ptr.Get());
    umq_socket->DestroyLocalUmq();

    if (topo_type_ == UMQ_TOPO_TYPE_CLOS) {
        checkOtherRet = CheckOtherRouteForClos(umq_socket);
    } else {
        checkOtherRet = CheckOtherRoute(umq_socket);
    }

    if (checkOtherRet != 0) {
        UBS_VLOG_ERR("Failed to get other route in retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        retry_state_ = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
        return UBS_OK;
    }

    other_route_message_.ub_handshake_state = UBHandshakeState::kRETRY;
    other_route_message_.other_route = other_conn_route;
    other_route_message_.other_back_route = other_back_conn_route;
    if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &other_route_message_, sizeof(other_route_message_),
                                             CONTROL_PLANE_TIMEOUT_MS) < 0) {
        return UBS_TCP_EXCHANGE;
    }

    std::vector<umq_port_id_t> used_port_vector;
    if (topo_type_ == UMQ_TOPO_TYPE_CLOS && UmqSetting::UMQ_IS_BONDING) {
        used_port_vector = {other_conn_route.src_port, other_back_conn_route.src_port};
    } else if (topo_type_ == UMQ_TOPO_TYPE_FULLMESH_1D && UmqSetting::UMQ_IS_BONDING) {
        used_port_vector = {other_conn_route.src_port};
    } else {
        used_port_vector = {};
    }
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    UBS_VLOG_DEBUG("DoConnect down to back, main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_conn_route.src_port.bs.chip_id, other_conn_route.src_port.bs.die_id,
                   other_conn_route.src_port.bs.port_idx);
    UBS_VLOG_DEBUG("DoConnect down to back, back route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_back_conn_route.src_port.bs.chip_id, other_back_conn_route.src_port.bs.die_id,
                   other_back_conn_route.src_port.bs.port_idx);
    ack_ret = DoUbConnect(umq_socket, used_ports);
    if (!IsOk(ack_ret)) {
        UBS_VLOG_ERR("Failed to finish ub bind in retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
    }

    // 通过返回错误码, 在函数调用处打印错误码
    if (SocketConnHelper::SendSocketData(raw_fd_, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ack_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, ack_ret);
        return UBS_TCP_EXCHANGE;
    }

    if (SocketConnHelper::RecvSocketData(raw_fd_, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to recv peer ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, peer_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_, peer_ret);
        return UBS_TCP_EXCHANGE;
    }

    degradable_ = IsDegradable(peer_ret);
    if (IsOk(ack_ret) && IsOk(peer_ret)) {
        retry_state_ = UBHandshakeState::kOK;
    } else if (degradable_) {
        retry_state_ = UBHandshakeState::kDEGRADE;
    } else {
        retry_state_ = UBHandshakeState::kFAILED;
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
    if (UmqConnHelper::GetRouteList(filtered_list, *src_eid, *dst_eid) != UBS_OK) {
        UBS_VLOG_ERR("Failed to get urma route info.\n");
        return UBS_ERROR;
    }

    RouteListRegistry::Instance().RegisterOrReplaceRouteList(*dst_eid, filtered_list);
    return UBS_OK;
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
        EidRegistry::Instance().RegisterOrReplaceEidIndex(dst_eid, index);
    }

    EidRegistry::Instance().GetEidIndex(dst_eid, index);
}

// CLOS组网 通过亲和性选择 Client端调用
// affine_routes: 输出，亲和组路由（本端和对端均为同芯片）
// non_aff_routes: 输出，非亲和组路由（跨芯片）
Result UmqConnectorOps::GetCpuAffinityUmqRoute(umq_route_list_t &route_list, std::vector<umq_route_t> &affine_routes,
                                               std::vector<umq_route_t> &non_aff_routes)
{
    affine_routes.clear();
    non_aff_routes.clear();
    uint32_t process_chip_Id = 0; //本段芯片id
    uint32_t peer_chip_id = 0;    //对端芯片id

    // 本端
    std::set<uint32_t> process_chip_ids; //本段芯片数组
    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        process_chip_ids.insert(route_list.routes[i].src_port.bs.chip_id);
    }
    std::vector<uint32_t> process_chip_id_list(process_chip_ids.begin(), process_chip_ids.end());
    process_chip_Id = GetTargetChipId(UmqSetting::UMQ_ALL_SOCKET_IDS, process_chip_id_list,
                                      UmqSetting::UMQ_PROCESS_SOCKET_ID); //得到本端芯片id
    UBS_VLOG_DEBUG("process_chip_Id: %u\n", process_chip_Id);

    // 对端
    std::set<uint32_t> peer_chip_ids;
    for (uint32_t i = 0; i < route_list.route_num; ++i) { //亲和
        peer_chip_ids.insert(route_list.routes[i].dst_port.bs.chip_id);
    }
    std::vector<uint32_t> peer_chip_id_list(peer_chip_ids.begin(), peer_chip_ids.end());
    peer_chip_id = GetTargetChipId(peer_all_socket_ids_, peer_chip_id_list, peer_socket_id_);
    UBS_VLOG_DEBUG("peer_chip_id: %u\n", peer_chip_id);

    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        if (route_list.routes[i].src_port.bs.chip_id == process_chip_Id &&
            route_list.routes[i].dst_port.bs.chip_id == peer_chip_id) {
            affine_routes.push_back(route_list.routes[i]);
        }
    }
    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        if (route_list.routes[i].src_port.bs.chip_id != process_chip_Id &&
            route_list.routes[i].dst_port.bs.chip_id != peer_chip_id) {
            non_aff_routes.push_back(route_list.routes[i]);
        }
    }

    if (!affine_routes.empty() && !non_aff_routes.empty()) {
        UBS_VLOG_DEBUG("Find umq route successfully\n");
        return UBS_OK;
    }

    UBS_VLOG_WARN("Default Route policy Not Applied, Finding Route Based on Process End Chip Id.\n");

    // 主或备为空 回退到client端同chip_id
    if (affine_routes.empty()) {
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            if (route_list.routes[i].src_port.bs.chip_id == process_chip_Id &&
                route_list.routes[i].dst_port.bs.chip_id == process_chip_Id) {
                affine_routes.push_back(route_list.routes[i]);
            }
        }
    }

    if (non_aff_routes.empty()) {
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            if (route_list.routes[i].src_port.bs.chip_id != process_chip_Id &&
                route_list.routes[i].dst_port.bs.chip_id != process_chip_Id) {
                non_aff_routes.push_back(route_list.routes[i]);
            }
        }
    }

    if (!affine_routes.empty() && !non_aff_routes.empty()) {
        UBS_VLOG_DEBUG("Find umq route successfully\n");
        return UBS_OK;
    }

    UBS_VLOG_ERR("Failed to find umq route\n");
    return UBS_ERROR;
}

void UmqConnectorOps::RRChooseMainRoute(std::vector<umq_route_t> &all_routes, uint32_t main_route_size,
                                        const umq_eid_t *dst_eid, umq_route_t &conn_main_route,
                                        std::vector<umq_route_t> &conn_back_routes)
{
    uint32_t startIndex = 0;
    if (UmqSetting::UMQ_RANDOM_ROUTE) {
        std::minstd_rand gen(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, main_route_size - 1);
        startIndex = dist(gen);
    }
    GetBondingEidMapIndex(*dst_eid, startIndex);

    // 一主三备：确认测试环境亲和组大小
    UBS_VLOG_DEBUG("RRChooseMainRoute: all_routes.size()=%zu, startIndex=%u\n", all_routes.size(), startIndex);

    // 确保索引在有效范围内
    startIndex = startIndex % all_routes.size();

    // 从起始索引开始轮询查找
    conn_main_route = all_routes[startIndex];

    // 一主三备：从主路下一位开始，取最多3条作为备路（循环取）
    conn_back_routes.clear();
    uint32_t size = static_cast<uint32_t>(all_routes.size());
    for (uint32_t i = 1; i <= NegotiateRoute::BACK_ROUTE_MAX_NUM && i < size; ++i) {
        conn_back_routes.push_back(all_routes[(startIndex + i) % size]);
    }

    // 更新下一个轮询位置（存下一个索引，让RR真正前进）
    uint32_t nextIndex = (startIndex + 1) % static_cast<uint32_t>(all_routes.size());
    EidRegistry::Instance().RegisterOrReplaceEidIndex(*dst_eid, nextIndex);

    UBS_VLOG_DEBUG("main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n", conn_main_route.src_port.bs.chip_id,
                   conn_main_route.src_port.bs.die_id, conn_main_route.src_port.bs.port_idx);
    for (size_t i = 0; i < conn_back_routes.size(); ++i) {
        UBS_VLOG_DEBUG("back route[%zu]: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n", i,
                       conn_back_routes[i].src_port.bs.chip_id, conn_back_routes[i].src_port.bs.die_id,
                       conn_back_routes[i].src_port.bs.port_idx);
    }
}

Result UmqConnectorOps::CheckRouteDevAddForConnect(const umq_eid_t &conn_eid, const UmqSocketPtr &umq_socket)
{
    // 使用 bonding 设备/裸设备连接，在初始化阶段已将其添加，无需再添加 ub dev.
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP ||
        GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::RAW_DEVICE) {
        return UBS_OK;
    }

    // 主设备
    if (umq_socket->CheckDevAdd(conn_eid) != 0) {
        UBS_VLOG_ERR("Failed to check main dev add in connect, target eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(conn_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_UMQ_ERROR;
    }

    return UBS_OK;
}

Result UmqConnectorOps::CheckOtherRoute(const UmqSocketPtr &umq_socket)
{
    // 当前处于重试阶段，由于裸设备不会重试，此处保证 peer_bonding_eid 必定有效.
    if (!RouteListRegistry::Instance().IsRegisteredRouteList(umq_conn_info_.peer_bonding_eid)) {
        UBS_VLOG_ERR("Failed to check other route to connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_CONN_ROUTE;
    }

    umq_route_list_t route_list = {};
    if (!RouteListRegistry::Instance().GetRouteList(umq_conn_info_.peer_bonding_eid, route_list)) {
        UBS_VLOG_ERR("Failed to get route list in map, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip.c_str(), raw_fd_);
        return UBS_CONN_ROUTE;
    }

    umq_route_list_t filtered_list = {};
    uint32_t filter_mum = 0;
    bool found = false;
    for (uint32_t i = 0; i < route_list.route_num; ++i) {
        if (route_list.routes[i].src_port.bs.chip_id != conn_route_.src_port.bs.chip_id) {
            if (filter_mum == 0) {
                other_conn_route = route_list.routes[i];
                found = true;
            }
            filtered_list.routes[filter_mum++] = route_list.routes[i];
        }
    }

    if (!found) {
        UBS_VLOG_DEBUG("Failed to find other route in map\n");
        return UBS_CONN_ROUTE;
    }

    filtered_list.route_num = filter_mum;
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(umq_conn_info_.peer_bonding_eid, filtered_list);

    if (umq_socket->CheckDevAdd(other_conn_route.src_eid) != 0) {
        UBS_VLOG_ERR("CheckDevAdd() failed in CheckOtherRoute, src eid:" EID_FMT ", ret: %d\n",
                     EID_ARGS(other_conn_route.src_eid), UBS_CONN_ROUTE);
        return UBS_CONN_ROUTE;
    }

    // 如果为 BONDING_ROUTE 策略，则接下来尝试这条路径
    umq_conn_info_.conn_eid = other_conn_route.src_eid;
    umq_conn_info_.peer_eid = other_conn_route.dst_eid;
    return UBS_OK;
}

Result UmqConnectorOps::CheckOtherRouteForClos(const UmqSocketPtr &umq_socket)
{
    umq_route_t conn_main_route;
    std::vector<umq_route_t> temp_back_routes;
    // 从容灾备路池选路，适配 RRChooseMainRoute 新签名
    RRChooseMainRoute(non_aff_route_list_, non_aff_route_list_.size(), &umq_conn_info_.peer_eid, conn_main_route,
                      temp_back_routes);
    other_conn_route = conn_main_route;
    // 取第一条备路，兼容 OtherRouteMessage 的二字段结构
    other_back_conn_route = temp_back_routes.empty() ? umq_route_t{} : temp_back_routes[0];

    UBS_VLOG_DEBUG("other main route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_conn_route.src_port.bs.chip_id, other_conn_route.src_port.bs.die_id,
                   other_conn_route.src_port.bs.port_idx);

    UBS_VLOG_DEBUG("other back route is: src_port(chip_id=%u, die_id=%u, port_idx=%u)\n",
                   other_back_conn_route.src_port.bs.chip_id, other_back_conn_route.src_port.bs.die_id,
                   other_back_conn_route.src_port.bs.port_idx);

    if (umq_socket->CheckDevAdd(other_conn_route.src_eid) != 0) {
        return UBS_UB_DEV_ERROR;
    }

    if (umq_socket->CheckDevAdd(other_back_conn_route.src_eid) != 0) {
        return UBS_UB_DEV_ERROR;
    }

    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
