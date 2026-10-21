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
#ifndef UBS_COMM_URMA_CONN_OPS_H
#define UBS_COMM_URMA_CONN_OPS_H

#include <sys/socket.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/ubsocket_conn_lp_reader.h"
#include "core/ubsocket_conn_ops_base.h"
#include "core/ubsocket_socket_connector.h"
#include "urma_socket.h"
#include "urma_socket_seg_ex.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

/* 续发暂存控制面帧的结果 */
enum class ConnFlushOutcome : uint8_t
{
    kOk,      /* 无待发或已发完 */
    kPending, /* EAGAIN 未发完，等 fd 可写重入续发 */
    kFailed,  /* 发送错误 */
};

/*
 * URMA Client/Server 统一全异步建链状态机（wire 与同步路径逐段等价，继承统一 ConnState）。
 * 握手是对称的 3 轮交换，R2/R3 两侧逐行同构，R1 仅方向相反（发起方先 send），故合并为单一类：
 *   阶段 A kWaitTcpConnect(仅 Client：poll 非阻塞 TCP 握手 + 校验 SO_ERROR；非 UB 则降级纯 TCP)
 *   阶段 B kWaitNegotiate(Client 续发 Req -> 收 Rsp；Server 收 Req -> 回 Rsp；收满后内联建资源+组 local seg)
 *   阶段 C kWaitPeerRes(续发 local seg -> 收 peer seg -> 内联 ImportPeerSegments + 组 ack)
 *   阶段 D kWaitPeerAck(续发 ack -> 收 peer ack -> 内联 CommitRemotePeer)
 *
 * Client/Server 真正的分叉收敛为 3 个钩子：PrepareSocket / BeforeImportPeer / OnConnEstablished。
 * 所有本地重活都在 poll 线程内联执行，不做任何阻塞 IO；控制面帧暂存 pending_send_buf_ 由 poll 非阻塞续发。
 */
class UrmaConnOps : public UBConnOpsBase {
public:
    UrmaConnOps(ConnRole role, int fd, const SocketPtr &sock, const ConnectorOpsPtr &connector_ops,
                const struct sockaddr &addr, socklen_t addr_len);
    ~UrmaConnOps() override = default;

    void OnConnDone() override;
    void OnConnFailed() override;

protected:
    ConnStageResult DoWaitTcpConnect() override;
    ConnStageResult DoWaitNegotiate() override;
    ConnStageResult DoWaitPeerRes() override;
    ConnStageResult DoWaitPeerAck() override;

private:
    /* 角色钩子：仅 Client/Server 真正分叉处 */
    Result PrepareSocket();    /* Client: GenerateSocketCommOps；Server: EnsureSocketCreated + ApplyConfig */
    Result BeforeImportPeer(); /* Server: RegisterSocket；Client: no-op */
    void OnConnEstablished();  /* Server: State(ESTABLISHED) + 恢复 blocking；Client: no-op */

    /* 协商帧构建/应用 */
    Result BuildNegotiateReq();   /* Client */
    Result BuildNegotiateReply(); /* Server */
    void ApplyNegotiatedConfig();

    /* 资源创建/导入 */
    Result CreateResourcesAndStageLocal();
    Result ImportPeerAndStageAck();
    bool EnsureSocketCreated(); /* Server：首帧收满后创建 UrmaSocket + eventfd + comm ops */
    Result RegisterSocket();    /* Server：注册进 ArraySet（先于 ImportPeerSegments） */

    /* Client 降级与收尾 */
    void MarkFailure(int err_no, bool degradable);
    void MarkDegraded();
    bool DegradeToTcp(SocketCreateType create_type = SOCK_CREATE_TYPE_CONNECT);
    void OnConnCleanup();
    void ClientOnConnFailed();

    /* Server 失败收尾 */
    void ServerOnConnFailed();
    /* 清理对端上次坏链遗留的陈旧连接（req.last_conn_id != 0 时调用） */
    void CleanStaleSock(uint64_t stale_conn_id);

    /* 交换骨架：续发暂存帧 / 非阻塞增量读固定长度帧，消除 R1/R2/R3 样板 */
    ConnFlushOutcome FlushStaged();
    int RecvFixedRaw(void *dst, size_t size);

private:
    SocketPtr sock_;                /* Client: 应用传入并保活；Server: EnsureSocketCreated 时创建 */
    ConnectorOpsPtr connector_ops_; /* Client per-socket 有状态 ops（route/retry 状态必须复用同一实例） */

    UrmaSocket *urma_sock_ = nullptr;

    /* Client 专有 */
    bool tcp_connect_started_ = false; /* 阶段 A 是否已发起非阻塞 connect */
    bool degradable_ = false;          /* 仅 poll 线程访问：本次失败是否可降级 */
    bool degraded_ = false; /* 仅 poll 线程访问：是否已降级为纯 TCP（阶段 A 提前判定） */
    std::vector<UrmaContextPtr> sorted_contexts_; /* 本端 context 排序快照：req 填充与 rsp index 解析共用 */
    uint64_t sent_last_conn_id_ = 0; /* 已随 req 发出的对端坏链 conn_id（0 = 无），协商成功后流转 EraseBrokenConn */

    /* Server 专有 */
    std::atomic<bool> registered_{false}; /* socket 是否已注册进 ArraySet（poll 线程在 DoWaitPeerRes 内联注册） */
    bool was_blocking_ = true;            /* 建链前 fd 是否阻塞态，成功后恢复 */
    bool negotiate_mismatch_ = false; /* 仅 poll 线程访问：版本 major mismatch，mismatch rsp 发完后带内降级为 TCP */

    /* 双方共用 */
    std::atomic<bool> peer_imported_{false}; /* 对端段是否已写入分配器 */

    LengthPrefixedReader lp_reader_; /* Client 读 NegotiateRsp / Server 读 NegotiateReq */
    UrmaNegotiateReq req_{};
    UrmaNegotiateRsp rsp_{};

    /* poll 阶段解析出的协商结果：socket 创建后才写回（见 ApplyNegotiatedConfig） */
    uint32_t negotiated_version_ = 0;
    uint32_t peer_version_ = 0;
    ub_trans_mode trans_mode_{RM_CTP};
    uint32_t conn_flag_ = 0;
    UrmaContextPtr selected_context_{nullptr}; /* 协商选出的 context（server: SelectContext；client: index 解析） */
    int32_t selected_context_index_{-1};
    uint64_t conn_id_{0}; /* 本连接号：server 分配 / client 来自 rsp */

    UrmaSockSegExInfo local_seg_ex_{};
    UrmaSockSegExInfo peer_seg_ex_{};
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_CONN_OPS_H
