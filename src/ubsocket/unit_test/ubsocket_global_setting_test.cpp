/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_functions.h"
#include "common/ubsocket_logger.h"

using namespace ock::ubs;

// ==================== Noinline helpers ====================
// GlobalSetting methods are ALWAYS_INLINE — each call site generates separate
// branch counters. Noinline wrappers consolidate to a single call site.

__attribute__((noinline)) static bool GetEnvInt64(const std::string &name, int64_t &out)
{
    return GlobalSetting::GetEnv(name, out);
}

__attribute__((noinline)) static bool GetEnvFloat(const std::string &name, float &out)
{
    return GlobalSetting::GetEnv(name, out);
}

__attribute__((noinline)) static bool GetEnvStr(const std::string &name, std::string &out)
{
    return GlobalSetting::GetEnv(name, out);
}

__attribute__((noinline)) static bool GetEnvAndValidateInt64(const std::string &name, int64_t &out)
{
    return GlobalSetting::GetEnvAndValidate(name, out);
}

__attribute__((noinline)) static bool GetEnvAndValidateFloat(const std::string &name, float &out)
{
    return GlobalSetting::GetEnvAndValidate(name, out);
}

__attribute__((noinline)) static bool GetEnvAndValidateStr(const std::string &name, std::string &out)
{
    return GlobalSetting::GetEnvAndValidate(name, out);
}

__attribute__((noinline)) static bool GetEnvAndValidateNotEmpty(const std::string &name, std::string &out)
{
    return GlobalSetting::GetEnvAndValidateNotEmpty(name, out);
}

__attribute__((noinline)) static bool CallAsyncAcceptorEnabled()
{
    return GlobalSetting::AsyncAcceptorEnabled();
}

__attribute__((noinline)) static bool CallAsyncConnectorEnabled()
{
    return GlobalSetting::AsyncConnectorEnabled();
}

__attribute__((noinline)) static bool CallAsyncEpollEnabled()
{
    return GlobalSetting::AsyncEpollEnabled();
}

__attribute__((noinline)) static Result CallVerifySetting()
{
    return GlobalSetting::VerifySetting();
}

__attribute__((noinline)) static Result CallLoadEnv()
{
    return GlobalSetting::LoadEnv();
}

// ==================== Env var name + valid value tables ====================

struct EnvInt64
{
    const char *name;
    int64_t valid;
    int64_t invalid; // below min
};

static const EnvInt64 G_INT64_ENVS[] = {
    {"UBSOCKET_ASYNC_CONNECTOR_THREAD_COUNT", 2, 9},
    {"UBSOCKET_ASYNC_EPOLL_WAIT_THREAD_COUNT", 1, 0},
    {"UBSOCKET_RX_DEPTH", 4, 1},
    {"UBSOCKET_TX_DEPTH", 4, 1},
    {"UBSOCKET_PROF_DUMP_INTERVAL_MIN", 3, 0},
    {"UBSOCKET_MONITOR_INTERVAL", 10, 0},
    {"UBSOCKET_MONITOR_FILE_SIZE", 40, 5},
    {"UBSOCKET_PROBE_INTERVAL_MS", 1000, 0},
    {"UBSOCKET_PROBE_BATCH_SIZE", 10, 0},
    {"UBSOCKET_SPLIT_TRACE_SAMPLE_RATE", 1, 1000},
    {"UBSOCKET_SPLIT_TRACE_DRAIN_INTERVAL_MS", 10, 0},
    {"UBSOCKET_ASYNC_ACCEPT_THREAD_NUM", 2, 0},
    {"UBSOCKET_PORT_COOLDOWN_SEC", 30, 0},
    {"UBSOCKET_TX_POLLER_SPIN_ROUNDS", 32, 0},
    {"UBSOCKET_TX_POLLER_BACKOFF_MAX_US", 100, 0},
    {"UBSOCKET_TX_POLLER_FALLBACK_MS", 50, 0},
    {"UBSOCKET_TX_STAT_INTERVAL_MS", 2000, 100},
    {"UBSOCKET_TX_STAT_MAX_MB", 32, 0},
    {"UBSOCKET_TX_STAT_HEARTBEAT_SEC", 60, 99999},
    {"UBS_BIG_PIN_TIMEOUT_MARGIN_MS", 500, 0},
    {"UBS_GRACE_MS", 50, 0},
    {"UBSOCKET_BIG_CTRL_RESERVED_SLOTS", 1, 65},
    {"UBSOCKET_RX_BATCH_PRINT_THRESHOLD", 128, 999},
    {"UBSOCKET_RNR_FATAL_TIMEOUT_MS", 30000, 999999},
};

struct EnvStrEnum
{
    const char *name;
    const char *valid;
    const char *invalid;
    bool required;
};

static const EnvStrEnum G_STR_ENUM_ENVS[] = {
    {"UBSOCKET_MONITOR_ENABLE", "true", "xyz", true},
    {"UBSOCKET_SPLIT_TRACE_ENABLE", "false", "xyz", false},
    {"UBSOCKET_SHARE_JFR_ENABLE", "true", "xyz", false},
    {"UBSOCKET_SHARE_JFR_LOOP_POLL_ENABLED", "false", "xyz", false},
    {"UBSOCKET_UB_HANDSHAKE_MODE", "tfo", "bad", false},
    {"UBSOCKET_PROF_ENABLE", "true", "xyz", false},
    {"UBSOCKET_PROF_MODE", "ext", "bad", false},
    {"UBSOCKET_ASYNC_ACCEPT_ENABLE", "true", "xyz", false},
    {"UBSOCKET_DEGRADE_ENABLE", "false", "xyz", false},
    {"UBSOCKET_CONNECT_PRECREATE", "true", "xyz", false},
    {"UBSOCKET_EARLY_ACK", "false", "xyz", false},
    {"UBSOCKET_CLI_ENABLE", "true", "xyz", false},
    {"UBSOCKET_PROBE_ENABLE", "false", "xyz", false},
    {"UBSOCKET_BACKUP_LINK_ENABLE", "true", "xyz", true},
    {"UBSOCKET_READV_UNLIMITED", "true", "xyz", true},
    {"UBSOCKET_TX_UNIFIED_POLL_ENABLED", "true", "xyz", false},
    {"UBSOCKET_TX_POLLER_ACTIVE_YIELD", "false", "xyz", false},
    {"UBSOCKET_TX_STAT_ENABLE", "true", "xyz", false},
    {"UBS_READ_GEN_CHECK_ENABLED", "true", "xyz", false},
    {"UBSOCKET_RNR_BACKPRESSURE_ENABLED", "false", "xyz", false},
};

static const char *G_STR_NOTEMPTY_ENVS[] = {
    "UBSOCKET_PROF_DUMP_FILE_PATH",
    "UBSOCKET_MONITOR_FILE_PATH",
    "UBSOCKET_TX_STAT_FILE",
};

static void UnsetAllEnvs()
{
    for (const auto &e : G_INT64_ENVS) {
        unsetenv(e.name);
    }
    for (const auto &e : G_STR_ENUM_ENVS) {
        unsetenv(e.name);
    }
    for (const auto &n : G_STR_NOTEMPTY_ENVS) {
        unsetenv(n);
    }
}

// ==================== GlobalSetting Tests ====================

class GlobalSettingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        GlobalSetting::AddRules();
        UnsetAllEnvs();
    }

    void TearDown() override
    {
        UnsetAllEnvs();
        // Reset static members that LoadEnv/VerifySetting may change
        GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = false;
        GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 1000;
        GlobalSetting::UBS_GRACE_MS = 100;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
        GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    }
};

// --- Default values ---

TEST_F(GlobalSettingTest, Defaults_TxDepth)
{
    EXPECT_EQ(GlobalSetting::GetTxDepth(), 0);
}

TEST_F(GlobalSettingTest, Defaults_PortCooldownSec)
{
    EXPECT_GT(GlobalSetting::UBS_PORT_COOLDOWN_SEC, 0u);
}

TEST_F(GlobalSettingTest, Defaults_ThreadPoolSize)
{
    EXPECT_GT(GlobalSetting::UBS_THREAD_POOL_SIZE, 0u);
}

// --- Gen Check / Timeout Settings (design §6) ---

TEST_F(GlobalSettingTest, Defaults_ReadGenCheckEnabled)
{
    EXPECT_FALSE(GlobalSetting::UBS_READ_GEN_CHECK_ENABLED);
}

TEST_F(GlobalSettingTest, Defaults_BigPinTimeoutMarginMs)
{
    EXPECT_EQ(GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS, 1000u);
}

TEST_F(GlobalSettingTest, Defaults_GraceMs)
{
    EXPECT_EQ(GlobalSetting::UBS_GRACE_MS, 100u);
}

// --- Async enabled checks ---

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenDisabled)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
    EXPECT_FALSE(CallAsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenThreadCountPositive)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
    EXPECT_TRUE(CallAsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenFlagEnabled)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    EXPECT_TRUE(CallAsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncConnectorEnabled_WhenZero)
{
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 0;
    EXPECT_FALSE(CallAsyncConnectorEnabled());
}

TEST_F(GlobalSettingTest, AsyncConnectorEnabled_WhenPositive)
{
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 5;
    EXPECT_TRUE(CallAsyncConnectorEnabled());
}

TEST_F(GlobalSettingTest, AsyncEpollEnabled_WhenZero)
{
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 0;
    EXPECT_FALSE(CallAsyncEpollEnabled());
}

TEST_F(GlobalSettingTest, AsyncEpollEnabled_WhenPositive)
{
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 3;
    EXPECT_TRUE(CallAsyncEpollEnabled());
}

// --- GetEnv: integer ---

TEST_F(GlobalSettingTest, GetEnv_Int_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_INT_VAR");
    int64_t out = 0;
    EXPECT_FALSE(GetEnvInt64("TEST_UBS_INT_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_Int_ValidValue)
{
    setenv("TEST_UBS_INT_VAR", "123", 1);
    int64_t out = 0;
    EXPECT_TRUE(GetEnvInt64("TEST_UBS_INT_VAR", out));
    EXPECT_EQ(out, 123);
    unsetenv("TEST_UBS_INT_VAR");
}

TEST_F(GlobalSettingTest, GetEnv_Int_NegativeValue)
{
    setenv("TEST_UBS_INT_NEG", "-456", 1);
    int64_t out = 0;
    EXPECT_TRUE(GetEnvInt64("TEST_UBS_INT_NEG", out));
    EXPECT_EQ(out, -456);
    unsetenv("TEST_UBS_INT_NEG");
}

TEST_F(GlobalSettingTest, GetEnv_Int_InvalidStringReturnsFalse)
{
    setenv("TEST_UBS_INT_INVALID", "not_a_number", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvInt64("TEST_UBS_INT_INVALID", out));
    unsetenv("TEST_UBS_INT_INVALID");
}

TEST_F(GlobalSettingTest, GetEnv_Int_EmptyStringReturnsFalse)
{
    setenv("TEST_UBS_INT_EMPTY", "", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvInt64("TEST_UBS_INT_EMPTY", out));
    unsetenv("TEST_UBS_INT_EMPTY");
}

TEST_F(GlobalSettingTest, GetEnv_Int_OverflowReturnsFalse)
{
    setenv("TEST_UBS_INT_OVERFLOW", "99999999999999999999999999", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvInt64("TEST_UBS_INT_OVERFLOW", out));
    unsetenv("TEST_UBS_INT_OVERFLOW");
}

TEST_F(GlobalSettingTest, GetEnv_Int_FloatStringReturnsFalse)
{
    setenv("TEST_UBS_INT_FLOAT", "3.14", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvInt64("TEST_UBS_INT_FLOAT", out));
    unsetenv("TEST_UBS_INT_FLOAT");
}

// --- GetEnv: float ---

TEST_F(GlobalSettingTest, GetEnv_Float_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_FLOAT_VAR");
    float out = 0;
    EXPECT_FALSE(GetEnvFloat("TEST_UBS_FLOAT_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_Float_IntegerValue)
{
    setenv("TEST_UBS_FLOAT_INT", "42", 1);
    float out = 0;
    EXPECT_TRUE(GetEnvFloat("TEST_UBS_FLOAT_INT", out));
    unsetenv("TEST_UBS_FLOAT_INT");
}

TEST_F(GlobalSettingTest, GetEnv_Float_DecimalValue)
{
    setenv("TEST_UBS_FLOAT_DEC", "3.14", 1);
    float out = 0;
    EXPECT_TRUE(GetEnvFloat("TEST_UBS_FLOAT_DEC", out));
    unsetenv("TEST_UBS_FLOAT_DEC");
}

TEST_F(GlobalSettingTest, GetEnv_Float_NegativeValue)
{
    setenv("TEST_UBS_FLOAT_NEG", "-1.5", 1);
    float out = 0;
    EXPECT_TRUE(GetEnvFloat("TEST_UBS_FLOAT_NEG", out));
    unsetenv("TEST_UBS_FLOAT_NEG");
}

TEST_F(GlobalSettingTest, GetEnv_Float_InvalidReturnsFalse)
{
    setenv("TEST_UBS_FLOAT_INVALID", "abc", 1);
    float out = 0;
    EXPECT_FALSE(GetEnvFloat("TEST_UBS_FLOAT_INVALID", out));
    unsetenv("TEST_UBS_FLOAT_INVALID");
}

TEST_F(GlobalSettingTest, GetEnv_Float_LargeNumberReturnsTrue)
{
    setenv("TEST_UBS_FLOAT_OVERFLOW", "999999999999999999999999999999999999", 1);
    float out = 0;
    EXPECT_TRUE(GetEnvFloat("TEST_UBS_FLOAT_OVERFLOW", out));
    unsetenv("TEST_UBS_FLOAT_OVERFLOW");
}

// --- GetEnv: string ---

TEST_F(GlobalSettingTest, GetEnv_String_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_STR_VAR");
    std::string out;
    EXPECT_FALSE(GetEnvStr("TEST_UBS_STR_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_String_ValidValue)
{
    setenv("TEST_UBS_STR_VAR", "hello", 1);
    std::string out;
    EXPECT_TRUE(GetEnvStr("TEST_UBS_STR_VAR", out));
    EXPECT_EQ(out, "hello");
    unsetenv("TEST_UBS_STR_VAR");
}

TEST_F(GlobalSettingTest, GetEnv_String_EmptyValue)
{
    setenv("TEST_UBS_STR_EMPTY", "", 1);
    std::string out;
    EXPECT_TRUE(GetEnvStr("TEST_UBS_STR_EMPTY", out));
    EXPECT_TRUE(out.empty());
    unsetenv("TEST_UBS_STR_EMPTY");
}

// --- GetEnvAndValidate: int64 ---

TEST_F(GlobalSettingTest, GetEnvAndValidate_Int64_NotSetReturnsFalse)
{
    unsetenv("UBSOCKET_RX_DEPTH");
    int64_t out = 0;
    EXPECT_FALSE(GetEnvAndValidateInt64("UBSOCKET_RX_DEPTH", out));
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Int64_ValidReturnsTrue)
{
    setenv("UBSOCKET_RX_DEPTH", "4", 1);
    int64_t out = 0;
    EXPECT_TRUE(GetEnvAndValidateInt64("UBSOCKET_RX_DEPTH", out));
    EXPECT_EQ(out, 4);
    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Int64_InvalidFormatReturnsFalse)
{
    setenv("UBSOCKET_RX_DEPTH", "abc", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvAndValidateInt64("UBSOCKET_RX_DEPTH", out));
    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Int64_OutOfRangeReturnsFalse)
{
    setenv("UBSOCKET_RX_DEPTH", "1", 1);
    int64_t out = 0;
    EXPECT_FALSE(GetEnvAndValidateInt64("UBSOCKET_RX_DEPTH", out));
    unsetenv("UBSOCKET_RX_DEPTH");
}

// --- GetEnvAndValidate: float ---

TEST_F(GlobalSettingTest, GetEnvAndValidate_Float_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_FV");
    float out = 0;
    EXPECT_FALSE(GetEnvAndValidateFloat("TEST_UBS_FV", out));
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Float_InvalidReturnsFalse)
{
    setenv("TEST_UBS_FV", "abc", 1);
    float out = 0;
    EXPECT_FALSE(GetEnvAndValidateFloat("TEST_UBS_FV", out));
    unsetenv("TEST_UBS_FV");
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Float_ValidFormat_ExercisePath)
{
    setenv("TEST_UBS_FV", "3.14", 1);
    float out = 0;
    // Exercise regex-match + stod path; no validator rule for this name
    GetEnvAndValidateFloat("TEST_UBS_FV", out);
    unsetenv("TEST_UBS_FV");
}

// --- GetEnvAndValidate: string ---

TEST_F(GlobalSettingTest, GetEnvAndValidate_Str_NotSetReturnsFalse)
{
    unsetenv("UBSOCKET_MONITOR_ENABLE");
    std::string out;
    EXPECT_FALSE(GetEnvAndValidateStr("UBSOCKET_MONITOR_ENABLE", out));
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Str_ValidReturnsTrue)
{
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    std::string out;
    EXPECT_TRUE(GetEnvAndValidateStr("UBSOCKET_MONITOR_ENABLE", out));
    unsetenv("UBSOCKET_MONITOR_ENABLE");
}

TEST_F(GlobalSettingTest, GetEnvAndValidate_Str_InvalidEnumReturnsFalse)
{
    setenv("UBSOCKET_MONITOR_ENABLE", "bad", 1);
    std::string out;
    EXPECT_FALSE(GetEnvAndValidateStr("UBSOCKET_MONITOR_ENABLE", out));
    unsetenv("UBSOCKET_MONITOR_ENABLE");
}

// --- GetEnvAndValidateNotEmpty ---

TEST_F(GlobalSettingTest, GetEnvAndValidateNotEmpty_NotSetReturnsFalse)
{
    unsetenv("UBSOCKET_PROF_DUMP_FILE_PATH");
    std::string out;
    EXPECT_FALSE(GetEnvAndValidateNotEmpty("UBSOCKET_PROF_DUMP_FILE_PATH", out));
}

TEST_F(GlobalSettingTest, GetEnvAndValidateNotEmpty_ValidReturnsTrue)
{
    setenv("UBSOCKET_PROF_DUMP_FILE_PATH", "/tmp/test", 1);
    std::string out;
    EXPECT_TRUE(GetEnvAndValidateNotEmpty("UBSOCKET_PROF_DUMP_FILE_PATH", out));
    unsetenv("UBSOCKET_PROF_DUMP_FILE_PATH");
}

TEST_F(GlobalSettingTest, GetEnvAndValidateNotEmpty_TooLongReturnsFalse)
{
    setenv("UBSOCKET_PROF_DUMP_FILE_PATH", std::string(600, 'x').c_str(), 1);
    std::string out;
    EXPECT_FALSE(GetEnvAndValidateNotEmpty("UBSOCKET_PROF_DUMP_FILE_PATH", out));
    unsetenv("UBSOCKET_PROF_DUMP_FILE_PATH");
}

// --- LoadEnv: all envs set with valid values ---

TEST_F(GlobalSettingTest, LoadEnv_AllEnvsValid)
{
    for (const auto &e : G_INT64_ENVS) {
        setenv(e.name, std::to_string(e.valid).c_str(), 1);
    }
    for (const auto &e : G_STR_ENUM_ENVS) {
        setenv(e.name, e.valid, 1);
    }
    setenv("UBSOCKET_PROF_DUMP_FILE_PATH", "/tmp/prof", 1);
    setenv("UBSOCKET_MONITOR_FILE_PATH", "/tmp/mon", 1);
    setenv("UBSOCKET_TX_STAT_FILE", "/tmp/tx_stat", 1);

    EXPECT_EQ(CallLoadEnv(), UBS_OK);

    EXPECT_EQ(GlobalSetting::UBS_RX_DEPTH, 4u);
    EXPECT_EQ(GlobalSetting::UBS_TX_DEPTH, 4u);
    EXPECT_EQ(GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT, 2);
    EXPECT_EQ(GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT, 1);
    EXPECT_TRUE(GlobalSetting::UBS_MONITOR_ENABLE);
    EXPECT_FALSE(GlobalSetting::UBS_SPLIT_TRACE_ENABLED);
    EXPECT_TRUE(GlobalSetting::UBS_ENABLE_SHARE_JFR);
    EXPECT_FALSE(GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED);
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::TFO);
    EXPECT_TRUE(GlobalSetting::UBS_PROF_ENABLE);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "ext");
    EXPECT_EQ(GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT, 1);
    EXPECT_FALSE(GlobalSetting::UBS_ENABLE_DEGRADE);
    EXPECT_TRUE(GlobalSetting::UBS_CONNECT_PRECREATE);
    EXPECT_FALSE(GlobalSetting::UBS_EARLY_ACK);
    EXPECT_TRUE(GlobalSetting::UBS_CLI_ENABLED);
    EXPECT_FALSE(GlobalSetting::UBS_PROBE_ENABLED);
    EXPECT_TRUE(GlobalSetting::UBS_BACKUP_LINK_ENABLED);
    EXPECT_TRUE(GlobalSetting::UBS_READV_UNLIMITED);
    EXPECT_TRUE(GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED);
    EXPECT_FALSE(GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD);
    EXPECT_TRUE(GlobalSetting::UBS_TX_STAT_ENABLE);
    EXPECT_TRUE(GlobalSetting::UBS_READ_GEN_CHECK_ENABLED);
    EXPECT_TRUE(GlobalSetting::UBS_MONITOR_ENABLE);
    EXPECT_FALSE(GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED);
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/prof");
    EXPECT_EQ(GlobalSetting::UBS_MONITOR_FILE_PATH, "/tmp/mon");
    EXPECT_EQ(GlobalSetting::UBS_TX_STAT_FILE, "/tmp/tx_stat");
    EXPECT_EQ(GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS, 1u); /* 1 < TX_DEPTH(4)/2, 不被钳制 */
}

/* 控制帧槽位预留: 预留数 >= TX_DEPTH/2 时钳制为 0(关闭特性)并告警 */
TEST_F(GlobalSettingTest, LoadEnv_BigCtrlReservedSlots_TooLargeForTxDepth_Disabled)
{
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    setenv("UBSOCKET_TX_DEPTH", "8", 1);
    setenv("UBSOCKET_BIG_CTRL_RESERVED_SLOTS", "4", 1); /* 4 >= 8/2 → 关闭 */

    EXPECT_EQ(CallLoadEnv(), UBS_OK);
    EXPECT_EQ(GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS, 0u);
    unsetenv("UBSOCKET_TX_DEPTH");
    unsetenv("UBSOCKET_BIG_CTRL_RESERVED_SLOTS");
}

// --- LoadEnv: all int64 envs set with non-numeric values (regex fail) ---

TEST_F(GlobalSettingTest, LoadEnv_Int64EnvsNonNumeric_AllFail)
{
    for (const auto &e : G_INT64_ENVS) {
        setenv(e.name, "abc", 1);
    }
    // Keep required str enum envs valid so LoadEnv doesn't early-return
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);

    EXPECT_EQ(CallLoadEnv(), UBS_OK);
}

// --- LoadEnv: all int64 envs set with overflow values (stol throw) ---

TEST_F(GlobalSettingTest, LoadEnv_Int64EnvsOverflow_AllFail)
{
    for (const auto &e : G_INT64_ENVS) {
        setenv(e.name, "99999999999999999999999999", 1);
    }
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);

    EXPECT_EQ(CallLoadEnv(), UBS_OK);
}

// --- LoadEnv: all int64 envs set with out-of-range values (validation fail) ---

TEST_F(GlobalSettingTest, LoadEnv_Int64EnvsOutOfRange_AllFail)
{
    for (const auto &e : G_INT64_ENVS) {
        setenv(e.name, std::to_string(e.invalid).c_str(), 1);
    }
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);

    EXPECT_EQ(CallLoadEnv(), UBS_OK);
}

// --- LoadEnv: all str enum envs set with invalid values ---

TEST_F(GlobalSettingTest, LoadEnv_StrEnumEnvsInvalid_AllFail)
{
    for (const auto &e : G_STR_ENUM_ENVS) {
        setenv(e.name, e.invalid, 1);
    }
    EXPECT_EQ(CallLoadEnv(), UBS_OK);
}

// --- LoadEnv: str not-empty envs with too-long values ---

TEST_F(GlobalSettingTest, LoadEnv_StrNotEmptyEnvsTooLong_AllFail)
{
    std::string longStr(600, 'x');
    for (const auto &n : G_STR_NOTEMPTY_ENVS) {
        setenv(n, longStr.c_str(), 1);
    }
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);

    EXPECT_EQ(CallLoadEnv(), UBS_OK);
}

// --- LoadEnv: AsyncAcceptor true/false paths ---

TEST_F(GlobalSettingTest, LoadEnv_AsyncAcceptor_True)
{
    setenv("UBSOCKET_ASYNC_ACCEPT_ENABLE", "true", 1);
    EXPECT_EQ(CallLoadEnv(), UBS_OK);
    EXPECT_EQ(GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT, 1);
}

TEST_F(GlobalSettingTest, LoadEnv_AsyncAcceptor_False)
{
    setenv("UBSOCKET_ASYNC_ACCEPT_ENABLE", "false", 1);
    EXPECT_EQ(CallLoadEnv(), UBS_OK);
    EXPECT_EQ(GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT, 0);
}

// --- VerifySetting ---

TEST_F(GlobalSettingTest, VerifySetting_NotTcpMode_Ok)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_OK);
}

TEST_F(GlobalSettingTest, VerifySetting_TcpMode_Ok)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = UBS_PROTOCOL_TCP;
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_OK);
    EXPECT_TRUE(GlobalSetting::UBS_NATIVE_TCP_MODE);
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
}

TEST_F(GlobalSettingTest, VerifySetting_AsyncAcceptorInvalid_Fails)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    setenv("UBSOCKET_ASYNC_ACCEPT_ENABLE", "bad_value", 1);
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_INVALID_PARAM);
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
}

TEST_F(GlobalSettingTest, VerifySetting_AsyncAcceptorValid_Ok)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    setenv("UBSOCKET_ASYNC_ACCEPT_ENABLE", "true", 1);
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_OK);
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
}

TEST_F(GlobalSettingTest, VerifySetting_AsyncAcceptorFalseValue_Ok)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    setenv("UBSOCKET_ASYNC_ACCEPT_ENABLE", "false", 1);
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_OK);
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
}

TEST_F(GlobalSettingTest, VerifySetting_ConnectorInvalid_Fails)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 9; // max is 8
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;
    EXPECT_EQ(CallVerifySetting(), UBS_INVALID_PARAM);
}

TEST_F(GlobalSettingTest, VerifySetting_EpollInvalid_Fails)
{
    GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
    unsetenv("UBSOCKET_ASYNC_ACCEPT_ENABLE");
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 0; // min is 1
    EXPECT_EQ(CallVerifySetting(), UBS_INVALID_PARAM);
}

// --- HandShakeFromStr (tested indirectly via LoadEnv) ---

TEST_F(GlobalSettingTest, LoadEnv_HandShakeMode_Tfo)
{
    setenv("UBSOCKET_UB_HANDSHAKE_MODE", "tfo", 1);
    CallLoadEnv();
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::TFO);
}

TEST_F(GlobalSettingTest, LoadEnv_HandShakeMode_UbSockOpt)
{
    setenv("UBSOCKET_UB_HANDSHAKE_MODE", "ub_sock_opt", 1);
    CallLoadEnv();
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::UB_SOCK_OPT);
}

TEST_F(GlobalSettingTest, LoadEnv_HandShakeMode_Invalid_DefaultsToUbSockOpt)
{
    setenv("UBSOCKET_UB_HANDSHAKE_MODE", "bad", 1);
    CallLoadEnv();
    // Invalid value fails ValidateStrEnum → GetEnvAndValidate returns false → not loaded
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::UB_SOCK_OPT);
}

// --- Gen Check / Timeout Settings (design §6) ---

TEST_F(GlobalSettingTest, Env_ReadGenCheckEnabled_True)
{
    setenv("UBS_READ_GEN_CHECK_ENABLED", "true", 1);
    CallLoadEnv();
    EXPECT_TRUE(GlobalSetting::UBS_READ_GEN_CHECK_ENABLED);
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = false;
    unsetenv("UBS_READ_GEN_CHECK_ENABLED");
}

TEST_F(GlobalSettingTest, Env_ReadGenCheckEnabled_False)
{
    setenv("UBS_READ_GEN_CHECK_ENABLED", "false", 1);
    CallLoadEnv();
    EXPECT_FALSE(GlobalSetting::UBS_READ_GEN_CHECK_ENABLED);
    unsetenv("UBS_READ_GEN_CHECK_ENABLED");
}

TEST_F(GlobalSettingTest, Env_BigPinTimeoutMarginMs)
{
    setenv("UBS_BIG_PIN_TIMEOUT_MARGIN_MS", "2500", 1);
    CallLoadEnv();
    EXPECT_EQ(GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS, 2500u);
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 1000;
    unsetenv("UBS_BIG_PIN_TIMEOUT_MARGIN_MS");
}

TEST_F(GlobalSettingTest, Env_GraceMs)
{
    setenv("UBS_GRACE_MS", "200", 1);
    CallLoadEnv();
    EXPECT_EQ(GlobalSetting::UBS_GRACE_MS, 200u);
    GlobalSetting::UBS_GRACE_MS = 100;
    unsetenv("UBS_GRACE_MS");
}
