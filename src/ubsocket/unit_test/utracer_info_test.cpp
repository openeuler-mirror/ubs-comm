// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include "utracer_info.h"
#include "utracer_utils.h"

using namespace testing;
using namespace Statistics;

namespace {
static const char* TEST_TRACE_NAME = "test_trace_point";
static const char* TEST_TRAN_NAME = "test_tran";
static const uint64_t TEST_DELAY_VALUE = 1000;
static const uint64_t TEST_MIN_DELAY = 50;
static const uint64_t TEST_MAX_DELAY = 5000;
static const int32_t TEST_SUCCESS_CODE = 0;
static const int32_t TEST_FAILURE_CODE = -1;
static const double TEST_QUANTILE_VALUE = 0.95;
static const double TEST_QUANTILE_INVALID = -1.0;

// 新增常量，替换魔鬼数字2
static const int TEST_SECOND_CALL_COUNT = 2;
}

class UTracerInfoTest : public testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

// 测试 UTracerInfo DelayBegin 方法
TEST_F(UTracerInfoTest, DelayBegin_SetsNameAndIncrementsBegin)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    EXPECT_EQ(info.GetName(), TEST_TRACE_NAME);
    EXPECT_EQ(info.GetBegin(), 1);
    EXPECT_TRUE(info.Valid());
}

// 测试 UTracerInfo DelayBegin 方法 - 多次调用
TEST_F(UTracerInfoTest, DelayBegin_MultipleCalls)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayBegin(TEST_TRACE_NAME);  // 第二次调用不应该改变名称
    EXPECT_EQ(info.GetName(), TEST_TRACE_NAME);
    EXPECT_EQ(info.GetBegin(), TEST_SECOND_CALL_COUNT);
}

// 测试 UTracerInfo DelayEnd 方法 - 成功情况
TEST_F(UTracerInfoTest, DelayEnd_SuccessUpdatesStats)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    EXPECT_EQ(info.GetGoodEnd(), 1);
    EXPECT_EQ(info.GetBadEnd(), 0);
    EXPECT_EQ(info.GetMin(), TEST_DELAY_VALUE);
    EXPECT_EQ(info.GetMax(), TEST_DELAY_VALUE);
    EXPECT_EQ(info.GetTotal(), TEST_DELAY_VALUE);
}

// 测试 UTracerInfo DelayEnd 方法 - 失败情况
TEST_F(UTracerInfoTest, DelayEnd_FailureIncreasesBadEnd)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_FAILURE_CODE, false);
    EXPECT_EQ(info.GetGoodEnd(), 0);
    EXPECT_EQ(info.GetBadEnd(), 1);
    // 失败时不应该更新 min/max/total
    EXPECT_EQ(info.GetMin(), UINT64_MAX);
    EXPECT_EQ(info.GetMax(), 0);
    EXPECT_EQ(info.GetTotal(), 0);
}

// 测试 UTracerInfo DelayEnd 方法 - 更新最小值
TEST_F(UTracerInfoTest, DelayEnd_UpdatesMin)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_MAX_DELAY, TEST_SUCCESS_CODE, false);
    info.DelayEnd(TEST_MIN_DELAY, TEST_SUCCESS_CODE, false);
    EXPECT_EQ(info.GetMin(), TEST_MIN_DELAY);
    EXPECT_EQ(info.GetMax(), TEST_MAX_DELAY);
}

// 测试 UTracerInfo DelayEnd 方法 - 更新最大值
TEST_F(UTracerInfoTest, DelayEnd_UpdatesMax)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_MIN_DELAY, TEST_SUCCESS_CODE, false);
    info.DelayEnd(TEST_MAX_DELAY, TEST_SUCCESS_CODE, false);
    EXPECT_EQ(info.GetMin(), TEST_MIN_DELAY);
    EXPECT_EQ(info.GetMax(), TEST_MAX_DELAY);
}

// 测试 UTracerInfo DelayEnd 方法 - 启用tdigest
TEST_F(UTracerInfoTest, DelayEnd_WithTdigestEnabled)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, true);
    EXPECT_EQ(info.GetGoodEnd(), 1);
    Tdigest tdigest = info.GetTdigest();
    // tdigest 应该有数据
}

// 测试 UTracerInfo Reset 方法
TEST_F(UTracerInfoTest, Reset_ClearsAllStats)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    info.Reset();
    EXPECT_EQ(info.GetBegin(), 0);
    EXPECT_EQ(info.GetGoodEnd(), 0);
    EXPECT_EQ(info.GetBadEnd(), 0);
    EXPECT_EQ(info.GetMin(), UINT64_MAX);
    EXPECT_EQ(info.GetMax(), 0);
    EXPECT_EQ(info.GetTotal(), 0);
}

// 测试 UTracerInfo ToString 方法
TEST_F(UTracerInfoTest, ToString_ReturnsFormattedString)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    std::string result = info.ToString();
    EXPECT_TRUE(result.length() > 0);
    EXPECT_TRUE(result.find(TEST_TRACE_NAME) != std::string::npos);
}

// 测试 UTracerInfo ToPeriodString 方法
TEST_F(UTracerInfoTest, ToPeriodString_ReturnsFormattedString)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    std::string result = info.ToPeriodString();
    EXPECT_TRUE(result.length() > 0);
}

// 测试 UTracerInfo RecordLatest 方法
TEST_F(UTracerInfoTest, RecordLatest_RecordsCurrentValues)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    info.RecordLatest();
    // 再次调用 DelayEnd 后，周期统计应该反映变化
    info.DelayEnd(TEST_DELAY_VALUE * TEST_SECOND_CALL_COUNT, TEST_SUCCESS_CODE, false);
    std::string result = info.ToPeriodString();
    EXPECT_TRUE(result.length() > 0);
}

// 测试 UTracerInfo Valid 方法 - 未初始化时返回false
TEST_F(UTracerInfoTest, Valid_ReturnsFalseBeforeInit)
{
    UTracerInfo info;
    EXPECT_FALSE(info.Valid());
}

// 测试 UTracerInfo Valid 方法 - 初始化后返回true
TEST_F(UTracerInfoTest, Valid_ReturnsTrueAfterDelayBegin)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    EXPECT_TRUE(info.Valid());
}

// 测试 UTracerInfo ValidPeriod 方法
TEST_F(UTracerInfoTest, ValidPeriod_ChecksPeriodProgress)
{
    UTracerInfo info;
    info.DelayBegin(TEST_TRACE_NAME);
    info.RecordLatest();
    // 开始后没有新操作，ValidPeriod 应该返回 false
    EXPECT_FALSE(info.ValidPeriod());
    info.DelayBegin(TEST_TRACE_NAME);
    // 有新操作后，ValidPeriod 应该返回 true
    EXPECT_TRUE(info.ValidPeriod());
}

// 测试 UTracerInfo SetName 和 GetName 方法
TEST_F(UTracerInfoTest, SetNameAndGet)
{
    UTracerInfo info;
    info.SetName(TEST_TRACE_NAME);
    EXPECT_EQ(info.GetName(), TEST_TRACE_NAME);
}

// 测试 TranTraceInfo 构造函数 - 从UTracerInfo转换
TEST_F(UTracerInfoTest, TranTraceInfo_FromUTracerInfo)
{
    UTracerInfo uinfo;
    uinfo.DelayBegin(TEST_TRACE_NAME);
    uinfo.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    TranTraceInfo tinfo(uinfo, TEST_QUANTILE_VALUE, true);
    std::string result = tinfo.ToString();
    EXPECT_TRUE(result.length() > 0);
}

// 测试 TranTraceInfo 构造函数 - 无效quantile
TEST_F(UTracerInfoTest, TranTraceInfo_InvalidQuantile)
{
    UTracerInfo uinfo;
    uinfo.DelayBegin(TEST_TRACE_NAME);
    uinfo.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    TranTraceInfo tinfo(uinfo, TEST_QUANTILE_INVALID, true);
    std::string result = tinfo.ToString();
    EXPECT_TRUE(result.length() > 0);
}

// 测试 TranTraceInfo 构造函数 - 禁用tp
TEST_F(UTracerInfoTest, TranTraceInfo_TpDisabled)
{
    UTracerInfo uinfo;
    uinfo.DelayBegin(TEST_TRACE_NAME);
    uinfo.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    TranTraceInfo tinfo(uinfo, TEST_QUANTILE_VALUE, false);
    std::string result = tinfo.ToString();
    EXPECT_TRUE(result.length() > 0);
}

// 测试 TranTraceInfo HeaderString 方法
TEST_F(UTracerInfoTest, TranTraceInfo_HeaderString)
{
    std::string header = TranTraceInfo::HeaderString();
    EXPECT_TRUE(header.length() > 0);
    EXPECT_TRUE(header.find("TP_NAME") != std::string::npos);
}

// 测试 TranTraceInfo ToString 方法 - 不同时间单位
TEST_F(UTracerInfoTest, TranTraceInfo_ToString_DifferentUnits)
{
    UTracerInfo uinfo;
    uinfo.DelayBegin(TEST_TRACE_NAME);
    uinfo.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);
    TranTraceInfo tinfo(uinfo, 0, false);
    // 测试默认单位（微秒）
    std::string resultUs = tinfo.ToString(TranTraceInfo::MICRO_SECOND);
    EXPECT_TRUE(resultUs.length() > 0);
    // 测试纳秒单位
    std::string resultNs = tinfo.ToString(TranTraceInfo::NANO_SECOND);
    EXPECT_TRUE(resultNs.length() > 0);
    // 测试毫秒单位
    std::string resultMs = tinfo.ToString(TranTraceInfo::MILLI_SECOND);
    EXPECT_TRUE(resultMs.length() > 0);
}

// 测试 TranTraceInfo operator+= 方法
TEST_F(UTracerInfoTest, TranTraceInfo_OperatorPlus)
{
    UTracerInfo uinfo1;
    uinfo1.DelayBegin(TEST_TRACE_NAME);
    uinfo1.DelayEnd(TEST_DELAY_VALUE, TEST_SUCCESS_CODE, false);

    UTracerInfo uinfo2;
    uinfo2.DelayBegin(TEST_TRACE_NAME);
    uinfo2.DelayEnd(TEST_DELAY_VALUE * TEST_SECOND_CALL_COUNT, TEST_SUCCESS_CODE, false);

    TranTraceInfo tinfo1(uinfo1, 0, false);
    TranTraceInfo tinfo2(uinfo2, 0, false);

    tinfo1 += tinfo2;
    std::string result = tinfo1.ToString();
    EXPECT_TRUE(result.length() > 0);
}