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

#include "ubsocket_common_includes.h"
#include "ubsocket_socket.h"
#include "ubsocket_socket_set.h"
#include "ubsocket_socket_helper.h"
#include "./umq/umq_socket_adapter.h"
#include "./umq/umq_socket_acceptor.h"

namespace ock {
namespace ubs {

// accept 操作抽象层
// TODO: AcceptorOps 和 ConnectorOps 接口基本一致，考虑两个合并
class AcceptorOps {
public:
    virtual ~AcceptorOps() = default;

    // ======================== 主流程方法 ========================
    // 阶段1：协商信息
    virtual Result Negotiate(int new_fd, Socket *socket_fd_obj) = 0;
    // 阶段2：创建资源（例如：umq create + bind + prefill rx）
    virtual Result CreateSocketResources(int new_fd, Socket *socket_fd_obj) = 0;
    // 阶段3：销毁资源（握手失败/重试时清理已创建的资源）
    virtual void DestroySocketResources() = 0;
    virtual void SetConnInfo(std::string peer_ip, int peer_fd, int type_fd) = 0;

    // ======================== 仅 accept ===========================
    virtual int ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size) = 0;

    // ======================== 成员变量 ===========================
    struct ConnInfo {
        std::string peer_ip;     // 对端IP地址
        int peer_fd = -1;        // 对端socket fd
        int type_fd = 0;         // 0 server; 1 client
        std::chrono::system_clock::time_point create_time;
    };

protected:

};

// accept 建链通用实现层：TCP 建链，协商，建链
class Acceptor {
public:
    Acceptor();
    ~Acceptor();

    // ======================== 基础方法 ========================
    ALWAYS_INLINE int Accept(const Socket& sock, struct sockaddr *address, socklen_t *address_len);
    ALWAYS_INLINE int Listen(int backlog);

    void SetAcceptorOps(std::shared_ptr<AcceptorOps> acceptor_ops);

    // peer & conn info
    struct RawConnInfoV4 {
        // TODO: 考虑内存分配, 优化变量类型
        int32_t peer_ip;           // 对端IP地址
        int peer_fd = -1;        // 对端socket fd
        int type_fd = 0;         // 0 server; 1 client
        std::chrono::system_clock::time_point create_time;
    };

    struct AsyncAcceptInfo {
        std::queue<std::tuple<int, struct sockaddr, socklen_t> > ready_queue;
        std::atomic<int32_t> asyncTaskNum{0U};
        u_mutex_t* lock = nullptr;
    };

    // connection status
    static constexpr int kControlPlaneTimeoutMs = 5000;
    static constexpr int kNegotiateTimeoutMs = 10000;

private:
    // ======================== Accept 主流程辅助函数 ========================
    ALWAYS_INLINE bool TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len);
    ALWAYS_INLINE void ProcessUBConnection(int fd, const std::string& peerIp);
    Result DoAccept(int new_fd, const std::string& peerIp);

    // ======================== Accept 其他辅助函数 ========================
    // TODO: 将 connect 和 accept 完全共用但是与 accept 和 connect 无关的函数提取出来
    ALWAYS_INLINE const std::string& GetPeerIp() const { return RawConnInfoV4.peer_ip; }
    ALWAYS_INLINE int GetPeerFd() const { return RawConnInfoV4.peer_fd; }
    ALWAYS_INLINE bool IsClient(void) { return RawConnInfoV4.type_fd == 1 ? true : false; }
    ALWAYS_INLINE int GetEventFd(void) { return event_fd_; }
    
    // ======================== 成员变量 ========================
    int raw_fd_;                            // 传入 sock 的原生 socket fd
    int event_fd_;                          // eventfd（通知上层可读）
    Ref(AcceptorOps) acceptor_ops_ = nullptr;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_ACCEPTOR_H
