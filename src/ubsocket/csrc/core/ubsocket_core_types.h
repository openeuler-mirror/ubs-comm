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

#include <arpa/inet.h> // INET6_ADDRSTRLEN

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "profiling/trace/ubsocket_trace.h"

/* StatsMgr 定义于全局 ::Statistics 命名空间（statistics_statsmgr.h）；此处仅前置声明 */
namespace Statistics {
class StatsMgr;
}

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

struct ConnInfo {
    /* 定长 IP 字符串，理由同 RawConnInfoV4::peer_ip */
    char peer_ip[INET6_ADDRSTRLEN] = {}; // 对端IP地址
    int peer_fd = -1;                    // 对端socket fd
    int type_fd = 0;                     // 0 server; 1 client
    std::chrono::system_clock::time_point create_time;

    void SetPeerIp(const char *ip)
    {
        int ret = snprintf(peer_ip, sizeof(peer_ip), "%s", (ip != nullptr) ? ip : "");
        if (ret < 0) {
            peer_ip[0] = '\0';
        }
    }
};

struct RawConnInfoV4 {
    /* 二进制对端地址 + 秒粒度建链时间：原实现为 46B 字符串缓冲 + 8B time_point
     * （64B/链路），而地址只在 CLI 查询与个别错误日志时才需要字符串形态、
     * CLI 展示的建链时间本就是秒——改为存 16B 二进制地址（v4 占前 4 字节）
     * 与 u32 epoch 秒，查询时 inet_ntop 现场重建（28B/链路）。 */
    uint8_t peer_addr[16] = {};   // 对端地址二进制（v4 取前 4 字节）
    int32_t peer_fd = -1;         // 对端socket fd
    uint32_t create_time_sec = 0; // 建链时刻（epoch 秒）
    uint8_t type_fd = 0;          // 0 server; 1 client
    uint8_t addr_family = 0;      // 0 未设置；AF_INET / AF_INET6

    void SetPeerIp(const char *ip)
    {
        if (ip != nullptr && inet_pton(AF_INET, ip, peer_addr) == 1) {
            addr_family = AF_INET;
        } else if (ip != nullptr && inet_pton(AF_INET6, ip, peer_addr) == 1) {
            addr_family = AF_INET6;
        } else {
            /* 上游恒为 inet_ntop 产物；异常输入按未设置处理（重建为空串） */
            addr_family = 0;
        }
    }

    /* buf 建议至少 INET6_ADDRSTRLEN；未设置/失败得到空串，返回 buf */
    const char *GetPeerIpStr(char *buf, size_t len) const
    {
        if (len == 0) {
            return buf;
        }
        buf[0] = '\0';
        if (addr_family != 0) {
            inet_ntop(addr_family, peer_addr, buf, static_cast<socklen_t>(len));
        }
        return buf;
    }

    void SetCreateTimeNow()
    {
        create_time_sec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count());
    }
};

class Socket;
using SocketPtr = Ref<Socket>;

class SplitTrace;

/* 单 ext 侧车：把"每链路必有但不在每包路径上"的身份/诊断/生命周期指针从
 * Socket 对象移到一块惰性分配的侧车里——对象本体只留 8B 指针。
 * 各层通过本结构的槽位存取自己的部分（跨层指针以 void* 存放、由所属层的
 * 访问器做类型收敛，见 SocketBase/UmqSocket 的包装方法）。 */
struct SocketExt {
    RawConnInfoV4 conn_info;                      /* 连接身份（对端地址/角色/建链秒） */
    SplitTrace *split_trace = nullptr;            /* 分段 trace（trace 模式） */
    Statistics::StatsMgr *stats_mgr = nullptr;    /* SocketBase：trace 统计（惰性） */
    void *hs = nullptr;                           /* SocketBase::HandshakeCtx*（建链期，完成即释放） */
    std::atomic<void *> eids_entry{nullptr};      /* umq::PeerEidTable::Entry* */
    std::atomic<void *> cold{nullptr};            /* umq::UmqSocketCold* */
    std::atomic<void *> bigdata{nullptr};         /* UbsBigdataSocketState* */
};

class Socket {
public:
    Socket(int fd, SocketType type) : raw_socket_(fd), type_(type)
    {
    }
    virtual ~Socket()
    {
        SocketExt *ext = ext_.load(std::memory_order_acquire);
        if (ext != nullptr) {
            delete ext;
        }
    }

    ALWAYS_INLINE SocketState State() const noexcept
    {
        return state_.load(std::memory_order_acquire);
    }

    void State(SocketState state)
    {
        state_.store(state, std::memory_order_release);
    }

    SocketType Type() const noexcept
    {
        return type_;
    }

    int Fd() const noexcept
    {
        return raw_socket_;
    }

    /* 连接身份由 socket 自持（原分散在 acceptor/connector 两份 ops 的 conn_info 中），
     * 无 connector 亦可判定角色：未走过 connect 路径即非 client */
    ALWAYS_INLINE bool IsClient() const noexcept
    {
        const SocketExt *ext = ExtOrNull();
        return ext != nullptr && ext->conn_info.type_fd == 1;
    }

public:
    virtual int GetTxFd() = 0;
    virtual bool IsBindRemote() = 0;
    virtual Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) = 0;
    virtual Result DelTxEvent(const SocketPtr &sock, int epoll_fd) = 0;
    virtual bool ShouldRegisterTxEvent() = 0;
    virtual Result ProcessEpollEvent(struct epoll_event &event) = 0;
    DEFINE_REF_OPERATION_FUNC

    /* 引用是否仅剩调用方一份：retire 收尾用（close(fd) 押后到独占时刻）。
     * 前提：socket 已从 ArraySet 摘除（OverrideItem），引用只减不增，读到 1 即稳定 */
    ALWAYS_INLINE bool SoleRef() const
    {
        return __atomic_load_n(&ref_count_, __ATOMIC_ACQUIRE) == 1;
    }

public:
    DECLARE_REF_COUNT_VARIABLE;                               /* ref count int32_t */
    int raw_socket_ = -1;                                     /* fd of raw socket */
    std::atomic<SocketState> state_{SOCK_STAT_INIT};            /* state of ubsocket */
    SocketType type_ = SocketType::SOCK_TYPE_TCP;             /* type of ubsocket */
    SocketCreateType create_type_ = SOCK_CREATE_TYPE_UNKNOWN; /* created because of what */

    /* TxCqePoller bookkeeping (see TxCqePoller::MarkActive / AddSocket):
     * tx_poller_active_ — this socket is in the poller's active set (needs
     *   per-round attention: in-flight TX WRs, pending/posted READs, deadline
     *   pins). Set by whoever creates such work, cleared by the poller once
     *   it finds nothing left to do. Idle sockets cost the poller nothing.
     * tx_poller_slot_ — index in the poller's registry (-1 = not registered);
     *   makes AddSocket/DelSocket O(1). Guarded by the poller's mutex_. */
    std::atomic<bool> tx_poller_active_{false};
    std::atomic<int32_t> tx_poller_slot_{-1};
    /* 单 ext 侧车（见 SocketExt）：CAS 惰性创建；分配失败时读者按空侧车降级 */
    std::atomic<SocketExt *> ext_{nullptr};

    SocketExt *ExtOrNull() const noexcept
    {
        return ext_.load(std::memory_order_acquire);
    }
    SocketExt *EnsureExt() noexcept
    {
        SocketExt *ext = ext_.load(std::memory_order_acquire);
        if (ext != nullptr) {
            return ext;
        }
        auto *candidate = new (std::nothrow) SocketExt();
        if (candidate == nullptr) {
            return nullptr;
        }
        if (!ext_.compare_exchange_strong(ext, candidate, std::memory_order_release, std::memory_order_acquire)) {
            delete candidate;
        } else {
            ext = candidate;
        }
        return ext;
    }
    /* 连接身份访问器：读侧空侧车按"未设置"降级，写侧 EnsureExt 失败丢弃写入 */
    const RawConnInfoV4 *ConnInfoOrNull() const noexcept
    {
        SocketExt *ext = ExtOrNull();
        return ext != nullptr ? &ext->conn_info : nullptr;
    }
    RawConnInfoV4 *MutableConnInfo() noexcept
    {
        SocketExt *ext = EnsureExt();
        return ext != nullptr ? &ext->conn_info : nullptr;
    }
    uint32_t CreateTimeSec() const noexcept
    {
        const RawConnInfoV4 *ci = ConnInfoOrNull();
        return ci != nullptr ? ci->create_time_sec : 0;
    }
    const char *PeerIpStr(char *buf, size_t len) const noexcept
    {
        const RawConnInfoV4 *ci = ConnInfoOrNull();
        if (ci != nullptr) {
            return ci->GetPeerIpStr(buf, len);
        }
        if (len > 0) {
            buf[0] = '\0';
        }
        return buf;
    }
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
