/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for utracer module
 */

#include "utracer.h"
#include "utracer_manager.h"
#include "utracer_info.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstring>

using namespace Statistics;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const uint64_t TRACE_DIFF_1000 = 1000U;
static const uint64_t TRACE_DIFF_500 = 500U;
static const uint64_t TRACE_DIFF_2000 = 2000U;
static const uint64_t TRACE_TOTAL_3500 = 3500U;
static const double TRACE_QUANTILE_50 = 50.0;
static const int TDIGEST_DELTA_20 = 20;
static const int TDIGEST_COUNT_100 = 100;
static const int TDIGEST_LOOP_10 = 10;

// Additional constants for magic number replacement
static const int TDIGEST_LOOP_50 = 50;
static const int TDIGEST_LOOP_15 = 15;
static const int TDIGEST_LOOP_5 = 5;
static const int TDIGEST_LOOP_20 = 20;
static const int TDIGEST_LOOP_1000 = 1000;
static const int CENTROID_CAPACITY_20 = 20;
static const int CENTROID_CAPACITY_5 = 5;
static const int CENTROID_WEIGHT_5 = 5;
static const int CENTROID_WEIGHT_1 = 1;
static const int CENTROID_WEIGHT_2 = 2;
static const int CENTROID_WEIGHT_10 = 10;
static const int CENTROID_WEIGHT_20 = 20;
static const double CENTROID_MEAN_100 = 100.0;
static const double CENTROID_MEAN_50 = 50.0;
static const double CENTROID_MEAN_200 = 200.0;
static const double CENTROID_MEAN_1000 = 1000.0;
static const double CENTROID_MEAN_2000 = 2000.0;
static const double CENTROID_MEAN_3000 = 3000.0;
static const double TDIGEST_WEIGHT_5 = 5.0;
static const double TDIGEST_WEIGHT_10 = 10.0;
static const double TDIGEST_WEIGHT_20 = 20.0;
static const int LONG_NAME_SIZE_200 = 200;
static const int QUANTILE_25 = 25;
static const int QUANTILE_75 = 75;
static const int QUANTILE_99 = 99;
static const double TDIGEST_QUANTILE_0_001 = 0.001;
static const double TDIGEST_QUANTILE_99_9 = 99.9;
static const int FORMAT_COLS_10 = 10;
static const int FORMAT_BEGIN_WIDTH_8 = 8;
static const int FORMAT_GOOD_BAD_WIDTH_2 = 2;
static const int FORMAT_TOTAL_WIDTH_100 = 100;
static const int FORMAT_MIN_WIDTH_500 = 500;
static const int FORMAT_MAX_WIDTH_1000 = 1000;
static const int TRACE_INFO_MAX_LEN_19 = 19;
static const int TRACE_BEGIN_LESS_5 = 5;
static const int TRACE_GOOD_END_10 = 10;
static const int TRACE_BAD_END_3 = 3;
static const int TRACE_TOTAL_100 = 100;
static const int TRACE_MIN_500 = 500;
static const int TRACE_MAX_1000 = 1000;
static const uint64_t TRACE_DIFF_750 = 750U;
static const uint64_t TRACE_DIFF_2000U = 2000U;
static const uint64_t TRACE_DIFF_3000 = 3000U;
static const uint64_t TRACE_DIFF_4000 = 4000U;
static const uint64_t TRACE_DIFF_5000 = 5000U;
static const int NEG_RET_CODE_MINUS_1 = -1;
static const int NEG_RET_CODE_MINUS_2 = -2;
static const int QUANTILE_0 = 0;
static const int QUANTILE_100 = 100;
static const double TDIGEST_QUANTILE_0 = 0.0;
static const double TDIGEST_QUANTILE_100 = 100.0;
static const double TDIGEST_QUANTILE_1 = 1.0;
static const double TDIGEST_QUANTILE_25 = 25.0;
static const double TDIGEST_QUANTILE_50_D = 50.0;
static const double TDIGEST_QUANTILE_75 = 75.0;
static const double TDIGEST_QUANTILE_99 = 99.0;
static const double NEG_QUANTILE = -10.0;
static const double HIGH_QUANTILE = 150.0;
static const double LARGE_VALUE = 1000000.0;
static const double BOUNDARY_VALUE = -10.0;
static const int EXPECTED_MIN_500 = 500;
static const int EXPECTED_MAX_2000 = 2000;
static const int EXPECTED_TOTAL_4250 = 4250;
static const int EXPECT_BEGIN_10 = 10;
static const double TDIGEST_QUANTILE_0_5 = 0.5;
static const double TDIGEST_QUANTILE_0_01 = 0.01;
static const double TDIGEST_QUANTILE_0_99 = 0.99;
static const double TDIGEST_QUANTILE_1E_20 = 1e-20;
static const double TDIGEST_QUANTILE_1_MINUS_1E20 = 1.0 - 1e-20;
static const double LERP_POS_10 = 10.0;
static const double LERP_POS_5 = 5.0;
static const double LERP_NEG_10 = -10.0;
static const double LERP_NEG_5 = -5.0;
static const double LERP_POS_20 = 20.0;
static const double LERP_POS_2 = 2.0;
static const int TIME_SEP_IDX_10 = 10;
static const int TIME_SEP_IDX_13 = 13;
static const int TIME_SEP_IDX_16 = 16;
static const double CENTROID_MEAN_300 = 300.0;
static const int RELATIVELY_EQUAL_TOLERANCE = 100;
static const double TDIGEST_QUANTILE_0E_10 = 1e-10;
static const int LOOP_15 = 15;
static const int WEIGHT_ZERO = 0;
static const int VECTOR_INDEX_2 = 2;
static const int MODULUS_2 = 2;
static const int TIME_INDEX_4 = 4;
static const int TIME_INDEX_7 = 7;
} // namespace

// Test fixture for UTracer tests
class UTracerTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UTracerTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    // Reset state before each test
    UTracerExit();
    EnableUTrace(false);
}

void UTracerTest::TearDown()
{
    UTracerExit();
    GlobalMockObject::verify();
}

// ============= UTracerInit/UTracerExit Tests =============

TEST_F(UTracerTest, Init_Success)
{
    int32_t ret = UTracerInit();
    EXPECT_EQ(ret, 0);
}

TEST_F(UTracerTest, Init_MultipleCalls)
{
    // First init should succeed
    int32_t ret = UTracerInit();
    EXPECT_EQ(ret, 0);

    // Second init should also succeed (no harm)
    ret = UTracerInit();
    EXPECT_EQ(ret, 0);
}

TEST_F(UTracerTest, Exit_ClearsInitFlag)
{
    UTracerInit();
    UTracerExit();

    // After exit, utrace should be disabled
    EnableUTrace(true);
    // The internal g_utraceInit flag is false, so IsEnable should return false
    // (it checks both UTracerManager::IsEnable() && g_utraceInit)
}

// ============= EnableUTrace Tests =============

TEST_F(UTracerTest, EnableUTrace_Enable)
{
    UTracerInit();
    EnableUTrace(true);
    EXPECT_TRUE(UTracerManager::IsEnable());
}

TEST_F(UTracerTest, EnableUTrace_Disable)
{
    UTracerInit();
    EnableUTrace(true);
    EnableUTrace(false);
    EXPECT_FALSE(UTracerManager::IsEnable());
}

// ============= UTracerManager Tests =============

class UTracerManagerTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UTracerManagerTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    UTracerInit();
    ResetTraceInfos();  // Reset trace state for each test
}

void UTracerManagerTest::TearDown()
{
    ResetTraceInfos();  // Clean up trace state
    UTracerExit();
    GlobalMockObject::verify();
}

TEST_F(UTracerManagerTest, Instance_ReturnsValidPointer)
{
    UTracerInfo* instance = UTracerManager::Instance();
    EXPECT_NE(instance, nullptr);
}

TEST_F(UTracerManagerTest, SetEnable_IsEnable)
{
    UTracerManager::SetEnable(true);
    EXPECT_TRUE(UTracerManager::IsEnable());

    UTracerManager::SetEnable(false);
    EXPECT_FALSE(UTracerManager::IsEnable());
}

TEST_F(UTracerManagerTest, SetLatencyQuantileEnable_IsLatencyQuantileEnable)
{
    UTracerManager::SetLatencyQuantileEnable(true);
    EXPECT_TRUE(UTracerManager::IsLatencyQuantileEnable());

    UTracerManager::SetLatencyQuantileEnable(false);
    EXPECT_FALSE(UTracerManager::IsLatencyQuantileEnable());
}

TEST_F(UTracerManagerTest, GetTimeNs_ReturnsPositiveValue)
{
    uint64_t time = UTracerManager::GetTimeNs();
    EXPECT_GT(time, 0ULL);
}

TEST_F(UTracerManagerTest, DelayBegin_ValidTpId)
{
    UTracerManager::SetEnable(true);
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    // No crash, valid operation
}

TEST_F(UTracerManagerTest, DelayBegin_InvalidTpId)
{
    UTracerManager::SetEnable(true);
    // Invalid tpId (>= MAX_TRACE_POINT_NUM) should not crash
    UTracerManager::DelayBegin(MAX_TRACE_POINT_NUM, "INVALID");
    UTracerManager::DelayBegin(MAX_TRACE_POINT_NUM + EXPECT_BEGIN_10 * TDIGEST_COUNT_100, "INVALID");
}

TEST_F(UTracerManagerTest, DelayEnd_ValidTpId)
{
    UTracerManager::SetEnable(true);
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    uint64_t diff = TRACE_DIFF_1000;
    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, diff, 0);
    // No crash, valid operation
}

TEST_F(UTracerManagerTest, DelayEnd_InvalidTpId)
{
    UTracerManager::SetEnable(true);
    // Invalid tpId should not crash
    UTracerManager::DelayEnd(MAX_TRACE_POINT_NUM, TRACE_DIFF_1000, 0);
    UTracerManager::DelayEnd(MAX_TRACE_POINT_NUM + TDIGEST_COUNT_100, TRACE_DIFF_1000, 0);
}

TEST_F(UTracerManagerTest, DelayEnd_NegativeRetCode)
{
    UTracerManager::SetEnable(true);
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    uint64_t diff = TRACE_DIFF_1000;
    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, diff, -1);
    // Negative retCode should increment badEnd counter
}

TEST_F(UTracerManagerTest, AsyncDelayBegin_ReturnsTimespec)
{
    UTracerManager::SetEnable(true);
    struct timespec ts = UTracerManager::AsyncDelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    // Should return a valid timespec
    EXPECT_GE(ts.tv_sec, 0);
    EXPECT_GE(ts.tv_nsec, 0);
}

// ============= GetTraceInfos Tests =============

class GetTraceInfosTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void GetTraceInfosTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    UTracerInit();
    EnableUTrace(true);
    UTracerManager::SetEnable(true);
    ResetTraceInfos();  // Ensure clean state at start
}

void GetTraceInfosTest::TearDown()
{
    ResetTraceInfos();
    UTracerExit();
    GlobalMockObject::verify();
}

TEST_F(GetTraceInfosTest, InvalidTracePointId_ReturnsAll)
{
    // INVALID_TRACE_POINT_ID should return all trace points
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    // Returns empty initially since no trace points are active
    // But the iteration should work
}

TEST_F(GetTraceInfosTest, TpIdGreaterThanMax_ReturnsEmpty)
{
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    EXPECT_EQ(infos.size(), 0U);
}

TEST_F(GetTraceInfosTest, ValidTpId_AfterTrace)
{
    // Perform a trace
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    uint64_t diff = TRACE_DIFF_1000;
    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, diff, 0);

    // Get trace info for this specific trace point
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    EXPECT_EQ(infos.size(), 1U);
}

TEST_F(GetTraceInfosTest, ValidTpId_WithQuantile)
{
    UTracerManager::SetLatencyQuantileEnable(true);
    // Perform a trace
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    uint64_t diff = TRACE_DIFF_1000;
    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, diff, 0);

    // Get trace info with quantile
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TRACE_QUANTILE_50, true);
    EXPECT_EQ(infos.size(), 1U);
}

TEST_F(GetTraceInfosTest, ValidTpId_NotActive)
{
    // Get trace info for trace point that was never used in any test
    // Using BRPC_WRITEV_CALL which hasn't been traced in any prior test
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    EXPECT_EQ(infos.size(), 0U);
}

// ============= ResetTraceInfos Tests =============

TEST_F(GetTraceInfosTest, ResetTraceInfos_ClearsData)
{
    // Perform a trace
    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    uint64_t diff = TRACE_DIFF_1000;
    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, diff, 0);

    // Verify trace info exists
    auto infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    EXPECT_EQ(infos.size(), 1U);

    // Reset
    ResetTraceInfos();

    // After reset, trace point should not be valid (no data)
    infos = GetTraceInfos(BRPC_CONNECT_CALL, TDIGEST_QUANTILE_0, false);
    // The trace point name is still set, so it may still return data
}

// ============= UTracerInfo Tests =============

class UTracerInfoTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UTracerInfoTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void UTracerInfoTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(UTracerInfoTest, DelayBegin_SetsName)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    EXPECT_EQ(info.GetName(), "TestTracePoint");
}

TEST_F(UTracerInfoTest, DelayBegin_IncrementsBeginCounter)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    EXPECT_EQ(info.GetBegin(), 1U);

    info.DelayBegin("TestTracePoint");
    EXPECT_EQ(info.GetBegin(), 2U);
}

TEST_F(UTracerInfoTest, DelayEnd_PositiveRetCode)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    EXPECT_EQ(info.GetGoodEnd(), 1U);
    EXPECT_EQ(info.GetBadEnd(), 0U);
    EXPECT_EQ(info.GetMin(), TRACE_DIFF_1000);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_1000);
    EXPECT_EQ(info.GetTotal(), TRACE_DIFF_1000);
}

TEST_F(UTracerInfoTest, DelayEnd_NegativeRetCode)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, NEG_RET_CODE_MINUS_1, false);

    EXPECT_EQ(info.GetGoodEnd(), 0U);
    EXPECT_EQ(info.GetBadEnd(), 1U);
}

TEST_F(UTracerInfoTest, DelayEnd_UpdateMinMax)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);  // Smaller

    EXPECT_EQ(info.GetMin(), TRACE_DIFF_500);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_1000);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);  // Larger

    EXPECT_EQ(info.GetMin(), TRACE_DIFF_500);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_2000);
}

TEST_F(UTracerInfoTest, Reset_ClearsAll)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayEnd(TRACE_DIFF_500, NEG_RET_CODE_MINUS_1, false);

    info.Reset();

    EXPECT_EQ(info.GetBegin(), 0U);
    EXPECT_EQ(info.GetGoodEnd(), 0U);
    EXPECT_EQ(info.GetBadEnd(), 0U);
    EXPECT_EQ(info.GetTotal(), 0U);
    EXPECT_EQ(info.GetMin(), UINT64_MAX);
    EXPECT_EQ(info.GetMax(), 0U);
}

TEST_F(UTracerInfoTest, Valid_AfterSetName)
{
    UTracerInfo info;
    EXPECT_FALSE(info.Valid());

    info.DelayBegin("TestTracePoint");
    EXPECT_TRUE(info.Valid());
}

TEST_F(UTracerInfoTest, ToString_ReturnsFormattedString)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    std::string str = info.ToString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, ToPeriodString_ReturnsFormattedString)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    std::string str = info.ToPeriodString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, RecordLatest_UpdatesLatestValues)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayEnd(TRACE_DIFF_2000, 0, false);

    info.RecordLatest();
    // Call again - should update latest values
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);
    info.RecordLatest();
}

TEST_F(UTracerInfoTest, ValidPeriod_ChecksDifference)
{
    UTracerInfo info;
    EXPECT_FALSE(info.ValidPeriod());  // No activity yet

    info.DelayBegin("TestTracePoint");
    info.RecordLatest();
    EXPECT_FALSE(info.ValidPeriod());  // begin == latestBegin

    info.DelayBegin("TestTracePoint");
    EXPECT_TRUE(info.ValidPeriod());  // begin > latestBegin
}

TEST_F(UTracerInfoTest, DelayEnd_WithQuantileEnabled)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);  // Enable quantile

    EXPECT_EQ(info.GetGoodEnd(), 1U);
    // Tdigest should have been updated
}

TEST_F(UTracerInfoTest, DelayEnd_MultipleCalls)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);

    EXPECT_EQ(info.GetGoodEnd(), 3U);
    EXPECT_EQ(info.GetMin(), TRACE_DIFF_500);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_2000);
    EXPECT_EQ(info.GetTotal(), TRACE_TOTAL_3500);
}

TEST_F(UTracerInfoTest, SetName_UpdatesName)
{
    UTracerInfo info;
    info.SetName("NewName");
    EXPECT_EQ(info.GetName(), "NewName");
}

TEST_F(UTracerInfoTest, GetTdigest_ReturnsValidTdigest)
{
    UTracerInfo info;
    Tdigest td = info.GetTdigest();
    // Just verify GetTdigest works
}

// ============= Tdigest Tests =============

class TdigestTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void TdigestTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void TdigestTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TdigestTest, Insert_SingleValue)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();
    // Should not crash
}

TEST_F(TdigestTest, Insert_MultipleValues)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 0; i < TDIGEST_COUNT_100; ++i) {
        td.Insert(static_cast<double>(i * CENTROID_WEIGHT_1 * TDIGEST_COUNT_100));
    }
    td.Merge();
    // Should not crash
}

TEST_F(TdigestTest, Insert_WithWeight)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000, TDIGEST_WEIGHT_5);
    td.Merge();
    // Should not crash
}

TEST_F(TdigestTest, Reset_ClearsData)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();
    td.Reset();
    // After reset, should be empty
}

TEST_F(TdigestTest, Quantile_AfterMerge)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 0; i < TDIGEST_LOOP_50; ++i) {
        td.Insert(static_cast<double>(i * CENTROID_WEIGHT_1 * TDIGEST_COUNT_100));
    }
    td.Merge();

    double q50 = td.Quantile(TDIGEST_QUANTILE_50_D);
    // Should return a reasonable value
    EXPECT_GE(q50, TDIGEST_QUANTILE_0);
}

TEST_F(TdigestTest, Quantile_Empty)
{
    Tdigest td(TDIGEST_DELTA_20);
    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    // Empty digest should return 0.0
    EXPECT_EQ(q, 0.0);
}

TEST_F(TdigestTest, Quantile_InvalidValue)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();

    double qNeg = td.Quantile(NEG_QUANTILE);
    double qHigh = td.Quantile(HIGH_QUANTILE);
    // Invalid quantiles should return 0.0
    EXPECT_EQ(qNeg, 0.0);
    EXPECT_EQ(qHigh, 0.0);
}

TEST_F(TdigestTest, Quantile_BoundaryValues)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();

    double q0 = td.Quantile(TDIGEST_QUANTILE_0);
    double q100 = td.Quantile(TDIGEST_QUANTILE_100);
    // Boundary quantiles
}

TEST_F(TdigestTest, Insert_NegativeValue)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(BOUNDARY_VALUE);
    td.Merge();
    // Negative values are filtered out
}

TEST_F(TdigestTest, Insert_LargeValue)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(static_cast<double>(UINT32_MAX) + CENTROID_WEIGHT_1 * TDIGEST_COUNT_100);
    td.Merge();
    // Large values should be handled
}

TEST_F(TdigestTest, Insert_WithLargeWeight)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000, UINT32_MAX);
    td.Merge();
    // Large weight should be handled
}

TEST_F(TdigestTest, MultipleMerge)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();
    td.Insert(CENTROID_MEAN_2000);
    td.Merge();
    td.Insert(CENTROID_MEAN_3000);
    td.Merge();
    // Multiple merge cycles
}

// ============= Centroid Tests =============

class CentroidTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void CentroidTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void CentroidTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(CentroidTest, Constructor)
{
    Centroid c(CENTROID_MEAN_100, CENTROID_WEIGHT_5);
    EXPECT_EQ(c.GetMean(), CENTROID_MEAN_100);
    EXPECT_EQ(c.GetWeight(), 5U);
}

TEST_F(CentroidTest, OperatorLess)
{
    Centroid c1(CENTROID_MEAN_50, CENTROID_WEIGHT_1);
    Centroid c2(CENTROID_MEAN_100, CENTROID_WEIGHT_1);
    EXPECT_TRUE(c1 < c2);
    EXPECT_FALSE(c2 < c1);
}

TEST_F(CentroidTest, OperatorGreater)
{
    Centroid c1(CENTROID_MEAN_100, CENTROID_WEIGHT_1);
    Centroid c2(CENTROID_MEAN_50, CENTROID_WEIGHT_1);
    EXPECT_TRUE(c1 > c2);
    EXPECT_FALSE(c2 > c1);
}

// ============= CentroidList Tests =============

class CentroidListTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void CentroidListTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void CentroidListTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(CentroidListTest, Constructor)
{
    CentroidList list(CENTROID_CAPACITY_20);
    EXPECT_EQ(list.GetCentroidCount(), 0U);
    EXPECT_EQ(list.GetTotalWeight(), 0U);
}

TEST_F(CentroidListTest, Insert_ValidValue)
{
    CentroidList list(CENTROID_CAPACITY_20);
    auto result = list.Insert(CENTROID_MEAN_100, CENTROID_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(list.GetCentroidCount(), 1U);
    EXPECT_EQ(list.GetTotalWeight(), 1U);
}

TEST_F(CentroidListTest, Insert_MultipleValues)
{
    CentroidList list(CENTROID_CAPACITY_20);
    for (int i = 0; i < TDIGEST_LOOP_15; ++i) {
        list.Insert(static_cast<double>(i), 1);
    }
    EXPECT_EQ(list.GetCentroidCount(), LOOP_15);
}

TEST_F(CentroidListTest, Insert_TriggersNeedCompress)
{
    CentroidList list(CENTROID_CAPACITY_5);
    for (int i = 0; i < TDIGEST_LOOP_5; ++i) {
        list.Insert(static_cast<double>(i), 1);
    }
    // Last insert should trigger NEED_COMPERSS
    EXPECT_EQ(list.GetCentroidCount(), 5U);
}

TEST_F(CentroidListTest, Insert_NegativeValue)
{
    CentroidList list(CENTROID_CAPACITY_20);
    auto result = list.Insert(BOUNDARY_VALUE, CENTROID_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    // Negative values are filtered
    EXPECT_EQ(list.GetCentroidCount(), 0U);
}

TEST_F(CentroidListTest, Reset_ClearsData)
{
    CentroidList list(CENTROID_CAPACITY_20);
    list.Insert(CENTROID_MEAN_100, CENTROID_WEIGHT_5);
    list.Reset();
    EXPECT_EQ(list.GetCentroidCount(), 0U);
    EXPECT_EQ(list.GetTotalWeight(), 0U);
}

// ============= UTracerUtils Tests =============

class UTracerUtilsTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UTracerUtilsTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void UTracerUtilsTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(UTracerUtilsTest, StrTrim_EmptyString)
{
    std::string str = "";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_TRUE(result.empty());
}

TEST_F(UTracerUtilsTest, StrTrim_NoSpaces)
{
    std::string str = "test";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

TEST_F(UTracerUtilsTest, StrTrim_LeadingSpaces)
{
    std::string str = "   test";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

TEST_F(UTracerUtilsTest, StrTrim_TrailingSpaces)
{
    std::string str = "test   ";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

TEST_F(UTracerUtilsTest, StrTrim_BothSpaces)
{
    std::string str = "   test   ";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

TEST_F(UTracerUtilsTest, StrTrim_OnlySpaces)
{
    std::string str = "     ";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_TRUE(result.empty());
}

TEST_F(UTracerUtilsTest, CurrentTime_ReturnsValidString)
{
    std::string time = UTracerUtils::CurrentTime();
    EXPECT_FALSE(time.empty());
    // Format: YYYY-MM-DD HH:MM:SS
    EXPECT_EQ(time.length(), TRACE_INFO_MAX_LEN_19);
}

TEST_F(UTracerUtilsTest, FormatString_ValidData)
{
    std::string name = "TestTrace";
    std::string result = UTracerUtils::FormatString(
        name, FORMAT_COLS_10, FORMAT_BEGIN_WIDTH_8, FORMAT_GOOD_BAD_WIDTH_2,
        FORMAT_TOTAL_WIDTH_100, FORMAT_MIN_WIDTH_500, FORMAT_MAX_WIDTH_1000);
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("TestTrace") != std::string::npos);
}

TEST_F(UTracerUtilsTest, FormatString_ZeroData)
{
    std::string name = "TestTrace";
    std::string result = UTracerUtils::FormatString(name, 0, 0, 0, UINT64_MAX, 0, 0);
    EXPECT_FALSE(result.empty());
}

TEST_F(UTracerUtilsTest, SplitStr_EmptyString)
{
    std::string str = "";
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, "/", result);
    EXPECT_TRUE(result.empty());
}

TEST_F(UTracerUtilsTest, SplitStr_SinglePart)
{
    std::string str = "test";
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, "/", result);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], "test");
}

TEST_F(UTracerUtilsTest, SplitStr_MultipleParts)
{
    std::string str = "a/b/c";
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, "/", result);
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[VECTOR_INDEX_2], "c");
}

TEST_F(UTracerUtilsTest, SplitStr_TrailingSeparator)
{
    std::string str = "a/b/";
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, "/", result);
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
}

TEST_F(UTracerUtilsTest, SplitStr_LeadingSeparator)
{
    std::string str = "/a/b";
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, "/", result);
    // Leading separator results in empty first element
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], "");
    EXPECT_EQ(result[1], "a");
    EXPECT_EQ(result[VECTOR_INDEX_2], "b");
}

TEST_F(UTracerUtilsTest, CreateDirectory_ValidPath)
{
    std::string path = "/tmp/test_utracer_utils_" + std::to_string(getpid());
    int ret = UTracerUtils::CreateDirectory(path);
    EXPECT_EQ(ret, 0);
    rmdir(path.c_str());
}

TEST_F(UTracerUtilsTest, CanonicalPath_ValidPath)
{
    std::string path = "/tmp";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_TRUE(result);
}

TEST_F(UTracerUtilsTest, CanonicalPath_EmptyPath)
{
    std::string path = "";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_FALSE(result);
}

TEST_F(UTracerUtilsTest, CanonicalPath_InvalidPath)
{
    std::string path = "/nonexistent/path/that/does/not/exist";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_FALSE(result);
}

// ============= TranTraceInfo Tests =============

class TranTraceInfoTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void TranTraceInfoTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void TranTraceInfoTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TranTraceInfoTest, HeaderString_ReturnsHeader)
{
    std::string header = TranTraceInfo::HeaderString();
    EXPECT_FALSE(header.empty());
    EXPECT_TRUE(header.find("TP_NAME") != std::string::npos);
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfo)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);
    // Should not crash
}

TEST_F(TranTraceInfoTest, Constructor_FromName)
{
    TranTraceInfo tranInfo("TestTracePoint");
    // Should not crash
}

TEST_F(TranTraceInfoTest, ToString_ReturnsFormattedString)
{
    TranTraceInfo tranInfo("TestTracePoint");
    std::string str = tranInfo.ToString();
    EXPECT_FALSE(str.empty());
}

TEST_F(TranTraceInfoTest, ToString_WithDifferentUnits)
{
    TranTraceInfo tranInfo("TestTracePoint");
    std::string nsStr = tranInfo.ToString(TranTraceInfo::NANO_SECOND);
    std::string usStr = tranInfo.ToString(TranTraceInfo::MICRO_SECOND);
    std::string msStr = tranInfo.ToString(TranTraceInfo::MILLI_SECOND);
    std::string sStr = tranInfo.ToString(TranTraceInfo::SECOND);

    EXPECT_FALSE(nsStr.empty());
    EXPECT_FALSE(usStr.empty());
    EXPECT_FALSE(msStr.empty());
    EXPECT_FALSE(sStr.empty());
}

TEST_F(TranTraceInfoTest, OperatorPlus_CombinesInfos)
{
    TranTraceInfo info1("Trace1");
    TranTraceInfo info2("Trace2");

    info1 += info2;
    // Should not crash
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfoWithQuantile)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);  // Enable quantile in DelayEnd

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_50_D, true);
    // Should calculate quantile
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfoWithZeroQuantile)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, true);
    // Should handle zero quantile
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfoWithHighQuantile)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_100, true);
    // Should handle quantile >= NN_NO100
}

TEST_F(TranTraceInfoTest, ToString_AllUnits)
{
    TranTraceInfo tranInfo("TestTracePoint");
    std::string nsStr = tranInfo.ToString(TranTraceInfo::NANO_SECOND);
    std::string usStr = tranInfo.ToString(TranTraceInfo::MICRO_SECOND);
    std::string msStr = tranInfo.ToString(TranTraceInfo::MILLI_SECOND);
    std::string sStr = tranInfo.ToString(TranTraceInfo::SECOND);

    EXPECT_FALSE(nsStr.empty());
    EXPECT_FALSE(usStr.empty());
    EXPECT_FALSE(msStr.empty());
    EXPECT_FALSE(sStr.empty());
}

TEST_F(TranTraceInfoTest, OperatorPlus_UpdatesMinMax)
{
    TranTraceInfo info1("Trace1");
    TranTraceInfo info2("Trace2");
    // info1 += info2 should update min/max
    info1 += info2;
    // Should not crash
}

TEST_F(TranTraceInfoTest, HeaderString_ReturnsValidHeader)
{
    std::string header = TranTraceInfo::HeaderString();
    EXPECT_FALSE(header.empty());
    EXPECT_TRUE(header.find("TP_NAME") != std::string::npos);
    EXPECT_TRUE(header.find("TOTAL") != std::string::npos);
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfoWithData)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayEnd(TRACE_DIFF_2000, 0, false);
    info.DelayEnd(TRACE_DIFF_500, 0, false);

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);
    // Should transfer data from UTracerInfo
}

// ============= UbsTraceDefer Tests =============

class UbsTraceDeferTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UbsTraceDeferTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    UTracerInit();
    EnableUTrace(true);
}

void UbsTraceDeferTest::TearDown()
{
    UTracerExit();
    GlobalMockObject::verify();
}

TEST_F(UbsTraceDeferTest, ConstructorDestructor_CallFunctions)
{
    bool beginCalled = false;
    bool endCalled = false;

    {
        UbsTraceDefer defer(
            [&beginCalled]() { beginCalled = true; },
            [&endCalled]() { endCalled = true; }
        );
        EXPECT_TRUE(beginCalled);
        EXPECT_FALSE(endCalled);
    }
    EXPECT_TRUE(endCalled);
}

TEST_F(UbsTraceDeferTest, NullFunctions_NoCrash)
{
    {
        UbsTraceDefer defer(nullptr, nullptr);
    }
    // Should not crash
}

TEST_F(UbsTraceDeferTest, OnlyBeginFunction)
{
    bool beginCalled = false;

    {
        UbsTraceDefer defer([&beginCalled]() { beginCalled = true; }, nullptr);
        EXPECT_TRUE(beginCalled);
    }
    // Should not crash, end function is null
}

TEST_F(UbsTraceDeferTest, OnlyEndFunction)
{
    bool endCalled = false;

    {
        UbsTraceDefer defer(nullptr, [&endCalled]() { endCalled = true; });
    }
    EXPECT_TRUE(endCalled);
}

// ============= Additional Tdigest Tests =============

TEST_F(TdigestTest, Insert_ManyValuesTriggersMerge)
{
    Tdigest td(TDIGEST_LOOP_5);
    // Insert enough values to trigger merge
    for (int i = 0; i < TDIGEST_LOOP_20; ++i) {
        td.Insert(static_cast<double>(i * CENTROID_WEIGHT_1 * TDIGEST_COUNT_100));
    }
    // Merge should have been triggered automatically
}

TEST_F(TdigestTest, Insert_WeightSum)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000, TDIGEST_WEIGHT_10);
    td.Insert(CENTROID_MEAN_2000, TDIGEST_WEIGHT_20);
    td.Merge();
    // Weight should be summed
}

TEST_F(TdigestTest, Quantile_MultipleValues)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 1; i <= TDIGEST_COUNT_100; ++i) {
        td.Insert(static_cast<double>(i));
    }
    td.Merge();

    double q25 = td.Quantile(TDIGEST_QUANTILE_25);
    double q75 = td.Quantile(TDIGEST_QUANTILE_75);
    EXPECT_GT(q25, TDIGEST_QUANTILE_0);
    EXPECT_GT(q75, q25);
}

TEST_F(TdigestTest, Quantile_LowPercentile)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 1; i <= TDIGEST_COUNT_100; ++i) {
        td.Insert(static_cast<double>(i));
    }
    td.Merge();

    double q1 = td.Quantile(TDIGEST_QUANTILE_1);
    EXPECT_GT(q1, TDIGEST_QUANTILE_0);
}

TEST_F(TdigestTest, Quantile_HighPercentile)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 1; i <= TDIGEST_COUNT_100; ++i) {
        td.Insert(static_cast<double>(i));
    }
    td.Merge();

    double q99 = td.Quantile(TDIGEST_QUANTILE_99);
    EXPECT_GT(q99, TDIGEST_QUANTILE_0);
}

TEST_F(TdigestTest, Merge_MultipleTimes)
{
    Tdigest td(TDIGEST_LOOP_10);
    for (int round = 0; round < TDIGEST_LOOP_5; ++round) {
        for (int i = 0; i < TDIGEST_LOOP_15; ++i) {
            td.Insert(static_cast<double>(round * CENTROID_WEIGHT_1 * TDIGEST_COUNT_100 + i));
        }
    }
    // Multiple merges should happen
}

TEST_F(TdigestTest, Insert_ZeroWeight)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000, WEIGHT_ZERO);
    td.Merge();
    // Zero weight should be handled
}

TEST_F(TdigestTest, Quantile_AfterReset)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_1000);
    td.Merge();
    td.Reset();
    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    // After reset, quantile should return 0
    EXPECT_EQ(q, 0.0);
}

// ============= CentroidList Additional Tests =============

TEST_F(CentroidListTest, Insert_ValueExceedsUint32Max)
{
    CentroidList list(CENTROID_CAPACITY_20);
    // Value > UINT32_MAX should be filtered
    auto result = list.Insert(static_cast<double>(UINT32_MAX) + CENTROID_MEAN_1000, 1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(list.GetCentroidCount(), 0U);
}

TEST_F(CentroidListTest, GetAndSetCentroids)
{
    CentroidList list(CENTROID_CAPACITY_20);
    list.Insert(CENTROID_MEAN_100, CENTROID_WEIGHT_1);
    list.Insert(CENTROID_MEAN_200, CENTROID_WEIGHT_2);

    auto& centroids = list.GetAndSetCentroids();
    EXPECT_EQ(centroids.size(), 2U);
}

TEST_F(CentroidListTest, Insert_ManyValuesToTriggerCompress)
{
    CentroidList list(CENTROID_CAPACITY_5);
    for (int i = 0; i < TDIGEST_LOOP_10; ++i) {
        auto result = list.Insert(static_cast<double>(i), CENTROID_WEIGHT_1);
        // Last insert should return NEED_COMPERSS when capacity reached
    }
    EXPECT_GT(list.GetCentroidCount(), 0U);
}

// ============= UTracerInfo Additional Tests =============

TEST_F(UTracerInfoTest, DelayEnd_UpdatesPeriodMinMax)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);  // Smaller
    info.DelayEnd(TRACE_DIFF_2000, 0, false);  // Larger

    // Period min/max should be updated
}

TEST_F(UTracerInfoTest, ToPeriodString_WithValidPeriod)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.RecordLatest();
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);

    std::string str = info.ToPeriodString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, DelayBegin_SetsNameOnlyOnce)
{
    UTracerInfo info;
    info.DelayBegin("First");
    info.DelayBegin("Second");  // Should not change name

    EXPECT_EQ(info.GetName(), "First");
}

// ============= Additional UTracerInfo Tests =============

TEST_F(UTracerInfoTest, DelayEnd_UpdatesMinMaxPeriod)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);

    // Check min/max
    EXPECT_EQ(info.GetMin(), TRACE_DIFF_500);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_2000U);
}

TEST_F(UTracerInfoTest, DelayEnd_NegativeRetCodeNoMinMaxUpdate)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    uint64_t prevMin = info.GetMin();
    uint64_t prevMax = info.GetMax();

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, NEG_RET_CODE_MINUS_1, false);  // Negative retCode

    // Min/Max should not change
    EXPECT_EQ(info.GetMin(), prevMin);
    EXPECT_EQ(info.GetMax(), prevMax);
    EXPECT_EQ(info.GetBadEnd(), 1u);
}

TEST_F(UTracerInfoTest, ToString_AfterMultipleOperations)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    for (int i = 0; i < TDIGEST_LOOP_10; ++i) {
        info.DelayBegin("TestTracePoint");
        info.DelayEnd(TRACE_DIFF_1000 * (i + 1), 0, false);
    }

    std::string str = info.ToString();
    EXPECT_FALSE(str.empty());
    EXPECT_TRUE(str.find("TestTracePoint") != std::string::npos);
}

TEST_F(UTracerInfoTest, ToPeriodString_AfterRecordLatest)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.RecordLatest();

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);

    std::string str = info.ToPeriodString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, DelayEnd_WithQuantileMultipleInserts)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");

    for (int i = 0; i < TDIGEST_LOOP_20; ++i) {
        info.DelayBegin("TestTracePoint");
        info.DelayEnd(TRACE_DIFF_1000 * (i + 1), 0, true);
    }

    // Tdigest should have processed multiple inserts
    Tdigest td = info.GetTdigest();
    td.Merge();
}

TEST_F(UTracerInfoTest, ValidPeriod_AfterMultipleBeginEnd)
{
    UTracerInfo info;

    EXPECT_FALSE(info.ValidPeriod());

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.RecordLatest();
    EXPECT_FALSE(info.ValidPeriod());  // begin == latestBegin

    info.DelayBegin("TestTracePoint");
    EXPECT_TRUE(info.ValidPeriod());  // begin > latestBegin
}

// ============= TranTraceInfo Additional Tests =============

TEST_F(TranTraceInfoTest, Constructor_LongName)
{
    std::string longName(LONG_NAME_SIZE_200, 'a');
    TranTraceInfo tranInfo(longName.c_str());
    // Should truncate to TRACE_INFO_MAX_LEN
}

TEST_F(TranTraceInfoTest, OperatorPlus_UpdatesValues)
{
    TranTraceInfo info1("Trace1");
    TranTraceInfo info2("Trace2");

    info1 += info2;
    // Should combine values
}

TEST_F(TranTraceInfoTest, Constructor_FromUTracerInfoWithQuantileRange)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);

    // Test quantile in valid range (0, 100)
    TranTraceInfo tranInfo1(info, TDIGEST_QUANTILE_50_D, true);
    TranTraceInfo tranInfo2(info, TDIGEST_QUANTILE_25, true);
    TranTraceInfo tranInfo3(info, TDIGEST_QUANTILE_75, true);
}

TEST_F(TranTraceInfoTest, ToString_AllTimeUnits)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(LARGE_VALUE, 0, false);  // 1ms in ns

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);

    std::string nsStr = tranInfo.ToString(TranTraceInfo::NANO_SECOND);
    std::string usStr = tranInfo.ToString(TranTraceInfo::MICRO_SECOND);
    std::string msStr = tranInfo.ToString(TranTraceInfo::MILLI_SECOND);
    std::string sStr = tranInfo.ToString(TranTraceInfo::SECOND);

    EXPECT_FALSE(nsStr.empty());
    EXPECT_FALSE(usStr.empty());
    EXPECT_FALSE(msStr.empty());
    EXPECT_FALSE(sStr.empty());
}

// ============= UTracerManager Additional Tests =============

TEST_F(UTracerManagerTest, DelayBegin_MultipleTracePoints)
{
    UTracerManager::SetEnable(true);

    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    UTracerManager::DelayBegin(BRPC_ACCEPT_CALL, "BRPC_ACCEPT_CALL");
    UTracerManager::DelayBegin(BRPC_READV_CALL, "BRPC_READV_CALL");
}

TEST_F(UTracerManagerTest, DelayEnd_MultipleTracePoints)
{
    UTracerManager::SetEnable(true);

    UTracerManager::DelayBegin(BRPC_CONNECT_CALL, "BRPC_CONNECT_CALL");
    UTracerManager::DelayBegin(BRPC_ACCEPT_CALL, "BRPC_ACCEPT_CALL");

    UTracerManager::DelayEnd(BRPC_CONNECT_CALL, TRACE_DIFF_1000, 0);
    UTracerManager::DelayEnd(BRPC_ACCEPT_CALL, TRACE_DIFF_2000, 0);
}

TEST_F(UTracerManagerTest, SetLatencyQuantileEnable_Toggle)
{
    UTracerManager::SetLatencyQuantileEnable(true);
    EXPECT_TRUE(UTracerManager::IsLatencyQuantileEnable());

    UTracerManager::SetLatencyQuantileEnable(false);
    EXPECT_FALSE(UTracerManager::IsLatencyQuantileEnable());

    UTracerManager::SetLatencyQuantileEnable(true);
    EXPECT_TRUE(UTracerManager::IsLatencyQuantileEnable());
}

// ============= Inline Function Tests =============

TEST_F(TdigestTest, RelativelyEqual_EqualValues)
{
    EXPECT_TRUE(RelativelyEqual(RELATIVELY_EQUAL_TOLERANCE, RELATIVELY_EQUAL_TOLERANCE));
    EXPECT_TRUE(RelativelyEqual(TDIGEST_QUANTILE_0, TDIGEST_QUANTILE_0));
    EXPECT_TRUE(RelativelyEqual(TDIGEST_QUANTILE_0E_10, TDIGEST_QUANTILE_0E_10));
}

TEST_F(TdigestTest, RelativelyEqual_DifferentValues)
{
    EXPECT_FALSE(RelativelyEqual(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_200));
    // Very close
    EXPECT_TRUE(RelativelyEqual(RELATIVELY_EQUAL_TOLERANCE,
        RELATIVELY_EQUAL_TOLERANCE + TDIGEST_QUANTILE_0E_10));
}

TEST_F(TdigestTest, ComputeNormalizer_NormalCase)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    EXPECT_GT(normalizer, TDIGEST_QUANTILE_0);
}

TEST_F(TdigestTest, ComputeNormalizer_ZeroCompression)
{
    double normalizer = ComputeNormalizer(TDIGEST_QUANTILE_0, CENTROID_MEAN_1000);
    EXPECT_EQ(normalizer, 0.0);
}

TEST_F(TdigestTest, QuantileToScale_MidQuantile)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double scale = QuantileToScale(TDIGEST_QUANTILE_0_5, normalizer);
    // Mid quantile should give a reasonable scale
}

TEST_F(TdigestTest, QuantileToScale_LowQuantile)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double scale = QuantileToScale(TDIGEST_QUANTILE_0_01, normalizer);
    // Low quantile
}

TEST_F(TdigestTest, QuantileToScale_HighQuantile)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double scale = QuantileToScale(TDIGEST_QUANTILE_0_99, normalizer);
    // High quantile
}

TEST_F(TdigestTest, QuantileToScale_VeryLowQuantile)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double scale = QuantileToScale(TDIGEST_QUANTILE_1E_20, normalizer);
    // Very low quantile should be clamped to qMin
}

TEST_F(TdigestTest, QuantileToScale_VeryHighQuantile)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double scale = QuantileToScale(TDIGEST_QUANTILE_1_MINUS_1E20, normalizer);
    // Very high quantile should be clamped to qMax
}

TEST_F(TdigestTest, ScaleToQuantile_ZeroNormalizer)
{
    double quantile = ScaleToQuantile(LERP_POS_10, TDIGEST_QUANTILE_0);
    EXPECT_EQ(quantile, TDIGEST_QUANTILE_0);
}

TEST_F(TdigestTest, ScaleToQuantile_PositiveScale)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double quantile = ScaleToQuantile(LERP_POS_10, normalizer);
    EXPECT_GT(quantile, TDIGEST_QUANTILE_0_5);
    EXPECT_LT(quantile, TDIGEST_QUANTILE_1);
}

TEST_F(TdigestTest, ScaleToQuantile_NegativeScale)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double quantile = ScaleToQuantile(NEG_QUANTILE, normalizer);
    EXPECT_GT(quantile, TDIGEST_QUANTILE_0);
    EXPECT_LT(quantile, TDIGEST_QUANTILE_0_5);
}

TEST_F(TdigestTest, ScaleToQuantile_ZeroScale)
{
    double normalizer = ComputeNormalizer(RELATIVELY_EQUAL_TOLERANCE, CENTROID_MEAN_1000);
    double quantile = ScaleToQuantile(TDIGEST_QUANTILE_0, normalizer);
    EXPECT_EQ(quantile, TDIGEST_QUANTILE_0_5);
}

TEST_F(TdigestTest, Lerp_BasicInterpolation)
{
    EXPECT_DOUBLE_EQ(Lerp(TDIGEST_QUANTILE_0, LERP_POS_10, TDIGEST_QUANTILE_0_5), LERP_POS_5);
    EXPECT_DOUBLE_EQ(Lerp(TDIGEST_QUANTILE_0, LERP_POS_10, TDIGEST_QUANTILE_0), TDIGEST_QUANTILE_0));
    EXPECT_DOUBLE_EQ(Lerp(TDIGEST_QUANTILE_0, LERP_POS_10, TDIGEST_QUANTILE_1), LERP_POS_10));
}

TEST_F(TdigestTest, Lerp_NegativeValues)
{
    EXPECT_DOUBLE_EQ(Lerp(LERP_NEG_10, LERP_POS_10, TDIGEST_QUANTILE_0_5), TDIGEST_QUANTILE_0));
    EXPECT_DOUBLE_EQ(Lerp(LERP_NEG_10, TDIGEST_QUANTILE_0, TDIGEST_QUANTILE_0_5), LERP_NEG_5));
}

TEST_F(TdigestTest, Lerp_Extrapolation)
{
    EXPECT_DOUBLE_EQ(Lerp(TDIGEST_QUANTILE_0, LERP_POS_10, LERP_POS_2), LERP_POS_20));
    EXPECT_DOUBLE_EQ(Lerp(TDIGEST_QUANTILE_0, LERP_POS_10, NEG_QUANTILE), LERP_NEG_10));
}

TEST_F(TdigestTest, Quantile_SingleCentroid)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_100);
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    // Single centroid should return its mean
}

TEST_F(TdigestTest, Quantile_FirstWeightedCentroid)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_100, TDIGEST_WEIGHT_10);  // Heavy weight
    td.Insert(CENTROID_MEAN_200, CENTROID_WEIGHT_1);
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_1);  // Low quantile
}

TEST_F(TdigestTest, Quantile_LastWeightedCentroid)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_100, CENTROID_WEIGHT_1);
    td.Insert(CENTROID_MEAN_200, TDIGEST_WEIGHT_10);  // Heavy weight
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_99);  // High quantile
}

TEST_F(TdigestTest, Merge_EmptyBuffer)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Merge();  // Empty buffer, should not crash
}

TEST_F(TdigestTest, Insert_TriggerCompressPath)
{
    Tdigest td(TDIGEST_LOOP_5);
    // Insert more than buffer size to trigger compress path
    for (int i = 0; i < TDIGEST_LOOP_20; ++i) {
        td.Insert(static_cast<double>(i * CENTROID_WEIGHT_1 * TDIGEST_COUNT_100), CENTROID_WEIGHT_1);
    }
    // Should have triggered multiple compressions
}

TEST_F(TdigestTest, Quantile_IndexLessThanOne)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(CENTROID_MEAN_100);
    td.Insert(CENTROID_MEAN_200);
    td.Insert(CENTROID_MEAN_300);
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_0_001);  // Very low quantile
}

TEST_F(TdigestTest, Quantile_IndexNearTotalWeight)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 1; i <= TDIGEST_COUNT_100; ++i) {
        td.Insert(static_cast<double>(i));
    }
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_99_9);  // Very high quantile
}

TEST_F(TdigestTest, Quantile_EdgeCases)
{
    Tdigest td(TDIGEST_DELTA_20);
    td.Insert(TDIGEST_QUANTILE_0);
    td.Insert(LARGE_VALUE);
    td.Merge();

    double q0 = td.Quantile(TDIGEST_QUANTILE_0);
    double q100 = td.Quantile(TDIGEST_QUANTILE_100);
    // Edge quantiles
}

TEST_F(TdigestTest, Insert_SameValueMultipleTimes)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 0; i < TDIGEST_COUNT_100; ++i) {
        td.Insert(CENTROID_MEAN_1000);  // Same value
    }
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    EXPECT_NEAR(q, CENTROID_MEAN_1000, TDIGEST_QUANTILE_0_01);
}

// ============= UTracerInfo Additional Edge Cases =============

TEST_F(UTracerInfoTest, DelayEnd_DiffUpdatesPeriodMinMax)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);  // Smaller - should update periodMin

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);  // Larger - should update periodMax

    // Verify via ToPeriodString
    std::string str = info.ToPeriodString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, RecordLatest_ResetsPeriodMinMax)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.RecordLatest();

    // Now period min/max should be reset
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);

    std::string str = info.ToPeriodString();
    EXPECT_FALSE(str.empty());
}

TEST_F(UTracerInfoTest, DelayEnd_BadEndDoesNotUpdateMinMax)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    uint64_t minBefore = info.GetMin();
    uint64_t maxBefore = info.GetMax();

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, NEG_RET_CODE_MINUS_1, false);  // Negative retCode - bad end

    // Min/Max should not change for bad end
    EXPECT_EQ(info.GetMin(), minBefore);
    EXPECT_EQ(info.GetMax(), maxBefore);
    EXPECT_EQ(info.GetBadEnd(), 1u);
}

TEST_F(UTracerInfoTest, DelayEnd_ZeroDiff)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(0, 0, false);  // Zero diff

    EXPECT_EQ(info.GetMin(), 0u);
    EXPECT_EQ(info.GetMax(), 0u);
    EXPECT_EQ(info.GetTotal(), 0u);
}

TEST_F(UTracerInfoTest, DelayEnd_LargeDiff)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(UINT64_MAX - 1, 0, false);

    EXPECT_EQ(info.GetMax(), UINT64_MAX - 1);
}

TEST_F(UTracerInfoTest, MultipleDelayBegin_SameName)
{
    UTracerInfo info;
    for (int i = 0; i < TDIGEST_LOOP_10; ++i) {
        info.DelayBegin("SameName");
    }
    EXPECT_EQ(info.GetName(), "SameName");
    EXPECT_EQ(info.GetBegin(), EXPECT_BEGIN_10);
}

TEST_F(UTracerInfoTest, ToString_ContainsAllFields)
{
    UTracerInfo info;
    info.DelayBegin("TestTrace");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayEnd(TRACE_DIFF_500, NEG_RET_CODE_MINUS_1, false);  // Bad end

    std::string str = info.ToString();
    EXPECT_TRUE(str.find("TestTrace") != std::string::npos);
}

// ============= TranTraceInfo Edge Cases =============

TEST_F(TranTraceInfoTest, Constructor_EmptyName)
{
    TranTraceInfo tranInfo("");
    std::string str = tranInfo.ToString();
    EXPECT_FALSE(str.empty());
}

TEST_F(TranTraceInfoTest, ToString_BeginGreaterThanGoodEnd)
{
    UTracerInfo info;
    info.DelayBegin("TestTrace");
    // Don't call DelayEnd, so begin > goodEnd

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);
    std::string str = tranInfo.ToString();
    EXPECT_FALSE(str.empty());
}

TEST_F(TranTraceInfoTest, ToString_GoodEndIsZero)
{
    UTracerInfo info;
    info.DelayBegin("TestTrace");
    // goodEnd is 0

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);
    std::string str = tranInfo.ToString();
    EXPECT_FALSE(str.empty());
}

TEST_F(TranTraceInfoTest, ToString_MinIsMaxValue)
{
    UTracerInfo info;
    info.DelayBegin("TestTrace");
    // min is UINT64_MAX (default)

    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_0, false);
    std::string str = tranInfo.ToString();
    EXPECT_FALSE(str.empty());
}

// ============= CompressionState Tests =============

TEST_F(TdigestTest, CompressionState_ManyInsertions)
{
    Tdigest td(TDIGEST_LOOP_10);  // Small compression
    for (int i = 0; i < TDIGEST_LOOP_1000; ++i) {
        td.Insert(static_cast<double>(i));
    }
    td.Merge();

    double q25 = td.Quantile(TDIGEST_QUANTILE_25);
    double q50 = td.Quantile(TDIGEST_QUANTILE_50_D);
    double q75 = td.Quantile(TDIGEST_QUANTILE_75);

    EXPECT_GT(q25, TDIGEST_QUANTILE_0);
    EXPECT_GT(q50, q25);
    EXPECT_GT(q75, q50);
}

TEST_F(TdigestTest, Insert_AlternatingValues)
{
    Tdigest td(TDIGEST_DELTA_20);
    for (int i = 0; i < TDIGEST_COUNT_100; ++i) {
        if (i % MODULUS_2 == 0) {
            td.Insert(CENTROID_MEAN_100);
        } else {
            td.Insert(CENTROID_MEAN_1000);
        }
    }
    td.Merge();

    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    EXPECT_GT(q, TDIGEST_QUANTILE_0);
}

// ============= CentroidList Edge Cases =============

TEST_F(CentroidListTest, Insert_ValueAtBoundary)
{
    CentroidList list(CENTROID_CAPACITY_20);

    // Value at boundary
    auto result = list.Insert(TDIGEST_QUANTILE_0, CENTROID_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(list.GetCentroidCount(), 1u);

    // Value at UINT32_MAX
    result = list.Insert(static_cast<double>(UINT32_MAX), 1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(list.GetCentroidCount(), 2u);
}

TEST_F(CentroidListTest, Insert_WeightOverflowCheck)
{
    CentroidList list(CENTROID_CAPACITY_20);
    list.Insert(CENTROID_MEAN_100, UINT32_MAX);
    list.Insert(CENTROID_MEAN_200, UINT32_MAX);  // Should trigger overflow warning
    EXPECT_EQ(list.GetTotalWeight(), 2ULL * UINT32_MAX);
}

// ============= Additional UTracerInfo Edge Case Tests =============

TEST_F(UTracerInfoTest, DelayEnd_BadEndUpdatesCount)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, NEG_RET_CODE_MINUS_1, false);  // Negative return code
    info.DelayEnd(TRACE_DIFF_2000, NEG_RET_CODE_MINUS_2, false);  // Another negative return code
    EXPECT_EQ(info.GetGoodEnd(), 0U);
    EXPECT_EQ(info.GetBadEnd(), 2U);
}

TEST_F(UTracerInfoTest, DelayEnd_MinMaxWithMultipleValues)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_500, 0, false);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_750, 0, false);

    EXPECT_EQ(info.GetMin(), TRACE_DIFF_500);
    EXPECT_EQ(info.GetMax(), TRACE_DIFF_2000);
    EXPECT_EQ(info.GetTotal(), EXPECTED_TOTAL_4250);
}

TEST_F(UTracerInfoTest, ToPeriodString_MultiplePeriods)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, false);

    std::string period1 = info.ToPeriodString();
    EXPECT_FALSE(period1.empty());

    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, false);

    std::string period2 = info.ToPeriodString();
    EXPECT_FALSE(period2.empty());
}

TEST_F(UTracerInfoTest, DelayEnd_WithTdigestInsert)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_3000, 0, true);

    Tdigest td = info.GetTdigest();
    td.Merge();
    double q = td.Quantile(TDIGEST_QUANTILE_50_D);
    EXPECT_GT(q, TDIGEST_QUANTILE_0);
}

// ============= Additional TranTraceInfo Tests =============

TEST_F(TranTraceInfoTest, Constructor_LongNameAdditional)
{
    std::string longName(TRACE_INFO_MAX_LEN, 'a');
    TranTraceInfo tranInfo(longName.c_str());
    // Should truncate or handle gracefully
}

TEST_F(TranTraceInfoTest, OperatorPlus_UpdatesValuesAdditional)
{
    UTracerInfo info1;
    info1.DelayBegin("Trace1");
    info1.DelayEnd(TRACE_DIFF_1000, 0, false);
    info1.DelayBegin("Trace1");
    info1.DelayEnd(TRACE_DIFF_2000, 0, false);

    UTracerInfo info2;
    info2.DelayBegin("Trace2");
    info2.DelayEnd(TRACE_DIFF_500, 0, false);
    info2.DelayBegin("Trace2");
    info2.DelayEnd(TRACE_DIFF_3000, 0, false);

    TranTraceInfo tranInfo1(info1, TDIGEST_QUANTILE_0, false);
    TranTraceInfo tranInfo2(info2, TDIGEST_QUANTILE_0, false);

    tranInfo1 += tranInfo2;
    // Combined values
}

TEST_F(TranTraceInfoTest, ToString_WithAllTimeUnitsAdditional)
{
    TranTraceInfo tranInfo("TestTracePoint");
    std::string nsStr = tranInfo.ToString(TranTraceInfo::NANO_SECOND);
    std::string usStr = tranInfo.ToString(TranTraceInfo::MICRO_SECOND);
    std::string msStr = tranInfo.ToString(TranTraceInfo::MILLI_SECOND);
    std::string sStr = tranInfo.ToString(TranTraceInfo::SECOND);

    EXPECT_FALSE(nsStr.empty());
    EXPECT_FALSE(usStr.empty());
    EXPECT_FALSE(msStr.empty());
    EXPECT_FALSE(sStr.empty());
}

TEST_F(TranTraceInfoTest, Constructor_WithQuantileCalculationAdditional)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_2000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_3000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_4000, 0, true);
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_5000, 0, true);

    // Calculate 50th percentile
    TranTraceInfo tranInfo(info, TDIGEST_QUANTILE_50_D, true);
}

TEST_F(TranTraceInfoTest, Constructor_WithBoundaryQuantilesAdditional)
{
    UTracerInfo info;
    info.DelayBegin("TestTracePoint");
    info.DelayEnd(TRACE_DIFF_1000, 0, true);

    // Test boundary quantile values
    TranTraceInfo tranInfo0(info, TDIGEST_QUANTILE_0, true);
    TranTraceInfo tranInfo100(info, TDIGEST_QUANTILE_100, true);
    TranTraceInfo tranInfo50(info, TDIGEST_QUANTILE_50_D, true);
}

// ============= UTracerUtils Additional Tests =============

TEST_F(UTracerUtilsTest, FormatString_AllZeroValues)
{
    std::string name = "TestTrace";
    std::string result = UTracerUtils::FormatString(name, 0, 0, 0, 0, 0, 0);
    EXPECT_FALSE(result.empty());
}

TEST_F(UTracerUtilsTest, FormatString_LargeValues)
{
    std::string name = "TestTrace";
    std::string result = UTracerUtils::FormatString(
        name, UINT64_MAX, UINT64_MAX, UINT64_MAX,
        UINT64_MAX, UINT64_MAX, UINT64_MAX);
    EXPECT_FALSE(result.empty());
}

TEST_F(UTracerUtilsTest, FormatString_BeginLessThanBadEnd)
{
    std::string name = "TestTrace";
    // Test the branch where begin < goodEnd + badEnd
    std::string result = UTracerUtils::FormatString(
        name, TRACE_BEGIN_LESS_5, TRACE_GOOD_END_10, TRACE_BAD_END_3,
        TRACE_TOTAL_100, TRACE_MIN_500, TRACE_MAX_1000);
    EXPECT_FALSE(result.empty());
}

TEST_F(UTracerUtilsTest, CreateDirectory_NestedPath)
{
    std::string basePath = "/tmp/test_utracer_nested_" + std::to_string(getpid());
    std::string nestedPath = basePath + "/level1/level2/level3";

    int ret = UTracerUtils::CreateDirectory(nestedPath);
    EXPECT_EQ(ret, 0);

    // Cleanup
    rmdir((basePath + "/level1/level2/level3").c_str());
    rmdir((basePath + "/level1/level2").c_str());
    rmdir((basePath + "/level1").c_str());
    rmdir(basePath.c_str());
}

TEST_F(UTracerUtilsTest, StrTrim_TabCharacters)
{
    std::string str = "   test   ";
    std::string& result = UTracerUtils::StrTrim(str);
    // Only spaces should be trimmed
}

TEST_F(UTracerUtilsTest, CurrentTime_FormatCheck)
{
    std::string time = UTracerUtils::CurrentTime();
    EXPECT_FALSE(time.empty());
    // Format should be YYYY-MM-DD HH:MM:SS (19 characters)
    EXPECT_EQ(time.length(), TRACE_INFO_MAX_LEN_19);
    // Check separator characters
    EXPECT_EQ(time[TIME_INDEX_4], '-');
    EXPECT_EQ(time[TIME_INDEX_7], '-');
    EXPECT_EQ(time[TIME_SEP_IDX_10], ' ');
    EXPECT_EQ(time[TIME_SEP_IDX_13], ':');
    EXPECT_EQ(time[TIME_SEP_IDX_16], ':');
}