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

#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_prof_tracepoint_combiner_ext.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

namespace {

static const uint32_t TEST_TP_COUNT_2 = 2;
static const uint32_t TEST_TP_COUNT_3 = 3;
static const uint32_t TEST_TP_ID_0 = 0;
static const uint32_t TEST_TP_ID_1 = 1;
static const char *TEST_TP_NAME_P0 = "p0";
static const char *TEST_TP_NAME_P1 = "p1";
static const char *TEST_TP_NAME_RESET = "reset";

// 输出行格式: [name] 然后依次 success_count / failure_count / total_time / avgTime /
//                       maxTime / minTime / pp99_time / pp9999_time(8 个数字字段)
static std::vector<uint64_t> ParseNumberFields(const std::string &line)
{
    std::istringstream iss(line);
    std::vector<uint64_t> numbers;
    std::string token;
    bool first = true;
    while (iss >> token) {
        if (first) {
            first = false;
            continue;
        }
        numbers.push_back(std::stoull(token));
    }
    return numbers;
}

class TraceCombinerExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        errno = 0;
    }
};

TEST_F(TraceCombinerExtTest, OutputTracePointStatsExt_NoName_OutputsNothing)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 0;
    tp.data.success_count = 1;
    std::ostringstream oss;
    combiner.OutputTracePointStatsExt(oss, tp);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceCombinerExtTest, OutputTracePointStatsExt_ZeroSuccess_OutputsZeroAvg)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 1;
    tp.SetNameExt(TEST_TP_NAME_P0);
    tp.data.success_count = 0;
    tp.data.failure_count = 3;
    tp.data.total_time = 200;
    tp.data.max_time = 60;
    tp.data.min_time = 10;
    std::ostringstream oss;
    combiner.OutputTracePointStatsExt(oss, tp);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_P0), std::string::npos);
    std::vector<uint64_t> nums = ParseNumberFields(outStr);
    ASSERT_EQ(nums.size(), 8U);
    EXPECT_EQ(nums[0], 0U);  // success_count
    EXPECT_EQ(nums[3], 0U);  // avgTime: 无成功样本 -> 0
    EXPECT_EQ(nums[4], 60U); // max_time
    EXPECT_EQ(nums[5], 10U); // min_time
}

TEST_F(TraceCombinerExtTest, OutputTracePointStatsExt_AvgComputedFromSuccessCount)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 1;
    tp.SetNameExt(TEST_TP_NAME_P1);
    tp.data.success_count = 4;
    tp.data.failure_count = 1;
    tp.data.total_time = 100;
    tp.data.max_time = 40;
    tp.data.min_time = 5;
    std::ostringstream oss;
    combiner.OutputTracePointStatsExt(oss, tp);
    std::string outStr = oss.str();
    std::vector<uint64_t> nums = ParseNumberFields(outStr);
    ASSERT_EQ(nums.size(), 8U);
    EXPECT_EQ(nums[0], 4U);  // success_count
    EXPECT_EQ(nums[3], 25U); // avgTime = total_time / success_count
    EXPECT_EQ(nums[4], 40U); // max_time
    EXPECT_EQ(nums[5], 5U);  // min_time
}

TEST_F(TraceCombinerExtTest, OutputTracePointStatsExt_MinTimeMax_ResetsMinAndMaxToZero)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 1;
    tp.SetNameExt(TEST_TP_NAME_RESET);
    tp.data.success_count = 1;
    tp.data.failure_count = 0;
    tp.data.total_time = 100;
    // min_time 保持默认 UINT64_MAX -> 触发防御性重置分支
    std::ostringstream oss;
    combiner.OutputTracePointStatsExt(oss, tp);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_RESET), std::string::npos);
    std::vector<uint64_t> nums = ParseNumberFields(outStr);
    ASSERT_EQ(nums.size(), 8U);
    EXPECT_EQ(nums[4], 0U); // max_time 重置为 0
    EXPECT_EQ(nums[5], 0U); // min_time 重置为 0
}

TEST_F(TraceCombinerExtTest, OutputTracePointCliExt_NoName_OutputsNothing)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 0;
    std::ostringstream oss;
    combiner.OutputTracePointCliExt(oss, tp);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceCombinerExtTest, OutputTracePointCliExt_WithName_OutputsFormattedStats)
{
    TraceCombinerExt combiner;
    TracepointExt tp;
    tp.id = TEST_TP_ID_0;
    tp.has_name = 1;
    tp.SetNameExt(TEST_TP_NAME_P0);
    tp.data.success_count = 2;
    tp.data.failure_count = 1;
    tp.data.total_time = 200;
    tp.data.max_time = 100;
    tp.data.min_time = 50;
    std::ostringstream oss;
    combiner.OutputTracePointCliExt(oss, tp);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_P0), std::string::npos);
    std::vector<uint64_t> nums = ParseNumberFields(outStr);
    ASSERT_EQ(nums.size(), 8U);
    EXPECT_EQ(nums[0], 2U);
    EXPECT_EQ(nums[3], 100U); // avgTime
}

TEST_F(TraceCombinerExtTest, OutputTraceGroupExt_EmptyGroup_NoOutput)
{
    TraceCombinerExt combiner;
    auto group = MakeRef<TraceGroupExt>(0);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->InitExt(), UBS_OK);
    std::ostringstream oss;
    combiner.OutputTraceGroupExt(oss, group);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceCombinerExtTest, OutputTraceGroupExt_MixedNamedUnnamed_OnlyNamedOutput)
{
    TraceCombinerExt combiner;
    auto group = MakeRef<TraceGroupExt>(TEST_TP_COUNT_3);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->InitExt(), UBS_OK);
    EXPECT_EQ(group->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_P0, 100, true), UBS_OK);
    EXPECT_EQ(group->RecordExt(TEST_TP_ID_1, TEST_TP_NAME_P1, 50, false), UBS_OK);
    std::ostringstream oss;
    combiner.OutputTraceGroupExt(oss, group);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_P0), std::string::npos);
    EXPECT_NE(outStr.find(TEST_TP_NAME_P1), std::string::npos);
}

TEST_F(TraceCombinerExtTest, OutputTraceGroupCliExt_WithData_AllocatesBuffer)
{
    TraceCombinerExt combiner;
    auto group = MakeRef<TraceGroupExt>(TEST_TP_COUNT_2);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->InitExt(), UBS_OK);
    EXPECT_EQ(group->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_P0, 100, true), UBS_OK);
    char *buf = nullptr;
    int len = combiner.OutputTraceGroupCliExt(&buf, group);
    EXPECT_GT(len, 0);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(std::string(buf).find(TEST_TP_NAME_P0), std::string::npos);
    free(buf);
}

TEST_F(TraceCombinerExtTest, OutputTraceGroupCliExt_NoNamedPoints_ReturnsMinusOne)
{
    TraceCombinerExt combiner;
    auto group = MakeRef<TraceGroupExt>(TEST_TP_COUNT_2);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->InitExt(), UBS_OK);
    char *buf = nullptr;
    int len = combiner.OutputTraceGroupCliExt(&buf, group);
    EXPECT_EQ(len, -1);
    EXPECT_EQ(buf, nullptr);
}

// 单一 DecreaseRef 调用点 + 循环切换引用计数，覆盖 DecreaseRef 的删除/不删除两个分支
TEST_F(TraceCombinerExtTest, DecreaseRef_BothBranches_SingleCallSite)
{
    for (int round = 0; round < 2; ++round) {
        TraceCombinerExt *combiner = new TraceCombinerExt();
        combiner->IncreaseRef();
        if (round == 1) {
            combiner->IncreaseRef();
        }
        combiner->DecreaseRef();
        if (round == 1) {
            delete combiner;
        }
    }
}

TEST_F(TraceCombinerExtTest, RefLifecycle_CopyAndRelease_DeletesCombiner)
{
    TraceCombinerExtPtr combiner = MakeRef<TraceCombinerExt>();
    ASSERT_NE(combiner.Get(), nullptr);
    {
        TraceCombinerExtPtr copy = combiner;
        EXPECT_EQ(copy.Get(), combiner.Get());
    }
    EXPECT_NE(combiner.Get(), nullptr);
    combiner = nullptr;
    EXPECT_EQ(combiner.Get(), nullptr);
}

TEST_F(TraceCombinerExtTest, RefLifecycle_MoveConstruct_TransfersOwnership)
{
    TraceCombinerExtPtr combiner = MakeRef<TraceCombinerExt>();
    ASSERT_NE(combiner.Get(), nullptr);
    TraceCombinerExtPtr moved(std::move(combiner));
    EXPECT_NE(moved.Get(), nullptr);
    EXPECT_EQ(combiner.Get(), nullptr);
    moved = nullptr;
}

TEST_F(TraceCombinerExtTest, MakeRef_NullCheck_DestroysWithoutCrash)
{
    TraceCombinerExtPtr combiner = MakeRef<TraceCombinerExt>();
    ASSERT_NE(combiner.Get(), nullptr);
    combiner = nullptr;
    EXPECT_EQ(combiner.Get(), nullptr);
}

} // namespace
