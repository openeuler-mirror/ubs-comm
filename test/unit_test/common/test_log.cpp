// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
// Author: zhiwei

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_log.h"

using ::testing::StartsWith;

namespace ock {
namespace hcom {
class TestLog : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(TestLog, gettimeofday)
{
    MOCKER_CPP(gettimeofday).stubs().will(returnValue(-1));

    testing::internal::CaptureStdout();
    UBSHcomNetOutLogger::Print(0, "something went wrong");
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_THAT(out, StartsWith("Fail to get the current system time, -1."));
}

TEST_F(TestLog, localtime_r)
{
    MOCKER_CPP(localtime_r).stubs().will(returnValue(static_cast<struct tm *>(nullptr)));

    testing::internal::CaptureStdout();
    UBSHcomNetOutLogger::Print(0, "something went wrong");
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_THAT(out, StartsWith("Invalid time trace"));
}

}  // namespace hcom
}  // namespace ock
