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
#include "umq_socket_connector.h"

#include <netinet/tcp.h>
#include <random>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_scope_exit.h"
#include "common/ubsocket_version.h"
#include "core/umq/umq_eid_table.h"
#include "core/umq/umq_backend.h"
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
    int saved_sendto_errno = 0;
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
    if (sendto_ret < 0) {
        errno = saved_sendto_errno;
        return UBS_ERROR;
    }
    /* 方案B：req 尾部携带 bind_info 后 send_buf 可能超过单次 SYN 载荷（≈MSS）。
     * 剩余字节在握手完成后的普通流上续发——服务端按 length-prefix 读满为止，
     * 语义与 ub_sock_opt 路径完全一致。 */
    if (sendto_ret < buf_len &&
        SocketConnHelper::SendSocketData(raw_fd_, send_buf + sendto_ret, buf_len - sendto_ret,
                                         CONTROL_PLANE_TIMEOUT_MS) != buf_len - sendto_ret) {
        UBS_VLOG_ERR("TFO remainder send failed, sent: %zd, total: %d, errno %d\n", sendto_ret, buf_len, errno);
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result UmqConnectorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                       const SocketPtr &sock)
{
    /* 方案B：把本端 bind_info 挂上 NegotiateReq 需要先有本端 umq——预建从"协商
     * RTT 内"提前到"发起连接前"（ub_sock_opt 下与 TCP 三次握手重叠；TFO 下这是
     * SYN 携带的前置条件）。预建或取 bind_info 失败仅意味着本轮不携带（不置能力
     * 位），自动回退 方案A'/经典 路径。 */
    local_bind_info_len_ = 0;
    if (GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO) {
        auto umq_socket_pre = RefConvert<Socket, UmqSocket>(sock);
        TryPrecreateLocalUmq(umq_socket_pre);
        if (precreate_done_) {
            local_bind_info_len_ = UmqApi::umq_bind_info_get(umq_socket_pre->UmqHandle(), local_bind_info_,
                                                             sizeof(local_bind_info_));
        }
    }
    Result ret = UBS_OK;
    UBHandshakeMode handshake_mode = GlobalSetting::UBS_HAND_SHAKE_MODE;
    if (handshake_mode == UBHandshakeMode::UB_SOCK_OPT) {
        ret = ConnectViaHandshakeOpt(sock, address, address_len);
    } else if (handshake_mode == UBHandshakeMode::TFO) {
        ret = ConnectViaTfo(sock, address, address_len);
    } else {
        if (address != nullptr) {
            // 使用提取的接口获取IP地址
            SocketConnHelper::ExtractIpFromSockAddr(address, umq_conn_info_.peer_ip, sizeof(umq_conn_info_.peer_ip));
            // 对端fd就是accept返回的fd
            umq_conn_info_.peer_fd = new_fd;
            umq_conn_info_.create_time = std::chrono::system_clock::now();
        }
        return LibcApi::connect(raw_fd_, address, address_len);
    }

    if (address != nullptr) {
        // 使用提取的接口获取IP地址（零分配版本，直接写入定长缓冲）
        SocketConnHelper::ExtractIpFromSockAddr(address, umq_conn_info_.peer_ip, sizeof(umq_conn_info_.peer_ip));
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
                          umq_conn_info_.peer_ip, SocketConnHelper::ExtractPortFromSockAddr(address), new_fd);
        }
    }
    if (ret == UBS_OK) {
        UBS_VLOG_DEBUG("tcp connect succeed, ip %s port %d fd %d\n", umq_conn_info_.peer_ip,
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
        UBS_VLOG_ERR("Failed to negotiate in connect,Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip, new_fd);
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

                std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();

                umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                               .num = static_cast<uint8_t>(used_port_vector.size())};
                const bool umq_precreated = precreate_done_;
                precreate_done_ = false; // 一次性消费：重试轮（DoUbConnectRetry）走全新创建
                if (ack_ret == UBS_OK) {
                    ack_ret = DoUbConnect(umq_socket, used_ports, umq_precreated);
                } else if (umq_precreated) {
                    // 前置检查失败：预建资源不进入后续流程，销毁以保持经典路径语义
                    umq_socket->DestroyLocalUmq();
                }
                if (ack_ret != UBS_OK) {
                    UBS_VLOG_ERR("Failed to finish ub bind in connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
                }
                const bool ack_carried = peer_req_carry_consumed_;
                const bool ack_deferred = peer_ack_deferred_;
                peer_req_carry_consumed_ = false; /* 一次性消费：重试轮回退经典 ack 轮 */
                peer_ack_deferred_ = false;
                if (SocketConnHelper::SendSocketData(raw_fd_, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) !=
                    sizeof(ack_ret)) {
                    UBS_VLOG_ERR("Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
                    return UBS_TCP_EXCHANGE;
                }
                if (ack_carried && !ack_deferred) {
                    /* 方案B：腿⑥ 已随协商应答到达（server_bind_ret），免收 */
                    peer_ret = peer_carried_ack_;
                } else if (SocketConnHelper::RecvSocketData(raw_fd_, &peer_ret, sizeof(peer_ret),
                                                            CONTROL_PLANE_TIMEOUT_MS) != sizeof(peer_ret)) {
                    UBS_VLOG_ERR("Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
                    return UBS_TCP_EXCHANGE;
                }

                // 如果服务端支持降级则客户端需要配合。
                // 交叉 ack 下服务端应答不含回声(echo)，由本端合成等价共识
                //（ClientDegradableVerdict，8 组合等价性见单元测试）。
                /* 方案B 携带的腿⑥ 无回声，与交叉 ack 同款：按 early 语义合成共识；
                 * 并行 bind 推迟的腿⑥ 同样先于本端 ack 发出，亦无回声 */
                degradable_ = ClientDegradableVerdict(peer_ret, ack_ret, peer_early_ack_ || ack_carried,
                                                      peer_degrade_consent_);
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
                                   EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
                    break;
                } else {
                    UBS_VLOG_ERR("Failed to retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d, err:%d\n",
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_, ret);
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
                                 EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
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
                             EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
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
    UBS_VLOG_DEBUG("UB connection has been successfully established new fd: %d\n", raw_fd_);

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
    req->rpc_timeout_ms = umq_socket->GetLocalRpcTimeoutMs(); /* design §4.2 */
    /* 交叉 ack 能力声明（服务端在 NegotiateRsp.reserved[0] 确认；老服务端丢弃该尾部字段） */
    req->cap_flags = GlobalSetting::UBS_EARLY_ACK ? NEGO_CAP_EARLY_ACK : 0;
    /* 方案A'：声明可解析应答尾部携带的服务端 bind_info（免去一整轮 CpMsg 等待） */
    if (GlobalSetting::UBS_NEGO_CARRY_BINDINFO) {
        req->cap_flags |= NEGO_CAP_CARRY_BINDINFO;
    }
    /* 方案B：本端 bind_info 已预建并暂存 ⇒ 声明随请求携带（服务端消费后应答将
     * 直接携带其 bind 结果，ack 轮免收） */
    if (local_bind_info_len_ > 0) {
        req->cap_flags |= NEGO_CAP_REQ_CARRY_BINDINFO;
        /* 并行 bind：声明可在末尾 ack 轮接收服务端的 bind 结果——服务端据此先应答再
         * bind，两端的 umq_bind 得以并行（服务端不置位则仍按 方案B 结果随应答） */
        if (GlobalSetting::UBS_NEGO_PARALLEL_BIND) {
            req->cap_flags |= NEGO_CAP_DEFER_BIND_RET;
        }
    }
    return UBS_OK;
}

Result UmqConnectorOps::BuildNegotiateReqBuffer(uint8_t *buf, const UmqSocketPtr &umq_socket, int &buf_len)
{
    int offset = 0;

    uint64_t magic = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    memcpy(buf + offset, &magic, sizeof(magic));
    offset += sizeof(magic);

    uint32_t version = UBS_PROTOCOL_VERSION.GetWhole();
    memcpy(buf + offset, &version, sizeof(version));
    offset += sizeof(version);

    NegotiateReq req{};
    BuildNegotiateReq(&req, umq_socket);
    const bool carry_req = (req.cap_flags & NEGO_CAP_REQ_CARRY_BINDINFO) != 0;
    uint32_t body_len = static_cast<uint32_t>(sizeof(req));
    if (carry_req) {
        /* 方案B：body = NegotiateReqExt 截断到实际 bind_info 长度（与 Rsp 同款） */
        body_len = static_cast<uint32_t>(offsetof(NegotiateReqExt, bind_info) + local_bind_info_len_);
    }
    memcpy(buf + offset, &body_len, sizeof(body_len));
    offset += sizeof(body_len);
    if (carry_req) {
        NegotiateReqExt ext{}; /* 栈上 ~8.3KB，与 CpMsg 局部量同级 */
        ext.req = req;
        ext.bind_info_size = local_bind_info_len_;
        std::copy_n(local_bind_info_, local_bind_info_len_, ext.bind_info);
        memcpy(buf + offset, &ext, body_len);
        offset += static_cast<int>(body_len);
    } else {
        memcpy(buf + offset, &req, sizeof(req));
        offset += sizeof(req);
    }
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
            UBS_VLOG_ERR("Failed to send negotiate request, Peer IP:%s, fd: %d\n", umq_conn_info_.peer_ip,
                         raw_fd_);
            return UBS_ERROR;
        }
    }
    // TFO模式: SYN包携带send_buf内容(见ConnectViaTfo，需同步改造)

    // 重叠优化：等待协商应答的 RTT 期间预建本地 umq（客户端建链最重的本地步骤）。
    // 应答到达后校验协商结果是否与预建假设一致（见下方 SetTransMode 之后）。
    TryPrecreateLocalUmq(umq_socket);

    // 接收negotiated_version(4B) — 独立于Rsp body
    uint32_t negotiated_version = 0;
    if (SocketConnHelper::RecvSocketData(raw_fd_, &negotiated_version, sizeof(negotiated_version),
                                         CONTROL_PLANE_TIMEOUT_MS) != sizeof(negotiated_version)) {
        UBS_VLOG_ERR("Failed to receive negotiated version in connect, Peer IP:%s, fd: %d\n",
                     umq_conn_info_.peer_ip, raw_fd_);
        DiscardPrecreatedUmq(umq_socket);
        return UBS_ERROR;
    }

    // 校验协商结果：Major必须一致
    VersionCheckResult vc_result = UBSVersion(negotiated_version).ValidateNegotiated(UBS_PROTOCOL_VERSION);
    if (vc_result == VersionCheckResult::kMajorMismatch) {
        UBS_SLOG_WARN("Version major mismatch: negotiated="
                      << UBSVersion(negotiated_version) << " local=" << UBS_PROTOCOL_VERSION << " fd=" << raw_fd_
                      << " Peer IP:" << umq_conn_info_.peer_ip << " fallback to TCP");
        DiscardPrecreatedUmq(umq_socket);
        return UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK;
    }

    if (UBSVersion(negotiated_version).minor != UBS_PROTOCOL_VERSION.minor) {
        UBS_SLOG_DEBUG("Minor diff: negotiated=" << UBSVersion(negotiated_version)
                                                 << " local=" << UBS_PROTOCOL_VERSION);
    }

    umq_socket->SetNegotiatedVersion(negotiated_version);
    UBS_SLOG_DEBUG("Version negotiated: local " << UBS_PROTOCOL_VERSION << " -> " << UBSVersion(negotiated_version));

    // 接收NegotiateRsp body — length-prefixed
    /* 方案A'：按扩展布局接收——新服务端尾部携带 bind_info；
       老服务端短 body 被零填充 => bind_info_size==0 自回退 */
    NegotiateRspExt rsp_ext{};
    NegotiateRsp &rsp = rsp_ext.rsp;
    if (SocketConnHelper::RecvLengthPrefixed(raw_fd_, &rsp_ext, sizeof(rsp_ext), CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive negotiate response in connect,Peer IP:%s, fd: %d\n",
                     umq_conn_info_.peer_ip, raw_fd_);
        DiscardPrecreatedUmq(umq_socket);
        return UBS_ERROR;
    }
    if (rsp.ret_code != 0) {
        UBS_VLOG_ERR("Failed to negotiate in connect, peer ret %d, Peer IP:%s, fd: %d\n", rsp.ret_code,
                     umq_conn_info_.peer_ip, raw_fd_);
        DiscardPrecreatedUmq(umq_socket);
        return UBS_ERROR;
    }

    // UB 传输模式优先级协商，值越小优先级越高。例如当服务端为 RM_TP 而客户端是 RC_TP 会协商至 RC_TP.
    ub_trans_mode local_trans_mode = UmqSetting::UMQ_UB_TRANS_MODE;
    umq_socket->SetTransMode(std::min(rsp.peer_trans_mode, local_trans_mode));

    // 服务端能力位（老服务端 reserved 恒 0 ⇒ 两标志均 false，自动回退经典 ack 轮）
    peer_early_ack_ = GlobalSetting::UBS_EARLY_ACK && ((rsp.reserved[0] & NEGO_CAP_EARLY_ACK) != 0);
    peer_degrade_consent_ = (rsp.reserved[0] & NEGO_CAP_DEGRADE_CONSENT) != 0;

    /* 方案A'：服务端确认携带时暂存其 bind_info，DoUbConnect 免收 CpMsg */
    peer_bind_info_len_ = 0;
    if ((rsp.reserved[0] & NEGO_CAP_CARRY_BINDINFO) != 0 && rsp_ext.bind_info_size > 0 &&
        rsp_ext.bind_info_size <= UMQ_BIND_INFO_SIZE_MAX) {
        peer_bind_info_len_ = rsp_ext.bind_info_size;
        std::copy_n(rsp_ext.bind_info, rsp_ext.bind_info_size, peer_bind_info_);
    }

    // 预建假设校验：协商降到了对端更高优先级的传输模式 ⇒ 预建 umq 形态不符，
    // 丢弃并回退经典路径（DoUbConnect 将按协商后的模式重新创建）。
    if (precreate_done_ && umq_socket->GetTransMode() != UmqSetting::UMQ_UB_TRANS_MODE) {
        DiscardPrecreatedUmq(umq_socket);
    }
    /* 方案B：服务端确认消费了请求携带的 bind_info ⇒ 应答已含其提前 bind 的结果
     *（经典腿⑥，server_bind_ret），本端 ack 轮免收。防御：服务端只应在模式匹配
     * 且请求确实携带时确认——违反即协议错误。 */
    peer_req_carry_consumed_ = false;
    peer_carried_ack_ = UBS_OK;
    peer_ack_deferred_ = false;
    if ((rsp.reserved[0] & NEGO_CAP_REQ_CARRY_BINDINFO) != 0) {
        if (local_bind_info_len_ == 0 || umq_socket->GetTransMode() != UmqSetting::UMQ_UB_TRANS_MODE) {
            UBS_VLOG_ERR("peer confirmed req-carry without a valid carry (len %lu, mode %d), fd: %d\n",
                         local_bind_info_len_, static_cast<int>(umq_socket->GetTransMode()), raw_fd_);
            DiscardPrecreatedUmq(umq_socket);
            return UBS_ERROR;
        }
        peer_req_carry_consumed_ = true;
        /* 并行 bind：服务端确认推迟 ⇒ 它在应答之后才 bind，结果以交叉 ack 送来，
         * 末尾 ack 轮照收腿⑥；未推迟则结果已在应答里（方案B），免收。服务端只会
         * 确认本端声明过的能力，故此处无条件跟随其确认。 */
        peer_ack_deferred_ = (rsp.reserved[0] & NEGO_CAP_DEFER_BIND_RET) != 0;
        peer_carried_ack_ = peer_ack_deferred_ ? UBS_OK : static_cast<Result>(rsp_ext.server_bind_ret);
    }

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
        umq_conn_info_.bonding_eid = local_eid;
        umq_conn_info_.peer_bonding_eid = peer_eid;
        return UBS_OK;
    }

    peer_socket_id_ = rsp.aff_sock_id;
    if (UNLIKELY(rsp.socket_id_count == 0 || (rsp.socket_id_count > NEGOTIATE_SOCKET_ID_MAX_NUM))) {
        UBS_VLOG_ERR("Invalid peer socket count, fd: %d\n", raw_fd_);
        DiscardPrecreatedUmq(umq_socket);
        return UBS_ERROR;
    }
    peer_all_socket_ids_.reserve(rsp.socket_id_count);
    for (size_t i = 0; i < rsp.socket_id_count; i++) {
        peer_all_socket_ids_.push_back(rsp.socket_ids[i]);
    }
    PrintSocketsInfo();

    // 使用umq_backend缓存的used_ports，取消DoRoute与Send Negotiate环节
    // 复用bonding eid
    topo_type_ = UmqBackend::GetTopoType();
    umq_conn_info_.conn_eid = local_eid;
    umq_conn_info_.peer_eid = peer_eid;
    umq_conn_info_.peer_bonding_eid = peer_eid;
    umq_conn_info_.bonding_eid = local_eid;

    return UBS_OK;
}

Result UmqConnectorOps::DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports, bool umq_precreated)
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
    if (umq_precreated && umq_socket->UmqHandle() != UMQ_INVALID_HANDLE) {
        // 预建路径：CreateLocalUmq 已在预建阶段以相同参数完成（见 TryPrecreateLocalUmq）
        ret = UBS_OK;
    } else {
        if (umq_precreated) {
            /* 第二道防线（fix_precreate_reentry）：标志与句柄不一致时回退全新创建 */
            UBS_VLOG_WARN("precreate flag set but umq handle invalid, fall back to create, fd: %d\n", raw_fd_);
        }
        ret = umq_socket->CreateLocalUmq(&eid, used_ports, topo_type_);
    }

    if (ret != UBS_OK) {
        UBS_VLOG_ERR("Failed to create umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
        return ret;
    }
    ret = SocketBase::GenerateSocketCommOps(socket);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("Failed to generate socket comm ops,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
        /* umq 已建成（id 已占）而数据面装配失败：当场销毁，不等 socket 析构（issue #49） */
        umq_socket->DestroyLocalUmq();
        precreate_done_ = false;
        return ret;
    }

    if (peer_req_carry_consumed_) {
        /* 方案B：本端 bind_info 已随 NegotiateReq 送达服务端——免取免发（腿③ 已并入请求） */
        UBS_LINK_TRACE(raw_fd_, "C_CPMSG_SENT", "carried=1");
    } else {
        PROF_START(UMQ_BIND_INFO_GET);
        local_cp_msg.queue_bind_info_size =
            UmqApi::umq_bind_info_get(umq_socket->UmqHandle(), local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
        if (local_cp_msg.queue_bind_info_size == 0) {
            PROF_END(UMQ_BIND_INFO_GET, false);
            int savedErrno = errno;
            errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_bind_info_get() failed, Peer eid:" EID_FMT ",Peer IP:%s, "
                         "fd: %d, ret: %ld, mapped errno: %d(%s), original errno: %d\n",
                         EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_,
                         local_cp_msg.queue_bind_info_size, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
            return UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
        }
        PROF_END(UMQ_BIND_INFO_GET, true);

        if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) <
            0) {
            UBS_VLOG_ERR("Failed to send local control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                         EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
            return UBS_ERROR;
        }
        UBS_VLOG_DEBUG("send local control message, fd: %d, cp msg size: %zu, bind info len: %lu", raw_fd_,
                       sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);
    }

    if (peer_bind_info_len_ > 0) {
        /* 方案A'：服务端 bind_info 已随协商应答到达——免收 CpMsg，整轮等待消失 */
        remote_cp_msg.queue_bind_info_size = peer_bind_info_len_;
        std::copy_n(peer_bind_info_, peer_bind_info_len_, remote_cp_msg.queue_bind_info);
        UBS_LINK_TRACE(raw_fd_, "C_CPMSG_RCVD", "carried=1");
    } else if (SocketConnHelper::RecvLengthPrefixed(raw_fd_, &remote_cp_msg, sizeof(remote_cp_msg),
                                                    CONTROL_PLANE_TIMEOUT_MS) < 0) {
        UBS_VLOG_ERR("Failed to receive remote control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
        return UBS_ERROR;
    } else {
        UBS_LINK_TRACE(raw_fd_, "C_CPMSG_RCVD", "");
    }
    /* 串台防御：与 accept 侧对称——魔数不符即控制面已错位，立即失败（见 DoUbAccept） */
    if (remote_cp_msg.protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        UBS_VLOG_ERR("Receive misaligned control message, magic: 0x%llx,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     static_cast<unsigned long long>(remote_cp_msg.protocol_negotiation),
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
        return UBS_ERROR;
    }
    if (remote_cp_msg.queue_bind_info_size == 0 || remote_cp_msg.queue_bind_info_size > UMQ_BIND_INFO_SIZE_MAX) {
        UBS_VLOG_ERR("Receive remote invalid control message, bind info len: %lu,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d",
                     static_cast<unsigned long>(remote_cp_msg.queue_bind_info_size),
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
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
                              umq_conn_info_.peer_ip, raw_fd_);
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
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_, umq_ret, errno,
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
    peer_bind_info_len_ = 0; /* 方案A' 一次性消费：重试轮回退经典 CpMsg 双向交换 */
    peer_req_carry_consumed_ = false; /* 方案B 一次性消费 */
    peer_carried_ack_ = UBS_OK;
    local_bind_info_len_ = 0;
    auto umq_socket = RefConvert<Socket, UmqSocket>(socket_ptr);
    // ub降级后检查other链路时，是否检查成功的ret值
    if (UmqSetting::UMQ_DEV_SCHEDULE_POLICY == dev_schedule_policy::CPU_AFFINITY) {
        UBS_VLOG_ERR("CPU_AFFINITY:%d failed, connect no need to retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                     static_cast<int>(UmqSetting::UMQ_DEV_SCHEDULE_POLICY), EID_ARGS(umq_conn_info_.peer_eid),
                     umq_conn_info_.peer_ip, raw_fd_);

        if (degradable_) {
            retry_state_ = UBHandshakeState::kDEGRADE;
        } else {
            retry_state_ = UBHandshakeState::kFAILED;
        }
        return UBS_OK;
    }
    umq_socket->UnbindAndFlushRemoteUmq(socket_ptr.Get());
    umq_socket->DestroyLocalUmq();

    other_route_message_.ub_handshake_state = UBHandshakeState::kRETRY;
    if (SocketConnHelper::SendLengthPrefixed(raw_fd_, &other_route_message_, sizeof(other_route_message_),
                                             CONTROL_PLANE_TIMEOUT_MS) < 0) {
        return UBS_TCP_EXCHANGE;
    }

    std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    // 重试轮总是全新创建（预建仅供首轮 kSTART 消费），显式传 false，勿依赖默认参
    ack_ret = DoUbConnect(umq_socket, used_ports, false);
    if (!IsOk(ack_ret)) {
        UBS_VLOG_ERR("Failed to finish ub bind in retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_);
    }

    // 通过返回错误码, 在函数调用处打印错误码
    if (SocketConnHelper::SendSocketData(raw_fd_, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(ack_ret)) {
        UBS_VLOG_ERR("Failed to send ack ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ack_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_, ack_ret);
        return UBS_TCP_EXCHANGE;
    }

    if (SocketConnHelper::RecvSocketData(raw_fd_, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("Failed to recv peer ret message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, peer_ret: %d",
                     EID_ARGS(umq_conn_info_.peer_eid), umq_conn_info_.peer_ip, raw_fd_, peer_ret);
        return UBS_TCP_EXCHANGE;
    }

    // 保留 kSTART 阶段的 degradable_ 标志：路由逻辑移除后无备路可选，重试使用同一路由。
    // 若重试在 CreateLocalUmq/控制信令交换等环节失败（非可降级错误码），不应丢失初始尝试
    // 的可降级判定，否则会导致故障降级 TCP 失效。
    degradable_ = degradable_ || IsDegradable(peer_ret);
    if (IsOk(ack_ret) && IsOk(peer_ret)) {
        retry_state_ = UBHandshakeState::kOK;
    } else if (degradable_) {
        retry_state_ = UBHandshakeState::kDEGRADE;
    } else {
        retry_state_ = UBHandshakeState::kFAILED;
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

void UmqConnectorOps::TryPrecreateLocalUmq(const UmqSocketPtr &umq_socket)
{
    /* 重叠优化：在等待协商应答的 RTT 内预建本地 umq（CreateLocalUmq 是客户端建链
     * 最重的本地步骤）。仅当协商结果不可能改变本地资源形态时才预建：
     *  - BONDING_ROUTE 需要选路结果决定 eid，不预建（首轮 conn_eid 虽等于本端 EID，
     *    但保持该路径与现状完全一致）；
     *  - 传输模式按 min(对端, 本端) 协商。预建假设协商结果 == 本端模式；若对端为更高
     *    优先级（更小枚举值）导致降模式，应答处理处会丢弃预建并回退经典路径。
     * 预建失败不缓存错误：销毁半程资源后回退经典路径，由 DoUbConnect 重新创建，
     * 保持原有的错误码与重试语义。 */
    if (!GlobalSetting::UBS_CONNECT_PRECREATE ||
        GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_ROUTE) {
        return;
    }
    /* 再入护栏（fix_precreate_reentry）：同一 connector ops 上的第二次协商（重试/
     * 方案B 已在 PrepareConnect 预建）不得重复预建——否则 CreateLocalUmq 命中
     * "重复创建"防御后，下方失败兜底会销毁仍然有效的 umq。 */
    if (precreate_done_ || umq_socket->UmqHandle() != UMQ_INVALID_HANDLE) {
        return;
    }
    if (GlobalSetting::LINK_SELECTION_POLICY != LinkSelectionPolicy::RAW_DEVICE) {
        // 与 ConnectNegotiate 中的赋值等价提前；RAW_DEVICE 保持成员默认值（与现状一致）
        topo_type_ = UmqBackend::GetTopoType();
    }
    // 预建假设：协商结果为本端模式（UmqSocket 成员默认值是 RM_TP，须显式覆盖）。
    // 不变量说明：从此处到 ConnectNegotiate 收到应答后重新 SetTransMode(min(对端,本端))
    // 之间，socket 的 trans mode 暂为未经协商的假设值；建链在单线程内串行执行，
    // 该窗口无并发观察者。若协商结果不同，预建 umq 会被丢弃（见应答处理处）。
    umq_socket->SetTransMode(UmqSetting::UMQ_UB_TRANS_MODE);

    std::vector<umq_port_id_t> used_port_vector = UmqBackend::GetUsedPorts();
    umq_used_ports_t used_ports = {.port = used_port_vector.data(),
                                   .num = static_cast<uint8_t>(used_port_vector.size())};
    // 首轮尝试 conn_eid == 本端 EID（见 ConnectNegotiate 对 umq_conn_info_ 的赋值）
    const umq_eid_t eid = UmqSetting::UMQ_LOCAL_EID;
    if (umq_socket->CreateLocalUmq(&eid, used_ports, topo_type_) == UBS_OK) {
        precreate_done_ = true;
    } else {
        // 半程失败兜底：确保后续经典路径 CreateLocalUmq 不会命中"重复创建"防御
        umq_socket->DestroyLocalUmq();
        precreate_done_ = false; /* 显式复位（fix_precreate_reentry） */
    }
}

void UmqConnectorOps::DiscardPrecreatedUmq(const UmqSocketPtr &umq_socket)
{
    if (precreate_done_) {
        umq_socket->DestroyLocalUmq();
        precreate_done_ = false;
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
                     EID_ARGS(conn_eid), umq_conn_info_.peer_ip, raw_fd_);
        return UBS_UMQ_ERROR;
    }

    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
