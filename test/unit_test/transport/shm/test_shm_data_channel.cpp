/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "shm_data_channel.h"

namespace ock {
namespace hcom {

class TestShmDataChannel : public testing::Test {
public:
    TestShmDataChannel();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestShmDataChannel::TestShmDataChannel()
{}

void TestShmDataChannel::SetUp()
{}

void TestShmDataChannel::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestShmDataChannel, ValidateOptionsNullNameFail)
{
    int ret;
    UBSHcomNetAtomicState<ShmChannelState> state{CH_NEW};
    ShmDataChannelOptions opt(0, NN_NO256, NN_NO16, true);
    ShmDataChannel *dc = new (std::nothrow) ShmDataChannel("", opt, &state);
    ASSERT_NE(dc, nullptr);

    ret = dc->ValidateOptions();
    EXPECT_EQ(ret, SH_PARAM_INVALID);

    if (dc != nullptr) {
        delete dc;
        dc = nullptr;
    }
}

TEST_F(TestShmDataChannel, ValidateOptionsNullStateFail)
{
    int ret;
    ShmDataChannelOptions opt(0, NN_NO256, NN_NO16, true);
    ShmDataChannel *dc = new (std::nothrow) ShmDataChannel("TestShmDataChannel", opt, nullptr);
    ASSERT_NE(dc, nullptr);

    ret = dc->ValidateOptions();
    EXPECT_EQ(ret, SH_PARAM_INVALID);

    if (dc != nullptr) {
        delete dc;
        dc = nullptr;
    }
}

TEST_F(TestShmDataChannel, ValidateOptionsOptionsFail)
{
    int ret;
    UBSHcomNetAtomicState<ShmChannelState> state{CH_NEW};
    ShmDataChannelOptions opt(0, 0, 0, true);
    ShmDataChannel *dc = new (std::nothrow) ShmDataChannel("TestShmDataChannel", opt, &state);
    ASSERT_NE(dc, nullptr);

    ret = dc->ValidateOptions();
    EXPECT_EQ(ret, SH_PARAM_INVALID);

    if (dc != nullptr) {
        delete dc;
        dc = nullptr;
    }
}

TEST_F(TestShmDataChannel, InitializeInvalidOptionFail)
{
    int ret;
    UBSHcomNetAtomicState<ShmChannelState> state{CH_NEW};
    ShmDataChannelOptions opt(0, NN_NO256, NN_NO16, true);
    ShmDataChannel *dc = new (std::nothrow) ShmDataChannel("TestShmDataChannel", opt, &state);
    ASSERT_NE(dc, nullptr);

    MOCKER_CPP(ShmDataChannel::ValidateOptions).stubs().will(returnValue(300));
    ret = dc->Initialize();
    EXPECT_EQ(ret, SH_ERROR);

    if (dc != nullptr) {
        delete dc;
        dc = nullptr;
    }
}

TEST_F(TestShmDataChannel, Initialize)
{
    int ret;
    UBSHcomNetAtomicState<ShmChannelState> state{CH_NEW};
    ShmDataChannelOptions opt(0, NN_NO256, NN_NO16, true);
    ShmDataChannel *dc = new (std::nothrow) ShmDataChannel("TestShmDataChannel", opt, &state);
    ASSERT_NE(dc, nullptr);

    ret = dc->Initialize();
    EXPECT_EQ(ret, SH_OK);
    ret = dc->Initialize();  // Initialize again
    EXPECT_EQ(ret, SH_OK);

    if (dc != nullptr) {
        delete dc;
        dc = nullptr;
    }
}

}  // namespace hcom
}  // namespace ock