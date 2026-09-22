/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_SOCKET_ACCEPTOR_H
#define UBS_COMM_UBSOCKET_SOCKET_ACCEPTOR_H

#include <memory>

#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"
#include "ubsocket_socket_helper.h"
#include "ubsocket_wakeup_event.h"
namespace ock {
namespace ubs {

// accept 操作抽象层
// TODO: AcceptorOps 和 ConnectorOps 接口基本一致，考虑两个合并
class AcceptorOps {
public:
    virtual ~AcceptorOps() = default;

    /* 握手期日志用对端 IP 暂存（监听 ops 复用）；连接的持久身份在 Socket::conn_info_ */
    char nego_peer_ip_[INET6_ADDRSTRLEN] = {};

    // ======================== 主流程方法 ========================
    // 阶段0：准备连接( TCP 辅助建链, 包括 TFO 发送 等 DoConnect 和 DoAccept 的前置操作)
    virtual Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                  const SocketPtr &sock) = 0;

    // 阶段1：协商信息
    virtual Result Negotiate(SocketPtr socketPtr) = 0;
    // 阶段2：创建资源（例如：umq create + bind + prefill rx）
    virtual Result CreateSocketResources(SocketPtr socketPtr) = 0;
    // 阶段3：销毁资源（握手失败/重试时清理已创建的资源）
    virtual void DestroySocketResources() = 0;

    // ======================== 仅 accept ===========================
    virtual int ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size) = 0;

    DEFINE_REF_OPERATION_FUNC

protected:
    DECLARE_REF_COUNT_VARIABLE;

protected:
    int fd;

    friend class Acceptor;
};
using AcceptorOpsPtr = Ref<AcceptorOps>;

// accept 建链通用实现层：TCP 建链，协商，建链
class Acceptor {
public:
    /* 按值内嵌于 SocketBase：先默认构造，fd/ops 就绪后经 Init 装配。
     * 异步 accept 上下文仍延迟到监听路径创建，普通数据链路不承担其分配 */
    Acceptor() = default;
    ~Acceptor();

    void Init(const SocketPtr &sock, AcceptorOps *acceptorOps)
    {
        raw_fd_ = sock->raw_socket_;
        acceptor_ops_ = acceptorOps;
    }

    ALWAYS_INLINE bool HasOps() const
    {
        return acceptor_ops_ != nullptr;
    }

    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;
    Acceptor(Acceptor &&) = delete;
    Acceptor &operator=(Acceptor &&) = delete;

    int Listen(int backlog);
    int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);

    void SetAcceptorOps(const AcceptorOpsPtr &acceptor_ops);
    AcceptorOpsPtr GetAcceptorOps()
    {
        return acceptor_ops_;
    }

    /* 建链完成后释放协商 ops（数据链路不再回读；监听 socket 不得调用） */
    void ReleaseOps()
    {
        acceptor_ops_ = nullptr;
    }

private:
    int AcceptSync(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);
    int AcceptAsync(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);
    void ProcessAcceptedFd(int fd, struct sockaddr *addr_tmp);
    void TracePeerIp(int fd, const std::string &peerIp, int peerPort);
    void HandleAcceptError(const SocketPtr &sock, int fd);
    bool TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len);
    /* 对已 accept 的 fd 执行 UB 协商。
     * @return true  fd 可交付上层（UB 建链成功，或可降级为 TCP 继续使用）；
     *         false fd 已在本函数内关闭（协议不匹配 / 对端在握手中途离开等致命失败），
     *               调用方不得再把该 fd 号交给上层或入 ready 队列。 */
    bool ProcessUBConnection(int fd, const std::string &peerIp);
    Result DoAccept(int new_fd, const std::string &peerIp);

    // 懒初始化：启动 ExecutorService + 初始化 wakeup_event_
    void InitWakeupEvent();

    // ======================== Accept 其他辅助函数 ========================
    // TODO: 将 connect 和 accept 完全共用但是与 accept 和 connect 无关的函数提取出来
    ALWAYS_INLINE const std::string &GetPeerIp() const
    {
        // return RawConnInfoV4.peer_ip;
        return EMPTY_STR;
    }

    ALWAYS_INLINE int GetPeerFd() const
    {
        //return RawConnInfoV4.peer_fd;
        return 0;
    }

    // connection status
    static constexpr int kControlPlaneTimeoutMs = 5000;
    static constexpr int kNegotiateTimeoutMs = 10000;

    // ======================== 成员变量 ========================
    int raw_fd_ = -1; // 传入 sock 的原生 socket fd（Init 时装配）
    Ref<AcceptorOps> acceptor_ops_ = nullptr;

    /*
     * 异步 accept 上下文：ready 队列 + 锁 + eventfd 唤醒机制。
     * 仅监听套接字的异步 accept 路径使用，但 Acceptor 会随每条链路创建
     * （accepted/connected 链路的 conn_info 也挂在 Acceptor 上），故所有成员
     * 收拢到本结构并惰性创建。原实现按值内嵌：std::queue 底层 deque 默认
     * 构造即预分配 512B 块（此处与 wakeup_event_ 内各一个）+ 每链路一把锁，
     * 数据链路白付约 1.2KB。
     */
    struct AsyncAcceptInfo {
        AsyncAcceptInfo()
        {
            lock = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        }
        ~AsyncAcceptInfo(); /* closes fds still queued (never delivered to brpc); in .cpp for LibcApi */
        std::queue<std::tuple<int, struct sockaddr, socklen_t>> ready_queue;
        std::atomic<int32_t> asyncTaskNum{0U};
        u_mutex_t *lock = nullptr;
        /* This listener's eventfd is registered with its epoll. Was a process-wide once in
         * InitWakeupEvent: only the first listening socket ever got a wakeup, so on every other
         * port completed handshakes sat in ready_queue until the next SYN (issue #50). */
        std::atomic<bool> wakeup_ready{false};
        // 唤醒机制：异步Accept任务完成后，通过 WakeUpReadyEventFd() 写 eventfd 唤醒 epoll_wait
        UbsocketWakeupEvent wakeup_event;
    };

    /* 首次进入异步 accept 路径时创建；创建失败返回 nullptr，调用方需判空 */
    AsyncAcceptInfo *EnsureAsyncAcceptCtx()
    {
        if (async_accept_ == nullptr) {
            async_accept_.reset(new (std::nothrow) AsyncAcceptInfo());
        }
        return async_accept_.get();
    }

    std::unique_ptr<AsyncAcceptInfo> async_accept_;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_ACCEPTOR_H