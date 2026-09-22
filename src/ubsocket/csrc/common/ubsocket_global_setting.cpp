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
#include "ubsocket_global_setting.h"
#include "ubsocket_setting_validator.h"

namespace ock {
namespace ubs {
std::mutex GlobalSetting::MUTEX;
uint32_t GlobalSetting::UBS_ALLOWED_PROTOCOL = 0; /* no protocol by default */
bool GlobalSetting::UBS_NATIVE_TCP_MODE = false;  /* use ubsocket by default */
bool GlobalSetting::UBS_MONITOR_ENABLE = true;
bool GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
uint32_t GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
uint32_t GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
bool GlobalSetting::UBS_CLI_ENABLED = false;
bool GlobalSetting::UBS_PROBE_ENABLED = false;
bool GlobalSetting::UBS_INITED = false;                      /* not inited by default */
std::string GlobalSetting::UBS_TRANS_MODE = "ub";            /* transport mode, from env */
int16_t GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;  /* disabled by default */
int16_t GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 0; /* disabled by default */
int16_t GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;     /* enabled by default */
bool GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_READV_UNLIMITED = true;
bool GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
bool GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
bool GlobalSetting::UBS_ENABLE_DEGRADE = true;
bool GlobalSetting::UBS_CONNECT_PRECREATE = true;
bool GlobalSetting::UBS_EARLY_ACK = true;
bool GlobalSetting::UBS_NEGO_CARRY_BINDINFO = true;
bool GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO = true;
bool GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
uint32_t GlobalSetting::UBS_TX_DEPTH = 255;
uint32_t GlobalSetting::UBS_RX_DEPTH = 2048;
std::atomic<bool> GlobalSetting::UBS_EXITING{false};
bool GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true; /* unified poller default on */
bool GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
uint32_t GlobalSetting::UBS_TX_POLLER_SPIN_ROUNDS = 64;
uint32_t GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 200;
uint32_t GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
uint32_t GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US = 100;
bool GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = false;
bool GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED = true;      /* rnr backpressure flow control, default on */
uint32_t GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD = 1000;  /* rx high-water batch print threshold */
uint32_t GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 60000;       /* rnr block fatal timeout ms, 0 = disabled */
bool GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = false;       /* design §6: gradual rollout, default off */
uint32_t GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 1000; /* design §6: brpc times out first */
uint32_t GlobalSetting::UBS_GRACE_MS = 100;                   /* design §4.3: two-stage release grace */
uint32_t GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS = 0;      /* SQ slots reserved for ctrl frames; 0 = off */
UBHandshakeMode GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
uint32_t GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
u_external_poller_ops_t *GlobalSetting::UBS_POLLER_OPS = nullptr;
bool GlobalSetting::UBS_PROF_ENABLE = false;
std::string GlobalSetting::UBS_PROF_MODE = "fast";
uint16_t GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
std::string GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
std::mutex GlobalSetting::ProfDumpMutex;
uint64_t GlobalSetting::UBS_MONITOR_INTERVAL = UBSOCKET_TRACE_TIME_DEFAULT;
std::string GlobalSetting::UBS_MONITOR_FILE_PATH = "/tmp/ubsocket/log";
uint64_t GlobalSetting::UBS_MONITOR_FILE_SIZE_MB = UBSOCKET_TRACE_FILE_SIZE_DEFAULT;
uint32_t GlobalSetting::UBS_PROBE_MS = 1000;
uint32_t GlobalSetting::UBS_PROBE_BATCH = 10;
std::string GlobalSetting::UBS_PROBE_DUMP_PATH = "/tmp/ubsocket/probe";
std::mutex GlobalSetting::ProbeDumpMutex;
bool GlobalSetting::UBS_TX_STAT_ENABLE = UBSOCKET_TX_STAT_ENABLE_DEFAULT;
uint32_t GlobalSetting::UBS_TX_STAT_INTERVAL_MS = UBSOCKET_TX_STAT_INTERVAL_MS_DEFAULT;
std::string GlobalSetting::UBS_TX_STAT_FILE = UBSOCKET_TX_STAT_FILE_DEFAULT;
uint32_t GlobalSetting::UBS_TX_STAT_MAX_MB = UBSOCKET_TX_STAT_MAX_MB_DEFAULT;
uint32_t GlobalSetting::UBS_TX_STAT_HEARTBEAT_SEC = UBSOCKET_TX_STAT_HEARTBEAT_SEC_DEFAULT;
bool GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
LinkSelectionPolicy GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
uint32_t GlobalSetting::UBS_PORT_COOLDOWN_SEC = 60;

/* environment variable name */
#define ENV_MONITOR_ENABLE "UBSOCKET_MONITOR_ENABLE"
#define ENV_SPLIT_TRACE_ENABLED "UBSOCKET_SPLIT_TRACE_ENABLE"
#define ENV_SPLIT_TRACE_SAMPLE_RATE "UBSOCKET_SPLIT_TRACE_SAMPLE_RATE"
#define ENV_SPLIT_TRACE_DRAIN_INTERVAL_MS "UBSOCKET_SPLIT_TRACE_DRAIN_INTERVAL_MS"
#define ENV_ASYNC_ACCEPTOR "UBSOCKET_ASYNC_ACCEPT_ENABLE" /* match brpc_test FLAGS_ubsocket_async_accept */
#define ENV_ASYNC_CONNECTOR "UBSOCKET_ASYNC_CONNECTOR_THREAD_COUNT"
#define ENV_ASYNC_EPOLL "UBSOCKET_ASYNC_EPOLL_WAIT_THREAD_COUNT"
#define ENV_SHARE_JFR_LOOP_POLL_ENABLED "UBSOCKET_SHARE_JFR_LOOP_POLL_ENABLED"
#define ENV_ENABLE_SHARE_JFR "UBSOCKET_SHARE_JFR_ENABLE"
#define ENV_UBS_RX_DEPTH "UBSOCKET_RX_DEPTH"
#define ENV_UBS_TX_DEPTH "UBSOCKET_TX_DEPTH"
#define ENV_UBS_HAND_SHAKE_MODE "UBSOCKET_UB_HANDSHAKE_MODE"
#define ENV_UBS_THREAD_POOL_SIZE "UBSOCKET_ASYNC_ACCEPT_THREAD_NUM"
#define ENV_PROF_ENABLE "UBSOCKET_PROF_ENABLE"
#define ENV_PROF_MODE "UBSOCKET_PROF_MODE"
#define ENV_PROF_DUMP_INTERVAL_MIN "UBSOCKET_PROF_DUMP_INTERVAL_MIN"
#define ENV_PROF_DUMP_PATH "UBSOCKET_PROF_DUMP_FILE_PATH"
#define ENV_MONITOR_INTERVAL "UBSOCKET_MONITOR_INTERVAL"
#define ENV_MONITOR_FILE_SIZE "UBSOCKET_MONITOR_FILE_SIZE"
#define ENV_MONITOR_FILE_PATH "UBSOCKET_MONITOR_FILE_PATH"
#define ENV_VAR_DEGRADE "UBSOCKET_DEGRADE_ENABLE"
#define ENV_VAR_CONNECT_PRECREATE "UBSOCKET_CONNECT_PRECREATE"
#define ENV_VAR_EARLY_ACK "UBSOCKET_EARLY_ACK"
#define ENV_VAR_NEGO_CARRY_BINDINFO "UBSOCKET_NEGO_CARRY_BINDINFO"
#define ENV_VAR_NEGO_REQ_CARRY_BINDINFO "UBSOCKET_NEGO_REQ_CARRY_BINDINFO"
#define ENV_VAR_NEGO_PARALLEL_BIND "UBSOCKET_NEGO_PARALLEL_BIND"
#define ENV_VAR_CLI "UBSOCKET_CLI_ENABLE"
#define ENV_VAR_PROBE "UBSOCKET_PROBE_ENABLE"
#define ENV_VAR_PROBE_TIME "UBSOCKET_PROBE_INTERVAL_MS"
#define ENV_VAR_PROBE_BATCH "UBSOCKET_PROBE_BATCH_SIZE"
#define ENV_VAR_PROBE_DUMP_PATH "UBSOCKET_PROBE_DUMP_FILE_PATH"
#define ENV_BACKUP_LINK_ENABLED "UBSOCKET_BACKUP_LINK_ENABLE"
#define ENV_PORT_COOLDOWN_SEC "UBSOCKET_PORT_COOLDOWN_SEC"
#define ENV_READV_UNLIMITED "UBSOCKET_READV_UNLIMITED"
#define ENV_TX_UNIFIED_POLL_ENABLED "UBSOCKET_TX_UNIFIED_POLL_ENABLED"
#define ENV_TX_POLLER_ACTIVE_YIELD "UBSOCKET_TX_POLLER_ACTIVE_YIELD"
#define ENV_TX_POLLER_SPIN_ROUNDS "UBSOCKET_TX_POLLER_SPIN_ROUNDS"
#define ENV_TX_POLLER_BACKOFF_MAX_US "UBSOCKET_TX_POLLER_BACKOFF_MAX_US"
#define ENV_TX_POLLER_FALLBACK_MS "UBSOCKET_TX_POLLER_FALLBACK_MS"
#define ENV_TX_POLLER_FAST_PERIOD_US "UBSOCKET_TX_POLLER_FAST_PERIOD_US"
#define ENV_TX_STAT_ENABLE "UBSOCKET_TX_STAT_ENABLE"
#define ENV_TX_POLLER_SLOW_SWITCH_ENABLED "UBSOCKET_TX_POLLER_SLOW_SWITCH_ENABLED"
#define ENV_TX_STAT_INTERVAL_MS "UBSOCKET_TX_STAT_INTERVAL_MS"
#define ENV_TX_STAT_FILE "UBSOCKET_TX_STAT_FILE"
#define ENV_TX_STAT_MAX_MB "UBSOCKET_TX_STAT_MAX_MB"
#define ENV_TX_STAT_HEARTBEAT_SEC "UBSOCKET_TX_STAT_HEARTBEAT_SEC"
#define ENV_READ_GEN_CHECK_ENABLED "UBS_READ_GEN_CHECK_ENABLED"
#define ENV_RNR_BACKPRESSURE_ENABLED "UBSOCKET_RNR_BACKPRESSURE_ENABLED"
#define ENV_RX_BATCH_PRINT_THRESHOLD "UBSOCKET_RX_BATCH_PRINT_THRESHOLD"
#define ENV_RNR_FATAL_TIMEOUT_MS "UBSOCKET_RNR_FATAL_TIMEOUT_MS"
#define ENV_BIG_PIN_TIMEOUT_MARGIN_MS "UBS_BIG_PIN_TIMEOUT_MARGIN_MS"
#define ENV_GRACE_MS "UBS_GRACE_MS"
#define ENV_BIG_CTRL_RESERVED_SLOTS "UBSOCKET_BIG_CTRL_RESERVED_SLOTS"

void GlobalSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {
        {ENV_ASYNC_ACCEPTOR, false, 0, 8L},
        {ENV_ASYNC_CONNECTOR, false, 0, 8L},
        {ENV_ASYNC_EPOLL, false, 1, 1L},
        {ENV_UBS_RX_DEPTH, false, 2, UBSOCKET_RX_DEPTH_MAX},
        {ENV_UBS_TX_DEPTH, false, 2, UBSOCKET_TX_DEPTH_MAX},
        {ENV_PROF_DUMP_INTERVAL_MIN, false, 1, 5},
        {ENV_MONITOR_INTERVAL, false, UBSOCKET_TRACE_TIME_MIN, UBSOCKET_TRACE_TIME_MAX},
        {ENV_MONITOR_FILE_SIZE, false, UBSOCKET_TRACE_FILE_SIZE_MIN, UBSOCKET_TRACE_FILE_SIZE_MAX},
        {ENV_VAR_PROBE_TIME, false, UBSOCKET_PROBE_TIME_MS_MIN, UBSOCKET_PROBE_TIME_MS_MAX},
        {ENV_VAR_PROBE_BATCH, false, UBSOCKET_PROBE_BATCH_MIN, UBSOCKET_PROBE_BATCH_MAX},
        {ENV_SPLIT_TRACE_SAMPLE_RATE, false, 1, 1000},
        {ENV_SPLIT_TRACE_DRAIN_INTERVAL_MS, false, 1, 10000},
        {ENV_UBS_THREAD_POOL_SIZE, false, 1, UBSOCKET_THREAD_POOL_SIZE_MAX},
        {ENV_PORT_COOLDOWN_SEC, false, 1, 300},
        {ENV_TX_POLLER_SPIN_ROUNDS, false, 1, 4096},
        {ENV_TX_POLLER_BACKOFF_MAX_US, false, 1, 10000},
        {ENV_TX_POLLER_FALLBACK_MS, false, 1, 1000},
        {ENV_TX_POLLER_FAST_PERIOD_US, false, 1, 100000},
        {ENV_TX_STAT_INTERVAL_MS, false, UBSOCKET_TX_STAT_INTERVAL_MS_MIN, UBSOCKET_TX_STAT_INTERVAL_MS_MAX},
        {ENV_TX_STAT_MAX_MB, false, UBSOCKET_TX_STAT_MAX_MB_MIN, UBSOCKET_TX_STAT_MAX_MB_MAX},
        {ENV_TX_STAT_HEARTBEAT_SEC, false, 0, UBSOCKET_TX_STAT_HEARTBEAT_SEC_MAX},
        {ENV_BIG_PIN_TIMEOUT_MARGIN_MS, false, 1, 60000},
        {ENV_GRACE_MS, false, 1, 10000},
        {ENV_BIG_CTRL_RESERVED_SLOTS, false, 0, 64},
        {ENV_RX_BATCH_PRINT_THRESHOLD, false, 0, UINT32_MAX},
        {ENV_RNR_FATAL_TIMEOUT_MS, false, 0, 600000},
    };

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_MONITOR_ENABLE, true, "true|false"},
                                    {ENV_SPLIT_TRACE_ENABLED, false, "true|false"},
                                    {ENV_ENABLE_SHARE_JFR, false, "true|false"},
                                    {ENV_SHARE_JFR_LOOP_POLL_ENABLED, false, "true|false"},
                                    {ENV_UBS_HAND_SHAKE_MODE, false, "tfo|ub_sock_opt"},
                                    {ENV_PROF_ENABLE, false, "true|false"},
                                    {ENV_PROF_MODE, false, "fast|ext"},
                                    {ENV_ASYNC_ACCEPTOR, false, "true|false"},
                                    {ENV_VAR_DEGRADE, false, "true|false"},
                                    {ENV_VAR_CONNECT_PRECREATE, false, "true|false"},
                                    {ENV_VAR_EARLY_ACK, false, "true|false"},
                                    {ENV_VAR_NEGO_CARRY_BINDINFO, false, "true|false"},
                                    {ENV_VAR_NEGO_REQ_CARRY_BINDINFO, false, "true|false"},
                                    {ENV_VAR_NEGO_PARALLEL_BIND, false, "true|false"},
                                    {ENV_VAR_CLI, false, "true|false"},
                                    {ENV_VAR_PROBE, false, "true|false"},
                                    {ENV_BACKUP_LINK_ENABLED, true, "true|false"},
                                    {ENV_READV_UNLIMITED, true, "true|false"},
                                    {ENV_TX_UNIFIED_POLL_ENABLED, false, "true|false"},
                                    {ENV_TX_POLLER_ACTIVE_YIELD, false, "true|false"},
                                    {ENV_TX_STAT_ENABLE, false, "true|false"},
                                     {ENV_READ_GEN_CHECK_ENABLED, false, "true|false"},
                                     {ENV_RNR_BACKPRESSURE_ENABLED, false, "true|false"},
                                     {ENV_TX_POLLER_SLOW_SWITCH_ENABLED, false, "true|false"}};

    /* str not empty rules: name, required, maxLen */
    StrNotEmptyRule rules_str_not_empty[] = {{ENV_PROF_DUMP_PATH, false, UBSOCKET_TRACE_FILE_PATH_LEN_MAX},
                                             {ENV_MONITOR_FILE_PATH, false, UBSOCKET_TRACE_FILE_PATH_LEN_MAX},
                                             {ENV_TX_STAT_FILE, false, UBSOCKET_TX_STAT_FILE_LEN_MAX},
                                             {ENV_VAR_PROBE_DUMP_PATH, false, UBSOCKET_TRACE_FILE_PATH_LEN_MAX}};

    for (auto &item : rules_int64) {
        Validator::Instance().AddNumRule(item);
    }

    for (auto &item : rules_str_enum) {
        Validator::Instance().AddStrEnumRule(item);
    }

    for (auto &item : rules_str_not_empty) {
        Validator::Instance().AddStrNotEmtpyRule(item);
    }

    UBS_SLOG_DEBUG(Validator::Instance().DumpString());
}

Result GlobalSetting::VerifySetting() noexcept
{
    UBS_VLOG_DEBUG("start");
    /* set native tcp mode */
    if (UBS_ALLOWED_PROTOCOL == UBS_PROTOCOL_TCP) {
        UBS_NATIVE_TCP_MODE = true;
    }

    // 手动验证 ENV_ASYNC_ACCEPTOR (字符串类型)
    std::string strAsyncAccept;
    if (GetEnv(ENV_ASYNC_ACCEPTOR, strAsyncAccept)) {
        if (strAsyncAccept != "true" && strAsyncAccept != "false") {
            UBS_VLOG_ERR("Invalid value for %s: %s, expected 'true' or 'false'\n", ENV_ASYNC_ACCEPTOR,
                         strAsyncAccept.c_str());
            return UBS_INVALID_PARAM;
        }
    }

    auto &validator = Validator::Instance();
    if (!validator.Validate(ENV_ASYNC_CONNECTOR, (int64_t)UBS_CONNECTOR_ASYNC_THREAD_COUNT,
                            "async_connector_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    if (!validator.Validate(ENV_ASYNC_EPOLL, (int64_t)UBS_EPOLL_ASYNC_THREAD_COUNT, "async_epoll_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    UBS_VLOG_DEBUG("end");
    return UBS_OK;
}

UBHandshakeMode HandShakeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == "tfo") {
        return UBHandshakeMode::TFO;
    } else if (typeStr == "ub_sock_opt") {
        return UBHandshakeMode::UB_SOCK_OPT;
    }
    // 如果字符串不匹配，返回默认值
    return UBHandshakeMode::UB_SOCK_OPT;
}

Result GlobalSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t envValue = 0;
    std::string strEnvValue;

    /* load from env */
    if (GetEnvAndValidate(ENV_MONITOR_ENABLE, strEnvValue)) {
        UBS_MONITOR_ENABLE = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_SPLIT_TRACE_ENABLED, strEnvValue)) {
        UBS_SPLIT_TRACE_ENABLED = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_SPLIT_TRACE_SAMPLE_RATE, envValue)) {
        UBS_SPLIT_TRACE_SAMPLE_RATE = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_SPLIT_TRACE_DRAIN_INTERVAL_MS, envValue)) {
        UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = static_cast<uint32_t>(envValue);
    }
    // 正确处理 ENV_ASYNC_ACCEPTOR (字符串类型 "true"|"false")
    std::string strAsyncAccept;
    if (GetEnvAndValidate(ENV_ASYNC_ACCEPTOR, strAsyncAccept)) {
        if (Func::BoolFromStr(strAsyncAccept)) {
            UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 1; // 默认线程数
        } else {
            UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
        }
    }

    if (GetEnvAndValidate(ENV_ASYNC_CONNECTOR, envValue)) {
        UBS_CONNECTOR_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_ASYNC_EPOLL, envValue)) {
        UBS_EPOLL_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_ENABLE_SHARE_JFR, strEnvValue)) {
        UBS_ENABLE_SHARE_JFR = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_SHARE_JFR_LOOP_POLL_ENABLED, strEnvValue)) {
        UBS_SHARE_JFR_LOOP_POLL_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_UBS_TX_DEPTH, envValue)) {
        UBS_TX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_UBS_RX_DEPTH, envValue)) {
        UBS_RX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_UBS_HAND_SHAKE_MODE, strEnvValue)) {
        UBS_HAND_SHAKE_MODE = HandShakeFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_UBS_THREAD_POOL_SIZE, envValue)) {
        UBS_THREAD_POOL_SIZE = static_cast<uint32_t>(envValue);
    }
    UBS_VLOG_INFO("Current thread pool size, UBSOCKET_ASYNC_ACCEPT_THREAD_NUM: %u\n", UBS_THREAD_POOL_SIZE);

    if (GetEnvAndValidate(ENV_PROF_ENABLE, strEnvValue)) {
        UBS_PROF_ENABLE = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_PROF_MODE, strEnvValue)) {
        UBS_PROF_MODE = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_PROF_DUMP_INTERVAL_MIN, envValue)) {
        UBS_PROF_DUMP_INTERVAL_MIN = static_cast<uint16_t>(envValue);
    }

    if (GetEnvAndValidateNotEmpty(ENV_PROF_DUMP_PATH, strEnvValue)) {
        UBS_PROF_DUMP_PATH = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_MONITOR_INTERVAL, envValue)) {
        UBS_MONITOR_INTERVAL = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_MONITOR_FILE_SIZE, envValue)) {
        UBS_MONITOR_FILE_SIZE_MB = static_cast<uint64_t>(envValue);
    }

    if (GetEnvAndValidateNotEmpty(ENV_MONITOR_FILE_PATH, strEnvValue)) {
        UBS_MONITOR_FILE_PATH = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_VAR_DEGRADE, strEnvValue)) {
        UBS_ENABLE_DEGRADE = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_CONNECT_PRECREATE, strEnvValue)) {
        UBS_CONNECT_PRECREATE = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_EARLY_ACK, strEnvValue)) {
        UBS_EARLY_ACK = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_NEGO_CARRY_BINDINFO, strEnvValue)) {
        UBS_NEGO_CARRY_BINDINFO = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_NEGO_REQ_CARRY_BINDINFO, strEnvValue)) {
        UBS_NEGO_REQ_CARRY_BINDINFO = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_NEGO_PARALLEL_BIND, strEnvValue)) {
        UBS_NEGO_PARALLEL_BIND = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_CLI, strEnvValue)) {
        UBS_CLI_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE, strEnvValue)) {
        UBS_PROBE_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE_TIME, envValue)) {
        UBS_PROBE_MS = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE_BATCH, envValue)) {
        UBS_PROBE_BATCH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidateNotEmpty(ENV_VAR_PROBE_DUMP_PATH, strEnvValue)) {
        UBS_PROBE_DUMP_PATH = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_BACKUP_LINK_ENABLED, strEnvValue)) {
        UBS_BACKUP_LINK_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_PORT_COOLDOWN_SEC, envValue)) {
        UBS_PORT_COOLDOWN_SEC = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_TX_STAT_ENABLE, strEnvValue)) {
        UBS_TX_STAT_ENABLE = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_TX_STAT_INTERVAL_MS, envValue)) {
        UBS_TX_STAT_INTERVAL_MS = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_STAT_MAX_MB, envValue)) {
        UBS_TX_STAT_MAX_MB = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_STAT_HEARTBEAT_SEC, envValue)) {
        UBS_TX_STAT_HEARTBEAT_SEC = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidateNotEmpty(ENV_TX_STAT_FILE, strEnvValue)) {
        UBS_TX_STAT_FILE = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_READV_UNLIMITED, strEnvValue)) {
        UBS_READV_UNLIMITED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_TX_UNIFIED_POLL_ENABLED, strEnvValue)) {
        UBS_TX_UNIFIED_POLL_ENABLED = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_ACTIVE_YIELD, strEnvValue)) {
        UBS_TX_POLLER_ACTIVE_YIELD = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_SPIN_ROUNDS, envValue)) {
        UBS_TX_POLLER_SPIN_ROUNDS = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_BACKOFF_MAX_US, envValue)) {
        UBS_TX_POLLER_BACKOFF_MAX_US = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_FALLBACK_MS, envValue)) {
        UBS_TX_POLLER_FALLBACK_MS = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_FAST_PERIOD_US, envValue)) {
        UBS_TX_POLLER_FAST_PERIOD_US = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_TX_POLLER_SLOW_SWITCH_ENABLED, strEnvValue)) {
 	    UBS_TX_POLLER_SLOW_SWITCH_ENABLED = Func::BoolFromStr(strEnvValue);
 	}
    if (GetEnvAndValidate(ENV_READ_GEN_CHECK_ENABLED, strEnvValue)) {
        UBS_READ_GEN_CHECK_ENABLED = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_RNR_BACKPRESSURE_ENABLED, strEnvValue)) {
        UBS_RNR_BACKPRESSURE_ENABLED = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_RX_BATCH_PRINT_THRESHOLD, envValue)) {
        UBS_RX_BATCH_PRINT_THRESHOLD = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_RNR_FATAL_TIMEOUT_MS, envValue)) {
        UBS_RNR_FATAL_TIMEOUT_MS = static_cast<uint32_t>(envValue);
    }
    UBS_VLOG_INFO("[GlobalSetting] UBS_READ_GEN_CHECK_ENABLED=%d, UBS_BIG_PIN_TIMEOUT_MARGIN_MS=%u, UBS_GRACE_MS=%u\n",
                  static_cast<int>(UBS_READ_GEN_CHECK_ENABLED), UBS_BIG_PIN_TIMEOUT_MARGIN_MS, UBS_GRACE_MS);
    if (GetEnvAndValidate(ENV_BIG_PIN_TIMEOUT_MARGIN_MS, envValue)) {
        UBS_BIG_PIN_TIMEOUT_MARGIN_MS = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_GRACE_MS, envValue)) {
        UBS_GRACE_MS = static_cast<uint32_t>(envValue);
    }
    if (GetEnvAndValidate(ENV_BIG_CTRL_RESERVED_SLOTS, envValue)) {
        UBS_BIG_CTRL_RESERVED_SLOTS = static_cast<uint32_t>(envValue);
    }
    /* 预留数必须显著小于 SQ 深度，否则数据路径额度不足（单条 READ 链最长
     * UBS_SEG_MAX=32 个 WR，额度长期低于链长会饿死 READ）。越界时告警并关闭。 */
    if (UBS_BIG_CTRL_RESERVED_SLOTS != 0 && UBS_BIG_CTRL_RESERVED_SLOTS >= UBS_TX_DEPTH / 2) {
        UBS_VLOG_WARN("UBSOCKET_BIG_CTRL_RESERVED_SLOTS %u >= UBSOCKET_TX_DEPTH %u / 2, feature disabled\n",
                      UBS_BIG_CTRL_RESERVED_SLOTS, UBS_TX_DEPTH);
        UBS_BIG_CTRL_RESERVED_SLOTS = 0;
    }
    if (UBS_BIG_CTRL_RESERVED_SLOTS != 0) {
        UBS_VLOG_INFO("[GlobalSetting] UBS_BIG_CTRL_RESERVED_SLOTS=%u (tx_depth=%u)\n", UBS_BIG_CTRL_RESERVED_SLOTS,
                      UBS_TX_DEPTH);
    }
    return UBS_OK;
}
} // namespace ubs
} // namespace ock