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

#include <sys/socket.h> /* struct linger */

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "profiling/trace/ubsocket_trace.h"

namespace ock {
namespace ubs {
enum SocketState : uint8_t {
    SOCK_STAT_INIT = 0,        /* init */
    SOCK_STAT_RAW_ESTABLISHED, /* the raw socket established */
    SOCK_STAT_ESTABLISHED,     /* all things established */
    SOCK_STAT_SHUTDOWN,        /* shutdown */
    SOCK_STAT_BROKEN,          /* broken */
    SOCK_STAT_CLOSE,           /* closed */
    SOCK_STAT_CONNECTING,      /* non-blocking connect in flight (async connector) */
                               /* add state before COUNT */
    SOCK_STATE_COUNT
};

enum class SocketType : uint8_t {
    SOCK_TYPE_TCP = 0, /* only contains raw socket */
    SOCK_TYPE_UMQ,     /* an ubsocket based on umq */
    SOCK_TYPE_SHM,     /* an ubsocket based on shm */
    SOCK_TYPE_URMA,    /* an ubsocket based on urma */
    SOCK_TYPE_COUNT    /* add type before COUNT */
};

enum SocketCreateType : uint8_t {
    SOCK_CREATE_TYPE_UNKNOWN = 0, /* unknown */
    SOCK_CREATE_TYPE_LISTEN,      /* created because of listen */
    SOCK_CREATE_TYPE_CONNECT,     /* created because of connect */
    SOCK_CREATE_TYPE_ACCEPT,      /* created because of accept */
                                  /* add here */
    SOCK_CREATE_TYPE_COUNT
};

enum class EpollRunnerType : uint8_t {
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
        if (type == SocketType::SOCK_TYPE_TCP) {
            return;
        }
        if (!GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
            return;
        }
        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UBSOCKET) != SplitTraceLevel::LEVEL_NONE) {
            split_trace_ = new (std::nothrow) SplitTrace();
        }
    }
    virtual ~Socket()
    {
        if (type_ == SocketType::SOCK_TYPE_TCP) {
            return;
        }
        if (split_trace_ != nullptr) {
            TRACE_FLUSH(split_trace_);
            delete split_trace_;
            split_trace_ = nullptr;
        }
    }

    /**
     * Get and set socket state
     */
    SocketState State() const noexcept;
    void State(const SocketState state) noexcept;

    /**
     * Get the socket type tcp, urma, etc
     */
    SocketType Type() const noexcept;

    /**
     * Get the raw fd
     */
    int Fd() const noexcept;

    uint64_t GetPeerAddrId() const noexcept;

public:
    virtual bool IsBindRemote() = 0;

    /*
     * Local-close hook invoked by ubsocket_close before the fd is dropped.
     * No-op unless a transport keeps waiters (e.g., condvar broadcast).
     */
    virtual void OnLocalClose() noexcept {}

    /*
     * Actively notify the peer that closing this socket, e.g. post a
     * FIN-like message if the transport supports it (URMA CLOSE opcode).
     * @param timeout_ms: drain timeout override in ms; -1 (default) derives
     *                    from the socket's SO_LINGER shadow config.
     * @return true: the transport took over the fd (placeholder swap); the hold
     *               registry returns the fd number later — caller must NOT close it.
     *               false: caller still owns the fd and must close it (skipped,
     *               post failed, or placeholder degraded).
     */
    virtual bool PostCloseToPeer(int timeout_ms = -1) noexcept
    {
        (void)timeout_ms;
        return false;
    }

    /*
     * SOL_SOCKET/SO_LINGER transport hook (shadow config).
     * return 0: consumed by the transport; -1: not supported, caller falls
     * back to libc passthrough.
     */
    virtual int SetLingerOpt(const struct linger &value) noexcept
    {
        (void)value;
        return -1;
    }
    virtual int GetLingerOpt(struct linger &value) noexcept
    {
        (void)value;
        return -1;
    }

    /*
     * shutdown(how) transport hook (half-close, SHUT_WR/SHUT_RD/SHUT_RDWR).
     * return 0: consumed by the transport; -1: not supported, caller falls
     * back to libc passthrough.
     */
    virtual int Shutdown(int how) noexcept
    {
        (void)how;
        return -1;
    }

    DEFINE_REF_OPERATION_FUNC

public:
    DECLARE_REF_COUNT_VARIABLE;                               /* ref count int16_t */
    SocketState state_ = SOCK_STAT_INIT;                      /* state of ubsocket */
    SocketType type_ = SocketType::SOCK_TYPE_TCP;             /* type of ubsocket */
    SocketCreateType create_type_ = SOCK_CREATE_TYPE_UNKNOWN; /* created because of what */
    int raw_socket_ = -1;                                     /* fd of raw socket */
    int event_fd_ = -1;                                       /* event fd */
    uint64_t conn_add_id = 0;                                 /* packed peer address: (ipv4 << 16) | port, 0 = unset */
    SplitTrace *split_trace_ = nullptr;
};

ALWAYS_INLINE SocketState Socket::State() const noexcept
{
    return state_;
}

ALWAYS_INLINE SocketType Socket::Type() const noexcept
{
    return type_;
}

ALWAYS_INLINE void Socket::State(const SocketState state) noexcept
{
    state_ = state;
    if (state == SOCK_STAT_CLOSE) {
        TRACE_FLUSH(split_trace_);
    }
}

ALWAYS_INLINE int Socket::Fd() const noexcept
{
    return raw_socket_;
}

ALWAYS_INLINE uint64_t Socket::GetPeerAddrId() const noexcept
{
    return conn_add_id;
}

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

const std::string &SocketStateToStr(const SocketState value);
const std::string &SocketTypeToStr(SocketType value);
const std::string &SocketCreateTypeToStr(SocketCreateType value);
bool SocketStateValid(SocketState value);
bool SocketTypeValid(SocketType value);
bool SocketCreateTypeValid(SocketCreateType value);
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CORE_TYPES_H
