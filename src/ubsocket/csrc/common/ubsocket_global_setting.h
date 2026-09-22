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
#ifndef UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
#define UBS_COMM_UBSOCKET_GLOBAL_SETTING_H

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <string>
#include <type_traits>

#include "include/ubsocket_def.h"
#include "ubsocket_defines.h"
#include "ubsocket_errno.h"
#include "ubsocket_logger.h"
#include <atomic>
#include "ubsocket_setting_validator.h"

namespace ock {
namespace ubs {
class GlobalSetting {
public:
    /* 进程退出标志：ubsocket_uninit() 置位一次。拆链路径据此跳过退出期
     * 无法推进的 drain 等待——umq 侧的 poll 入口在退出期被 g_ubsocket_exiting
     * 拦截、恒返回 0（umq_pro_ub.c），此时等 tx_outstanding 归零只会把每条
     * 链的超时全额烧完（fork#28：StopReaper 卡住的根因）。 */
    static void MarkExiting() noexcept
    {
        UBS_EXITING.store(true, std::memory_order_release);
    }
    static bool IsExiting() noexcept
    {
        return UBS_EXITING.load(std::memory_order_acquire);
    }

    static bool AsyncAcceptorEnabled() noexcept;
    static bool AsyncConnectorEnabled() noexcept;
    static bool AsyncEpollEnabled() noexcept;

    static uint16_t GetTxDepth() noexcept;

public:
    /* disable constructor */
    GlobalSetting() = delete;

    /**
     * @brief verify setting
     */
    static Result VerifySetting() noexcept;

    /**
     * @brief Load envs
     */
    static Result LoadEnv() noexcept;

    /**
      * @brief Get env, only provide three types,
      * use int64_t for int16_t, uint16_t, int32_t, uint32_t, int64_t
      * use double for float and double
      */
    static bool GetEnv(const std::string &name, int64_t &out) noexcept;
    static bool GetEnv(const std::string &name, float &out) noexcept;
    static bool GetEnv(const std::string &name, std::string &out) noexcept;

    /**
     * @brief Get env and validate by rule
     *
     * @param name         [in] name of the env and rule
     * @param out          [in] converted value
     * @return true if have env and validate result is ok
     * false if no env
     * false if has env but validate failed
     */
    static bool GetEnvAndValidate(const std::string &name, int64_t &out) noexcept;
    static bool GetEnvAndValidate(const std::string &name, float &out) noexcept;
    static bool GetEnvAndValidate(const std::string &name, std::string &out) noexcept;
    static bool GetEnvAndValidateNotEmpty(const std::string &name, std::string &out) noexcept;

    /**
     * @brief Add setting verify rules
     */
    static void AddRules() noexcept;

public:
    static std::mutex MUTEX;
    static uint32_t UBS_ALLOWED_PROTOCOL;              /* allowed protocol, from API */
    static bool UBS_NATIVE_TCP_MODE;                   /* native tcp mode, pass all logic of this library, from API */
    static bool UBS_MONITOR_ENABLE;                    /* if enable statistics/monitor, from env */
    static bool UBS_SPLIT_TRACE_ENABLED;               /* if enable split trace, from env */
    static uint32_t UBS_SPLIT_TRACE_SAMPLE_RATE;       /* split trace sample rate (1/N), from env */
    static uint32_t UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS; /* split trace drain interval in ms, from env */
    static bool UBS_CLI_ENABLED;
    static bool UBS_PROBE_ENABLED;
    static bool UBS_BACKUP_LINK_ENABLED;
    static bool UBS_INITED;                          /* if ubsocket initialized, from API */
    static std::string UBS_TRANS_MODE;               /* transport mode, from env */
    static int16_t UBS_ACCEPTOR_ASYNC_THREAD_COUNT;  /* if enable async acceptor, from API override by env */
    static int16_t UBS_CONNECTOR_ASYNC_THREAD_COUNT; /* if enable async connector, from API override by env */
    static int16_t UBS_EPOLL_ASYNC_THREAD_COUNT;     /* if enable async epoll_wait, from API override by env */
    static bool UBS_ACCEPTOR_ASYNC_ENABLED;          /* if enable async acceptor, from API override by env */
    static bool UBS_READV_UNLIMITED;                 /* if enable readv limit report, from env */
    static bool UBS_ENABLE_SHARE_JFR;                /* if enable share jfr, from env */
    static bool UBS_SHARE_JFR_LOOP_POLL_ENABLED;     /* if enable share jfr loop poll, from env */
    static bool UBS_ENABLE_DEGRADE;                  /* if enable degrade, from env */
    static bool UBS_CONNECT_PRECREATE;               /* connect: precreate local umq during negotiate RTT, from env */
    static bool UBS_EARLY_ACK;                       /* handshake: crossed (early) ack round capability, from env */
    static bool UBS_NEGO_CARRY_BINDINFO;             /* handshake 方案A': server bind_info piggybacks on NegotiateRsp */
    static bool UBS_NEGO_REQ_CARRY_BINDINFO;         /* handshake 方案B: client bind_info piggybacks on NegotiateReq,
                                                        server ack piggybacks on NegotiateRsp */
    static bool UBS_NEGO_PARALLEL_BIND;              /* handshake: on top of 方案B the server replies BEFORE umq_bind so
                                                        both sides bind in parallel; its bind result rides a crossed ack */
    static uint32_t UBS_TX_DEPTH;                    /* tx queue depth, from env */
    static uint32_t UBS_RX_DEPTH;                    /* rx queue depth, from env */
    /* TX/RX unified poller feature flag (default false: gradual rollout).
     * flag=OFF reverts to 1ms timer + PollAllSockets; flag=ON creates wake
     * eventfd + 100ms fallback timer (TX wake wired in slice 04). */
    static bool UBS_TX_UNIFIED_POLL_ENABLED;
    static bool UBS_TX_POLLER_ACTIVE_YIELD;       /* spin-then-yield in active loop */
    static uint32_t UBS_TX_POLLER_SPIN_ROUNDS;    /* spin rounds before backoff */
    static uint32_t UBS_TX_POLLER_BACKOFF_MAX_US; /* max backoff sleep in us */
    static uint32_t UBS_TX_POLLER_FALLBACK_MS;    /* fallback timer period in ms */
    static uint32_t UBS_TX_POLLER_FAST_PERIOD_US; /* FAST timer period in us, default 100 */
    static bool UBS_TX_POLLER_SLOW_SWITCH_ENABLED; /* FAST→SLOW timer adaptive switching, from env */

    /* RNR backpressure flow control (default on): treat the RNR soft signal as
     * a backpressure notification instead of a fatal error. When on, the socket
     * is blocked (no EPOLLOUT, writev/ubs_post return EAGAIN) until a successful
     * TX completion is observed again. The fatal RNR code still disconnects. */
    static bool UBS_RNR_BACKPRESSURE_ENABLED;

    /* RX high-water batch print threshold: when a single share-JFR event
     * cycle polls >= this many packets, print a diagnostic line (packet
     * count + elapsed time). Default POLL_BATCH_MAX (256). 0 disables the
     * print. */
    static uint32_t UBS_RX_BATCH_PRINT_THRESHOLD;

    /* RNR 反压 fatal 超时（ms）：写路径 (writev/ubs_post) 与 per-socket poll 路径
     * 检查到连续反压超过该时长后主动断链（shutdown(SHUT_RD) + State(CLOSE)，
     * brpc 读到 EOF 后 SetFailed）。0 = 关闭该机制，仅依赖 status=99 重传耗尽断链。 */
    static uint32_t UBS_RNR_FATAL_TIMEOUT_MS;

    /* Design UBSOCKET-READ-TIMEOUT-GEN-CHECK §6: READ_OFFER generation-check
     * feature flag (default off: gradual rollout). When off, the engine reverts
     * to the legacy behavior (no headroom, no sender timeout release, no
     * receiver gen validation). */
    static bool UBS_READ_GEN_CHECK_ENABLED;
    /* Design §6: fixed margin added to the RPC timeout to derive both the
     * sender pinned deadline and the receiver ctx deadline. Guarantees brpc
     * times out first, ubsocket releases second. */
    static uint32_t UBS_BIG_PIN_TIMEOUT_MARGIN_MS;
    /* Design §4.3: two-stage release grace period (ms) between headroom gen
     * invalidation (stage 1) and Block DecRef (stage 2). Must outlast a single
     * fallback tick so a late RDMA READ crossing the window reads gen=0. */
    static uint32_t UBS_GRACE_MS;
    /* bigdata 控制帧 SQ 槽位预留数（默认 0 = 关闭）。>0 时数据路径（READ WR 链 /
     * 发送端批量 SEND）post 前按 tx_queue_avail_num_ - 预留数 裁剪额度，保证
     * READ_DONE/READ_ABORT 等控制帧总能拿到 SQ 槽，不会因 SQ 长期打满被挤进
     * deferred-ctrl 队列直至溢出丢帧（丢 DONE → 发送端 pin 到超时才解）。
     * 控制路径（SendSimpleCtrl / DrainDeferredControls）不受限。
     * env: UBSOCKET_BIG_CTRL_RESERVED_SLOTS，建议 2~8；须 < UBS_TX_DEPTH/2，
     * 否则解析时告警并关闭该特性。 */
    static uint32_t UBS_BIG_CTRL_RESERVED_SLOTS;
    static bool UBS_PROF_ENABLE;
    static std::string UBS_PROF_MODE; /* profiling mode: "fast" or "ext" */
    static uint16_t UBS_PROF_DUMP_INTERVAL_MIN;
    static std::string UBS_PROF_DUMP_PATH;
    static std::mutex ProfDumpMutex; /* protects concurrent read/write of UBS_PROF_DUMP_PATH */
    static uint32_t UBS_PROBE_MS;
    static uint32_t UBS_PROBE_BATCH;
    static std::string UBS_PROBE_DUMP_PATH;
    static std::mutex ProbeDumpMutex;

    /* TX-STAT: 发送侧流控统计探测（默认开启） */
    static bool UBS_TX_STAT_ENABLE;
    static uint32_t UBS_TX_STAT_INTERVAL_MS;   /* 采样/落盘间隔，下限 1s */
    static std::string UBS_TX_STAT_FILE;       /* 落盘文件路径，<pid> 自动展开 */
    static uint32_t UBS_TX_STAT_MAX_MB;        /* 单文件上限，超限轮转 */
    static uint32_t UBS_TX_STAT_HEARTBEAT_SEC; /* 0=关闭；>0 时即使无异常也周期打一行 */

    static UBHandshakeMode UBS_HAND_SHAKE_MODE;
    static uint32_t UBS_THREAD_POOL_SIZE;
    static u_external_poller_ops_t *UBS_POLLER_OPS;

    /* trace statistic related */
    static uint64_t UBS_MONITOR_INTERVAL;
    static std::string UBS_MONITOR_FILE_PATH;
    /* KPI 落盘磁盘总占用上限（MB），默认 40，范围 [10,600]，由 UBSOCKET_MONITOR_FILE_SIZE 配置；
       单文件轮转大小由该上限按固定文件数推导 */
    static uint64_t UBS_MONITOR_FILE_SIZE_MB;

    // 通信链路选择，不直接由外部环境变量控制
    static LinkSelectionPolicy LINK_SELECTION_POLICY;

    // 故障 port 口冷却时间
    static uint32_t UBS_PORT_COOLDOWN_SEC;

    // 进程退出标志（见 MarkExiting/IsExiting）
    static std::atomic<bool> UBS_EXITING;
};

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, int64_t &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    // Regex mismatch: not an integer, e.g. "2min" will fail validation.
    static const std::regex num_regex("^-?[0-9]+$");
    if (!std::regex_match(envStr, num_regex)) {
        UBS_VLOG_WARN("Invalid value for %s: %s, should be an integer.\n", name.c_str(), envStr.c_str());
        return false;
    }
    try {
        out = static_cast<int64_t>(std::stol(envStr));
        return true;
    } catch (...) {
        UBS_VLOG_WARN("Invalid value for %s: %s, should be an integer.\n", name.c_str(), envStr.c_str());
        return false;
    }
}

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, float &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    // Validate number format (supports integers and decimals)
    // If regex mismatch, it is not a valid number → fail (e.g., "2.31min")
    static const std::regex float_regex("^-?[0-9]+(\\.[0-9]+)?$");
    if (!std::regex_match(envStr, float_regex)) {
        UBS_VLOG_WARN("Invalid value for %s: %s, should be an number.\n", name.c_str(), envStr.c_str());
        return false;
    }

    try {
        out = static_cast<int64_t>(std::stod(envStr));
        return true;
    } catch (...) {
        UBS_VLOG_WARN("Invalid value for %s: %s, should be an number.\n", name.c_str(), envStr.c_str());
        return false;
    }
}

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, std::string &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    out = envValue;
    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, int64_t &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().Validate(name, static_cast<int64_t>(out))) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, float &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().Validate(name, static_cast<float>(out))) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, std::string &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().ValidateStrEnum(name, out)) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidateNotEmpty(const std::string &name, std::string &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().ValidateStrEmpty(name, out)) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::AsyncAcceptorEnabled() noexcept
{
    return UBS_ACCEPTOR_ASYNC_THREAD_COUNT > 0 || UBS_ACCEPTOR_ASYNC_ENABLED;
}

ALWAYS_INLINE bool GlobalSetting::AsyncConnectorEnabled() noexcept
{
    return UBS_CONNECTOR_ASYNC_THREAD_COUNT > 0;
}

ALWAYS_INLINE bool GlobalSetting::AsyncEpollEnabled() noexcept
{
    return UBS_EPOLL_ASYNC_THREAD_COUNT > 0;
}

ALWAYS_INLINE uint16_t GlobalSetting::GetTxDepth() noexcept
{
    // TODO
    return 0;
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
