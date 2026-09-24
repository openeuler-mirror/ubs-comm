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
#ifndef UBS_COMM_UBSOCKET_CONN_OPS_BASE_H
#define UBS_COMM_UBSOCKET_CONN_OPS_BASE_H

#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/ubsocket_common_includes.h"
#include "ubsocket_conn_buffer.h"
#include "ubsocket_conn_types.h"

namespace ock {
namespace ubs {

/*
 * Server/Client 全异步建链状态机统一基类：一个连接一个实例，由共享建链 poll 线程驱动（Execute）。
 * Client/Server 共用同一套 ConnState（见 ubsocket_conn_types.h），差异收敛为 role_（初始状态 +
 * CloseOnFailure 策略）与各阶段钩子（由传输层派生类实现）；单一 Execute 按 ConnState 分发。
 *
 * 建链以 recv 为界拆为多个阶段；poll 线程只做非阻塞增量读 + 轻量应答，半包未读完返回 kPending（fd 保留在监听数组）；
 * 读满后在同一个 poll 线程内联执行本地重活，绝不发起阻塞 IO；需发送的控制面帧一律暂存 pending_send_buf_，
 * 由后续 Execute 非阻塞续发。故不存在"重活在途"状态。
 */
class UBConnOpsBase : public std::enable_shared_from_this<UBConnOpsBase> {
public:
    UBConnOpsBase(ConnRole role, int fd, const struct sockaddr &addr, socklen_t addr_len);
    virtual ~UBConnOpsBase() = default;

    /* poll 线程唯一入口：按 state_ 分发到对应阶段。effective_now_ms 为 reactor 有效时钟（墙钟扣除本线程处理耗时） */
    ConnStageResult Execute(uint64_t effective_now_ms);

    /* 当前状态名（仅用于超时/错误日志；kWaitNegotiate 按角色细分 Req/Rsp） */
    const char *StateName() const;

    /* 每连接空闲超时阈值（毫秒） */
    uint64_t IdleTimeoutMs() const
    {
        return CONN_IDLE_TIMEOUT_MS;
    }

    /* 失败收尾是否由 reactor close(fd)：server=true（fd 自 accept 所得），client=false（fd 归应用） */
    bool CloseOnFailure() const
    {
        return role_ == ConnRole::kServer;
    }

    /* 成功收尾钩子：注册 epoll / 置 ESTABLISHED / 通知应用等（Client 降级/ESTABLISHED、Server 由阶段钩子收尾） */
    virtual void OnConnDone() {}

    /* 失败收尾钩子：清 ArraySet / remote 段 / 记 connect_error_ 等 */
    virtual void OnConnFailed() {}

    int Fd() const
    {
        return fd_;
    }

    const struct sockaddr &Addr() const
    {
        return addr_;
    }

    socklen_t AddrLen() const
    {
        return addr_len_;
    }

    /* 最近一次 Execute 的有效时刻；仅 poll 线程读写 */
    uint64_t LastActiveMs() const
    {
        return last_active_ms_;
    }

    /* poll 线程在失败/Stop/Cancel 时调用：据此跳过后续阶段推进，避免操作已关闭 fd */
    void Abort()
    {
        aborted_.store(true, std::memory_order_release);
    }

    bool IsAborted() const
    {
        return aborted_.load(std::memory_order_acquire);
    }

    /* 记录失败原因（POSIX errno）：阶段失败路径或 reactor 空闲超时在收尾前设置 */
    void SetFailureErrno(int err)
    {
        failure_errno_.store(err, std::memory_order_release);
    }

    int FailureErrno() const
    {
        return failure_errno_.load(std::memory_order_acquire);
    }

    /* 存在待续发的控制面帧（暂存或 EAGAIN 未发完）时置位，reactor 据此为该 fd 追加 POLLOUT 监听 */
    bool WantsWritable() const
    {
        return wants_writable_;
    }

protected:
    /* 阶段钩子：按 ConnState 由基类 Execute 分发，由传输层派生类实现各阶段的非阻塞 IO + 内联本地重活。
     * DoWaitTcpConnect 仅 Client 进入，默认失败兜底；其余阶段 Client/Server 各自实现（协议方向相反）。 */
    virtual ConnStageResult DoWaitTcpConnect()
    {
        return ConnStageResult::kFailed;
    }
    virtual ConnStageResult DoWaitNegotiate() = 0;
    virtual ConnStageResult DoWaitPeerRes() = 0;
    virtual ConnStageResult DoWaitPeerAck() = 0;

    ConnState State() const
    {
        return state_.load(std::memory_order_acquire);
    }

    /* 非阻塞发送：EAGAIN 时返回 UBS_WOULD_BLOCK，sent 回传本次已发送字节数（供续发） */
    Result SendControlNonBlocking(const void *data, size_t size, size_t &sent);
    /* 非阻塞续发 pending_send_buf_ 剩余字节：UBS_OK 发完清空 / UBS_WOULD_BLOCK 未发完（置 wants_writable_）/ 其他错误 */
    Result FlushPendingSend();
    /* 暂存待续发的控制面帧（拷贝入 pending_send_buf_），并置 wants_writable_ 让 reactor 追加 POLLOUT */
    void StageControl(const void *data, size_t size);

    int fd_;
    struct sockaddr addr_;
    socklen_t addr_len_ = 0;
    ConnRole role_;
    AsyncConnBuffer buf_;
    std::vector<uint8_t> pending_send_buf_; /* 阶段间待发控制面帧（非阻塞续发） */
    size_t pending_send_off_ = 0;
    std::atomic<ConnState> state_{ConnState::kConnFailed}; /* 统一建链状态；构造按 role_ 置初值 */
    uint64_t last_active_ms_{0};                           /* 有效时钟；仅 poll 线程读写，无需 atomic */
    std::atomic<bool> aborted_{false};
    std::atomic<int> failure_errno_{0}; /* 失败原因（POSIX errno），收尾时读取 */
    bool wants_writable_ = false; /* 仅 poll 线程访问：存在待续发控制面帧时为 true，续发完成后清位 */
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONN_OPS_BASE_H
