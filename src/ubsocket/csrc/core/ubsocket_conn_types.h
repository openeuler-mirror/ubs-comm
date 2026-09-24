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
#ifndef UBS_COMM_UBSOCKET_CONN_TYPES_H
#define UBS_COMM_UBSOCKET_CONN_TYPES_H

#include <cstdint>

namespace ock {
namespace ubs {

/* 控制面消息缓冲大小：URMA 控制面最大 wire 帧（UrmaSockSegExInfo ~408B）可容纳 */
constexpr uint32_t CONN_CTRL_BUF_SIZE = 512;

/*
 * 全异步建链连接空闲超时（毫秒），Client/Server 共用。对端任一 recv 阶段的最大静默时长。
 * 取值必须显著大于对端控制面单阶段阻塞预算（CONTROL_PLANE_TIMEOUT_MS=2000ms）：
 * 高并发下对端（尤其客户端）URMA 资源创建/导入本身可能耗时接近该预算，若用同一紧值做回收，
 * 会在对端仍处于合法交换窗口内时抢先误杀连接。真正的死连接会被对端 close 触发 recv=0 快速回收，
 * 空闲扫描仅兜底对端开着但静默的泄漏连接，故可用宽松值。
 *
 * 注意：重活内联在 poll 线程执行会占用线程，超时判定必须使用 reactor 的"有效时钟"
 * （墙钟扣除本线程处理耗时），否则线程忙于其它连接时会把等待中的连接误判为空闲。
 */
constexpr uint64_t CONN_IDLE_TIMEOUT_MS = 10000;

/* 建链角色：决定初始状态、失败时 fd 归属（close 策略）及状态名细节 */
enum class ConnRole : uint8_t
{
    kClient = 0, /* 发起方：fd 归应用，失败不 close，支持降级纯 TCP */
    kServer,     /* 接受方：fd 自 accept 所得，失败由 reactor close */
};

/*
 * 统一建链状态（Client/Server 共用一套枚举，以 recv 对端消息为界拆阶段）。
 * 发起方独有的 TCP 握手态 kWaitTcpConnect 仅 Client 进入；kWaitNegotiate 在 Client 侧表示
 * 等 NegotiateRsp、Server 侧表示等 NegotiateReq（方向相反）。其后各态方向一致。
 * recv 阶段由全异步建链 poll 线程驱动（非阻塞增量读）；读满后在同一个 poll 线程内联执行本地重活，
 * 不做任何阻塞 IO，故不存在"重活在途"状态。
 */
enum class ConnState : uint8_t
{
    kWaitTcpConnect = 0, /* Client 阶段 A：非阻塞 TCP 握手 + 校验 SO_ERROR；非 UB 则降级纯 TCP */
    kWaitNegotiate,      /* Client：续发 NegotiateReq -> 收 Rsp；Server：收 NegotiateReq -> 回 Rsp */
    kWaitPeerRes,        /* 双方：续发 local seg -> 收 peer seg -> 内联 ImportPeerSegments */
    kWaitPeerAck,        /* 双方：续发 ack -> 收 peer ack -> 内联 CommitRemotePeer */
    kConnEstablished,    /* 建链完成（终态） */
    kConnFailed,         /* 建链失败（终态） */
};

inline const char *ConnStateName(ConnState state)
{
    switch (state) {
        case ConnState::kWaitTcpConnect:
            return "WaitTcpConnect";
        case ConnState::kWaitNegotiate:
            return "WaitNegotiate";
        case ConnState::kWaitPeerRes:
            return "WaitPeerRes";
        case ConnState::kWaitPeerAck:
            return "WaitPeerAck";
        case ConnState::kConnEstablished:
            return "ConnEstablished";
        case ConnState::kConnFailed:
            return "ConnFailed";
    }
    return "Unknown";
}

/* 阶段执行结果：poll 线程据此决定 fd 去留（Server/Client 共享） */
enum class ConnStageResult : uint8_t
{
    kPending = 0, /* 本阶段 recv 半包或非阻塞续发 EAGAIN，fd 保留在监听数组 */
    kDone,        /* 建链完成，fd 移出监听 + 通知用户 */
    kFailed,      /* 建链失败，fd 移出监听 + 按 close 策略处理 + 清映射表 */
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONN_TYPES_H
