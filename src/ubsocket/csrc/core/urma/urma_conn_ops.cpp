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
#include "urma_conn_ops.h"

#include <sys/eventfd.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "under_api/dl_libc_api.h"
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {

namespace {

uint32_t NegotiateServerConnFlag(uint32_t peer_flag)
{
    uint32_t ret_flag = 0;
    if (GlobalSetting::UBS_ENABLE_DEGRADE) {
        ret_flag |= (peer_flag & URMA_SOCKET_MASK_DEGRADE);
    }
    if (GlobalSetting::UBS_BACKUP_LINK_ENABLED) {
        ret_flag |= (peer_flag & URMA_SOCKET_MASK_LINK_SWITCH);
    }
    return ret_flag;
}

} // namespace

UrmaConnOps::UrmaConnOps(ConnRole role, int fd, const SocketPtr &sock, const ConnectorOpsPtr &connector_ops,
                         const struct sockaddr &addr, socklen_t addr_len)
    : UBConnOpsBase(role, fd, addr, addr_len),
      sock_(sock),
      connector_ops_(connector_ops)
{
    if (role == ConnRole::kClient) {
        urma_sock_ = RefStaticCastToRaw<UrmaSocket>(sock);
        lp_reader_.Begin(&rsp_, sizeof(rsp_));
    } else {
        was_blocking_ = SocketConnHelper::IsBlocking(fd);
        lp_reader_.Begin(&req_, sizeof(req_));
    }
}

/* ==================== 交换骨架 ==================== */

ConnFlushOutcome UrmaConnOps::FlushStaged()
{
    if (pending_send_buf_.empty()) {
        return ConnFlushOutcome::kOk;
    }
    Result ret = FlushPendingSend();
    if (ret == UBS_WOULD_BLOCK) {
        return ConnFlushOutcome::kPending;
    }
    return (ret == UBS_OK) ? ConnFlushOutcome::kOk : ConnFlushOutcome::kFailed;
}

int UrmaConnOps::RecvFixedRaw(void *dst, size_t size)
{
    ssize_t n = buf_.Advance(fd_, size);
    if (n < 0) {
        return -1;
    }
    if (!buf_.Ready(size)) {
        return 0;
    }
    memcpy(dst, buf_.Data(), size);
    buf_.Reset();
    return 1;
}

/* Server + TFO：同步路径在协商前由 AcceptorOps::ValidateProtocol 消费 SYN 前导 magic(8B)，
 * reactor 路径无该调用点；若不消费，magic 前 4 字节（"TPDA" = 0x41445054）会被
 * LengthPrefixedReader 误当 body_len 触发丢弃上限，建链必然失败。
 * 因此这里按同一 wire 布局 [magic 8B][4B body_len][body] 先非阻塞消费并校验前导，再进入帧解析。 */
TfoPreambleOutcome UrmaConnOps::RecvTfoPreamble()
{
    /* 非 TFO 模式：URMA 控制面无前导（对齐 UrmaAcceptorOps::ValidateProtocol 的非 TFO 空实现） */
    if (GlobalSetting::UBS_HAND_SHAKE_MODE != UBHandshakeMode::TFO) {
        tfo_preamble_done_ = true;
        return TfoPreambleOutcome::kOk;
    }

    /* Advance 只累积到 expected，不会多读 body；对端关闭/recv 出错返回 kFailed */
    if (buf_.Advance(fd_, sizeof(uint64_t)) < 0) {
        UBS_VLOG_WARN("UrmaConnOps recv tfo preamble failed, fd: %d, errno: %d\n", fd_, errno);
        return TfoPreambleOutcome::kFailed;
    }
    if (!buf_.Ready(sizeof(uint64_t))) {
        return TfoPreambleOutcome::kPending; /* 前导未收全：保持监听，可读后重入续读 */
    }

    uint64_t magic = 0;
    memcpy(&magic, buf_.Data(), sizeof(magic));
    buf_.Reset();
    if (magic != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
        UBS_VLOG_ERR("UrmaConnOps tfo preamble mismatch, fd: %d, magic: 0x%lx\n", fd_,
                     static_cast<unsigned long>(magic));
        return TfoPreambleOutcome::kFailed;
    }
    tfo_preamble_done_ = true;
    return TfoPreambleOutcome::kOk;
}

/* ==================== 角色钩子 ==================== */

Result UrmaConnOps::PrepareSocket()
{
    if (role_ == ConnRole::kServer) {
        if (!EnsureSocketCreated()) {
            return UBS_ERROR;
        }
        ApplyNegotiatedConfig();
        return UBS_OK;
    }

    Result ret = SocketBase::GenerateSocketCommOps(sock_);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps GenerateSocketCommOps failed, fd: %d, ret: %d\n", fd_, ret);
    }
    return ret;
}

Result UrmaConnOps::BeforeImportPeer()
{
    if (role_ == ConnRole::kServer) {
        return RegisterSocket();
    }
    return UBS_OK;
}

void UrmaConnOps::OnConnEstablished()
{
    if (role_ != ConnRole::kServer) {
        return;
    }
    if (sock_ != nullptr) {
        sock_->State(SOCK_STAT_ESTABLISHED);
    }
    if (was_blocking_) {
        SocketConnHelper::SetBlocking(fd_);
    }
}

/* ==================== Client 降级与收尾 ==================== */

void UrmaConnOps::MarkFailure(int err_no, bool degradable)
{
    SetFailureErrno(err_no);
    degradable_ = degradable;
}

uint64_t UrmaConnOps::IdleTimeoutMs() const
{
    /* client 等 NegotiateRsp 阶段对齐同步路径预算（NEGOTIATE_REPLY_TIMEOUT_MS=2s，见同步
     * UrmaConnectorOps::Negotiate 收 rsp 预算）；其余阶段（含 server 等 req、双方资源交换）
     * 沿用全局宽松空闲超时：重活内联在共享 poll 线程，收紧会误杀仍在合法交换窗口内的连接 */
    if (role_ == ConnRole::kClient && State() == ConnState::kWaitNegotiate) {
        return NEGOTIATE_REPLY_TIMEOUT_MS;
    }
    return CONN_IDLE_TIMEOUT_MS;
}

bool UrmaConnOps::DegradeToTcp(SocketCreateType create_type)
{
    SocketPtr raw;
    Result ret = SocketBase::Create(fd_, SocketType::SOCK_TYPE_TCP, raw);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps degrade create raw socket failed, fd: %d, ret: %d\n", fd_, ret);
        return false;
    }
    raw->create_type_ = create_type;
    ArraySet<Socket>::GetInstance().OverrideItem(fd_, raw.Get());

    if (GlobalSetting::UBS_TRACE_ENABLED) {
        auto *sockBase = RefStaticCastToRaw<SocketBase>(sock_);
        if (sockBase != nullptr) {
            sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
            sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::ACTIVE_OPEN_COUNT, 1);
        }
    }
    UBS_VLOG_INFO("ubsocket is degraded to TCP.\n");
    return true;
}

void UrmaConnOps::OnConnDone()
{
    if (role_ != ConnRole::kClient) {
        return; /* Server 的 ESTABLISHED/blocking 收尾在 DoWaitPeerAck 内联完成 */
    }
    if (sock_ == nullptr) {
        return;
    }
    auto *sockBase = RefStaticCastToRaw<SocketBase>(sock_);
    if (sockBase == nullptr) {
        return;
    }

    sockBase->connect_error_.store(0, std::memory_order_release);

    if (connector_ops_ != nullptr) {
        connector_ops_->conn_info.type_fd = 1;
    }
    sock_->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    sock_->State(SOCK_STAT_ESTABLISHED);
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::ACTIVE_OPEN_COUNT, 1);
    }

    /* 连接已就绪可写：先置 writable_ready_，延迟注册时据此补投 EPOLLOUT（应用尚未 ADD 则维持该标志） */
    sockBase->SetWritableReady(true);
    sockBase->RegisterPendingEpollAdd();
}

void UrmaConnOps::OnConnFailed()
{
    if (role_ == ConnRole::kClient) {
        ClientOnConnFailed();
        return;
    }
    ServerOnConnFailed();
}

void UrmaConnOps::ClientOnConnFailed()
{
    if (sock_ == nullptr) {
        return;
    }
    auto *sockBase = RefStaticCastToRaw<SocketBase>(sock_);
    if (sockBase == nullptr) {
        return;
    }

    if (degradable_ && DegradeToTcp()) {
        sockBase->connect_error_.store(0, std::memory_order_release);
        sockBase->SetWritableReady(true);
        sockBase->RegisterPendingEpollAdd();
        return;
    }

    OnConnCleanup();
    int err = FailureErrno();
    if (err == 0) {
        err = ECONNREFUSED;
    }
    sockBase->connect_error_.store(err, std::memory_order_release);
    sockBase->State(SOCK_STAT_CLOSE);
    sockBase->NotifyConnectFailed();
}

void UrmaConnOps::ServerOnConnFailed()
{
    bool expected = true;
    if (registered_.compare_exchange_strong(expected, false)) {
        ArraySet<Socket>::GetInstance().OverrideItem(fd_, nullptr);
    }
}

void UrmaConnOps::CleanStaleSock(uint64_t stale_conn_id)
{
    /* 对齐同步路径：遍历 ArraySet，清理同 conn_id 的陈旧 URMA accept 连接（本连接尚未注册，无自清理风险） */
    ArraySet<Socket>::GetInstance().ForEach([&](int stale_fd, Socket *stale_sock) {
        if (stale_sock == nullptr || stale_sock->Type() != SocketType::SOCK_TYPE_URMA ||
            stale_sock->create_type_ != SOCK_CREATE_TYPE_ACCEPT || stale_fd == fd_) {
            return;
        }
        auto *stale_urma = RefStaticCastToRaw<UrmaSocket, Socket>(stale_sock);
        if (stale_urma->GetConnId() != stale_conn_id ||
            stale_urma->State() == SocketState::SOCK_STAT_BROKEN) {
            return;
        }
        stale_urma->State(SocketState::SOCK_STAT_BROKEN);
        LibcApi::shutdown(stale_fd, SHUT_RD);
        stale_urma->NotifySocketReadable();
        UBS_VLOG_TRACE("UrmaConnOps negotiate fd=%d, stale conn_id=%lu cleaned, stale fd=%d", fd_,
                       (unsigned long)stale_conn_id, stale_fd);
    });
}

void UrmaConnOps::OnConnCleanup()
{
    if (peer_imported_) {
        peer_imported_ = false;
    }
    if (urma_sock_ != nullptr) {
        urma_sock_->DestroyUrmaResources();
    }
}

/* ==================== 协商帧 ==================== */

Result UrmaConnOps::BuildNegotiateReq()
{
    if (!pending_send_buf_.empty()) {
        return UBS_OK; /* 已构建，等待 poll 线程续发 */
    }
    if (urma_sock_ == nullptr) {
        UBS_VLOG_ERR("UrmaConnOps build negotiate req urma_sock null, fd: %d\n", fd_);
        return UBS_ERROR;
    }

    UBSVersion local_version = UBS_PROTOCOL_VERSION;
    UrmaNegotiateReq req = {};
    req.peer_version = local_version.GetWhole();
    req.trans_mode = UrmaSetting::UBS_TRANS_MODE;
    req.send_buf_kb = UrmaSetting::UBS_CONNECTS_BUF_DEFAULT_KB;
    req.recv_buf_kb = UrmaSetting::UBS_CONNECTS_BUF_DEFAULT_KB;
    req.conn_flag |= GlobalSetting::UBS_ENABLE_DEGRADE ? URMA_SOCKET_MASK_DEGRADE : 0;
    req.conn_flag |= GlobalSetting::UBS_BACKUP_LINK_ENABLED ? URMA_SOCKET_MASK_LINK_SWITCH : 0;

    /* 本端 context 健康列表：server 据此选择健康 context pair，选中 index 由 rsp 带回 */
    sorted_contexts_ = UrmaBackend::Instance().GetContextPoll()->SortedContexts();
    req.context_count = std::min(static_cast<uint32_t>(sorted_contexts_.size()), URMA_MAX_CONTEXT_COUNT);
    for (uint32_t i = 0; i < req.context_count; ++i) {
        const auto &ctx = sorted_contexts_[i];
        req.context_health[i].eid_index = ctx->EidInfo().eid_index;
        req.context_health[i].is_bad = ctx->IsBadPort() ? 1 : 0;
    }

    /* 携带对端上次坏链的 conn_id，server 据此清理陈旧连接 */
    sent_last_conn_id_ = UrmaBackend::Instance().TakeBrokenConnId(urma_sock_->GetPeerAddrId());
    req.last_conn_id = sent_last_conn_id_;

    uint32_t body_len = sizeof(req);
    pending_send_buf_.resize(sizeof(uint32_t) + sizeof(req));
    memcpy(pending_send_buf_.data(), &body_len, sizeof(body_len));
    memcpy(pending_send_buf_.data() + sizeof(body_len), &req, sizeof(req));
    pending_send_off_ = 0;
    wants_writable_ = false;
    return UBS_OK;
}

Result UrmaConnOps::BuildNegotiateReply()
{
    if (!pending_send_buf_.empty()) {
        return UBS_OK; /* 已构建，等待 poll 线程续发 */
    }

    UBSVersion local_version = UBS_PROTOCOL_VERSION;
    UBSVersion peer_version(req_.peer_version);
    UBSVersion negotiated;
    VersionCheckResult ver_result = local_version.Negotiate(peer_version, negotiated);
    if (ver_result == VersionCheckResult::kMajorMismatch) {
        UBS_VLOG_WARN("UrmaConnOps version major mismatch, peer=%u, local=%u, fd=%d\n", peer_version.major,
                      local_version.major, fd_);
        /* 版本 major 不匹配：negotiated_version 发本端版本号，供对端 ValidateNegotiated 判定降级；
         * rsp 照常经 FlushStaged 发出，降级决策由 DoWaitNegotiate 依据 negotiate_mismatch_ 执行 */
        UrmaNegotiateRsp rsp = {};
        rsp.negotiated_version = local_version.GetWhole();
        rsp.selected_context_index = -1; /* mismatch 通知不携带 context，对齐正常路径 -1 表示无 context 的约定 */
        uint32_t body_len = sizeof(rsp);
        pending_send_buf_.resize(sizeof(uint32_t) + sizeof(rsp));
        memcpy(pending_send_buf_.data(), &body_len, sizeof(body_len));
        memcpy(pending_send_buf_.data() + sizeof(body_len), &rsp, sizeof(rsp));
        pending_send_off_ = 0;
        wants_writable_ = false;
        negotiate_mismatch_ = true;
        return UBS_OK;
    }

    /* 协商结果先缓存到本状态机：socket 对象由共享 executor 创建后再写回（见 ApplyNegotiatedConfig） */
    negotiated_version_ = negotiated.GetWhole();
    peer_version_ = peer_version.GetWhole();
    trans_mode_ = std::min(req_.trans_mode, UrmaSetting::UBS_TRANS_MODE);
    conn_flag_ = NegotiateServerConnFlag(req_.conn_flag);

    /* 对端上次坏链遗留的陈旧连接先清理，并分配本连接号（同步/异步共用全局分配器） */
    if (req_.last_conn_id != 0) {
        CleanStaleSock(req_.last_conn_id);
    }
    conn_id_ = UrmaBackend::Instance().AllocConnId();

    /* 对端 context 健康列表 × 本端 context 列表 → 选出健康 pair（无则 selected_context_ 为空，rsp 携带 -1） */
    selected_context_ = UrmaBackend::Instance().GetContextPoll()->SelectContext(
        UrmaPortSelectionPolicy::PSP_ROUND_ROBIN, &selected_context_index_, req_.context_health, req_.context_count);

    UrmaNegotiateRsp rsp = {};
    rsp.ret_code = 0;
    rsp.negotiated_version = negotiated_version_;
    rsp.trans_mode = trans_mode_;
    rsp.send_buf_kb = UrmaSetting::UBS_CONNECTS_BUF_DEFAULT_KB;
    rsp.recv_buf_kb = UrmaSetting::UBS_CONNECTS_BUF_DEFAULT_KB;
    rsp.conn_flag = conn_flag_;
    rsp.selected_context_index = selected_context_index_;
    rsp.conn_id = conn_id_;

    uint32_t body_len = sizeof(rsp);
    pending_send_buf_.resize(sizeof(uint32_t) + sizeof(rsp));
    memcpy(pending_send_buf_.data(), &body_len, sizeof(body_len));
    memcpy(pending_send_buf_.data() + sizeof(body_len), &rsp, sizeof(rsp));
    pending_send_off_ = 0;
    wants_writable_ = false;
    return UBS_OK;
}

void UrmaConnOps::ApplyNegotiatedConfig()
{
    /* 协商结果已由各自分支缓存到统一字段（client 来自 rsp_，server 来自 req_/协商结果）；仅 peer 版本为 server 专有 */
    urma_sock_->SetTransMode(trans_mode_);
    urma_sock_->SetSocketFlags(conn_flag_); /* wire 上的 conn_flag 已是 URMA_SOCKET_MASK 域 */
    urma_sock_->SetConnId(conn_id_);
    if (selected_context_ != nullptr) {
        urma_sock_->context_ = selected_context_; /* friend 访问，与同步路径一致 */
    }
    if (role_ == ConnRole::kServer) {
        urma_sock_->SetPeerVersion(peer_version_);
    }
}

/* ==================== 资源创建/导入 ==================== */

Result UrmaConnOps::CreateResourcesAndStageLocal()
{
    if (urma_sock_ == nullptr && role_ == ConnRole::kClient) {
        UBS_VLOG_ERR("UrmaConnOps create resources urma_sock null, fd: %d\n", fd_);
        return UBS_ERROR;
    }

    Result ret = PrepareSocket(); /* Client: GenerateSocketCommOps；Server: EnsureSocketCreated + ApplyConfig */
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps prepare socket failed, fd: %d, ret: %d\n", fd_, ret);
        return ret;
    }
    if (urma_sock_ == nullptr) {
        UBS_VLOG_ERR("UrmaConnOps create resources urma_sock null, fd: %d\n", fd_);
        return UBS_ERROR;
    }
    ret = urma_sock_->CreateUrmaResources();
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps CreateUrmaResources failed, fd: %d, ret: %d\n", fd_, ret);
        return ret;
    }
    ret = urma_sock_->BuildLocalSegExInfo(local_seg_ex_);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps BuildLocalSegExInfo failed, fd: %d, ret: %d\n", fd_, ret);
        return ret;
    }
    /* 不在重活内发送：暂存 local seg，交由后续 Execute 非阻塞续发；置 wants_writable_ 让 reactor 追加 POLLOUT */
    StageControl(&local_seg_ex_, sizeof(local_seg_ex_));
    return UBS_OK;
}

Result UrmaConnOps::ImportPeerAndStageAck()
{
    if (urma_sock_ == nullptr) {
        return UBS_ERROR;
    }
    /* Server 侧 ArraySet 注册已由 BeforeImportPeer 内联完成；防御性校验，保证投空 recv WR 前 socket 已可见 */
    if (role_ == ConnRole::kServer && !registered_.load(std::memory_order_acquire)) {
        UBS_VLOG_ERR("UrmaConnOps import peer before register, fd: %d\n", fd_);
        return UBS_ERROR;
    }

    Result ret = urma_sock_->ImportPeerSegments(peer_seg_ex_);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps ImportPeerSegments failed, fd: %d, ret: %d\n", fd_, ret);
        return ret;
    }
    peer_imported_.store(true, std::memory_order_release);

    /* 不在重活内发送：暂存 ack，交由后续 Execute 非阻塞续发；置 wants_writable_ 让 reactor 追加 POLLOUT */
    Result ack_ret = UBS_OK;
    StageControl(&ack_ret, sizeof(ack_ret));
    return UBS_OK;
}

bool UrmaConnOps::EnsureSocketCreated()
{
    if (sock_ != nullptr) {
        return true;
    }

    int nb_ret = SocketConnHelper::SetNonBlocking(fd_);
    if (nb_ret != 0) {
        UBS_VLOG_ERR("UrmaConnOps set nonblocking failed, fd: %d, errno: %d\n", fd_, errno);
        return false;
    }

    SocketPtr created;
    Result ret = SocketBase::Create(fd_, SocketType::SOCK_TYPE_URMA, created);
    int event_fd = (ret == UBS_OK) ? eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC) : -1;
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps create urma socket failed, fd: %d, ret: %d\n", fd_, ret);
        return false;
    }

    if (event_fd < 0) {
        UBS_VLOG_ERR("UrmaConnOps eventfd() failed, fd: %d, errno: %d, errmsg: %s\n", fd_, errno,
                     Func::Error2Str(errno));
        return false;
    }
    created->event_fd_ = event_fd;

    ret = SocketBase::GenerateSocketCommOps(created);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps GenerateSocketCommOps failed, fd: %d, ret: %d\n", fd_, ret);
        return false; /* created 释放时 UnInitialize 会关闭 event_fd_ */
    }

    created->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
    sock_ = created;
    urma_sock_ = RefStaticCastToRaw<UrmaSocket>(sock_);
    UBS_VLOG_DEBUG("UrmaConnOps socket created, fd: %d\n", fd_);
    return true;
}

Result UrmaConnOps::RegisterSocket()
{
    if (registered_.load(std::memory_order_acquire)) {
        return UBS_OK;
    }
    if (sock_ == nullptr) {
        return UBS_ERROR;
    }
    bool expected = false;
    if (!registered_.compare_exchange_strong(expected, true)) {
        return UBS_OK; /* 其他路径已注册 */
    }
    ArraySet<Socket>::GetInstance().OverrideItem(fd_, sock_.Get());
    UBS_VLOG_DEBUG("UrmaConnOps socket registered into ArraySet, fd: %d\n", fd_);
    return UBS_OK;
}

/* ==================== 阶段 ==================== */

ConnStageResult UrmaConnOps::DoWaitTcpConnect()
{
    if (role_ != ConnRole::kClient) {
        return ConnStageResult::kFailed; /* Server 自 accept 起即已连接，不会进入本态 */
    }

    if (!tcp_connect_started_) {
        Result ret = connector_ops_->PrepareConnect(fd_, &addr_, addr_len_, sock_);
        if (ret != UBS_OK) {
            UBS_VLOG_WARN("UrmaConnOps PrepareConnect failed, fd: %d, ret: %d\n", fd_, ret);
            MarkFailure(ECONNREFUSED, false);
            return ConnStageResult::kFailed;
        }
        tcp_connect_started_ = true;
        wants_writable_ = true; /* reactor 据此监听 POLLOUT，等非阻塞 TCP 握手完成 */
        return ConnStageResult::kPending;
    }

    /* POLLOUT 重入：TCP 握手结果由 SO_ERROR 判定 */
    int err = 0;
    socklen_t len = sizeof(err);
    if (LibcApi::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        int get_err = (errno != 0) ? errno : ECONNREFUSED;
        UBS_VLOG_WARN("UrmaConnOps getsockopt SO_ERROR failed, fd: %d, errno: %d\n", fd_, get_err);
        MarkFailure(get_err, false);
        return ConnStageResult::kFailed;
    }
    if (err != 0) {
        UBS_VLOG_WARN("UrmaConnOps tcp connect failed, fd: %d, so_error: %d\n", fd_, err);
        MarkFailure(err, false);
        return ConnStageResult::kFailed;
    }

    SocketConnHelper::SetTcpNoDelay(fd_);

    /* 记录对端地址 id（坏链记录以 peer 地址为 key，对齐同步路径 Connector::Connect） */
    if (sock_ != nullptr) {
        sock_->conn_add_id = SocketConnHelper::PackSockAddrToId(&addr_);
    }

    /* 对端非 UB（如 client UB + server TCP）时不做提前降级，继续走 Negotiate，与同步路径
     * Connector::Connect 保持一致：同步路径同样是协商失败而非直接降级（协商告吹的错误不含
     * UBS_DEGRADABLE_MASK，Connector::Connect 收敛为建链失败）。仅 server 协商回带 DEGRADE
     * 标志（如版本 mismatch、无健康 context）时才降级，与同步语义一致。 */
    if (BuildNegotiateReq() != UBS_OK) {
        MarkFailure(ECONNREFUSED, false);
        return ConnStageResult::kFailed;
    }
    wants_writable_ = true; /* 暂存 NegotiateReq，待 POLLOUT 续发 */
    state_.store(ConnState::kWaitNegotiate, std::memory_order_release);
    return ConnStageResult::kPending;
}

ConnStageResult UrmaConnOps::DoWaitNegotiate()
{
    const bool is_client = (role_ == ConnRole::kClient);

    /* 发起方先续发 NegotiateReq；接受方先收 NegotiateReq */
    if (is_client) {
        ConnFlushOutcome flush = FlushStaged();
        if (flush == ConnFlushOutcome::kPending) {
            return ConnStageResult::kPending; /* 保留 fd + POLLOUT，可写后重入续发 */
        }
        if (flush == ConnFlushOutcome::kFailed) {
            UBS_VLOG_WARN("UrmaConnOps send negotiate req failed, fd: %d\n", fd_);
            MarkFailure(ECONNREFUSED, false);
            return ConnStageResult::kFailed;
        }
    }

    /* Server + TFO：先非阻塞消费并校验 SYN 前导 magic(8B)，再解析长度前缀帧（对齐同步路径
     * ProcessUBConnection -> ValidateProtocol 的消费顺序）；非 TFO 模式无前导，helper 内部直接放行 */
    if (role_ == ConnRole::kServer && !tfo_preamble_done_) {
        TfoPreambleOutcome preamble_outcome = RecvTfoPreamble();
        if (preamble_outcome == TfoPreambleOutcome::kPending) {
            return ConnStageResult::kPending; /* 前导未收全：保持监听，可读后重入续读 */
        }
        if (preamble_outcome == TfoPreambleOutcome::kFailed) {
            MarkFailure(ECONNREFUSED, false); /* 对齐同步路径 magic 校验失败即关闭，不降级 */
            return ConnStageResult::kFailed;
        }
    }

    if (!lp_reader_.Done()) {
        LpReadResult recv_ret = lp_reader_.Read(fd_, buf_, is_client ? "UrmaConnOps recv rsp" : "UrmaConnOps recv req");
        if (recv_ret != LpReadResult::kComplete) {
            if (recv_ret == LpReadResult::kPartial) {
                return ConnStageResult::kPending;
            }
            MarkFailure(ECONNREFUSED, false);
            return ConnStageResult::kFailed;
        }
    }
    if (IsAborted()) {
        return ConnStageResult::kFailed;
    }

    if (is_client) {
        if (rsp_.ret_code != 0) {
            UBS_VLOG_WARN("UrmaConnOps negotiate rsp ret_code=%d, fd: %d\n", rsp_.ret_code, fd_);
            MarkFailure(ECONNREFUSED, false);
            return ConnStageResult::kFailed;
        }
        UBSVersion local_version = UBS_PROTOCOL_VERSION;
        UBSVersion negotiated_ver(rsp_.negotiated_version);
        if (negotiated_ver.ValidateNegotiated(local_version) == VersionCheckResult::kMajorMismatch) {
            /* 置 degradable_ 后由 ClientOnConnFailed 走 DegradeToTcp 带内降级 */
            UBS_VLOG_WARN("UrmaConnOps version major mismatch, server version=%u.%u.%u, fd: %d\n",
                          negotiated_ver.major, negotiated_ver.minor, negotiated_ver.patch, fd_);
            MarkFailure(ECONNREFUSED, true);
            return ConnStageResult::kFailed;
        }

        /* 协商成功：清除本端坏链记录 */
        if (sent_last_conn_id_ != 0) {
            UrmaBackend::Instance().EraseBrokenConn(urma_sock_->GetPeerAddrId());
            sent_last_conn_id_ = 0;
        }

        negotiated_version_ = rsp_.negotiated_version;
        trans_mode_ = rsp_.trans_mode;
        conn_flag_ = rsp_.conn_flag;
        conn_id_ = rsp_.conn_id;

        /* 按 server 选中的 index 解析 context；先写回 flags（失败路径据此判定可降级性，对齐同步顺序） */
        if (rsp_.selected_context_index >= 0 &&
            static_cast<uint32_t>(rsp_.selected_context_index) < sorted_contexts_.size()) {
            selected_context_ = sorted_contexts_[static_cast<uint32_t>(rsp_.selected_context_index)];
        }
        ApplyNegotiatedConfig();
        if (selected_context_ == nullptr) {
            UBS_VLOG_WARN("UrmaConnOps negotiate has no context valid: %d, fd: %d\n", rsp_.selected_context_index, fd_);
            MarkFailure(ECONNREFUSED, urma_sock_->DegradeEnable());
            return ConnStageResult::kFailed;
        }
    } else {
        Result build_ret = BuildNegotiateReply();
        if (build_ret != UBS_OK) {
            UBS_VLOG_WARN("UrmaConnOps BuildNegotiateReply failed, fd: %d, ret: %d\n", fd_, build_ret);
            return ConnStageResult::kFailed;
        }
        ConnFlushOutcome flush = FlushStaged();
        if (flush == ConnFlushOutcome::kPending) {
            /* 非阻塞发送未发完：保留 fd 并由 reactor 追加 POLLOUT，可写后重入本函数续发（绝不阻塞 poll 线程） */
            return ConnStageResult::kPending;
        }
        if (flush == ConnFlushOutcome::kFailed) {
            UBS_VLOG_WARN("UrmaConnOps send negotiate reply failed, fd: %d\n", fd_);
            return ConnStageResult::kFailed;
        }
        /* mismatch rsp 已发回对端，本侧带内降级为 TCP；kDone 触发 done_cb，brpc 以普通 TCP 接受该 fd */
        if (negotiate_mismatch_) {
            if (!DegradeToTcp(SOCK_CREATE_TYPE_ACCEPT)) {
                UBS_VLOG_ERR("UrmaConnOps negotiate mismatch degrade to TCP failed, fd: %d\n", fd_);
                return ConnStageResult::kFailed;
            }
            state_.store(ConnState::kConnEstablished, std::memory_order_release);
            return ConnStageResult::kDone;
        }
        /* 无健康 context pair：rsp 已携带 selected_context_index=-1 发回，对齐同步路径在此直接失败 */
        if (selected_context_ == nullptr) {
            UBS_VLOG_WARN("UrmaConnOps negotiate has no context valid, fd: %d\n", fd_);
            MarkFailure(ECONNREFUSED, false);
            return ConnStageResult::kFailed;
        }
    }

    /* 内联重活：建 URMA 资源 + 组 local seg（暂存待发），不做阻塞 IO */
    Result create_ret = CreateResourcesAndStageLocal();
    if (create_ret != UBS_OK) {
        UBS_VLOG_WARN("UrmaConnOps CreateResourcesAndStageLocal failed, fd: %d, ret: %d\n", fd_, create_ret);
        MarkFailure(ECONNREFUSED, IsDegradable(create_ret) || (urma_sock_ != nullptr && urma_sock_->DegradeEnable()));
        return ConnStageResult::kFailed;
    }
    state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    return ConnStageResult::kPending; /* 下一阶段由后续 POLLOUT（续发 local seg）驱动 */
}

ConnStageResult UrmaConnOps::DoWaitPeerRes()
{
    if (IsAborted()) {
        return ConnStageResult::kFailed;
    }

    ConnFlushOutcome flush = FlushStaged();
    if (flush == ConnFlushOutcome::kPending) {
        return ConnStageResult::kPending;
    }
    if (flush == ConnFlushOutcome::kFailed) {
        UBS_VLOG_WARN("UrmaConnOps send local seg failed, fd: %d\n", fd_);
        MarkFailure(ECONNREFUSED, urma_sock_ != nullptr && urma_sock_->DegradeEnable());
        return ConnStageResult::kFailed;
    }

    int recv_ret = RecvFixedRaw(&peer_seg_ex_, sizeof(peer_seg_ex_));
    if (recv_ret == 0) {
        return ConnStageResult::kPending;
    }
    if (recv_ret < 0) {
        MarkFailure(ECONNREFUSED, urma_sock_ != nullptr && urma_sock_->DegradeEnable());
        return ConnStageResult::kFailed;
    }

    /* Server 先注册 ArraySet（先于 ImportPeerSegments，保证导入期间投空 recv WR 产生 WRITE_IMM 时可回查 socket） */
    Result hook_ret = BeforeImportPeer();
    if (hook_ret != UBS_OK) {
        UBS_VLOG_ERR("UrmaConnOps register socket failed, fd: %d, ret: %d\n", fd_, hook_ret);
        return ConnStageResult::kFailed;
    }

    /* 内联重活：导入对端段 + 组 ack（暂存待发），不做阻塞 IO */
    Result import_ret = ImportPeerAndStageAck();
    if (import_ret != UBS_OK) {
        UBS_VLOG_WARN("UrmaConnOps ImportPeerAndStageAck failed, fd: %d, ret: %d\n", fd_, import_ret);
        MarkFailure(ECONNREFUSED, IsDegradable(import_ret) || (urma_sock_ != nullptr && urma_sock_->DegradeEnable()));
        return ConnStageResult::kFailed;
    }
    state_.store(ConnState::kWaitPeerAck, std::memory_order_release);
    return ConnStageResult::kPending; /* 下一阶段由后续 POLLOUT（续发 ack）驱动 */
}

ConnStageResult UrmaConnOps::DoWaitPeerAck()
{
    if (IsAborted()) {
        return ConnStageResult::kFailed;
    }

    ConnFlushOutcome flush = FlushStaged();
    if (flush == ConnFlushOutcome::kPending) {
        return ConnStageResult::kPending;
    }
    if (flush == ConnFlushOutcome::kFailed) {
        UBS_VLOG_WARN("UrmaConnOps send peer ack failed, fd: %d\n", fd_);
        MarkFailure(ECONNREFUSED, false);
        return ConnStageResult::kFailed;
    }

    Result peer_ack = UBS_OK; /* peer_ret 只校验字节数，值不参与判定（与同步路径一致） */
    int recv_ret = RecvFixedRaw(&peer_ack, sizeof(peer_ack));
    if (recv_ret == 0) {
        return ConnStageResult::kPending;
    }
    if (recv_ret < 0) {
        MarkFailure(ECONNREFUSED, false);
        return ConnStageResult::kFailed;
    }

    if (urma_sock_ != nullptr) {
        urma_sock_->CommitRemotePeer(peer_seg_ex_); /* 内部含 SetSocketFlags(BIND_REMOTE) */
    }
    OnConnEstablished(); /* Server：State(ESTABLISHED) + 恢复 blocking；Client：no-op */
    state_.store(ConnState::kConnEstablished, std::memory_order_release);
    return ConnStageResult::kDone;
}

} // namespace urma
} // namespace ubs
} // namespace ock
