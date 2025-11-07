// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mockcpp/mockcpp.hpp>

#include "htracer_manager.h"

namespace ock {
namespace hcom {
class TestHTracerManager : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(TestHTracerManager, TestCreateInstanceSuccess)
{
    TraceManager traceManager {};
    EXPECT_NE(traceManager.CreateInstance(), nullptr);
}

TEST_F(TestHTracerManager, TestCreateInstanceMemSetFailed)
{
    TraceManager traceManager {};
    MOCKER_CPP(memset_s).stubs().will(returnValue(1)).then(returnValue(-1));
    EXPECT_EQ(traceManager.CreateInstance(), nullptr);
}

TEST_F(TestHTracerManager, DumpTraceSplitInfoSuccess)
{
    std::string tpName = "test_tracer_manager";
    uint64_t diff = 1;
    int32_t retCode = 0;
    TraceManager::DumpTraceSplitInfo(tpName, diff, retCode);
    ASSERT_EQ(TraceManager::mDumpEnable, false);
}
} // namespace hcom
} // namespace ock
