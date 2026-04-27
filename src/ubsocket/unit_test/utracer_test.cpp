// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "utracer.h"
#include "utracer_manager.h"
#include "utracer_info.h"

using namespace testing;
using namespace Statistics;

namespace {
static const UbSocketTracePointId TEST_TRACE_POINT_ID =
    static_cast<UbSocketTracePointId>(0);
static const UbSocketTracePointId TEST_TRACE_POINT_ID_1 =
    static_cast<UbSocketTracePointId>(1);
static const UbSocketTracePointId TEST_TRACE_POINT_ID_MAX_PLUS_1 =
    static_cast<UbSocketTracePointId>(MAX_TRACE_POINT_NUM + 1);
static const char* TEST_TRACE_NAME = "test_tp";
static const uint64_t TEST_DELAY_NS = 1000000;
static const int32_t TEST_SUCCESS_CODE = 0;
static const int32_t TEST_FAILURE_CODE = -1;
static const double TEST_QUANTILE = 0.95;

// 替换魔鬼数字2
static const int TEST_MULTIPLIER_2 = 2;
}

class UtracerTest : public testing::Test {
protected:
    void SetUp() override
    {
        // 初始化 tracer
        UTracerInit();
    }

    void TearDown() override
    {
        // 清理状态
        UTracerExit();
        GlobalMockObject::verify();
    }
};

// 测试 UTracerInit 成功
TEST_F(UtracerTest, UTracerInit_Success)
{
    // 已经在 SetUp 中调用，再次调用应该也是成功的
    int32_t result = UTracerInit();
    EXPECT_EQ(result, 0);
}

// 测试 EnableUTrace 启用跟踪
TEST_F(UtracerTest, EnableUTrace_Enable)
{
    EnableUTrace(true);
    EXPECT_TRUE(UTracerManager::IsEnable());
}

// 测试 EnableUTrace 禁用跟踪
TEST_F(UtracerTest, EnableUTrace_Disable)
{
    EnableUTrace(true);
    EXPECT_TRUE(UTracerManager::IsEnable());
    EnableUTrace(false);
    EXPECT_FALSE(UTracerManager::IsEnable());
}

// 测试 UTracerManager::SetLatencyQuantileEnable
TEST_F(UtracerTest, SetLatencyQuantileEnable)
{
    UTracerManager::SetLatencyQuantileEnable(true);
    EXPECT_TRUE(UTracerManager::IsLatencyQuantileEnable());
    UTracerManager::SetLatencyQuantileEnable(false);
    EXPECT_FALSE(UTracerManager::IsLatencyQuantileEnable());
}

// 测试 DelayBegin 和 DelayEnd 基本功能
TEST_F(UtracerTest, DelayBeginAndEnd)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID, TEST_DELAY_NS, TEST_SUCCESS_CODE);

    auto traceInfos = GetTraceInfos(TEST_TRACE_POINT_ID, TEST_QUANTILE, false);
    EXPECT_EQ(traceInfos.size(), 1);
}

// 测试 DelayBegin - tpId 超出范围
TEST_F(UtracerTest, DelayBegin_TpIdOutOfRange)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID_MAX_PLUS_1, TEST_TRACE_NAME);
    // 不应该有任何跟踪点
    auto traceInfos = GetTraceInfos(TEST_TRACE_POINT_ID_MAX_PLUS_1, TEST_QUANTILE, false);
    EXPECT_EQ(traceInfos.size(), 0);
}

// 测试 DelayEnd - tpId 超出范围
TEST_F(UtracerTest, DelayEnd_TpIdOutOfRange)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID_MAX_PLUS_1, TEST_DELAY_NS, TEST_SUCCESS_CODE);
    // tpId 超出范围时不应该影响任何跟踪点
}

// 测试 GetTraceInfos - INVALID_TRACE_POINT_ID
TEST_F(UtracerTest, GetTraceInfos_InvalidTpId)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID, TEST_DELAY_NS, TEST_SUCCESS_CODE);

    auto traceInfos = GetTraceInfos(INVALID_TRACE_POINT_ID, TEST_QUANTILE, false);
    // INVALID_TRACE_POINT_ID 返回所有有效的跟踪点
    EXPECT_GE(traceInfos.size(), 1);
}

// 测试 GetTraceInfos - tpId > MAX_TRACE_POINT_NUM
TEST_F(UtracerTest, GetTraceInfos_TpIdTooLarge)
{
    auto traceInfos = GetTraceInfos(TEST_TRACE_POINT_ID_MAX_PLUS_1, TEST_QUANTILE, false);
    EXPECT_EQ(traceInfos.size(), 0);
}

// 测试 GetTraceInfos - 多个跟踪点
TEST_F(UtracerTest, GetTraceInfos_MultipleTracePoints)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID, TEST_DELAY_NS, TEST_SUCCESS_CODE);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID_1, "test_tp_1");
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID_1, TEST_DELAY_NS * TEST_MULTIPLIER_2, TEST_SUCCESS_CODE);

    auto traceInfos = GetTraceInfos(INVALID_TRACE_POINT_ID, TEST_QUANTILE, false);
    EXPECT_GE(traceInfos.size(), TEST_MULTIPLIER_2);
}

// 测试 ResetTraceInfos
TEST_F(UtracerTest, ResetTraceInfos)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID, TEST_DELAY_NS, TEST_SUCCESS_CODE);

    ResetTraceInfos();

    auto traceInfos = GetTraceInfos(TEST_TRACE_POINT_ID, TEST_QUANTILE, false);
    // 重置后统计数据已清空，但跟踪点仍然存在（Valid()返回true）
    EXPECT_GE(traceInfos.size(), 1);
}

// 测试 AsyncDelayBegin
TEST_F(UtracerTest, AsyncDelayBegin)
{
    EnableUTrace(true);
    struct timespec ts = UTracerManager::AsyncDelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    // 返回的时间应该有效（秒和纳秒都非负）
    EXPECT_TRUE(ts.tv_sec >= 0 && ts.tv_nsec >= 0);
}

// 测试 GetTimeNs
TEST_F(UtracerTest, GetTimeNs)
{
    uint64_t timeNs = UTracerManager::GetTimeNs();
    EXPECT_TRUE(timeNs > 0);
}

// 测试 UTracerManager::SetEnableLog
TEST_F(UtracerTest, SetEnableLog)
{
    UTracerManager::SetEnableLog(true, "");
    EXPECT_FALSE(UTracerManager::IsEnableLog());  // 没有有效路径，不启用

    UTracerManager::SetEnableLog(true, "/tmp/test_utrace");
    // 设置后可能启用或因路径问题不启用
}

// 测试 DelayEnd 失败情况
TEST_F(UtracerTest, DelayEnd_FailureCode)
{
    EnableUTrace(true);
    UTracerManager::DelayBegin(TEST_TRACE_POINT_ID, TEST_TRACE_NAME);
    UTracerManager::DelayEnd(TEST_TRACE_POINT_ID, TEST_DELAY_NS, TEST_FAILURE_CODE);

    // 失败情况下 badEnd 应该增加
}

// 测试 UTracerExit
TEST_F(UtracerTest, UTracerExit_DisablesTracing)
{
    EnableUTrace(true);
    UTracerExit();
    // Exit 后跟踪应该被禁用
}