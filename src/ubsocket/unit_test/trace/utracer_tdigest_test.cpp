// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cmath>
#include <limits>
#include "trace/utracer_tdigest.h"
#include "trace/utracer_def.h"
#include "rpc_adpt_vlog.h"

using namespace Statistics;

namespace {
static const size_t TEST_TDIGEST_SIZE = 100;
static const size_t TEST_SMALL_TDIGEST_SIZE = 10;
static const double TEST_VALUE_1 = 100.0;
static const double TEST_VALUE_2 = 200.0;
static const double TEST_VALUE_3 = 300.0;
static const double TEST_WEIGHT_1 = 1;
static const uint32_t TEST_WEIGHT_LARGE = 100;
static const double TEST_QUANTILE_0 = 0.0;
static const double TEST_QUANTILE_50 = 50.0;
static const double TEST_QUANTILE_100 = 100.0;
static const double TEST_QUANTILE_INVALID = -1.0;
static const double TEST_QUANTILE_OVERFLOW = 101.0;
static const double TEST_EPSILON = 1e-8;
static const double TEST_MEAN_NEGATIVE = -1.0;
static const double TEST_MEAN_LARGE = static_cast<double>(UINT32_MAX) + 1.0;

// 替换魔鬼数字
static const double TEST_HALF = 0.5;
static const double TEST_LERP_MID = 150.0;
static const int TEST_REPEAT_MULTIPLIER = 2;
}

class UtracerTdigestTest : public testing::Test {
protected:
    void SetUp() override
    {
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

// 测试 Centroid 构造函数和基本方法
TEST_F(UtracerTdigestTest, Centroid_BasicOperations)
{
    Centroid centroid(TEST_VALUE_1, TEST_WEIGHT_LARGE);

    EXPECT_DOUBLE_EQ(centroid.GetMean(), TEST_VALUE_1);
    EXPECT_EQ(centroid.GetWeight(), TEST_WEIGHT_LARGE);
}

// 测试 Centroid 比较运算符
TEST_F(UtracerTdigestTest, Centroid_ComparisonOperators)
{
    Centroid centroid1(TEST_VALUE_1, TEST_WEIGHT_1);
    Centroid centroid2(TEST_VALUE_2, TEST_WEIGHT_1);

    EXPECT_TRUE(centroid1 < centroid2);
    EXPECT_FALSE(centroid2 < centroid1);
    EXPECT_TRUE(centroid2 > centroid1);
    EXPECT_FALSE(centroid1 > centroid2);
}

// 测试 CentroidList 构造函数和基本方法
TEST_F(UtracerTdigestTest, CentroidList_BasicOperations)
{
    CentroidList list(TEST_TDIGEST_SIZE);

    EXPECT_EQ(list.GetCentroidCount(), 0U);
    EXPECT_EQ(list.GetTotalWeight(), 0U);
}

// 测试 CentroidList Insert 成功路径
TEST_F(UtracerTdigestTest, CentroidList_Insert_Success)
{
    CentroidList list(TEST_TDIGEST_SIZE);

    InsertResultCode result = list.Insert(TEST_VALUE_1, TEST_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(list.GetCentroidCount(), 1U);
    EXPECT_EQ(list.GetTotalWeight(), TEST_WEIGHT_1);
}

// 测试 CentroidList Insert - mean 为负值
TEST_F(UtracerTdigestTest, CentroidList_Insert_NegativeMean)
{
    CentroidList list(TEST_TDIGEST_SIZE);

    InsertResultCode result = list.Insert(TEST_MEAN_NEGATIVE, TEST_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
}

// 测试 CentroidList Insert - mean 过大
TEST_F(UtracerTdigestTest, CentroidList_Insert_MeanTooLarge)
{
    CentroidList list(TEST_TDIGEST_SIZE);

    InsertResultCode result = list.Insert(TEST_MEAN_LARGE, TEST_WEIGHT_1);
    EXPECT_EQ(result, InsertResultCode::NO_NEED_COMPERSS);
}

// 测试 CentroidList Insert - 达到容量触发压缩
TEST_F(UtracerTdigestTest, CentroidList_Insert_ReachCapacity)
{
    CentroidList list(TEST_SMALL_TDIGEST_SIZE);

    // 填满到容量
    for (size_t i = 0; i < TEST_SMALL_TDIGEST_SIZE; i++) {
        InsertResultCode result = list.Insert(static_cast<double>(i), TEST_WEIGHT_1);
        if (result == InsertResultCode::NEED_COMPERSS) {
            break;
        }
    }

    // 应该达到容量
    EXPECT_GE(list.GetCentroidCount(), TEST_SMALL_TDIGEST_SIZE - 1);
}

// 测试 CentroidList Reset
TEST_F(UtracerTdigestTest, CentroidList_Reset)
{
    CentroidList list(TEST_TDIGEST_SIZE);
    list.Insert(TEST_VALUE_1, TEST_WEIGHT_1);

    list.Reset();

    EXPECT_EQ(list.GetCentroidCount(), 0U);
    EXPECT_EQ(list.GetTotalWeight(), 0U);
}

// 测试 RelativelyEqual 函数
TEST_F(UtracerTdigestTest, RelativelyEqual_SameValues)
{
    EXPECT_TRUE(RelativelyEqual(TEST_VALUE_1, TEST_VALUE_1, TEST_EPSILON));
}

TEST_F(UtracerTdigestTest, RelativelyEqual_DifferentValues)
{
    EXPECT_FALSE(RelativelyEqual(TEST_VALUE_1, TEST_VALUE_2, TEST_EPSILON));
}

TEST_F(UtracerTdigestTest, RelativelyEqual_SmallDifference)
{
    double smallDiff = TEST_VALUE_1 + TEST_EPSILON / NN_NO2;
    EXPECT_TRUE(RelativelyEqual(TEST_VALUE_1, smallDiff, TEST_EPSILON));
}

// 测试 ComputeNormalizer 函数
TEST_F(UtracerTdigestTest, ComputeNormalizer_ZeroCompression)
{
    double result = ComputeNormalizer(0, TEST_TDIGEST_SIZE);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST_F(UtracerTdigestTest, ComputeNormalizer_NormalCase)
{
    double result = ComputeNormalizer(TEST_TDIGEST_SIZE, TEST_TDIGEST_SIZE);
    EXPECT_GT(result, 0.0);
}

// 测试 QuantileToScale 函数 - q 过小
TEST_F(UtracerTdigestTest, QuantileToScale_TooSmall)
{
    double normalizer = 1.0;
    double result = QuantileToScale(1e-20, normalizer);
    EXPECT_NE(result, 0.0);  // 会使用 qMin
}

// 测试 QuantileToScale 函数 - q 过大
TEST_F(UtracerTdigestTest, QuantileToScale_TooLarge)
{
    double normalizer = 1.0;
    double result = QuantileToScale(1.0 - 1e-20, normalizer);
    EXPECT_NE(result, 0.0);  // 会使用 qMax
}

// 测试 QuantileToScale 函数 - q <= 0.5
TEST_F(UtracerTdigestTest, QuantileToScale_LessThanHalf)
{
    double normalizer = 1.0;
    double result = QuantileToScale(0.25, normalizer);
    EXPECT_LT(result, 0.0);
}

// 测试 QuantileToScale 函数 - q > 0.5
TEST_F(UtracerTdigestTest, QuantileToScale_GreaterThanHalf)
{
    double normalizer = 1.0;
    double result = QuantileToScale(0.75, normalizer);
    EXPECT_GT(result, 0.0);
}

// 测试 ScaleToQuantile 函数 - normalizer 为 0
TEST_F(UtracerTdigestTest, ScaleToQuantile_ZeroNormalizer)
{
    double result = ScaleToQuantile(TEST_VALUE_1, 0.0);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

// 测试 ScaleToQuantile 函数 - k <= 0
TEST_F(UtracerTdigestTest, ScaleToQuantile_KLessThanZero)
{
    double normalizer = 1.0;
    double result = ScaleToQuantile(-1.0, normalizer);
    EXPECT_GT(result, 0.0);
    EXPECT_LT(result, TEST_HALF);
}

// 测试 ScaleToQuantile 函数 - k > 0
TEST_F(UtracerTdigestTest, ScaleToQuantile_KGreaterThanZero)
{
    double normalizer = 1.0;
    double result = ScaleToQuantile(1.0, normalizer);
    EXPECT_GT(result, TEST_HALF);
    EXPECT_LT(result, 1.0);
}

// 测试 Lerp 函数
TEST_F(UtracerTdigestTest, Lerp_Interpolation)
{
    double result = Lerp(TEST_VALUE_1, TEST_VALUE_2, TEST_HALF);
    EXPECT_DOUBLE_EQ(result, TEST_LERP_MID);
}

TEST_F(UtracerTdigestTest, Lerp_InterpolationZero)
{
    double result = Lerp(TEST_VALUE_1, TEST_VALUE_2, 0.0);
    EXPECT_DOUBLE_EQ(result, TEST_VALUE_1);
}

TEST_F(UtracerTdigestTest, Lerp_InterpolationOne)
{
    double result = Lerp(TEST_VALUE_1, TEST_VALUE_2, 1.0);
    EXPECT_DOUBLE_EQ(result, TEST_VALUE_2);
}

// 测试 CompressionState 结构体
TEST_F(UtracerTdigestTest, CompressionState_Initialization)
{
    double normalizer = ComputeNormalizer(TEST_TDIGEST_SIZE, TEST_TDIGEST_SIZE);
    CompressionState state(TEST_WEIGHT_LARGE, normalizer);

    EXPECT_EQ(state.newTotalWeight, TEST_WEIGHT_LARGE);
    EXPECT_DOUBLE_EQ(state.weightSoFar, 0.0);
    EXPECT_DOUBLE_EQ(state.weightToAdd, 0.0);
    EXPECT_DOUBLE_EQ(state.meanToAdd, 0.0);
}

// 测试 Tdigest 构造函数
TEST_F(UtracerTdigestTest, Tdigest_Construction)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    // 构造成功，无异常
}

// 测试 Tdigest Insert 单值
TEST_F(UtracerTdigestTest, Tdigest_Insert_SingleValue)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);

    // Insert后可能触发Merge
}

// 测试 Tdigest Insert 多值
TEST_F(UtracerTdigestTest, Tdigest_Insert_MultipleValues)
{
    Tdigest tdigest(TEST_SMALL_TDIGEST_SIZE);

    int limit = static_cast<int>(TEST_SMALL_TDIGEST_SIZE) * TEST_REPEAT_MULTIPLIER;
    for (int i = 0; i < limit; i++) {
        tdigest.Insert(static_cast<double>(i));
    }

    // 应触发多次Merge
}

// 测试 Tdigest Reset
TEST_F(UtracerTdigestTest, Tdigest_Reset)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);

    tdigest.Reset();

    // Reset后可以重新使用
    tdigest.Insert(TEST_VALUE_2);
}

// 测试 Tdigest Merge 空数据
TEST_F(UtracerTdigestTest, Tdigest_Merge_EmptyData)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Merge();  // 空数据，不应崩溃
}

// 测试 Tdigest Quantile - 无效范围
TEST_F(UtracerTdigestTest, Tdigest_Quantile_InvalidRange)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_INVALID);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

// 测试 Tdigest Quantile - 超过100
TEST_F(UtracerTdigestTest, Tdigest_Quantile_Overflow)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_OVERFLOW);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

// 测试 Tdigest Quantile - 正常值
TEST_F(UtracerTdigestTest, Tdigest_Quantile_NormalValue)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    for (int i = 0; i < static_cast<int>(TEST_TDIGEST_SIZE); i++) {
        tdigest.Insert(static_cast<double>(i));
    }
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_50);
    // 应返回合理的分位数值
    EXPECT_GE(result, 0.0);
}

// 测试 Tdigest Quantile - 边界值 0
TEST_F(UtracerTdigestTest, Tdigest_Quantile_Zero)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_0);
    EXPECT_DOUBLE_EQ(result, TEST_VALUE_1);
}

// 测试 Tdigest Quantile - 边界值 100
TEST_F(UtracerTdigestTest, Tdigest_Quantile_OneHundred)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_100);
    EXPECT_DOUBLE_EQ(result, TEST_VALUE_1);
}

// 测试 Tdigest Quantile - 单个centroid
TEST_F(UtracerTdigestTest, Tdigest_Quantile_SingleCentroid)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);
    tdigest.Insert(TEST_VALUE_1);
    tdigest.Merge();

    double result = tdigest.Quantile(TEST_QUANTILE_50);
    EXPECT_DOUBLE_EQ(result, TEST_VALUE_1);
}

// 测试 Tdigest Quantile - 多个centroid
TEST_F(UtracerTdigestTest, Tdigest_Quantile_MultipleCentroids)
{
    Tdigest tdigest(TEST_TDIGEST_SIZE);

    tdigest.Insert(TEST_VALUE_1);
    tdigest.Insert(TEST_VALUE_2);
    tdigest.Insert(TEST_VALUE_3);
    tdigest.Merge();

    double result50 = tdigest.Quantile(TEST_QUANTILE_50);
    EXPECT_GT(result50, TEST_VALUE_1);
    EXPECT_LT(result50, TEST_VALUE_3);
}