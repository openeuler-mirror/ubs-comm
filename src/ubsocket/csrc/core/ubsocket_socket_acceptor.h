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

#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"
#include "ubsocket_socket_helper.h"

namespace ock {
namespace ubs {

// accept 操作抽象层
// TODO: AcceptorOps 和 ConnectorOps 接口基本一致，考虑两个合并
class AcceptorOps {
public:
    virtual ~AcceptorOps() = default;

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
    RawConnInfoV4 conn_info;
    int fd;
    int event_fd;

    friend class Acceptor;
};
using AcceptorOpsPtr = Ref<AcceptorOps>;

// accept 建链通用实现层：TCP 建链，协商，建链
class Acceptor {
public:
    Acceptor(const SocketPtr &sock, AcceptorOps *acceptorOps) : raw_fd_(sock->raw_socket_),
          acceptor_ops_(acceptorOps) {}
    ~Acceptor();

    int Listen(int backlog);
    int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);

    void SetAcceptorOps(const AcceptorOpsPtr &acceptor_ops);

    // connection status
    static constexpr int kControlPlaneTimeoutMs = 5000;
    static constexpr int kNegotiateTimeoutMs = 10000;

private:
    // ======================== Accept 主流程辅助函数 ========================
    bool TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len);
    void ProcessUBConnection(int fd, const std::string &peerIp);
    Result DoAccept(int new_fd, const std::string &peerIp);

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

    ALWAYS_INLINE bool IsClient(void)
    {
        // return RawConnInfoV4.type_fd == 1 ? true : false;
        return true;
    }

    // ======================== 成员变量 ========================
    int raw_fd_;   // 传入 sock 的原生 socket fd
    Ref<AcceptorOps> acceptor_ops_ = nullptr;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_ACCEPTOR_H
