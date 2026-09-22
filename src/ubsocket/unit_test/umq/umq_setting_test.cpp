/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#define _GNU_SOURCE 1

#include "umq_setting.h"

#include <cstdlib>
#include <cstring>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_defines.h"

using namespace ock::ubs;
using namespace umq;

namespace {
/* LoadEnv 读取的环境变量名(与 umq_setting.cpp 的 ENV_UMQ_* 定义一致) */
constexpr const char *ENV_INITIAL_CREDIT = "UBSOCKET_INITIAL_CREDIT";
constexpr const char *ENV_MAX_CREDIT_PER_REQUEST = "UBSOCKET_MAX_CREDIT_PER_REQUEST";
constexpr const char *ENV_MIN_RESERVED_CREDIT = "UBSOCKET_MIN_RESERVED_CREDIT";
constexpr const char *ENV_MEM_POOL_MAX_SIZE = "UBSOCKET_POOL_MAX_SIZE";
constexpr const char *ENV_BLOCK_TYPE = "UBSOCKET_BLOCK_TYPE";
constexpr const char *ENV_TINY_POOL_ENABLE = "UBSOCKET_UMQ_TINY_POOL_ENABLE";
constexpr const char *ENV_TINY_POOL_BLOCK_SIZE = "UBSOCKET_UMQ_TINY_POOL_BLOCK_SIZE";
constexpr const char *ENV_TINY_POOL_BLOCK_COUNT = "UBSOCKET_UMQ_TINY_POOL_BLOCK_COUNT";
constexpr const char *ENV_TLS_TINY_POOL_DEPTH = "UBSOCKET_UMQ_TLS_TINY_POOL_DEPTH";
constexpr const char *ENV_SCHEDULE_POLICY = "UBSOCKET_SCHEDULE_POLICY";
constexpr const char *ENV_UB_TRANS_MODE = "UBSOCKET_UB_TRANS_MODE";
constexpr const char *ENV_FLOW_CONTROL_ENABLED = "UBSOCKET_FLOW_CONTROL_ENABLE";
constexpr const char *ENV_RANDOM_ROUTE = "UBSOCKET_RANDOM_ROUTE";
constexpr const char *ENV_LINK_PRIORITY = "UBSOCKET_LINK_PRIORITY";
constexpr const char *ENV_TP_TYPE = "UBSOCKET_JETTY_TYPE";
constexpr const char *ENV_TP_POOL_SIZE = "UBSOCKET_JETTY_POOL_SIZE";
constexpr const char *ENV_O3_TIMEOUT_MS = "UBSOCKET_O3_TIMEOUT_MS";
constexpr const char *ENV_SMALL_BUF_POOL_DEPTH = "UBSOCKET_SMALL_BUF_POOL_DEPTH";
constexpr const char *ENV_SMALL_GLOBAL_POOL_DEPTH = "UBSOCKET_SMALL_GLOBAL_POOL_DEPTH";
constexpr const char *ENV_MIDDLE_BUF_POOL_DEPTH = "UBSOCKET_MIDDLE_BUF_POOL_DEPTH";
constexpr const char *ENV_MIDDLE_GLOBAL_POOL_DEPTH = "UBSOCKET_MIDDLE_GLOBAL_POOL_DEPTH";
constexpr const char *ENV_MIDDLE_POOL_BLOCK_SIZE = "UBSOCKET_MIDDLE_POOL_BLOCK_SIZE";

constexpr const char *ALL_ENV_NAMES[] = {ENV_INITIAL_CREDIT,
                                         ENV_MAX_CREDIT_PER_REQUEST,
                                         ENV_MIN_RESERVED_CREDIT,
                                         ENV_MEM_POOL_MAX_SIZE,
                                         ENV_BLOCK_TYPE,
                                         ENV_TINY_POOL_ENABLE,
                                         ENV_TINY_POOL_BLOCK_SIZE,
                                         ENV_TINY_POOL_BLOCK_COUNT,
                                         ENV_TLS_TINY_POOL_DEPTH,
                                         ENV_SCHEDULE_POLICY,
                                         ENV_UB_TRANS_MODE,
                                         ENV_FLOW_CONTROL_ENABLED,
                                         ENV_RANDOM_ROUTE,
                                         ENV_LINK_PRIORITY,
                                         ENV_TP_TYPE,
                                         ENV_TP_POOL_SIZE,
                                         ENV_O3_TIMEOUT_MS,
                                         ENV_SMALL_BUF_POOL_DEPTH,
                                         ENV_SMALL_GLOBAL_POOL_DEPTH,
                                         ENV_MIDDLE_BUF_POOL_DEPTH,
                                         ENV_MIDDLE_GLOBAL_POOL_DEPTH,
                                         ENV_MIDDLE_POOL_BLOCK_SIZE};

void UnsetAllEnv()
{
    for (const char *name : ALL_ENV_NAMES) {
        unsetenv(name);
    }
}

/* setenv 的 overwrite 标志:覆盖已存在的环境变量 */
constexpr int SETENV_OVERWRITE = 1;

void SetEnv(const char *name, const char *value)
{
    setenv(name, value, SETENV_OVERWRITE);
}

/* UmqSetting 出厂默认值(对齐 umq_setting.cpp:59-95 初始值)。
 * ResetStatics 与断言共用,独立于成员当前值——成员被用例污染时断言仍能抓住回归 */
constexpr uint16_t DEFAULT_FC_DEFAULT_CREDIT = 16;
constexpr uint16_t DEFAULT_FC_MAX_CREDIT = 256;
constexpr uint16_t DEFAULT_FC_MIN_CREDIT = 2;
constexpr uint64_t DEFAULT_MEM_POOL_MAX_SIZE_MB = 2048;
constexpr uint32_t DEFAULT_TINY_POOL_BLOCK_COUNT = 8192;
constexpr uint64_t DEFAULT_TLS_TINY_POOL_DEPTH = 8192;
constexpr uint64_t DEFAULT_SMALL_BUF_POOL_DEPTH = 1024;
constexpr uint64_t DEFAULT_SMALL_GLOBAL_POOL_DEPTH = 512;
constexpr uint64_t DEFAULT_MIDDLE_BUF_POOL_DEPTH = 1024;
constexpr uint64_t DEFAULT_MIDDLE_GLOBAL_POOL_DEPTH = 512;
constexpr uint32_t DEFAULT_TP_POOL_SIZE = 800;
constexpr uint64_t DEFAULT_O3_TIMEOUT_MS = 60000;

/* 恢复 UmqSetting 静态成员为 umq_setting.cpp 初始值,避免用例间状态污染 */
void ResetStatics()
{
    UmqSetting::IO_BLOCK_TYPE = BLOCK_SIZE_4K;
    UmqSetting::UMQ_POOL_BASE_BLOCK_SIZE = BLOCK_SIZE_4K;
    UmqSetting::UMQ_SIZE_CLASS_COUNT = 2;
    UmqSetting::UMQ_EXPLICIT_BLOCK_SIZES[0] = static_cast<uint32_t>(SIZE_4K);
    UmqSetting::UMQ_EXPLICIT_BLOCK_SIZES[1] = static_cast<uint32_t>(SIZE_64K);
    UmqSetting::UMQ_FC_DEFAULT_CREDIT = DEFAULT_FC_DEFAULT_CREDIT;
    UmqSetting::UMQ_FC_MAX_CREDIT = DEFAULT_FC_MAX_CREDIT;
    UmqSetting::UMQ_FC_MIN_CREDIT = DEFAULT_FC_MIN_CREDIT;
    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = DEFAULT_MEM_POOL_MAX_SIZE_MB;
    UmqSetting::UMQ_TINY_POOL_ENABLE = true;
    UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE = TINY_BLOCK_SIZE_1K;
    UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT = DEFAULT_TINY_POOL_BLOCK_COUNT;
    UmqSetting::UMQ_TLS_TINY_POOL_DEPTH = DEFAULT_TLS_TINY_POOL_DEPTH;
    UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH = DEFAULT_SMALL_BUF_POOL_DEPTH;
    UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH = DEFAULT_SMALL_GLOBAL_POOL_DEPTH;
    UmqSetting::UMQ_MIDDLE_BUF_POOL_DEPTH = DEFAULT_MIDDLE_BUF_POOL_DEPTH;
    UmqSetting::UMQ_MIDDLE_GLOBAL_POOL_DEPTH = DEFAULT_MIDDLE_GLOBAL_POOL_DEPTH;
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_64K);
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME = "affinity_priority";
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = CPU_AFFINITY_PRIORITY;
    UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
    UmqSetting::UMQ_UB_TP_MODE = UMQ_TM_RM;
    UmqSetting::UMQ_UB_TP_TYPE = UMQ_TP_TYPE_RTP;
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
    UmqSetting::UMQ_RANDOM_ROUTE = true;
    UmqSetting::UMQ_LINK_PRIORITY = UBSOCKET_LINK_PRIORITY_DEFAULT;
    UmqSetting::UMQ_TP_TYPE = POOL;
    UmqSetting::UMQ_TP_POOL_SIZE = DEFAULT_TP_POOL_SIZE;
    UmqSetting::UMQ_O3_TIMEOUT_MS = DEFAULT_O3_TIMEOUT_MS;
}
} // namespace

class UmqSettingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        UnsetAllEnv();
        ResetStatics();
        /* 注册全部规则(Validator emplace 幂等,重复调用安全) */
        UmqSetting::AddRules();
    }

    void TearDown() override
    {
        UnsetAllEnv();
        ResetStatics();
        GlobalMockObject::verify();
    }
};

// ==================== LoadEnv: 无环境变量 ====================

TEST_F(UmqSettingTest, LoadEnv_NoEnvSet_KeepsDefaults)
{
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));

    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, DEFAULT_FC_DEFAULT_CREDIT);
    EXPECT_EQ(UmqSetting::UMQ_FC_MAX_CREDIT, DEFAULT_FC_MAX_CREDIT);
    EXPECT_EQ(UmqSetting::UMQ_FC_MIN_CREDIT, DEFAULT_FC_MIN_CREDIT);
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, DEFAULT_MEM_POOL_MAX_SIZE_MB);
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, DEFAULT_SMALL_BUF_POOL_DEPTH);
    EXPECT_EQ(UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH, DEFAULT_SMALL_GLOBAL_POOL_DEPTH);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_BUF_POOL_DEPTH, DEFAULT_MIDDLE_BUF_POOL_DEPTH);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_GLOBAL_POOL_DEPTH, DEFAULT_MIDDLE_GLOBAL_POOL_DEPTH);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE, static_cast<uint32_t>(SIZE_64K));
    EXPECT_TRUE(UmqSetting::UMQ_TINY_POOL_ENABLE);
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE, TINY_BLOCK_SIZE_1K);
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT, DEFAULT_TINY_POOL_BLOCK_COUNT);
    EXPECT_EQ(UmqSetting::UMQ_TLS_TINY_POOL_DEPTH, DEFAULT_TLS_TINY_POOL_DEPTH);
    EXPECT_EQ(UmqSetting::UMQ_TP_TYPE, POOL);
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, DEFAULT_TP_POOL_SIZE);
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, UBSOCKET_LINK_PRIORITY_DEFAULT);
    EXPECT_FALSE(UmqSetting::UMQ_FLOW_CONTROL_ENABLE);
    EXPECT_TRUE(UmqSetting::UMQ_RANDOM_ROUTE);
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME, "affinity_priority");
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY, CPU_AFFINITY_PRIORITY);
    EXPECT_EQ(UmqSetting::UMQ_UB_TRANS_MODE, RM_TP);
    EXPECT_EQ(UmqSetting::IO_BLOCK_TYPE, BLOCK_SIZE_4K);
}

// ==================== LoadEnv: 全部 int64 环境变量 ====================

TEST_F(UmqSettingTest, LoadEnv_AllInt64EnvSet_AppliesAll)
{
    SetEnv(ENV_INITIAL_CREDIT, "100");
    SetEnv(ENV_MAX_CREDIT_PER_REQUEST, "200");
    SetEnv(ENV_MIN_RESERVED_CREDIT, "50");
    SetEnv(ENV_MEM_POOL_MAX_SIZE, "1024");
    SetEnv(ENV_SMALL_BUF_POOL_DEPTH, "2048");
    SetEnv(ENV_SMALL_GLOBAL_POOL_DEPTH, "1024");
    SetEnv(ENV_MIDDLE_BUF_POOL_DEPTH, "2048");
    SetEnv(ENV_MIDDLE_GLOBAL_POOL_DEPTH, "1024");
    SetEnv(ENV_MIDDLE_POOL_BLOCK_SIZE, "131072");
    SetEnv(ENV_TINY_POOL_BLOCK_COUNT, "4096");
    SetEnv(ENV_TLS_TINY_POOL_DEPTH, "4096");
    SetEnv(ENV_TP_POOL_SIZE, "500");
    SetEnv(ENV_LINK_PRIORITY, "10");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));

    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, static_cast<uint16_t>(100));
    EXPECT_EQ(UmqSetting::UMQ_FC_MAX_CREDIT, static_cast<uint16_t>(200));
    EXPECT_EQ(UmqSetting::UMQ_FC_MIN_CREDIT, static_cast<uint16_t>(50));
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, 1024u);
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, 2048u);
    EXPECT_EQ(UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH, 1024u);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_BUF_POOL_DEPTH, 2048u);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_GLOBAL_POOL_DEPTH, 1024u);
    EXPECT_EQ(UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE, static_cast<uint32_t>(SIZE_128K));
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT, 4096u);
    EXPECT_EQ(UmqSetting::UMQ_TLS_TINY_POOL_DEPTH, 4096u);
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, 500u);
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, static_cast<int8_t>(10));
}

// ==================== LoadEnv: 全部 str 环境变量 ====================

TEST_F(UmqSettingTest, LoadEnv_AllStrEnvSet_AppliesAll)
{
    SetEnv(ENV_TINY_POOL_ENABLE, "false");
    SetEnv(ENV_TINY_POOL_BLOCK_SIZE, "4K");
    SetEnv(ENV_TP_TYPE, "single");
    SetEnv(ENV_FLOW_CONTROL_ENABLED, "true");
    SetEnv(ENV_RANDOM_ROUTE, "false");
    SetEnv(ENV_SCHEDULE_POLICY, "rr");
    SetEnv(ENV_UB_TRANS_MODE, "RM_CTP");
    SetEnv(ENV_BLOCK_TYPE, "large");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));

    EXPECT_FALSE(UmqSetting::UMQ_TINY_POOL_ENABLE);
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE, TINY_BLOCK_SIZE_4K);
    EXPECT_EQ(UmqSetting::UMQ_TP_TYPE, SINGLE);
    EXPECT_TRUE(UmqSetting::UMQ_FLOW_CONTROL_ENABLE);
    EXPECT_FALSE(UmqSetting::UMQ_RANDOM_ROUTE);
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME, "rr");
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY, ROUND_ROBIN);
    EXPECT_EQ(UmqSetting::UMQ_UB_TRANS_MODE, RM_CTP);
    EXPECT_EQ(UmqSetting::UMQ_UB_TP_MODE, UMQ_TM_RM);
    EXPECT_EQ(UmqSetting::UMQ_UB_TP_TYPE, UMQ_TP_TYPE_CTP);
    EXPECT_EQ(UmqSetting::IO_BLOCK_TYPE, BLOCK_SIZE_64K);
}

// ==================== LoadEnv: AddRules 区间边界(边界值 + 相邻值) ====================

TEST_F(UmqSettingTest, LoadEnv_InitialCredit_MinBoundary_Applied)
{
    SetEnv(ENV_INITIAL_CREDIT, "1");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, static_cast<uint16_t>(1));
}

TEST_F(UmqSettingTest, LoadEnv_InitialCredit_MaxBoundary_Applied)
{
    SetEnv(ENV_INITIAL_CREDIT, "1024");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, static_cast<uint16_t>(1024));
}

TEST_F(UmqSettingTest, LoadEnv_InitialCredit_OutOfRange_KeepsDefault)
{
    SetEnv(ENV_INITIAL_CREDIT, "0");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, DEFAULT_FC_DEFAULT_CREDIT);

    SetEnv(ENV_INITIAL_CREDIT, "1025");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, DEFAULT_FC_DEFAULT_CREDIT);
}

TEST_F(UmqSettingTest, LoadEnv_InvalidIntegerFormat_KeepsDefault)
{
    SetEnv(ENV_INITIAL_CREDIT, "abc");
    SetEnv(ENV_TINY_POOL_BLOCK_COUNT, "2min");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_FC_DEFAULT_CREDIT, DEFAULT_FC_DEFAULT_CREDIT);
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT, DEFAULT_TINY_POOL_BLOCK_COUNT);
}

TEST_F(UmqSettingTest, LoadEnv_PoolMaxSize_MinMaxBoundary_Applied)
{
    SetEnv(ENV_MEM_POOL_MAX_SIZE, "1");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, 1u);

    SetEnv(ENV_MEM_POOL_MAX_SIZE, "6144");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, 6144u);
}

TEST_F(UmqSettingTest, LoadEnv_PoolMaxSize_OutOfRange_KeepsDefault)
{
    SetEnv(ENV_MEM_POOL_MAX_SIZE, "0");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, DEFAULT_MEM_POOL_MAX_SIZE_MB);

    SetEnv(ENV_MEM_POOL_MAX_SIZE, "6145");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB, DEFAULT_MEM_POOL_MAX_SIZE_MB);
}

TEST_F(UmqSettingTest, LoadEnv_LinkPriority_NegativeBoundary_Applied)
{
    SetEnv(ENV_LINK_PRIORITY, "-1");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, static_cast<int8_t>(-1));
}

TEST_F(UmqSettingTest, LoadEnv_LinkPriority_MaxBoundary_Applied)
{
    SetEnv(ENV_LINK_PRIORITY, "15");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, static_cast<int8_t>(15));
}

TEST_F(UmqSettingTest, LoadEnv_LinkPriority_OutOfRange_KeepsDefault)
{
    SetEnv(ENV_LINK_PRIORITY, "-2");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, UBSOCKET_LINK_PRIORITY_DEFAULT);

    SetEnv(ENV_LINK_PRIORITY, "16");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_LINK_PRIORITY, UBSOCKET_LINK_PRIORITY_DEFAULT);
}

TEST_F(UmqSettingTest, LoadEnv_TpPoolSize_MinMaxBoundary_Applied)
{
    SetEnv(ENV_TP_POOL_SIZE, "1");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, 1u);

    SetEnv(ENV_TP_POOL_SIZE, "1000");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, 1000u);
}

TEST_F(UmqSettingTest, LoadEnv_TpPoolSize_OutOfRange_KeepsDefault)
{
    SetEnv(ENV_TP_POOL_SIZE, "0");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, DEFAULT_TP_POOL_SIZE);

    SetEnv(ENV_TP_POOL_SIZE, "1001");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_POOL_SIZE, DEFAULT_TP_POOL_SIZE);
}

TEST_F(UmqSettingTest, LoadEnv_O3Timeout_RuleRegistered_KeepsDefault)
{
    /* O3 超时仅注册规则(min=2),LoadEnv 不读取 → 静态成员保持默认 */
    SetEnv(ENV_O3_TIMEOUT_MS, "2");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_O3_TIMEOUT_MS, DEFAULT_O3_TIMEOUT_MS);
}

TEST_F(UmqSettingTest, LoadEnv_SmallBufPoolDepth_Boundary_Applied)
{
    SetEnv(ENV_SMALL_BUF_POOL_DEPTH, "1");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, 1u);

    SetEnv(ENV_SMALL_BUF_POOL_DEPTH, "15360");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, 15360u);
}

TEST_F(UmqSettingTest, LoadEnv_SmallBufPoolDepth_OutOfRange_KeepsDefault)
{
    SetEnv(ENV_SMALL_BUF_POOL_DEPTH, "0");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, DEFAULT_SMALL_BUF_POOL_DEPTH);

    SetEnv(ENV_SMALL_BUF_POOL_DEPTH, "15361");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH, DEFAULT_SMALL_BUF_POOL_DEPTH);
}

TEST_F(UmqSettingTest, LoadEnv_GlobalPoolDepth_ZeroAllowed_Applied)
{
    SetEnv(ENV_SMALL_GLOBAL_POOL_DEPTH, "0");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH, 0u);

    SetEnv(ENV_SMALL_GLOBAL_POOL_DEPTH, "15360");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH, 15360u);

    SetEnv(ENV_SMALL_GLOBAL_POOL_DEPTH, "15361");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH, 15360u);
}

// ==================== LoadEnv: 布尔与字符串枚举 ====================

TEST_F(UmqSettingTest, LoadEnv_TinyPoolEnable_TrueAndFalse_Applied)
{
    SetEnv(ENV_TINY_POOL_ENABLE, "false");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_FALSE(UmqSetting::UMQ_TINY_POOL_ENABLE);

    SetEnv(ENV_TINY_POOL_ENABLE, "TRUE");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_TRUE(UmqSetting::UMQ_TINY_POOL_ENABLE);
}

TEST_F(UmqSettingTest, LoadEnv_TinyPoolBlockSize_AllForms_Applied)
{
    struct {
        const char *str;
        umq_tiny_buf_block_size_t expected;
    } kCases[] = {
        {"512", TINY_BLOCK_SIZE_512}, {"1024", TINY_BLOCK_SIZE_1K}, {"1K", TINY_BLOCK_SIZE_1K},
        {"2048", TINY_BLOCK_SIZE_2K}, {"2K", TINY_BLOCK_SIZE_2K},   {"4096", TINY_BLOCK_SIZE_4K},
        {"4K", TINY_BLOCK_SIZE_4K},   {"8192", TINY_BLOCK_SIZE_8K}, {"8K", TINY_BLOCK_SIZE_8K},
    };
    for (const auto &tc : kCases) {
        SetEnv(ENV_TINY_POOL_BLOCK_SIZE, tc.str);
        ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK)) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE, tc.expected) << "str=" << tc.str;
    }
}

TEST_F(UmqSettingTest, LoadEnv_TinyPoolBlockSize_Invalid_KeepsDefault)
{
    SetEnv(ENV_TINY_POOL_BLOCK_SIZE, "16K");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE, TINY_BLOCK_SIZE_1K);
}

TEST_F(UmqSettingTest, LoadEnv_JettyType_PoolAndSingle_Applied)
{
    SetEnv(ENV_TP_TYPE, "pool");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_TYPE, POOL);

    SetEnv(ENV_TP_TYPE, "single");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_TP_TYPE, SINGLE);
}

TEST_F(UmqSettingTest, LoadEnv_SchedulePolicy_AllThree_Applied)
{
    struct {
        const char *str;
        dev_schedule_policy expected;
    } kCases[] = {
        {"rr", ROUND_ROBIN},
        {"affinity", CPU_AFFINITY},
        {"affinity_priority", CPU_AFFINITY_PRIORITY},
    };
    for (const auto &tc : kCases) {
        SetEnv(ENV_SCHEDULE_POLICY, tc.str);
        ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK)) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY, tc.expected) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME, tc.str) << "str=" << tc.str;
    }
}

TEST_F(UmqSettingTest, LoadEnv_SchedulePolicy_Invalid_KeepsDefault)
{
    SetEnv(ENV_SCHEDULE_POLICY, "unknown");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY, CPU_AFFINITY_PRIORITY);
    EXPECT_EQ(UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME, "affinity_priority");
}

TEST_F(UmqSettingTest, LoadEnv_TransMode_AllFour_Applied)
{
    struct {
        const char *str;
        ub_trans_mode transMode;
        umq_tp_mode_t tpMode;
        umq_tp_type_t tpType;
    } kCases[] = {
        {"RM_TP", RM_TP, UMQ_TM_RM, UMQ_TP_TYPE_RTP},
        {"RM_CTP", RM_CTP, UMQ_TM_RM, UMQ_TP_TYPE_CTP},
        {"RC_TP", RC_TP, UMQ_TM_RC, UMQ_TP_TYPE_RTP},
        {"RC_CTP", RC_CTP, UMQ_TM_RC, UMQ_TP_TYPE_CTP},
    };
    for (const auto &tc : kCases) {
        SetEnv(ENV_UB_TRANS_MODE, tc.str);
        ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK)) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_UB_TRANS_MODE, tc.transMode) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_UB_TP_MODE, tc.tpMode) << "str=" << tc.str;
        EXPECT_EQ(UmqSetting::UMQ_UB_TP_TYPE, tc.tpType) << "str=" << tc.str;
    }
}

TEST_F(UmqSettingTest, LoadEnv_TransMode_Invalid_KeepsDefault)
{
    SetEnv(ENV_UB_TRANS_MODE, "XYZ");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_UB_TRANS_MODE, RM_TP);
    EXPECT_EQ(UmqSetting::UMQ_UB_TP_MODE, UMQ_TM_RM);
    EXPECT_EQ(UmqSetting::UMQ_UB_TP_TYPE, UMQ_TP_TYPE_RTP);
}

TEST_F(UmqSettingTest, LoadEnv_BlockType_Large_Applied)
{
    SetEnv(ENV_BLOCK_TYPE, "large");
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::IO_BLOCK_TYPE, BLOCK_SIZE_64K);
}

TEST_F(UmqSettingTest, LoadEnv_BlockType_Unset_UsesDefaultBlockType)
{
    ASSERT_EQ(UmqSetting::LoadEnv(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::IO_BLOCK_TYPE, BLOCK_SIZE_4K);
}

// ==================== VerifySetting: MIDDLE_POOL_BLOCK_SIZE 8K..1M/4K 对齐/2 的幂 ====================

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_BelowMin_Invalid)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_4K);
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_MinBoundary_Ok)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_8K);
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_OK));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_AboveMax_Invalid)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_1M) * 2;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_MaxBoundary_Ok)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_1M);
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_OK));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_NotAligned_Invalid)
{
    /* 10K = 8K + 4K/2:在 [8K,1M] 内但非 4K 对齐 → 首个约束分支(仅 align 子条件;min 子条件由 BelowMin 用例隔离) */
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_8K) + static_cast<uint32_t>(SIZE_4K) / 2;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_NotPowerOfTwo_Invalid)
{
    /* 12288 = 12K:4K 对齐但非 2 的幂 → 幂次约束分支 */
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_4K) * 3;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
}

TEST_F(UmqSettingTest, VerifySetting_MiddleBlockSize_Valid_UpdatesExplicitSizes)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_64K);
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(UmqSetting::UMQ_EXPLICIT_BLOCK_SIZES[0], static_cast<uint32_t>(SIZE_4K));
    EXPECT_EQ(UmqSetting::UMQ_EXPLICIT_BLOCK_SIZES[1], static_cast<uint32_t>(SIZE_64K));
    EXPECT_EQ(UmqSetting::UMQ_SIZE_CLASS_COUNT, 2u);
}

TEST_F(UmqSettingTest, VerifySetting_MemPoolMaxSize_MinMaxBoundary_Ok)
{
    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 1;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_OK));

    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 6144;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_OK));
}

TEST_F(UmqSettingTest, VerifySetting_MemPoolMaxSize_OutOfRange_Invalid)
{
    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 0;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));

    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 6145;
    EXPECT_EQ(UmqSetting::VerifySetting(), static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
}

// ==================== Init(private, -fno-access-control) ====================

TEST_F(UmqSettingTest, Init_ValidDefaults_ReturnsOk)
{
    EXPECT_EQ(UmqSetting::Init(), static_cast<ock::ubs::Result>(UBS_OK));
}

TEST_F(UmqSettingTest, Init_InvalidMiddleBlockSize_ReturnsInvalidAndSetsErrno)
{
    UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(SIZE_4K);
    int ret = UmqSetting::Init();
    EXPECT_EQ(ret, static_cast<ock::ubs::Result>(UBS_INVALID_PARAM));
    EXPECT_EQ(errno, EINVAL);
}

// ==================== 字符串解析函数(private) ====================

TEST_F(UmqSettingTest, BlockTypeFromStr_Large_Returns64K)
{
    EXPECT_EQ(UmqSetting::BlockTypeFromStr("large"), BLOCK_SIZE_64K);
}

TEST_F(UmqSettingTest, BlockTypeFromStr_Default_Returns4K)
{
    EXPECT_EQ(UmqSetting::BlockTypeFromStr("default"), BLOCK_SIZE_4K);
    EXPECT_EQ(UmqSetting::BlockTypeFromStr("small"), BLOCK_SIZE_4K);
    EXPECT_EQ(UmqSetting::BlockTypeFromStr(""), BLOCK_SIZE_4K);
}

TEST_F(UmqSettingTest, TinyBlockSizeFromStr_AllValid_ReturnsCorrect)
{
    struct {
        const char *str;
        umq_tiny_buf_block_size_t expected;
    } kCases[] = {
        {"512", TINY_BLOCK_SIZE_512}, {"1024", TINY_BLOCK_SIZE_1K}, {"1K", TINY_BLOCK_SIZE_1K},
        {"2048", TINY_BLOCK_SIZE_2K}, {"2K", TINY_BLOCK_SIZE_2K},   {"4096", TINY_BLOCK_SIZE_4K},
        {"4K", TINY_BLOCK_SIZE_4K},   {"8192", TINY_BLOCK_SIZE_8K}, {"8K", TINY_BLOCK_SIZE_8K},
    };
    for (const auto &tc : kCases) {
        EXPECT_EQ(UmqSetting::TinyBlockSizeFromStr(tc.str), tc.expected) << "str=" << tc.str;
    }
}

TEST_F(UmqSettingTest, TinyBlockSizeFromStr_Invalid_FallsBackTo1K)
{
    EXPECT_EQ(UmqSetting::TinyBlockSizeFromStr("16K"), TINY_BLOCK_SIZE_1K);
    EXPECT_EQ(UmqSetting::TinyBlockSizeFromStr(""), TINY_BLOCK_SIZE_1K);
}

TEST_F(UmqSettingTest, SchedulePolicyFromStr_AllValid_ReturnsCorrect)
{
    EXPECT_EQ(UmqSetting::SchedulePolicyFromStr("rr"), ROUND_ROBIN);
    EXPECT_EQ(UmqSetting::SchedulePolicyFromStr("affinity"), CPU_AFFINITY);
    EXPECT_EQ(UmqSetting::SchedulePolicyFromStr("affinity_priority"), CPU_AFFINITY_PRIORITY);
}

TEST_F(UmqSettingTest, SchedulePolicyFromStr_Invalid_FallsBackToAffinityPriority)
{
    EXPECT_EQ(UmqSetting::SchedulePolicyFromStr("unknown"), CPU_AFFINITY_PRIORITY);
    EXPECT_EQ(UmqSetting::SchedulePolicyFromStr(""), CPU_AFFINITY_PRIORITY);
}

TEST_F(UmqSettingTest, BlockSizeToBytes_AllSizes_ReturnsBytes)
{
    struct {
        umq_buf_block_size_t blockType;
        uint64_t expected;
    } kCases[] = {
        {BLOCK_SIZE_4K, SIZE_4K},     {BLOCK_SIZE_8K, SIZE_8K},     {BLOCK_SIZE_16K, SIZE_16K},
        {BLOCK_SIZE_32K, SIZE_32K},   {BLOCK_SIZE_64K, SIZE_64K},   {BLOCK_SIZE_128K, SIZE_128K},
        {BLOCK_SIZE_256K, SIZE_256K}, {BLOCK_SIZE_512K, SIZE_512K}, {BLOCK_SIZE_1M, SIZE_1M},
    };
    for (const auto &tc : kCases) {
        EXPECT_EQ(UmqSetting::BlockSizeToBytes(tc.blockType), tc.expected) << "blockType=" << tc.blockType;
    }
}

TEST_F(UmqSettingTest, BlockSizeToBytes_Invalid_FallsBack4K)
{
    EXPECT_EQ(UmqSetting::BlockSizeToBytes(BLOCK_SIZE_MAX), SIZE_4K);
}

// ==================== MergeBufLists ====================

TEST_F(UmqSettingTest, MergeBufLists_EmptyLists_ReturnsNull)
{
    umq_buf_t *lists[2] = {nullptr, nullptr};
    uint32_t counts[2] = {0, 0};
    EXPECT_EQ(UmqSetting::MergeBufLists(lists, counts, 2), nullptr);
}

TEST_F(UmqSettingTest, MergeBufLists_SingleChain_ReturnsHead)
{
    umq_buf_t b1 = {};
    umq_buf_t b2 = {};
    b1.qbuf_next = &b2;
    b2.qbuf_next = nullptr;
    umq_buf_t *lists[2] = {&b1, nullptr};
    uint32_t counts[2] = {2, 0};
    EXPECT_EQ(UmqSetting::MergeBufLists(lists, counts, 2), &b1);
}

TEST_F(UmqSettingTest, MergeBufLists_MultipleChains_MergedInOrder)
{
    umq_buf_t b1 = {};
    umq_buf_t b2 = {};
    umq_buf_t b3 = {};
    b1.qbuf_next = &b2; // SC[0] 链: b1 -> b2
    b2.qbuf_next = nullptr;
    b3.qbuf_next = nullptr; // SC[1] 链: b3
    umq_buf_t *lists[2] = {&b1, &b3};
    uint32_t counts[2] = {2, 1};
    umq_buf_t *head = UmqSetting::MergeBufLists(lists, counts, 2);
    EXPECT_EQ(head, &b1);
    EXPECT_EQ(b2.qbuf_next, &b3);
    EXPECT_EQ(b3.qbuf_next, nullptr);
}

TEST_F(UmqSettingTest, MergeBufLists_NullHeadList_SkipsToNext)
{
    umq_buf_t b3 = {};
    b3.qbuf_next = nullptr;
    umq_buf_t *lists[2] = {nullptr, &b3};
    uint32_t counts[2] = {0, 1};
    EXPECT_EQ(UmqSetting::MergeBufLists(lists, counts, 2), &b3);
}

// ==================== 越界回退与分类边界 ====================

TEST_F(UmqSettingTest, GetIOBufSizeByClass_OutOfRange_FallsBackToLastClass)
{
    uint32_t lastClassSize = UmqSetting::GetIOBufSizeByClass(UmqSetting::GetSizeClassCount() - 1);
    EXPECT_EQ(UmqSetting::GetIOBufSizeByClass(UmqSetting::GetSizeClassCount()), lastClassSize);
    EXPECT_EQ(UmqSetting::GetIOBufSizeByClass(UINT32_MAX), lastClassSize);
}

TEST_F(UmqSettingTest, CountRXBufByClass_BufAtFirstClassBoundary_GoesToFirstClass)
{
    /* total = 4096:恰好命中最小 class(SC[0])边界 → SC[0] */
    umq_buf_t buf = {};
    buf.data_size = static_cast<uint32_t>(SIZE_4K);
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 1u);
    EXPECT_EQ(counts[1], 0u);
}

TEST_F(UmqSettingTest, CountRXBufByClass_OverBoundaryBuf_GoesToLastClass)
{
    /* total = 4097:恰好越过 SC[0] 边界 4096 → SC[1] */
    umq_buf_t buf = {};
    buf.data_size = static_cast<uint32_t>(SIZE_4K) + 1;
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 0u);
    EXPECT_EQ(counts[1], 1u);
}

TEST_F(UmqSettingTest, CountRXBufByClass_BufAtLastClassBoundary_GoesToLastClass)
{
    /* total = 65536:恰好命中最大 class(SC[1])边界 */
    umq_buf_t buf = {};
    buf.data_size = static_cast<uint32_t>(SIZE_64K);
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 0u);
    EXPECT_EQ(counts[1], 1u);
}

TEST_F(UmqSettingTest, CountRXBufByClass_BufBeyondAllClasses_GoesToLastClass)
{
    /* total = 65537:超出所有 class → 超界回退归入最大 class(SC[1]) */
    umq_buf_t buf = {};
    buf.data_size = static_cast<uint32_t>(SIZE_64K) + 1;
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 0u);
    EXPECT_EQ(counts[1], 1u);
}

TEST_F(UmqSettingTest, CountRXBufByClass_CountSizeTruncation_DropsExcess)
{
    /* count_size=1:SC[1] buf 超出计数数组范围,不计入 */
    umq_buf_t buf = {};
    buf.data_size = static_cast<uint32_t>(SIZE_64K);
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};
    uint32_t counts[1] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, 1);
    EXPECT_EQ(counts[0], 0u);
}
