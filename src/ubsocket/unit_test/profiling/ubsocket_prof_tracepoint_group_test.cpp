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

#include <cstring>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_prof_tracepoint_group.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

namespace {

static const uint32_t TEST_TP_COUNT = 4;
static const uint32_t TEST_TP_ID_0 = 0;
static const uint32_t TEST_TP_ID_1 = 1;
static const char *TEST_TP_NAME_0 = "tp0";
static const char *TEST_TP_NAME_1 = "tp1";

class TraceGroupTest : public ::testing::Test {
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

TEST_F(TraceGroupTest, Init_AssignsSequentialIds)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    for (uint32_t i = 0; i < TEST_TP_COUNT; i++) {
        EXPECT_EQ(group.Get(i).id, i);
    }
}

TEST_F(TraceGroupTest, Record_TpIdOutOfRange_ReturnsError)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_COUNT, TEST_TP_NAME_0, 100, true), UBS_ERROR);
}

TEST_F(TraceGroupTest, Record_NullName_ReturnsError)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, nullptr, 100, true), UBS_ERROR);
}

TEST_F(TraceGroupTest, Record_FirstTime_SetsNameAndRecords)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true), UBS_OK);

    Tracepoint tp = group.Get(TEST_TP_ID_0);
    EXPECT_EQ(tp.has_name, 1);
    EXPECT_STREQ(tp.GetName(), TEST_TP_NAME_0);
    EXPECT_EQ(tp.data.success_count, 1);
    EXPECT_EQ(tp.data.failure_count, 0);
    EXPECT_EQ(tp.data.total_time, 100);
}

TEST_F(TraceGroupTest, Record_SecondTime_KeepsExistingName)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, TEST_TP_NAME_0, 50, true), UBS_OK);

    Tracepoint tp = group.Get(TEST_TP_ID_0);
    EXPECT_STREQ(tp.GetName(), TEST_TP_NAME_0);
    EXPECT_EQ(tp.data.success_count, 2);
    EXPECT_EQ(tp.data.total_time, 150);
}

TEST_F(TraceGroupTest, Record_GoodFalse_IncrementsFailure)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, TEST_TP_NAME_0, 100, false), UBS_OK);

    Tracepoint tp = group.Get(TEST_TP_ID_0);
    EXPECT_EQ(tp.data.success_count, 0);
    EXPECT_EQ(tp.data.failure_count, 1);
    EXPECT_EQ(tp.data.total_time, 0);
}

TEST_F(TraceGroupTest, Get_IndexOutOfRange_ReturnsDefault)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);

    Tracepoint tp = group.Get(TEST_TP_COUNT);
    EXPECT_EQ(tp.id, 0);
    EXPECT_EQ(tp.has_name, 0);
    EXPECT_EQ(tp.GetName(), static_cast<const char *>(nullptr));
}

TEST_F(TraceGroupTest, Get_ValidIndex_ReturnsPoint)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_1, TEST_TP_NAME_1, 200, true), UBS_OK);

    Tracepoint tp = group.Get(TEST_TP_ID_1);
    EXPECT_EQ(tp.id, TEST_TP_ID_1);
    EXPECT_STREQ(tp.GetName(), TEST_TP_NAME_1);
}

TEST_F(TraceGroupTest, Reset_ClearsAllPoints)
{
    TraceGroup group(TEST_TP_COUNT);
    EXPECT_EQ(group.Init(), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true), UBS_OK);
    EXPECT_EQ(group.Record(TEST_TP_ID_1, TEST_TP_NAME_1, 50, true), UBS_OK);

    group.Reset();

    EXPECT_EQ(group.Get(TEST_TP_ID_0).data.success_count, 0);
    EXPECT_EQ(group.Get(TEST_TP_ID_0).data.total_time, 0);
    EXPECT_EQ(group.Get(TEST_TP_ID_1).data.failure_count, 0);
}

TEST_F(TraceGroupTest, RefCount_SharedRefs_DecreaseRefKeepsObjectAlive)
{
    TraceGroupPtr p1 = MakeRef<TraceGroup>(TEST_TP_COUNT);
    ASSERT_NE(p1.Get(), nullptr);
    TraceGroup *raw = p1.Get();
    EXPECT_EQ(raw->ref_count_, 1);

    {
        TraceGroupPtr p2 = p1;
        EXPECT_EQ(raw->ref_count_, 2);
    }

    EXPECT_EQ(raw->ref_count_, 1);
}

TEST_F(TraceGroupTest, RefCount_LastRefRelease_DeletesObject)
{
    TraceGroupPtr p1 = MakeRef<TraceGroup>(TEST_TP_COUNT);
    ASSERT_NE(p1.Get(), nullptr);
    EXPECT_EQ(p1.Get()->ref_count_, 1);

    p1 = nullptr;
    EXPECT_EQ(p1.Get(), nullptr);
}

} // namespace
