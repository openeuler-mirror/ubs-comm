/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_CORE_TYPES_H
#define UBS_COMM_UBSOCKET_CORE_TYPES_H

#include "common/ubsocket_common_includes.h"
#include "profiling/trace/ubsocket_trace.h"

namespace ock {
namespace ubs {
enum SocketState : uint8_t
{
    SOCK_STAT_INIT = 0,        /* init */
    SOCK_STAT_RAW_ESTABLISHED, /* the raw socket established */
    SOCK_STAT_ESTABLISHED,     /* all things established */
    SOCK_STAT_SHUTDOWN,        /* shutdown */
    SOCK_STAT_CLOSE,           /* closed */
                               /* add state before COUNT */
    SOCK_STATE_COUNT
};

enum class SocketType : uint8_t
{
    SOCK_TYPE_TCP = 0, /* only contains raw socket */
    SOCK_TYPE_UMQ,     /* an ubsocket based on umq */
    SOCK_TYPE_SHM,     /* an ubsocket based on shm */
    SOCK_TYPE_COUNT    /* add type before COUNT */
};

enum SocketCreateType : uint8_t
{
    SOCK_CREATE_TYPE_UNKNOWN = 0, /* unknown */
    SOCK_CREATE_TYPE_LISTEN,      /* created because of listen */
    SOCK_CREATE_TYPE_CONNECT,     /* created because of connect */
    SOCK_CREATE_TYPE_ACCEPT,      /* created because of accept */
                                  /* add here */
    SOCK_CREATE_TYPE_COUNT
};

enum class EpollRunnerType : uint8_t
{
    SHARE_JFR_RX_RUNNER = 0,     /* Share JFR Rx Epoll Runner */
    TRANSPORT_POOL_TX_RUNNER,    /* Transport Pool Tx Epoll Runner */
    TRANSPORT_POOL_EVENT_RUNNER, /* Transport Pool Event Epoll Runner */
};

class Socket;
using SocketPtr = Ref<Socket>;

class Socket {
public:
    Socket(int fd, SocketType type) : raw_socket_(fd), type_(type)
    {
        if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
            split_trace_ = new SplitTrace();
        }
    }
    virtual ~Socket()
    {
        if (split_trace_ != nullptr) {
            split_trace_->Flush();
            delete split_trace_;
            split_trace_ = nullptr;
        }
    }

    ALWAYS_INLINE SocketState State() const noexcept
    {
        return state_;
    }

    void State(SocketState state)
    {
        state_ = state;
        if (state == SOCK_STAT_CLOSE && split_trace_ != nullptr) {
            split_trace_->Flush();
        }
    }

    SocketType Type() const noexcept
    {
        return type_;
    }

    int Fd() const noexcept
    {
        return raw_socket_;
    }

public:
    virtual int GetTxFd() = 0;
    virtual bool IsBindRemote() = 0;
    virtual Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) = 0;
    virtual Result DelTxEvent(const SocketPtr &sock, int epoll_fd) = 0;
    virtual bool ShouldRegisterTxEvent() = 0;
    virtual Result ProcessEpollEvent(struct epoll_event &event) = 0;
    DEFINE_REF_OPERATION_FUNC

public:
    DECLARE_REF_COUNT_VARIABLE;                               /* ref count int16_t */
    int raw_socket_ = -1;                                     /* fd of raw socket */
    int event_fd_ = -1;                                       /* event fd */
    SocketState state_ = SOCK_STAT_INIT;                      /* state of ubsocket */
    SocketType type_ = SocketType::SOCK_TYPE_TCP;             /* type of ubsocket */
    SocketCreateType create_type_ = SOCK_CREATE_TYPE_UNKNOWN; /* created because of what */
    SplitTrace *split_trace_ = nullptr;
};

struct ConnInfo {
    std::string peer_ip; // 对端IP地址
    int peer_fd = -1;    // 对端socket fd
    int type_fd = 0;     // 0 server; 1 client
    std::chrono::system_clock::time_point create_time;
};

struct RawConnInfoV4 {
    // TODO: 考虑内存分配, 优化变量类型
    std::string peer_ip; // 对端IP地址
    int peer_fd = -1;    // 对端socket fd
    int type_fd = 0;     // 0 server; 1 client
    std::chrono::system_clock::time_point create_time;
};

struct AsyncAcceptInfo {
    std::queue<std::tuple<int, struct sockaddr, socklen_t>> ready_queue;
    std::atomic<int32_t> asyncTaskNum{0U};
    u_mutex_t *lock = nullptr;
};

const std::string &SocketStateToStr(SocketState value);
const std::string &SocketTypeToStr(SocketType value);
const std::string &SocketCreateTypeToStr(SocketCreateType value);
bool SocketStateValid(SocketState value);
bool SocketTypeValid(SocketType value);
bool SocketCreateTypeValid(SocketCreateType value);
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CORE_TYPES_H
