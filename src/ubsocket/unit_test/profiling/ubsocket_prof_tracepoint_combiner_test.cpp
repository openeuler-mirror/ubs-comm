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

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_prof_tracepoint_combiner.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

namespace {

static const uint32_t TEST_TP_ID_1 = 1;
static const uint32_t TEST_TP_ID_2 = 2;
static const uint32_t TEST_TP_ID_5 = 5;
static const char *TEST_TP_NAME = "tpName";
static const char *TEST_TP_NAME_EXISTING = "existing";
static const char *TEST_TP_NAME_CLI = "cliPoint";

class TraceCombinerTest : public ::testing::Test {
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

TEST_F(TraceCombinerTest, CombinerTracePoint_IdMismatch_ReturnsError)
{
    TraceCombiner combiner;
    Tracepoint out;
    out.id = TEST_TP_ID_1;
    Tracepoint pointB;
    pointB.id = TEST_TP_ID_2;
    pointB.has_name = 1;
    pointB.SetName(TEST_TP_NAME);
    int ret = combiner.CombinerTracePoint(out, pointB);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(out.id, TEST_TP_ID_1);
    EXPECT_EQ(out.has_name, 0);
}

TEST_F(TraceCombinerTest, CombinerTracePoint_SameIdNoName_CopiesNameAndAggregates)
{
    TraceCombiner combiner;
    Tracepoint out;
    out.id = TEST_TP_ID_5;
    Tracepoint pointB;
    pointB.id = TEST_TP_ID_5;
    pointB.has_name = 1;
    pointB.SetName(TEST_TP_NAME);
    pointB.data.success_count = 3;
    pointB.data.failure_count = 2;
    pointB.data.total_time = 100;
    pointB.data.max_time = 60;
    pointB.data.min_time = 10;
    int ret = combiner.CombinerTracePoint(out, pointB);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(out.has_name, 1);
    EXPECT_STREQ(out.GetName(), TEST_TP_NAME);
    EXPECT_EQ(out.data.success_count, 3);
    EXPECT_EQ(out.data.failure_count, 2);
    EXPECT_EQ(out.data.total_time, 100);
    EXPECT_EQ(out.data.max_time, 60);
    EXPECT_EQ(out.data.min_time, 10);
}

TEST_F(TraceCombinerTest, CombinerTracePoint_OutHasName_KeepsNameAndSelectsMaxMin)
{
    TraceCombiner combiner;
    Tracepoint out;
    out.id = TEST_TP_ID_5;
    out.has_name = 1;
    out.SetName(TEST_TP_NAME_EXISTING);
    out.data.success_count = 5;
    out.data.failure_count = 1;
    out.data.total_time = 500;
    out.data.max_time = 100;
    out.data.min_time = 20;
    Tracepoint pointB;
    pointB.id = TEST_TP_ID_5;
    pointB.has_name = 1;
    pointB.SetName(TEST_TP_NAME);
    pointB.data.success_count = 2;
    pointB.data.failure_count = 0;
    pointB.data.total_time = 100;
    pointB.data.max_time = 50;
    pointB.data.min_time = 30;
    int ret = combiner.CombinerTracePoint(out, pointB);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_STREQ(out.GetName(), TEST_TP_NAME_EXISTING);
    EXPECT_EQ(out.data.success_count, 7);
    EXPECT_EQ(out.data.failure_count, 1);
    EXPECT_EQ(out.data.total_time, 600);
    EXPECT_EQ(out.data.max_time, 100);
    EXPECT_EQ(out.data.min_time, 20);
}

TEST_F(TraceCombinerTest, CombinerTracePoint_MinTimeLower_SelectsLowerMin)
{
    TraceCombiner combiner;
    Tracepoint out;
    out.id = TEST_TP_ID_5;
    out.has_name = 1;
    out.data.max_time = 100;
    out.data.min_time = 20;
    Tracepoint pointB;
    pointB.id = TEST_TP_ID_5;
    pointB.data.max_time = 200;
    pointB.data.min_time = 5;
    int ret = combiner.CombinerTracePoint(out, pointB);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(out.data.max_time, 200);
    EXPECT_EQ(out.data.min_time, 5);
}

TEST_F(TraceCombinerTest, OutputTracePointCli_WithName_OutputsFormattedStats)
{
    TraceCombiner combiner;
    Tracepoint tp;
    tp.id = TEST_TP_ID_1;
    tp.has_name = 1;
    tp.SetName(TEST_TP_NAME_CLI);
    tp.data.success_count = 2;
    tp.data.failure_count = 1;
    tp.data.total_time = 200;
    tp.data.max_time = 100;
    tp.data.min_time = 50;
    std::ostringstream oss;
    combiner.OutputTracePointCli(oss, tp);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_CLI), std::string::npos);
    EXPECT_NE(outStr.find("2"), std::string::npos);
}

TEST_F(TraceCombinerTest, OutputTracePointCli_NoName_OutputsNothing)
{
    TraceCombiner combiner;
    Tracepoint tp;
    tp.id = TEST_TP_ID_1;
    tp.has_name = 0;
    tp.data.success_count = 1;
    std::ostringstream oss;
    combiner.OutputTracePointCli(oss, tp);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceCombinerTest, OutputTracePointStats_NoName_ReturnsEarly)
{
    TraceCombiner combiner;
    Tracepoint tp;
    tp.id = TEST_TP_ID_1;
    tp.has_name = 0;
    tp.data.success_count = 1;
    std::ostringstream oss;
    combiner.OutputTracePointStats(oss, tp);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceCombinerTest, OutputTracePointStats_ZeroSuccess_OutputsZeroAvg)
{
    TraceCombiner combiner;
    Tracepoint tp;
    tp.id = TEST_TP_ID_1;
    tp.has_name = 1;
    tp.SetName(TEST_TP_NAME_CLI);
    tp.data.success_count = 0;
    tp.data.failure_count = 3;
    tp.data.total_time = 200;
    tp.data.max_time = 100;
    tp.data.min_time = 50;
    std::ostringstream oss;
    combiner.OutputTracePointStats(oss, tp);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find(TEST_TP_NAME_CLI), std::string::npos);
}

TEST_F(TraceCombinerTest, OutputTraceGroup_MixedNamedUnnamed_OnlyNamedOutput)
{
    TraceCombiner combiner;
    auto group = MakeRef<TraceGroup>(3);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->Init(), UBS_OK);
    EXPECT_EQ(group->Record(0, "p0", 100, true), UBS_OK);
    EXPECT_EQ(group->Record(1, "p1", 50, false), UBS_OK);
    std::ostringstream oss;
    combiner.OutputTraceGroup(oss, group);
    std::string outStr = oss.str();
    EXPECT_NE(outStr.find("p0"), std::string::npos);
    EXPECT_NE(outStr.find("p1"), std::string::npos);
}

TEST_F(TraceCombinerTest, OutputTraceGroupCli_WithData_AllocatesBuffer)
{
    TraceCombiner combiner;
    auto group = MakeRef<TraceGroup>(2);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->Init(), UBS_OK);
    EXPECT_EQ(group->Record(0, "cliGroup", 100, true), UBS_OK);
    char *buf = nullptr;
    int len = combiner.OutputTraceGroupCli(&buf, group);
    EXPECT_GT(len, 0);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(std::string(buf).find("cliGroup"), std::string::npos);
    free(buf);
}

TEST_F(TraceCombinerTest, OutputTraceGroupCli_NoNamedPoints_ReturnsZeroLenBuf)
{
    TraceCombiner combiner;
    auto group = MakeRef<TraceGroup>(2);
    ASSERT_NE(group.Get(), nullptr);
    ASSERT_EQ(group->Init(), UBS_OK);
    char *buf = nullptr;
    int len = combiner.OutputTraceGroupCli(&buf, group);
    EXPECT_EQ(len, 0);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf[0], '\0');
    free(buf);
}

TEST_F(TraceCombinerTest, RefLifecycle_LastRefReleased_DeletesCombiner)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    {
        TraceCombinerPtr copy = combiner;
        EXPECT_EQ(copy.Get(), combiner.Get());
    }
    Tracepoint out;
    Tracepoint pointB;
    out.id = TEST_TP_ID_1;
    pointB.id = TEST_TP_ID_1;
    EXPECT_EQ(combiner->CombinerTracePoint(out, pointB), UBS_OK);
}

TEST_F(TraceCombinerTest, DecreaseRef_RefCountOne_DeletesSelf)
{
    TraceCombiner *combiner = new TraceCombiner();
    combiner->IncreaseRef();
    combiner->DecreaseRef();
}

TEST_F(TraceCombinerTest, MakeRef_NullCheck_DestroysWithoutCrash)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    combiner = nullptr;
    EXPECT_EQ(combiner.Get(), nullptr);
}

TEST_F(TraceCombinerTest, DecreaseRef_RefCountTwo_NoDeleteThenDelete)
{
    TraceCombiner *combiner = new TraceCombiner();
    for (int i = 0; i < 2; ++i) {
        combiner->IncreaseRef();
    }
    for (int i = 0; i < 2; ++i) {
        combiner->DecreaseRef();
    }
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, DecreaseRef_ChainDecrement_NoDeleteThenDelete)
{
    TraceCombiner *combiner = new TraceCombiner();
    for (int i = 0; i < 4; ++i) {
        combiner->IncreaseRef();
    }
    for (int i = 0; i < 4; ++i) {
        combiner->DecreaseRef();
    }
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, IncreaseDecreaseRef_RoundTrip_StaysAlive)
{
    TraceCombiner *combiner = new TraceCombiner();
    for (int i = 0; i < 3; ++i) {
        combiner->IncreaseRef();
    }
    combiner->DecreaseRef();
    combiner->IncreaseRef();
    combiner->DecreaseRef();
    combiner->DecreaseRef();
    combiner->DecreaseRef();
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, RefLifecycle_CopyAssign_NoDeleteWhileShared)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    TraceCombinerPtr other;
    other = combiner;
    EXPECT_EQ(other.Get(), combiner.Get());
    Tracepoint out;
    Tracepoint pointB;
    out.id = TEST_TP_ID_1;
    pointB.id = TEST_TP_ID_1;
    EXPECT_EQ(combiner->CombinerTracePoint(out, pointB), UBS_OK);
    other = nullptr;
    EXPECT_EQ(combiner->CombinerTracePoint(out, pointB), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, RefLifecycle_CopyAssignReplace_ReleasesOldTarget)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    TraceCombinerPtr other = MakeRef<TraceCombiner>();
    ASSERT_NE(other.Get(), nullptr);
    other = combiner;
    EXPECT_EQ(other.Get(), combiner.Get());
    Tracepoint out;
    Tracepoint pointB;
    out.id = TEST_TP_ID_1;
    pointB.id = TEST_TP_ID_1;
    EXPECT_EQ(combiner->CombinerTracePoint(out, pointB), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, RefLifecycle_MoveAssign_ReleasesOldTarget)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    TraceCombinerPtr other = MakeRef<TraceCombiner>();
    ASSERT_NE(other.Get(), nullptr);
    other = std::move(combiner);
    EXPECT_NE(other.Get(), nullptr);
    Tracepoint out;
    Tracepoint pointB;
    out.id = TEST_TP_ID_1;
    pointB.id = TEST_TP_ID_1;
    EXPECT_EQ(other->CombinerTracePoint(out, pointB), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(TraceCombinerTest, RefLifecycle_MoveConstruct_TransfersOwnership)
{
    TraceCombinerPtr combiner = MakeRef<TraceCombiner>();
    ASSERT_NE(combiner.Get(), nullptr);
    TraceCombinerPtr moved(std::move(combiner));
    EXPECT_NE(moved.Get(), nullptr);
    Tracepoint out;
    Tracepoint pointB;
    out.id = TEST_TP_ID_1;
    pointB.id = TEST_TP_ID_1;
    EXPECT_EQ(moved->CombinerTracePoint(out, pointB), UBS_OK);
    GlobalMockObject::verify();
}

} // namespace
