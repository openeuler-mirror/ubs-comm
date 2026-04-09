/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for configure_settings module
 */

#include "configure_settings.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstring>

using namespace ubsocket;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const int CFG_TX_DEPTH_128 = 128;
static const int CFG_TX_DEPTH_256 = 256;
static const int CFG_TX_DEPTH_65535 = 65535;
static const int CFG_RX_DEPTH_128 = 128;
static const int CFG_RX_DEPTH_256 = 256;
static const int CFG_RX_DEPTH_65535 = 65535;
static const int CFG_EID_IDX_2 = 2;
static const int CFG_EID_IDX_5 = 5;
static const int CFG_EID_IDX_255 = 255;
static const int CFG_POOL_SIZE_20 = 20;
static const int CFG_POOL_SIZE_100 = 100;
static const int CFG_TRACE_TIME_30 = 30;
static const int CFG_TRACE_TIME_1 = 1;
static const int CFG_TRACE_TIME_300 = 300;
static const int CFG_TRACE_FILE_SIZE_50 = 50;
static const int CFG_TRANS_MODE_COUNT_6 = 6;
static const int CFG_LOG_LEVEL_COUNT_4 = 4;
static const int CFG_BLOCK_TYPE_COUNT_5 = 5;
static const int CFG_UB_TRANS_MODE_COUNT_4 = 4;
static const int CFG_INT_VAL_1 = 1;
static const int CFG_INT_VAL_2 = 2;
static const int CFG_INT_VAL_99 = 99;
static const int SETENV_OVERWRITE = 1;
static const int CFG_EID_IDX_LOOP_10 = 10;
} // namespace

// Test fixture for configure_settings tests
class ConfigureSettingsTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void ConfigureSettingsTest::SetUp()
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);
}

void ConfigureSettingsTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

// ============= TransMode Converter Tests =============

TEST_F(ConfigureSettingsTest, TransModeConverter_NullStr)
{
    umq_trans_mode_t result = TransMode::TransModeConverter(nullptr, UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UB);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_EmptyStr)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("", UMQ_TRANS_MODE_IB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_IB);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_UB)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ub", UMQ_TRANS_MODE_IB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UB);

    result = TransMode::TransModeConverter("UB", UMQ_TRANS_MODE_IB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UB);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_IB)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ib", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_IB);

    result = TransMode::TransModeConverter("IB", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_IB);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_UBMM)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ubmm", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UBMM);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_UBPlus)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ub_plus", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UB_PLUS);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_IBPlus)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ib_plus", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_IB_PLUS);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_UBMMPlus)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("ubmm_plus", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_UBMM_PLUS);
}

TEST_F(ConfigureSettingsTest, TransModeConverter_UnknownStr)
{
    umq_trans_mode_t result = TransMode::TransModeConverter("unknown", UMQ_TRANS_MODE_IB);
    EXPECT_EQ(result, UMQ_TRANS_MODE_IB);  // Should return default
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_UB)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_UB);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "UB");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_IB)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_IB);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "IB");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_UBMM)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_UBMM);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "UBMM");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_UBPlus)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_UB_PLUS);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "UB_PLUS");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_IBPlus)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_IB_PLUS);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "IB_PLUS");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_ToStr_UBMMPlus)
{
    const char* result = TransMode::TransModeConverter(UMQ_TRANS_MODE_UBMM_PLUS);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "UBMM_PLUS");
}

// ============= BoolVal Converter Tests =============

TEST_F(ConfigureSettingsTest, BoolConverter_NullStr)
{
    bool result = BoolVal::BoolConverter(nullptr, true);
    EXPECT_TRUE(result);

    result = BoolVal::BoolConverter(nullptr, false);
    EXPECT_FALSE(result);
}

TEST_F(ConfigureSettingsTest, BoolConverter_True)
{
    bool result = BoolVal::BoolConverter("true", false);
    EXPECT_TRUE(result);

    result = BoolVal::BoolConverter("TRUE", false);
    EXPECT_TRUE(result);

    result = BoolVal::BoolConverter("True", false);
    EXPECT_TRUE(result);
}

TEST_F(ConfigureSettingsTest, BoolConverter_False)
{
    bool result = BoolVal::BoolConverter("false", true);
    EXPECT_FALSE(result);

    result = BoolVal::BoolConverter("FALSE", true);
    EXPECT_FALSE(result);

    result = BoolVal::BoolConverter("False", true);
    EXPECT_FALSE(result);
}

TEST_F(ConfigureSettingsTest, BoolConverter_UnknownStr)
{
    bool result = BoolVal::BoolConverter("unknown", true);
    EXPECT_TRUE(result);  // Should return default

    result = BoolVal::BoolConverter("unknown", false);
    EXPECT_FALSE(result);  // Should return default
}

TEST_F(ConfigureSettingsTest, BoolConverter_ToStr_True)
{
    const char* result = BoolVal::BoolConverter(true);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "True");
}

TEST_F(ConfigureSettingsTest, BoolConverter_ToStr_False)
{
    const char* result = BoolVal::BoolConverter(false);
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "False");
}

// ============= ConfigSettings Basic Tests =============

TEST_F(ConfigureSettingsTest, ConfigSettings_DefaultValues)
{
    ConfigSettings config;
    // Just verify constructor doesn't crash
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetLogLevel)
{
    ConfigSettings config;
    ubsocket::util_vlog_level_t level = config.GetLogLevel();
    // Just verify the function doesn't crash, actual default may vary
    (void)level;  // Suppress unused variable warning
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetTxDepth)
{
    ConfigSettings config;
    uint32_t depth = config.GetTxDepth();
    EXPECT_EQ(depth, DEFAULT_TX_DEPTH);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetRxDepth)
{
    ConfigSettings config;
    uint32_t depth = config.GetRxDepth();
    EXPECT_EQ(depth, DEFAULT_RX_DEPTH);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetEidIdx)
{
    ConfigSettings config;
    uint32_t idx = config.GetEidIdx();
    EXPECT_EQ(idx, DEFAULT_EID_IDX);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetTransMode)
{
    ConfigSettings config;
    umq_trans_mode_t mode = config.GetTransMode();
    // Default should be UB
    EXPECT_EQ(mode, UMQ_TRANS_MODE_UB);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetIOTotalSize)
{
    ConfigSettings config;
    uint64_t size = config.GetIOTotalSize();
    EXPECT_EQ(size, DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetIOBlockTypeStr)
{
    ConfigSettings config;
    const char* typeStr = config.GetIOBlockTypeStr();
    EXPECT_NE(typeStr, nullptr);
    EXPECT_STREQ(typeStr, DEFAULT_QBUF_BLOCK_TYPE);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetStatsEnable)
{
    ConfigSettings config;
    bool enable = config.GetStatsEnable();
    EXPECT_FALSE(enable);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetSocketFdTransMode)
{
    ConfigSettings::socket_fd_trans_mode mode = ConfigSettings::GetSocketFdTransMode();
    EXPECT_EQ(mode, ConfigSettings::SOCKET_FD_TRANS_MODE_UNSET);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbTransMode)
{
    ConfigSettings config;
    ub_trans_mode mode = config.GetUbTransMode();
    EXPECT_EQ(mode, ub_trans_mode::RC_TP);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_SetUbTransMode)
{
    ConfigSettings config;
    config.SetUbTransMode(ub_trans_mode::RM_TP);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RM_TP);

    config.SetUbTransMode(ub_trans_mode::RC_CTP);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RC_CTP);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetTraceEnable)
{
    ConfigSettings config;
    bool enable = config.GetTraceEnable();
    EXPECT_TRUE(enable);  // Default is true
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevSchedulePolicy)
{
    ConfigSettings config;
    dev_schedule_policy policy = config.GetDevSchedulePolicy();
    EXPECT_EQ(policy, dev_schedule_policy::CPU_AFFINITY_PRIORITY);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_IsDevIpv6)
{
    ConfigSettings config;
    bool isIpv6 = config.IsDevIpv6();
    EXPECT_FALSE(isIpv6);  // Default is false
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevIpStr)
{
    ConfigSettings config;
    const char* ipStr = config.GetDevIpStr();
    EXPECT_EQ(ipStr, nullptr);  // Default is empty
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevNameStr)
{
    ConfigSettings config;
    const char* nameStr = config.GetDevNameStr();
    EXPECT_EQ(nameStr, nullptr);  // Default is empty
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetLogUse)
{
    ConfigSettings config;
    bool logUse = config.GetLogUse();
    EXPECT_TRUE(logUse);  // Default is true
}

// ============= ConfigSettings Init Tests =============

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_Basic)
{
    // Set up environment variables
    setenv("UBSOCKET_LOG_LEVEL", "INFO", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    setenv("UBSOCKET_TX_DEPTH", "128", SETENV_OVERWRITE);
    setenv("UBSOCKET_RX_DEPTH", "128", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_LOG_LEVEL");
    unsetenv("UBSOCKET_TRANS_MODE");
    unsetenv("UBSOCKET_TX_DEPTH");
    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_IPv4Address)
{
    setenv("UBSOCKET_DEV_IP", "192.168.1.100", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_IPv6Address)
{
    setenv("UBSOCKET_DEV_IP", "::1", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_InvalidIP)
{
    setenv("UBSOCKET_DEV_IP", "invalid_ip_address", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, -1);  // Should fail with invalid IP

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_DevNameWithValidEid)
{
    setenv("UBSOCKET_DEV_NAME", "ub0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "::1", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_DevNameWithInvalidEid)
{
    setenv("UBSOCKET_DEV_NAME", "ub0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "invalid_eid", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, -1);  // Should fail with invalid EID

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_StatsEnable)
{
    setenv("UBSOCKET_STATS_CLI", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(config.GetStatsEnable());

    unsetenv("UBSOCKET_STATS_CLI");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogUsePrintf)
{
    setenv("UBSOCKET_LOG_USE_PRINTF", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_LOG_USE_PRINTF");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TxDepthBelowMin)
{
    setenv("UBSOCKET_TX_DEPTH", "1", SETENV_OVERWRITE);  // Below MIN_TX_DEPTH

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    // Should be set to DEFAULT_TX_DEPTH
    EXPECT_EQ(config.GetTxDepth(), DEFAULT_TX_DEPTH);

    unsetenv("UBSOCKET_TX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_RxDepthBelowMin)
{
    setenv("UBSOCKET_RX_DEPTH", "1", SETENV_OVERWRITE);  // Below MIN_RX_DEPTH

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    // Should be set to DEFAULT_RX_DEPTH
    EXPECT_EQ(config.GetRxDepth(), DEFAULT_RX_DEPTH);

    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevSrcEid)
{
    ConfigSettings config;
    umq_eid_t eid = config.GetDevSrcEid();
    (void)eid;  // Just verify function exists
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetIOBlockType)
{
    ConfigSettings config;
    umq_buf_block_size_t type = config.GetIOBlockType();
    (void)type;  // Just verify function exists
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceTime)
{
    ConfigSettings config;
    uint64_t time = config.GetUbsocketTraceTime();
    (void)time;
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceFilePath)
{
    ConfigSettings config;
    const char* path = config.GetUbsocketTraceFilePath();
    (void)path;
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceFileSize)
{
    ConfigSettings config;
    uint64_t size = config.GetUbsocketTraceFileSize();
    (void)size;
}

// ============= Additional Init Tests for Coverage =============

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogUsePrintfTrue)
{
    setenv("UBSOCKET_LOG_USE_PRINTF", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_LEVEL", "DEBUG", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(config.GetLogUse());

    unsetenv("UBSOCKET_LOG_USE_PRINTF");
    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogUsePrintfFalse)
{
    setenv("UBSOCKET_LOG_USE_PRINTF", "false", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_LOG_USE_PRINTF");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeIB)
{
    setenv("UBSOCKET_TRANS_MODE", "IB", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTransMode(), UMQ_TRANS_MODE_IB);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeUBMM)
{
    setenv("UBSOCKET_TRANS_MODE", "UBMM", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTransMode(), UMQ_TRANS_MODE_UBMM);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeUBPlus)
{
    setenv("UBSOCKET_TRANS_MODE", "UB_PLUS", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTransMode(), UMQ_TRANS_MODE_UB_PLUS);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeIBPlus)
{
    setenv("UBSOCKET_TRANS_MODE", "IB_PLUS", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTransMode(), UMQ_TRANS_MODE_IB_PLUS);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeUBMMPlus)
{
    setenv("UBSOCKET_TRANS_MODE", "UBMM_PLUS", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTransMode(), UMQ_TRANS_MODE_UBMM_PLUS);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UseBrpcZCopy)
{
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TxDepthValid)
{
    setenv("UBSOCKET_TX_DEPTH", "256", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTxDepth(), CFG_TX_DEPTH_256);

    unsetenv("UBSOCKET_TX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_RxDepthValid)
{
    setenv("UBSOCKET_RX_DEPTH", "256", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetRxDepth(), CFG_RX_DEPTH_256);

    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_StatsEnableTrue)
{
    setenv("UBSOCKET_STATS_CLI", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(config.GetStatsEnable());

    unsetenv("UBSOCKET_STATS_CLI");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_StatsEnableFalse)
{
    setenv("UBSOCKET_STATS_CLI", "false", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(config.GetStatsEnable());

    unsetenv("UBSOCKET_STATS_CLI");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EidIdx)
{
    setenv("UBSOCKET_EID_IDX", "5", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetEidIdx(), CFG_EID_IDX_5);

    unsetenv("UBSOCKET_EID_IDX");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_IOTotalSize)
{
    setenv("UBSOCKET_POOL_INITIAL_SIZE", "20", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    uint64_t expectedSize = static_cast<uint64_t>(CFG_POOL_SIZE_20) * 1024UL * 1024UL;
    EXPECT_EQ(config.GetIOTotalSize(), expectedSize);

    unsetenv("UBSOCKET_POOL_INITIAL_SIZE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockType)
{
    setenv("UBSOCKET_BLOCK_TYPE", "4K", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

// Test for DevName only (no IP, no EID)
TEST_F(ConfigureSettingsTest, ConfigSettings_Init_DevNameOnly)
{
    setenv("UBSOCKET_DEV_NAME", "ub0", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);

    unsetenv("UBSOCKET_DEV_NAME");
}

// Test multiple env vars together
TEST_F(ConfigureSettingsTest, ConfigSettings_Init_MultipleEnvVars)
{
    setenv("UBSOCKET_LOG_LEVEL", "INFO", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    setenv("UBSOCKET_TX_DEPTH", "128", SETENV_OVERWRITE);
    setenv("UBSOCKET_RX_DEPTH", "128", SETENV_OVERWRITE);
    setenv("UBSOCKET_EID_IDX", "2", SETENV_OVERWRITE);
    setenv("UBSOCKET_STATS_CLI", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    int ret = config.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(config.GetTxDepth(), CFG_TX_DEPTH_128);
    EXPECT_EQ(config.GetRxDepth(), CFG_RX_DEPTH_128);
    EXPECT_EQ(config.GetEidIdx(), CFG_EID_IDX_2);
    EXPECT_TRUE(config.GetStatsEnable());

    unsetenv("UBSOCKET_LOG_LEVEL");
    unsetenv("UBSOCKET_TRANS_MODE");
    unsetenv("UBSOCKET_TX_DEPTH");
    unsetenv("UBSOCKET_RX_DEPTH");
    unsetenv("UBSOCKET_EID_IDX");
    unsetenv("UBSOCKET_STATS_CLI");
    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
}

// ============= Additional Get Methods Tests =============

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevSchedulePolicy_Default)
{
    ConfigSettings config;
    dev_schedule_policy policy = config.GetDevSchedulePolicy();
    EXPECT_EQ(policy, dev_schedule_policy::CPU_AFFINITY_PRIORITY);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_IsDevIpv6_Default)
{
    ConfigSettings config;
    bool isIpv6 = config.IsDevIpv6();
    EXPECT_FALSE(isIpv6);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetLogUse_Default)
{
    ConfigSettings config;
    bool logUse = config.GetLogUse();
    EXPECT_TRUE(logUse);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbTransMode_Default)
{
    ConfigSettings config;
    ub_trans_mode mode = config.GetUbTransMode();
    EXPECT_EQ(mode, ub_trans_mode::RC_TP);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_SetUbTransMode_Multiple)
{
    ConfigSettings config;

    config.SetUbTransMode(ub_trans_mode::RM_TP);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RM_TP);

    config.SetUbTransMode(ub_trans_mode::RC_CTP);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RC_CTP);

    config.SetUbTransMode(ub_trans_mode::RC_TP);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RC_TP);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetTraceEnable_Default)
{
    ConfigSettings config;
    bool enable = config.GetTraceEnable();
    EXPECT_TRUE(enable);
}

// ============= Init Edge Cases =============

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_AllTransModes)
{
    const char* modes[] = {"UB", "IB", "UBMM", "UB_PLUS", "IB_PLUS", "UBMM_PLUS"};
    umq_trans_mode_t expected[] = {
        UMQ_TRANS_MODE_UB, UMQ_TRANS_MODE_IB, UMQ_TRANS_MODE_UBMM,
        UMQ_TRANS_MODE_UB_PLUS, UMQ_TRANS_MODE_IB_PLUS, UMQ_TRANS_MODE_UBMM_PLUS
    };

    for (int i = 0; i < CFG_TRANS_MODE_COUNT_6; ++i) {
        setenv("UBSOCKET_TRANS_MODE", modes[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        EXPECT_EQ(config.GetTransMode(), expected[i]);
        unsetenv("UBSOCKET_TRANS_MODE");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogLevels)
{
    const char* levels[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    for (int i = 0; i < CFG_LOG_LEVEL_COUNT_4; ++i) {
        setenv("UBSOCKET_LOG_LEVEL", levels[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        unsetenv("UBSOCKET_LOG_LEVEL");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TxRxDepths_MinValues)
{
    setenv("UBSOCKET_TX_DEPTH", "1", SETENV_OVERWRITE);
    setenv("UBSOCKET_RX_DEPTH", "1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Should be set to DEFAULT values since below MIN
    EXPECT_EQ(config.GetTxDepth(), DEFAULT_TX_DEPTH);
    EXPECT_EQ(config.GetRxDepth(), DEFAULT_RX_DEPTH);

    unsetenv("UBSOCKET_TX_DEPTH");
    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TxRxDepths_MaxValues)
{
    setenv("UBSOCKET_TX_DEPTH", "65535", SETENV_OVERWRITE);
    setenv("UBSOCKET_RX_DEPTH", "65535", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetTxDepth(), CFG_TX_DEPTH_65535);
    EXPECT_EQ(config.GetRxDepth(), CFG_RX_DEPTH_65535);

    unsetenv("UBSOCKET_TX_DEPTH");
    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockTypes)
{
    const char* types[] = {"4K", "8K", "16K", "32K", "64K"};

    for (int i = 0; i < CFG_BLOCK_TYPE_COUNT_5; ++i) {
        setenv("UBSOCKET_BLOCK_TYPE", types[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        unsetenv("UBSOCKET_BLOCK_TYPE");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EidIdxRange)
{
    for (uint32_t idx = 0; idx < CFG_EID_IDX_LOOP_10; ++idx) {
        char buf[32];
        int ret = snprintf(buf, sizeof(buf), "%u", idx);
        EXPECT_GT(ret, 0);
        setenv("UBSOCKET_EID_IDX", buf, SETENV_OVERWRITE);

        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        EXPECT_EQ(config.GetEidIdx(), idx);

        unsetenv("UBSOCKET_EID_IDX");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_PoolSizeRange)
{
    setenv("UBSOCKET_POOL_INITIAL_SIZE", "1", SETENV_OVERWRITE);
    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetIOTotalSize(), static_cast<uint64_t>(CFG_INT_VAL_1) * 1024UL * 1024UL);
    unsetenv("UBSOCKET_POOL_INITIAL_SIZE");

    setenv("UBSOCKET_POOL_INITIAL_SIZE", "100", SETENV_OVERWRITE);
    ConfigSettings config2;
    EXPECT_EQ(config2.Init(), 0);
    EXPECT_EQ(config2.GetIOTotalSize(), static_cast<uint64_t>(CFG_POOL_SIZE_100) * 1024UL * 1024UL);
    unsetenv("UBSOCKET_POOL_INITIAL_SIZE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BooleanCombinations)
{
    // All true
    setenv("UBSOCKET_STATS_CLI", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_USE_PRINTF", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_TRUE(config.GetStatsEnable());

    unsetenv("UBSOCKET_STATS_CLI");
    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
    unsetenv("UBSOCKET_LOG_USE_PRINTF");

    // All false
    setenv("UBSOCKET_STATS_CLI", "false", SETENV_OVERWRITE);
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "false", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_USE_PRINTF", "false", SETENV_OVERWRITE);

    ConfigSettings config2;
    EXPECT_EQ(config2.Init(), 0);
    EXPECT_FALSE(config2.GetStatsEnable());

    unsetenv("UBSOCKET_STATS_CLI");
    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
    unsetenv("UBSOCKET_LOG_USE_PRINTF");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_IPv6FullAddress)
{
    setenv("UBSOCKET_DEV_IP", "2001:0db8:85a3:0000:0000:8a2e:0370:7334", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_TRUE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LocalhostIPv4)
{
    setenv("UBSOCKET_DEV_IP", "127.0.0.1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_FALSE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LocalhostIPv6)
{
    setenv("UBSOCKET_DEV_IP", "::1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_TRUE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

// ============= Additional Tests for Better Coverage =============

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockTypeSmall)
{
    setenv("UBSOCKET_BLOCK_TYPE", "small", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_STREQ(config.GetIOBlockTypeStr(), "small");

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockTypeMedium)
{
    setenv("UBSOCKET_BLOCK_TYPE", "medium", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_STREQ(config.GetIOBlockTypeStr(), "medium");

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockTypeLarge)
{
    setenv("UBSOCKET_BLOCK_TYPE", "large", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_STREQ(config.GetIOBlockTypeStr(), "large");

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_BlockTypeDefault)
{
    setenv("UBSOCKET_BLOCK_TYPE", "default", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_STREQ(config.GetIOBlockTypeStr(), "default");

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SchedulePolicyAffinity)
{
    setenv("UBSOCKET_SCHEDULE_POLICY", "affinity", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetDevSchedulePolicy(), dev_schedule_policy::CPU_AFFINITY);

    unsetenv("UBSOCKET_SCHEDULE_POLICY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SchedulePolicyRR)
{
    setenv("UBSOCKET_SCHEDULE_POLICY", "rr", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetDevSchedulePolicy(), dev_schedule_policy::ROUND_ROBIN);

    unsetenv("UBSOCKET_SCHEDULE_POLICY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SchedulePolicyAffinityPriority)
{
    setenv("UBSOCKET_SCHEDULE_POLICY", "affinity_priority", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetDevSchedulePolicy(), dev_schedule_policy::CPU_AFFINITY_PRIORITY);

    unsetenv("UBSOCKET_SCHEDULE_POLICY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SchedulePolicyInvalid)
{
    setenv("UBSOCKET_SCHEDULE_POLICY", "invalid", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Invalid policy should fall back to CPU_AFFINITY_PRIORITY
    EXPECT_EQ(config.GetDevSchedulePolicy(), dev_schedule_policy::CPU_AFFINITY_PRIORITY);

    unsetenv("UBSOCKET_SCHEDULE_POLICY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceEnableTrue)
{
    setenv("UBSOCKET_TRACE_ENABLE", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_TRUE(config.GetTraceEnable());

    unsetenv("UBSOCKET_TRACE_ENABLE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceEnableFalse)
{
    setenv("UBSOCKET_TRACE_ENABLE", "false", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_FALSE(config.GetTraceEnable());

    unsetenv("UBSOCKET_TRACE_ENABLE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceTimeValid)
{
    setenv("UBSOCKET_TRACE_TIME", "30", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceTime(), static_cast<uint64_t>(CFG_TRACE_TIME_30));

    unsetenv("UBSOCKET_TRACE_TIME");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceTimeMin)
{
    setenv("UBSOCKET_TRACE_TIME", "1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceTime(), static_cast<uint64_t>(CFG_TRACE_TIME_1));

    unsetenv("UBSOCKET_TRACE_TIME");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceTimeMax)
{
    setenv("UBSOCKET_TRACE_TIME", "300", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceTime(), static_cast<uint64_t>(CFG_TRACE_TIME_300));

    unsetenv("UBSOCKET_TRACE_TIME");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceFileSizeValid)
{
    setenv("UBSOCKET_TRACE_FILE_SIZE", "50", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceFileSize(), static_cast<uint64_t>(CFG_TRACE_FILE_SIZE_50));

    unsetenv("UBSOCKET_TRACE_FILE_SIZE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UbTransModeRMTP)
{
    setenv("UBSOCKET_UB_TRANS_MODE", "RM_TP", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RM_TP);

    unsetenv("UBSOCKET_UB_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UbTransModeRMCTP)
{
    setenv("UBSOCKET_UB_TRANS_MODE", "RM_CTP", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RM_CTP);

    unsetenv("UBSOCKET_UB_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UbTransModeRCCTP)
{
    setenv("UBSOCKET_UB_TRANS_MODE", "RC_CTP", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RC_CTP);

    unsetenv("UBSOCKET_UB_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UbTransModeInvalid)
{
    setenv("UBSOCKET_UB_TRANS_MODE", "INVALID", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Invalid mode should fall back to RC_TP
    EXPECT_EQ(config.GetUbTransMode(), ub_trans_mode::RC_TP);

    unsetenv("UBSOCKET_UB_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceFilePathValid)
{
    setenv("UBSOCKET_TRACE_FILE_PATH", "/tmp/valid_trace_path", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_NE(config.GetUbsocketTraceFilePath(), nullptr);

    unsetenv("UBSOCKET_TRACE_FILE_PATH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TraceFilePathInvalid)
{
    // Path not starting with / should use default
    setenv("UBSOCKET_TRACE_FILE_PATH", "invalid_path", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_TRACE_FILE_PATH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EidIdxZero)
{
    setenv("UBSOCKET_EID_IDX", "0", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetEidIdx(), DEFAULT_EID_IDX);

    unsetenv("UBSOCKET_EID_IDX");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TxDepthZero)
{
    setenv("UBSOCKET_TX_DEPTH", "0", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetTxDepth(), DEFAULT_TX_DEPTH);

    unsetenv("UBSOCKET_TX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_RxDepthZero)
{
    setenv("UBSOCKET_RX_DEPTH", "0", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetRxDepth(), DEFAULT_RX_DEPTH);

    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_PoolSizeZero)
{
    setenv("UBSOCKET_POOL_INITIAL_SIZE", "0", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Zero should use default
    EXPECT_EQ(config.GetIOTotalSize(), DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB);

    unsetenv("UBSOCKET_POOL_INITIAL_SIZE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_AllUbTransModes)
{
    const char* modes[] = {"RC_TP", "RM_TP", "RM_CTP", "RC_CTP"};
    ub_trans_mode expected[] = {
        ub_trans_mode::RC_TP, ub_trans_mode::RM_TP, ub_trans_mode::RM_CTP, ub_trans_mode::RC_CTP
    };

    for (int i = 0; i < CFG_LOG_LEVEL_COUNT_4; ++i) {
        setenv("UBSOCKET_UB_TRANS_MODE", modes[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        EXPECT_EQ(config.GetUbTransMode(), expected[i]);
        unsetenv("UBSOCKET_UB_TRANS_MODE");
    }
}

// ============= EnvStrConverter Template Tests =============

TEST_F(ConfigureSettingsTest, EnvStrConverter_IntType)
{
    // Create a simple EnvStrConverter for int
    static const char* gValOne[] = {"one", "1", nullptr};
    static const char* gValTwo[] = {"two", "2", nullptr};

    static const EnvStrConverter<int>::EnvStrDef gIntDef[] = {
        {CFG_INT_VAL_1, "ONE", gValOne},
        {CFG_INT_VAL_2, "TWO", gValTwo}
    };

    EnvStrConverter<int> converter(gIntDef, CFG_INT_VAL_2);

    EXPECT_EQ(converter.EnvStrConvert("one", 0), CFG_INT_VAL_1);
    EXPECT_EQ(converter.EnvStrConvert("1", 0), CFG_INT_VAL_1);
    EXPECT_EQ(converter.EnvStrConvert("two", 0), CFG_INT_VAL_2);
    EXPECT_EQ(converter.EnvStrConvert("unknown", CFG_INT_VAL_99), CFG_INT_VAL_99);  // Default

    EXPECT_STREQ(converter.EnvStrConvert(CFG_INT_VAL_1), "ONE");
    EXPECT_STREQ(converter.EnvStrConvert(CFG_INT_VAL_2), "TWO");
    EXPECT_EQ(converter.EnvStrConvert(CFG_INT_VAL_99), nullptr);  // Not found
}

TEST_F(ConfigureSettingsTest, EnvStrConverter_NullStr)
{
    static const char* gValTrue[] = {"true", nullptr};
    static const EnvStrConverter<bool>::EnvStrDef gBoolDef[] = {
        {true, "True", gValTrue}
    };

    EnvStrConverter<bool> converter(gBoolDef, CFG_INT_VAL_1);

    EXPECT_FALSE(converter.EnvStrConvert(nullptr, false));
    EXPECT_TRUE(converter.EnvStrConvert(nullptr, true));
}

TEST_F(ConfigureSettingsTest, EnvStrConvert_CaseInsensitive)
{
    // Test case insensitivity
    EXPECT_EQ(TransMode::TransModeConverter("ub", UMQ_TRANS_MODE_IB), UMQ_TRANS_MODE_UB);
    EXPECT_EQ(TransMode::TransModeConverter("UB", UMQ_TRANS_MODE_IB), UMQ_TRANS_MODE_UB);
    EXPECT_EQ(TransMode::TransModeConverter("Ub", UMQ_TRANS_MODE_IB), UMQ_TRANS_MODE_UB);
}

TEST_F(ConfigureSettingsTest, BoolConverter_VariousInputs)
{
    EXPECT_TRUE(BoolVal::BoolConverter("true", false));
    EXPECT_FALSE(BoolVal::BoolConverter("false", true));
    EXPECT_TRUE(BoolVal::BoolConverter("unknown", true));  // Default true
    EXPECT_FALSE(BoolVal::BoolConverter("unknown", false));  // Default false
}

TEST_F(ConfigureSettingsTest, BoolConverter_ToString)
{
    EXPECT_STREQ(BoolVal::BoolConverter(true), "True");
    EXPECT_STREQ(BoolVal::BoolConverter(false), "False");
}

// ============= ConfigSettings Additional Tests =============

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevSchedulePolicy_SetValue)
{
    setenv("UBSOCKET_SCHEDULE_POLICY", "affinity", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetDevSchedulePolicy(), CPU_AFFINITY);

    unsetenv("UBSOCKET_SCHEDULE_POLICY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_IsDevIpv6_True)
{
    setenv("UBSOCKET_DEV_IP", "::1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_TRUE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_IsDevIpv6_False)
{
    setenv("UBSOCKET_DEV_IP", "192.168.1.1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_FALSE(config.IsDevIpv6());

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetDevSrcEidAdditional)
{
    setenv("UBSOCKET_DEV_NAME", "eth0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "0102:0304:0506:0708:090a:0b0c:0d0e:0f10", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_TransModeToStr_All)
{
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_UB), "UB");
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_IB), "IB");
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_UBMM), "UBMM");
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_UB_PLUS), "UB_PLUS");
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_IB_PLUS), "IB_PLUS");
    EXPECT_STREQ(TransMode::TransModeConverter(UMQ_TRANS_MODE_UBMM_PLUS), "UBMM_PLUS");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_TransModeAllVariants)
{
    // Test all alias names for UB
    EXPECT_EQ(TransMode::TransModeConverter("ub", UMQ_TRANS_MODE_IB), UMQ_TRANS_MODE_UB);

    // Test all alias names for IB
    EXPECT_EQ(TransMode::TransModeConverter("ib", UMQ_TRANS_MODE_UB), UMQ_TRANS_MODE_IB);

    // Test all alias names for UBMM
    EXPECT_EQ(TransMode::TransModeConverter("ubmm", UMQ_TRANS_MODE_UB), UMQ_TRANS_MODE_UBMM);

    // Test UB_PLUS
    EXPECT_EQ(TransMode::TransModeConverter("ub_plus", UMQ_TRANS_MODE_UB), UMQ_TRANS_MODE_UB_PLUS);

    // Test IB_PLUS
    EXPECT_EQ(TransMode::TransModeConverter("ib_plus", UMQ_TRANS_MODE_UB), UMQ_TRANS_MODE_IB_PLUS);

    // Test UBMM_PLUS
    EXPECT_EQ(TransMode::TransModeConverter("ubmm_plus", UMQ_TRANS_MODE_UB), UMQ_TRANS_MODE_UBMM_PLUS);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_InvalidIP)
{
    setenv("UBSOCKET_DEV_IP", "invalid_ip_address", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), -1);  // Should fail for invalid IP

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_InvalidEid)
{
    setenv("UBSOCKET_DEV_NAME", "eth0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "invalid_eid_format", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), -1);  // Should fail for invalid EID

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceTimeDefault)
{
    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceTime(), UBSOCKET_TRACE_TIME_DEFAULT);
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceFilePathDefault)
{
    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    std::string path = config.GetUbsocketTraceFilePath();
    EXPECT_FALSE(path.empty());
}

TEST_F(ConfigureSettingsTest, ConfigSettings_GetUbsocketTraceFileSizeDefault)
{
    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetUbsocketTraceFileSize(), UBSOCKET_TRACE_FILE_SIZE_DEFAULT);
}

// ============= Additional Tests for Higher Coverage =============

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogLevelDebug)
{
    setenv("UBSOCKET_LOG_LEVEL", "DEBUG", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetLogLevel(), ubsocket::UTIL_VLOG_LEVEL_DEBUG);

    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogLevelWarn)
{
    setenv("UBSOCKET_LOG_LEVEL", "WARN", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetLogLevel(), ubsocket::UTIL_VLOG_LEVEL_WARN);

    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogLevelError)
{
    setenv("UBSOCKET_LOG_LEVEL", "ERROR", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetLogLevel(), ubsocket::UTIL_VLOG_LEVEL_ERR);

    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_AllBlockTypes)
{
    const char* types[] = {"4K", "8K", "16K", "32K", "64K"};

    for (int i = 0; i < CFG_BLOCK_TYPE_COUNT_5; ++i) {
        setenv("UBSOCKET_BLOCK_TYPE", types[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        unsetenv("UBSOCKET_BLOCK_TYPE");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_InvalidBlockType)
{
    setenv("UBSOCKET_BLOCK_TYPE", "INVALID", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);  // Should still succeed with default

    unsetenv("UBSOCKET_BLOCK_TYPE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EmptyLogLevel)
{
    setenv("UBSOCKET_LOG_LEVEL", "", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EmptyTransMode)
{
    setenv("UBSOCKET_TRANS_MODE", "", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_TRANS_MODE");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EmptyDevIp)
{
    setenv("UBSOCKET_DEV_IP", "", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_EmptyDevName)
{
    setenv("UBSOCKET_DEV_NAME", "", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_NAME");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LargeTxDepth)
{
    setenv("UBSOCKET_TX_DEPTH", "65535", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetTxDepth(), CFG_TX_DEPTH_65535);

    unsetenv("UBSOCKET_TX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LargeRxDepth)
{
    setenv("UBSOCKET_RX_DEPTH", "65535", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetRxDepth(), CFG_RX_DEPTH_65535);

    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_AllBooleanTrue)
{
    setenv("UBSOCKET_STATS_CLI", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_USE_PRINTF", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_STATS_CLI");
    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
    unsetenv("UBSOCKET_LOG_USE_PRINTF");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_AllBooleanFalse)
{
    setenv("UBSOCKET_STATS_CLI", "false", SETENV_OVERWRITE);
    setenv("UBSOCKET_USE_BRPC_ZCOPY", "false", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_USE_PRINTF", "false", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_STATS_CLI");
    unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
    unsetenv("UBSOCKET_LOG_USE_PRINTF");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_ZeroEidIdx)
{
    setenv("UBSOCKET_EID_IDX", "0", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_EID_IDX");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LargeEidIdx)
{
    setenv("UBSOCKET_EID_IDX", "255", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    EXPECT_EQ(config.GetEidIdx(), CFG_EID_IDX_255);

    unsetenv("UBSOCKET_EID_IDX");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LargePoolSize)
{
    setenv("UBSOCKET_POOL_INITIAL_SIZE", "1000", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_POOL_INITIAL_SIZE");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_AllModesToFromStr)
{
    // Test all modes with round-trip conversion
    umq_trans_mode_t modes[] = {
        UMQ_TRANS_MODE_UB, UMQ_TRANS_MODE_IB, UMQ_TRANS_MODE_UBMM,
        UMQ_TRANS_MODE_UB_PLUS, UMQ_TRANS_MODE_IB_PLUS, UMQ_TRANS_MODE_UBMM_PLUS
    };

    for (int i = 0; i < CFG_TRANS_MODE_COUNT_6; ++i) {
        const char* str = TransMode::TransModeConverter(modes[i]);
        EXPECT_NE(str, nullptr);
        umq_trans_mode_t back = TransMode::TransModeConverter(str, UMQ_TRANS_MODE_UB);
        EXPECT_EQ(back, modes[i]);
    }
}

TEST_F(ConfigureSettingsTest, BoolConverter_RoundTrip)
{
    bool values[] = {true, false};

    for (int i = 0; i < CFG_INT_VAL_2; ++i) {
        const char* str = BoolVal::BoolConverter(values[i]);
        EXPECT_NE(str, nullptr);
        bool back = BoolVal::BoolConverter(str, !values[i]);
        EXPECT_EQ(back, values[i]);
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_InvalidIpAddress)
{
    setenv("UBSOCKET_DEV_IP", "invalid.ip.address", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), -1);  // Invalid IP should return -1

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_ValidIpv6Address)
{
    setenv("UBSOCKET_DEV_IP", "::1", SETENV_OVERWRITE);  // IPv6 loopback

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_ValidIpv6Address2)
{
    setenv("UBSOCKET_DEV_IP", "2001:db8::1", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_IP");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_DevNameWithValidEidAdditional)
{
    setenv("UBSOCKET_DEV_NAME", "eth0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "::1", SETENV_OVERWRITE);  // Valid IPv6 EID

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_DevNameWithInvalidEidAdditional)
{
    setenv("UBSOCKET_DEV_NAME", "eth0", SETENV_OVERWRITE);
    setenv("UBSOCKET_SRC_EID", "invalid_eid", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), -1);  // Invalid EID should return -1

    unsetenv("UBSOCKET_DEV_NAME");
    unsetenv("UBSOCKET_SRC_EID");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_TransModeAllTypes)
{
    const char* modes[] = {"ub", "ib", "ubmm", "ub_plus", "ib_plus", "ubmm_plus"};

    for (int i = 0; i < CFG_TRANS_MODE_COUNT_6; ++i) {
        setenv("UBSOCKET_TRANS_MODE", modes[i], SETENV_OVERWRITE);
        ConfigSettings config;
        EXPECT_EQ(config.Init(), 0);
        unsetenv("UBSOCKET_TRANS_MODE");
    }
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_LogUsePrintfTrueAdditional)
{
    setenv("UBSOCKET_LOG_USE_PRINTF", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_LOG_LEVEL", "INFO", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_LOG_USE_PRINTF");
    unsetenv("UBSOCKET_LOG_LEVEL");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_StatsEnabledAdditional)
{
    setenv("UBSOCKET_STATS", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_STATS");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_UseBrpcZcopyAdditional)
{
    setenv("UBSOCKET_USE_ZCOPY", "true", SETENV_OVERWRITE);

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);

    unsetenv("UBSOCKET_USE_ZCOPY");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SmallTxDepth)
{
    setenv("UBSOCKET_TX_DEPTH", "1", SETENV_OVERWRITE);  // Less than MIN_TX_DEPTH

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Should be adjusted to DEFAULT_TX_DEPTH since 1 < MIN_TX_DEPTH

    unsetenv("UBSOCKET_TX_DEPTH");
}

TEST_F(ConfigureSettingsTest, ConfigSettings_Init_SmallRxDepth)
{
    setenv("UBSOCKET_RX_DEPTH", "1", SETENV_OVERWRITE);  // Less than MIN_RX_DEPTH

    ConfigSettings config;
    EXPECT_EQ(config.Init(), 0);
    // Should be adjusted to DEFAULT_RX_DEPTH since 1 < MIN_RX_DEPTH

    unsetenv("UBSOCKET_RX_DEPTH");
}

TEST_F(ConfigureSettingsTest, TransModeConverter_InvalidString)
{
    umq_trans_mode_t mode = TransMode::TransModeConverter("invalid_mode", UMQ_TRANS_MODE_UB);
    EXPECT_EQ(mode, UMQ_TRANS_MODE_UB);  // Should return default
}

TEST_F(ConfigureSettingsTest, BoolConverter_InvalidString)
{
    bool val = BoolVal::BoolConverter("invalid", true);
    EXPECT_EQ(val, true);  // Should return default
}