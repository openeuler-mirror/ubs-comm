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
#ifndef UBS_COMM_UMQ_SOCKET_H
#define UBS_COMM_UMQ_SOCKET_H

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include <new>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_bounded_seq.h"
#include "core/umq/umq_buffer_receive_queue.h"
#include "core/umq/umq_data_plane.h"
#include "core/umq/umq_data_rx_ops.h"
#include "core/umq/umq_data_tx_ops.h"
#include "core/umq/umq_setting.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/cli_message.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
class UbsBigdataSocketState;

namespace umq {

enum class JettyAllocState : uint8_t
{
    IDLE,    // 空闲状态
    WAITING, // 等待Jetty资源分配
};

using UmqSocketSeq =
    UmqSocketBoundedSequence<UmqSetting::UMQ_SOCKET_SEQ_NUM_BIT_WIDTH, uint32_t, UmqSetting::UMQ_SOCKET_SEQ_NUM_MAX>;

/* RNR 时延汇总打印阈值：累计到该样本数后打印一次 min/avg/max 并清零累计 */
constexpr uint32_t UMQ_RNR_LATENCY_PRINT_CNT = 10;

/* 冷侧构：仅在罕见路径（进入 RNR 反压 / 光组网 used_ports / FC 事件注册 /
 * setsockopt 指定 RPC 超时）首次触达时经 CAS 惰性创建（模式与 bigdata_state_
 * 一致，禁 std::call_once）。常规链路终生不分配，UmqSocket 本体只留 8B 指针。 */
struct UmqSocketCold {
    /* RNR 定位统计（诊断用） */
    std::atomic<uint64_t> rnr_enter_cnt{0};      /* 进入反压次数 */
    std::atomic<uint64_t> rnr_recover_cnt{0};    /* 解除反压（恢复）次数 */
    std::atomic<uint64_t> rnr_timeout_cnt{0};    /* 重传耗尽断链（status=99）次数 */
    std::atomic<uint64_t> rnr_block_start_ns{0}; /* 最近一次进入反压的时间戳 */
    std::atomic<uint64_t> rnr_latency_sum_us{0}; /* 时延累计（us） */
    std::atomic<uint64_t> rnr_latency_cnt{0};    /* 已累计时延样本数 */
    std::atomic<uint64_t> rnr_latency_min_us{0}; /* 时延最小值（us） */
    std::atomic<uint64_t> rnr_latency_max_us{0}; /* 时延最大值（us） */
    /* 异常 CQE 时标记这些 port 不可用（光组网；num==0 的链路不再分配） */
    std::unique_ptr<umq_port_id_t[]> used_ports;
    std::size_t used_ports_num = 0;
    /* design §4.2: setsockopt(UBS_OPT_RPC_TIMEOUT_MS) 指定的本端超时，0 = 未设 */
    uint32_t local_rpc_timeout_ms = 0;
    int fc_event_fd = -1;
};

/* 进程级 EID 四元组共享表：同一 (本端 conn/bonding, 对端 peer/peer_bonding)
 * 组合的所有链路共享一份 64B 快照条目。典型现场 4 万链路对 ~48 个对端×少量本端
 * 设备 → 数百倍去重，每链路只留 8B 条目指针。仅建链（Acquire）、拆链（Release）、
 * 首次 CLI 查询触达，全部操作持分片互斥锁，临界区为 O(1) 哈希操作，无阻塞调用。 */
class PeerEidTable {
public:
    struct Entry {
        umq_eid_t conn_eid{};
        umq_eid_t peer_eid{};
        umq_eid_t bonding_eid{};
        umq_eid_t peer_bonding_eid{};
        uint32_t ref_cnt = 0; /* 由所在分片锁保护 */
    };

    static PeerEidTable &Instance();

    /* 查找或插入四元组条目并加引用；分配失败返回 nullptr（CLI 显示回退为全零）。 */
    Entry *Acquire(const umq_eid_t &conn, const umq_eid_t &peer, const umq_eid_t &bonding,
                   const umq_eid_t &peer_bonding);
    /* 解引用；归零即从表中摘除并释放。entry 为 nullptr 时空操作。 */
    void Release(Entry *entry);

private:
    static constexpr std::size_t STRIPE_NUM = 16;
    struct Key {
        uint8_t raw[UMQ_EID_SIZE * 4];
        bool operator==(const Key &o) const
        {
            return memcmp(raw, o.raw, sizeof(raw)) == 0;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key &k) const
        {
            /* FNV-1a */
            std::size_t h = 1469598103934665603ULL;
            for (uint8_t b : k.raw) {
                h = (h ^ b) * 1099511628211ULL;
            }
            return h;
        }
    };
    struct Stripe {
        std::mutex mtx;
        std::unordered_map<Key, std::unique_ptr<Entry>, KeyHash> map;
    };
    static Key MakeKey(const Entry &e);
    Stripe stripes_[STRIPE_NUM];
};

class UmqSocket
    : public SocketBase
    , public UmqSocketSeq {
public:
    /* container_of 回查（ops → DataPlaneEntry → owner） */
    friend class ::ock::ubs::DataTxOps;
    friend class ::ock::ubs::DataRxOps;

    explicit UmqSocket(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ) {}

    ~UmqSocket() override
    {
        UnInitialize();
        /* 数据面条目：仅当仍归本 socket 所有（fd 复用后新 socket 可能已重建）才销毁 */
        DataPlaneEntry *e = DataPlaneTable::Instance().Peek(raw_socket_);
        if (e != nullptr && e->owner == this) {
            DataPlaneTable::DestroyEntry(e);
        }
        SocketExt *ext = ExtOrNull();
        if (ext != nullptr) {
            delete static_cast<UmqSocketCold *>(ext->cold.load(std::memory_order_acquire));
            PeerEidTable::Instance().Release(
                static_cast<PeerEidTable::Entry *>(ext->eids_entry.load(std::memory_order_acquire)));
        }
    }

    Result Initialize() noexcept override;
    void UnInitialize() noexcept override;

    bool IsBonding() const noexcept
    {
        return is_bonding_;
    }
    void SetBonding(bool bonding)
    {
        is_bonding_ = bonding;
    }

    uint64_t UmqHandle() const noexcept
    {
        return umq_handle_;
    }

    uint64_t ShareUmqHandle() const noexcept
    {
        return share_umq_handle_;
    }

    /* bigdata engine per-socket state (lazily created on first READ_OFFER). */
    UbsBigdataSocketState *GetOrCreateBigdataState();
    UbsBigdataSocketState *GetBigdataState() const noexcept;
    UbsBigdataSocketState *ReleaseBigdataState() noexcept;

    /* 冷侧构（见 UmqSocketCold）：CAS 惰性创建，失败返回 nullptr（调用方降级）。 */
    UmqSocketCold *GetOrCreateCold() noexcept;
    UmqSocketCold *GetCold() const noexcept
    {
        SocketExt *ext = ExtOrNull();
        return ext != nullptr ? static_cast<UmqSocketCold *>(ext->cold.load(std::memory_order_acquire)) : nullptr;
    }

    bool IsBindRemote() override
    {
        return umq_is_bind_remote_;
    }

    void SetBindRemote(bool bound)
    {
        umq_is_bind_remote_ = bound;
    }

    bool IsBindind() const
    {
        return is_bonding_;
    }

    void SetIsBind(bool bound)
    {
        is_bonding_ = bound;
    }

    ub_trans_mode GetTransMode() const
    {
        return static_cast<ub_trans_mode>(trans_mode_);
    }

    void SetTransMode(ub_trans_mode mode)
    {
        trans_mode_ = static_cast<uint8_t>(mode);
    }

    umq_topo_type_t GetTopoType()
    {
        return static_cast<umq_topo_type_t>(topo_type_);
    }

    void SetTopoType(umq_topo_type_t type)
    {
        topo_type_ = static_cast<uint8_t>(type);
    }

    uint32_t GetNegotiatedVersion() const
    {
        return negotiated_version_;
    }

    void SetNegotiatedVersion(uint32_t version)
    {
        /* 协商结果 = min(本端, 对端)，恒 ≤ 本端版本，u16 足够 */
        negotiated_version_ = static_cast<uint16_t>(std::min<uint32_t>(version, UINT16_MAX));
    }

    uint32_t GetPeerVersion() const
    {
        return peer_version_;
    }

    void SetPeerVersion(uint32_t version)
    {
        /* 仅诊断展示；异常对端的超大版本号钳位到 u16 上限 */
        peer_version_ = static_cast<uint16_t>(std::min<uint32_t>(version, UINT16_MAX));
    }

    /* design §4.2: connection-level RPC timeout (ms). local timeout is set by
     * the client via setsockopt(UBS_OPT_RPC_TIMEOUT_MS) before connect (opt-in,
     * lives in UmqSocketCold); peer_rpc_timeout_ms_ is learned from the peer's
     * NegotiateReq at accept (per-RPC deadline input, stays in the hot object).
     * The bigdata engine derives sender pinned deadline and receiver ctx deadline
     * from effective_timeout = local!=0 ? local : peer. 0 = not set / old peer. */
    uint32_t GetLocalRpcTimeoutMs() const noexcept
    {
        const UmqSocketCold *cold = GetCold();
        return cold != nullptr ? cold->local_rpc_timeout_ms : 0;
    }
    void SetLocalRpcTimeoutMs(uint32_t ms) noexcept
    {
        UmqSocketCold *cold = GetOrCreateCold();
        if (cold == nullptr) {
            /* 分配失败：放弃记录（等价于未设置，走 peer/默认超时），不影响链路可用性 */
            UBS_VLOG_ERR("SetLocalRpcTimeoutMs dropped, fd: %d, reason: cold-part alloc failed\n", raw_socket_);
            return;
        }
        cold->local_rpc_timeout_ms = ms;
    }
    uint32_t GetPeerRpcTimeoutMs() const noexcept
    {
        return peer_rpc_timeout_ms_;
    }
    void SetPeerRpcTimeoutMs(uint32_t ms) noexcept
    {
        peer_rpc_timeout_ms_ = ms;
    }

    ALWAYS_INLINE void NewRxEpollIn()
    {
        DataRxOps *ops = GetRxOps();
        if (UNLIKELY(ops == nullptr)) {
            /* Close-vs-deliver race: the socket object is still ref-held by the
             * delivery batch, but its DataPlane slot is already torn down
             * (issue #30 core 6: fetch_add on nullptr+8). The link is closing —
             * drop the event; the close path drains/acks its own queue. */
            return;
        }
        if (ops->epoll_event_num_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            ops->get_and_ack_event_ = true;
            ops->poll_ = true;
            ops->expect_epoll_event_num_ = 1;
        }
    }

    ALWAYS_INLINE void NewTxEpollIn()
    {
        DataTxOps *ops = GetTxOps();
        if (UNLIKELY(ops == nullptr)) {
            return; /* same close-vs-deliver window as NewRxEpollIn */
        }
        if (ops->epoll_event_num_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            ops->get_and_ack_event_ = true;
            ops->expect_epoll_event_num_ = 1;
        }
    }

    ALWAYS_INLINE bool TryAcquireForWaiting() noexcept
    {
        JettyAllocState expected = JettyAllocState::IDLE;
        return jetty_alloc_state_.compare_exchange_strong(expected, JettyAllocState::WAITING, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed);
    }

    ALWAYS_INLINE void ResetToIdle() noexcept
    {
        jetty_alloc_state_.store(JettyAllocState::IDLE, std::memory_order_release);
    }

    ALWAYS_INLINE JettyAllocState GetJettyAllocState() noexcept
    {
        return jetty_alloc_state_.load(std::memory_order_acquire);
    }

    /* RNR backpressure: set when a soft RNR notification (透传) arrives from
     * URMA while the WR is still being retried in flight. While blocked, the
     * socket must not produce EPOLLOUT and writev/ubs_post must return EAGAIN. */
    ALWAYS_INLINE bool IsRnrBlocked() const noexcept
    {
        return rnr_blocked_.load(std::memory_order_acquire);
    }

    ALWAYS_INLINE void SetRnrBlocked(bool blocked) noexcept
    {
        rnr_blocked_.store(blocked, std::memory_order_release);
    }

    /* RNR 反压 fatal 超时判定 + 断链认领：连续反压超过
     * GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS 后返回 true；TryRnrBlockFatal
     * 通过 CAS 把 rnr_blocked_ 从 true 抢到 false（防与恢复路径/其他检查点并发
     * 竞态导致重复断链），返回 true 表示本线程负责执行断链（shutdown + State(CLOSE)）。
     * 定义在 umq_socket.cpp。 */
    bool IsRnrBlockFatal() const noexcept;
    bool TryRnrBlockFatal() noexcept;

    bool CanNotifyWritable() const override
    {
        return !IsRnrBlocked();
    }

    /* 写路径 (writev/ubs_post) 因 RNR 反压返回 EAGAIN 时的断链认领钩子：
     * 返回 true 表示本线程已执行断链。 */
    bool FatalIfWriteBlocked() override
    {
        return TryRnrBlockFatal();
    }

    /* RNR 定位统计：进入/恢复/超时断链 per-socket 计数 + 进入→恢复时延（累计 10 次打印一次汇总）。 */
    void OnRnrEnter() noexcept;
    void OnRnrRecover() noexcept;
    void OnRnrTimeout() noexcept;

    Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override;
    Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override;
    bool ShouldRegisterTxEvent() override;
    Result ProcessEpollEvent(struct epoll_event &event) override;
    int GetTxFd() override;
    void SetAddedEpollFd(EventPoll *fd, const epoll_data_t &data = {}) override;
/* ↑ RX/EPOLL_ADD 交接竞态兜底（RX_RESCUE）实现于此 override：EPOLL_ADD 时发现 RX 队列已有
 * 早到数据则补发通知。交叉 ack（UBSOCKET_EARLY_ACK）令客户端提前 ~1 RTT ESTABLISHED，
 * 首包可能早于服务端把 fd 交付数据面——强依赖该兜底。下方为存在性标记：把建链快路径
 * cherry-pick 到无此兜底的分支时，umq_socket_acceptor.cpp 的 #error 会在编译期拦截。 */
#define UBS_HAS_RX_EPOLL_ADD_RESCUE 1

    Result UpdateRxQueueAvailNum();
    Result CreateLocalUmq(const umq_eid_t *conn_eid, umq_used_ports_t &used_ports, umq_topo_type_t &topo_type);
    void UnbindAndFlushRemoteUmq(Socket *sock, uint32_t flush_timeout_ms = FLUSH_TIMEOUT_MS);
    void DestroyLocalUmq();

    /* ---- 延迟拆链（由 TxCqePoller::RetireSweep 在 poller 线程驱动） ----
     * 原路径：brpc 关闭 fd → 最后一个引用释放 → ~UmqSocket → UnInitialize 在 brpc worker 上
     * 同步 unbind + 忙轮询 drain（对端遗留在途 WR 时最长 UMQ_DESTROY_FLUSH_TIMEOUT_MS）+ destroy。
     * 规模拆链下数十个这样的关闭即可占满 worker 池，令建链严重变慢。
     * 现改为：DelSocket 把 socket 挂到 poller 的 retire 列表；poller 每 tick 对其调用
     * RetireStep() 推进一个有界切片；drain 完成或到达 deadline 后释放最后引用，
     * 此时 ~UmqSocket 里的 UnInitialize 只剩 destroy（drain 已完成，不再阻塞）。 */

    /* 第一次调用：解绑远端 + 注销 FC 事件；之后每次调用：推进一次有界 TX/RX drain 切片。
     * @return true = drain 已完成（TX 队列已空或无在途 WR），可释放；false = 仍有在途 WR。 */
    bool RetireStep() noexcept;

    bool IsRetiring() const noexcept
    {
        return retiring_.load(std::memory_order_acquire);
    }

    /* retire 已推进到"无在途 WR"。仅 reaper 线程读写（retire_drained_ 无原子），
     * 供 RetireSweep 的 deadline 兜底判断（提前释放数据面槽位的安全前提） */
    bool RetireDrained() const noexcept
    {
        return retire_drained_;
    }
    int AddQbuf(umq_buf_t *qbuf);
    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size);
    void FlushRxQueue();
    bool RxQueueEmpty();
    Result CheckDevAdd(const umq_eid_t &conn_eid);

    std::tuple<const umq_port_id_t *, std::size_t> GetUsedPorts() const;

    /* GetData Func Set For CLI*/
    virtual void OutputStats(std::ostringstream &oss);
    virtual void GetSocketCLIData(Statistics::CLISocketData *data);
    virtual void GetSocketFlowControlData(Statistics::CLIFlowControlData *data);
    virtual void GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data);
    virtual void GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data);
    virtual void GetSocketIoPacketData(Statistics::CLIIoPacketData *data);
    virtual void GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data);

    /* 建链成功：把此后仍会读取的 EID/身份信息收拢到 socket 本体，随后释放
     * 两个协商 ops（UmqAcceptorOps/UmqConnectorOps，每链路合计 ~0.3-0.6KB）。 */
    void OnEstablished() override;

    /* Rx/Tx ops 按值内嵌：建链/重协商（DoUbAcceptRetry/DoUbConnectRetry 后重进
     * GenerateSocketCommOps）时以 placement-new 重建，语义等价于旧实现的
     * 释放旧 ops + new 新 ops。返回非拥有指针，由 DataTx/DataRx 持有。 */
    /* 数据面条目装配。调用顺序约定（GenerateSocketCommOps 是唯一调用点）：
     * ReinitTxOps 先行、负责（重）构造整个条目——重协商路径上等价于旧实现
     * 对两个 ops 的 placement-new 重建；ReinitRxOps 仅取址。
     * 页/条目分配失败返回 nullptr，Generate 侧按 UBS_MALLOC_FAILED 处理。 */
    UmqTxOps *ReinitTxOps()
    {
        DataPlaneEntry *e = DataPlaneTable::Instance().SlotFor(raw_socket_);
        if (e == nullptr) {
            return nullptr;
        }
        if (e->owner == this) {
            /* 本 socket 重协商：销毁自己的旧条目后原地重建（原语义） */
            e->~DataPlaneEntry();
        } else if (e->owner != nullptr) {
            /* 槽位仍归另一个 socket 所有 —— 该 socket 尚未走到 ~UmqSocket
             * （DestroyEntry 会把 owner 清零），即它仍然活着并在使用这个条目：
             * 它的 DataTx/DataRx 壳指向此处，共享 JFR runner 也可能正在向它派发。
             * 此时销毁+重建等于把别人的 TxStatCounters 释放掉、并让它的壳悬空，
             * 现场表现为 free() 处 glibc abort（SIGABRT）或随后 SIGSEGV。
             * 该窗口来自 fd 复用：旧 socket 的 fd 已关闭并被内核复用给新 accept，
             * 而旧 socket 对象仍被引用（brpc 侧未放手 / 未走 reaper 的关闭路径）。
             * 安全出口是拒绝而非破坏：返回 nullptr，Generate 侧按
             * UBS_MALLOC_FAILED 处理（本条链建链失败，对端可重试），
             * 与析构侧 "仅当 owner == this 才销毁" 的保护对称。 */
            UBS_VLOG_ERR("data-plane slot for fd %d still owned by another socket (%p != %p); "
                         "refusing to reinit (fd reuse window)\n",
                         raw_socket_, static_cast<void *>(e->owner), static_cast<void *>(this));
            return nullptr;
        }
        new (e) DataPlaneEntry(raw_socket_, umq_handle_, this);
        return &e->tx;
    }

    /* 建链失败路径的急切清槽：socket 的最后一个引用可能进入 ArraySet 的延迟释放
     * 队列（OverrideItem → EnqueueDeferred），~UmqSocket 要等 reaper 的
     * DrainDeferredRelease 才执行；而 fd 在那之前就被关闭并可能被内核复用，于是
     * 新 socket 会在同一槽位上撞见"仍归旧 socket"的条目——这正是 fork#24 那批
     * core 的窗口来源。在制造该窗口的路径上主动清槽，窗口就不存在。
     * 安全性：拆除路径（UnbindAndFlushRemoteUmq / DestroyLocalUmq）只用
     * umq_handle_ 与 SocketExt，不依赖本条目；空 ops 已有判空保护。 */
    void ReleaseDataPlane() noexcept override
    {
        DataPlaneEntry *e = DataPlaneTable::Instance().Peek(raw_socket_);
        if (e != nullptr && e->owner == this) {
            DataPlaneTable::DestroyEntry(e);
        }
    }

    void DiscardUnboundUmq() noexcept override
    {
        if (!umq_is_bind_remote_) {
            DestroyLocalUmq();
        }
    }

    UmqRxOps *ReinitRxOps()
    {
        DataPlaneEntry *e = DataPlaneTable::Instance().Peek(raw_socket_);
        if (e == nullptr || e->owner != this) {
            return nullptr;
        }
        return &e->rx;
    }

    UmqTxOps *GetUmqTxOps()
    {
        DataPlaneEntry *e = DataPlaneTable::Instance().Peek(raw_socket_);
        return (e != nullptr && e->owner == this) ? &e->tx : nullptr;
    }
    UmqRxOps *GetUmqRxOps()
    {
        DataPlaneEntry *e = DataPlaneTable::Instance().Peek(raw_socket_);
        return (e != nullptr && e->owner == this) ? &e->rx : nullptr;
    }

private:
    uint64_t CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid);
    uint64_t GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid);
    uint64_t RegisterFcTxEvent();
    void UnregisterFcTxEvent();


    /* 建链完成后从协商 ops 收拢的 EID 快照（64B），供 CLI 查询使用；
     * 快照落位后协商 ops 即释放，不再随链路常驻 */
    
    // 链接类型相关
    bool is_bonding_ = false;
    /* 枚举值域极小，u8 存储；访问器收敛类型 */
    uint8_t trans_mode_ = static_cast<uint8_t>(RM_TP);
    uint8_t topo_type_ = static_cast<uint8_t>(UMQ_TOPO_TYPE_FULLMESH_1D);
    /* 版本协商（u16 存储：协商结果恒 ≤ 本端版本；对端原始值仅诊断用，钳位存放） */
    uint16_t negotiated_version_ = 0;
    uint16_t peer_version_ = 0;
    uint32_t peer_rpc_timeout_ms_ = 0; /* design §4.2: learned from peer NegotiateReq（每请求 deadline 输入，留热侧） */
    // UMQ bind
    bool umq_is_bind_remote_ = false;
    /* 延迟拆链状态：RetireStep 首次调用后置位；~UmqSocket 据此跳过同步 drain。
     * 跨线程访问：reaper 写，数据面线程（share-JFR runner / TxCqePoller）读 */
    std::atomic<bool> retiring_{false};
    bool retire_drained_ = false;
    // UMQ 句柄
    uint64_t umq_handle_ = UMQ_INVALID_HANDLE;

    uint64_t share_umq_handle_ = UMQ_INVALID_HANDLE;

    std::unique_ptr<UmqBufferReceiveQueue> rxQueue;

public:
    /* Four-point-trace byte-offset bridge: connection-level cumulative bytes
     * delivered to the caller via ubs_poll. Advanced only on the ubs_poll
     * consumer thread (single-consumer of the SPSC rxQueue), so a plain
     * uint64_t suffices. Mirrors brpc's per-socket _ub_rx_cursor on the same
     * byte stream so the offline tool can align cid <-> SN. */
    uint64_t delivered_bytes_ = 0;

private:
    std::atomic<JettyAllocState> jetty_alloc_state_{JettyAllocState::IDLE};

    // RNR 反压标志：置位期间抑制 EPOLLOUT 并使 writev/ubs_post 返回 EAGAIN
    std::atomic<bool> rnr_blocked_{false};

    /* 冷侧构 / bigdata state / EID 条目均存于 SocketExt 侧车槽位（见 core_types） */

    /* AddQbuf (the SPSC RX queue producer side) can be entered from two
     * threads in share-JFR mode: the share-JFR runner and the TxCqePoller's
     * bigdata READ-completion chain (PollTx → HandleTxCompletion → FinalizeIo
     * → DeliverToRxQueue → AddQbuf). SPSC assumes one producer, so the two
     * are serialised per socket. This used to be one LockRegistry lock per
     * socket (bthread::Mutex wrapper + pooled Butex ≈ 0.1 KB per link, the
     * "brpc_external_lock_create" line of the per-link profile). It is now
     * a process-wide table of RX_ENQ_LOCK_STRIPES locks indexed by fd: all
     * producers of one socket always hit the same stripe, so mutual
     * exclusion per socket is unchanged; two sockets sharing a stripe only
     * ever contend when both are in the (rare) bigdata READ-completion
     * path at the same instant. Non-share-JFR builds never take it (AddQbuf
     * has no callers there). */
    static constexpr uint32_t RX_ENQ_LOCK_STRIPES = 64;
    static u_mutex_t *RxEnqueueLockFor(int fd) noexcept;
};
using UmqSocketPtr = Ref<UmqSocket>;

// UmqAcceptor 和 UmqConnector 共用结构体
struct CpMsg {
    uint64_t protocol_negotiation = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    uint64_t queue_bind_info_size;
    uint8_t queue_bind_info[UMQ_BIND_INFO_SIZE_MAX];
};

/* NegotiateReq.cap_flags / NegotiateRsp.reserved[0] 建链能力位：
 * NEGO_CAP_EARLY_ACK       交叉 ack 轮 —— 服务端先发本端结果再收对端 ack（应答不含回声）
 * NEGO_CAP_DEGRADE_CONSENT 仅 rsp 使用 —— 服务端 UBS_ENABLE_DEGRADE=true（替代回声中的降级共识） */
constexpr uint8_t NEGO_CAP_EARLY_ACK = 0x1;
constexpr uint8_t NEGO_CAP_DEGRADE_CONSENT = 0x2;
/* 方案A'（NEGO_CAP_CARRY_BINDINFO）：客户端声明可解析 NegotiateRsp 尾部携带的
 * 服务端 bind_info（NegotiateRspExt），服务端确认后不再单独发送 CpMsg——
 * 握手 6 腿减为 5 腿，客户端消除一整轮对服务端 CpMsg 的等待。 */
constexpr uint8_t NEGO_CAP_CARRY_BINDINFO = 0x4;
/* 方案B（NEGO_CAP_REQ_CARRY_BINDINFO）：客户端把本端（预建 umq 的）bind_info 挂在
 * NegotiateReq 尾部（NegotiateReqExt）随请求送达；服务端在协商应答前完成
 * create+bind，应答尾部除自身 bind_info 外再携带 bind 结果（server_bind_ret，
 * 即经典腿⑥）——握手 5 腿减为 3 条消息，客户端整轮 ack 等待消失。
 * 服务端仅在（开关开 && 协商后的 trans_mode == 客户端提议值）时确认并消费；
 * 其余情况不置位，双方自动回退 方案A'/经典 路径。 */
constexpr uint8_t NEGO_CAP_REQ_CARRY_BINDINFO = 0x8;
/* 并行 bind（NEGO_CAP_DEFER_BIND_RET，叠加在 方案B 之上）：客户端声明可在末尾 ack 轮
 * 接收服务端的 bind 结果；服务端确认后先发应答（只带自身 bind_info）、再做 umq_bind，
 * 两端的 bind 重新并行，bind 结果改以交叉 ack 送出（与 NEGO_CAP_EARLY_ACK 同型，无回声）。
 * 任一端未置位或开关关 ⇒ 退回 方案B（应答前 bind、结果随应答）。 */
constexpr uint8_t NEGO_CAP_DEFER_BIND_RET = 0x10;

struct NegotiateReq {
    ub_trans_mode trans_mode = RM_TP;
    uint8_t is_bonding = 0;
    uint8_t enable_share_jfr = 0;
    uint8_t schedule_policy = static_cast<uint8_t>(dev_schedule_policy::ROUND_ROBIN);
    umq_eid_t local_eid = {};
    uint32_t rpc_timeout_ms = 0; /* design §4.2: client RPC timeout for receiver ctx deadline; 0 = not set */
    /* 建链能力位（NEGO_CAP_*）。新增尾部字段的前后兼容由 RecvLengthPrefixed 保证：
     * 老服务端对超长 body 静默丢弃多余字节；新服务端对老客户端的短 body 零填充。 */
    uint8_t cap_flags = 0;
    uint8_t cap_rsv[3] = {0};
};

struct NegotiateRsp {
    int32_t ret_code = 0;
    int32_t aff_sock_id = 0;
    ub_trans_mode peer_trans_mode = RM_TP;
    uint8_t is_bonding = 0;
    uint8_t reserved[2] = {0}; /* [0]: 服务端能力位 NEGO_CAP_*（老服务端恒为 0） */
    uint32_t socket_id_count = 0;
    uint32_t socket_ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    umq_eid_t local_eid = {};
};

/* 方案A' 扩展应答：NegotiateRsp 尾部携带服务端 bind_info。两侧共用同一结构
 * 保证内存布局一致（发送长度 = offsetof(bind_info) + 实际 size）。老客户端按
 * sizeof(NegotiateRsp) 接收、多余字节被 RecvLengthPrefixed 丢弃；新客户端整体
 * 接收、老服务端的短 body 被零填充 ⇒ bind_info_size==0 且能力位不置，自描述回退。 */
struct NegotiateRspExt {
    NegotiateRsp rsp;
    /* 方案B：服务端提前 bind 的结果（经典腿⑥ 的 Result 语义）。仅当
     * reserved[0] 置 NEGO_CAP_REQ_CARRY_BINDINFO 时有意义；老服务端短 body
     * 零填充 ⇒ 恒 0 且能力位不置，客户端自动回退经典 ack 轮。 */
    int32_t server_bind_ret = 0;
    uint32_t ext_rsv = 0;
    uint64_t bind_info_size = 0;
    uint8_t bind_info[UMQ_BIND_INFO_SIZE_MAX] = {};
};

/* 方案B 扩展请求：NegotiateReq 尾部携带客户端（预建 umq 的）bind_info。
 * 兼容支柱与 Rsp 同款：老服务端 RecvLengthPrefixed 丢弃超长 body；新服务端对
 * 老客户端的短 body 零填充 ⇒ bind_info_size==0。发送长度 = offsetof(bind_info)
 * + 实际 size。 */
struct NegotiateReqExt {
    NegotiateReq req;
    uint64_t bind_info_size = 0;
    uint8_t bind_info[UMQ_BIND_INFO_SIZE_MAX] = {};
};

struct NegotiateRoute {
    enum : uint32_t
    {
        BACK_ROUTE_MAX_NUM = 3
    };
    umq_topo_type_t topo_type;
    umq_route master_route;
    umq_route back_routes[BACK_ROUTE_MAX_NUM];
    uint32_t back_route_num{0};
    NegotiateRoute() = default;
    NegotiateRoute(umq_topo_type_t t_type, const umq_route &m_route, const std::vector<umq_route_t> &b_routes)
        : topo_type(t_type),
          master_route(m_route),
          back_route_num(std::min(static_cast<uint32_t>(b_routes.size()), static_cast<uint32_t>(BACK_ROUTE_MAX_NUM)))
    {
        for (uint32_t i = 0; i < back_route_num; ++i) {
            back_routes[i] = b_routes[i];
        }
    }
};

struct OtherRouteMessage {
    UBHandshakeState ub_handshake_state;
    umq_route_t other_route;
    umq_route_t other_back_route;
};

// BuildNegotiateReqBuffer total wire size: magic(8) + version(4) + body_len(4) + body
// 方案B：body 上限为 NegotiateReqExt 的 offsetof(bind_info) + 实际 bind_info 长度
constexpr uint32_t NEGOTIATE_REQ_WIRE_SIZE =
    sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(NegotiateReqExt);
static_assert(NEGOTIATE_REQ_WIRE_SIZE <= NEGOTIATE_REQ_BUFFER_SIZE,
              "NegotiateReqExt grew beyond NEGOTIATE_REQ_BUFFER_SIZE");

#ifndef EID_FMT
#define EID_FMT "%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"
#endif

#ifndef EID_RAW_ARGS
#define EID_RAW_ARGS(eid)                                                                                      \
    eid[0], eid[1], eid[2], eid[3], eid[4], eid[5], eid[6], eid[7], eid[8], eid[9], eid[10], eid[11], eid[12], \
        eid[13], eid[14], eid[15]
#endif

#ifndef EID_ARGS
#define EID_ARGS(eid) EID_RAW_ARGS((eid).raw)
#endif

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SOCKET_H
