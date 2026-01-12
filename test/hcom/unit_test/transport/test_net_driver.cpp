// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom.h"
#include "net_common.h"

namespace ock {
namespace hcom {

class TestNetDriver : public testing::Test {
public:
    virtual void SetUp(void)
    {
    }

    virtual void TearDown(void)
    {
        GlobalMockObject::verify();
    }
};

TEST_F(TestNetDriver, VersionParseFailed)
{
    MOCKER(NetFunc::NN_SplitStr).stubs().will(ignoreReturnValue());

    UBSHcomNetDriver *driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "test", false);
    EXPECT_NE(driver, nullptr);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "test", false);
    EXPECT_NE(driver, nullptr);
}
}
}
