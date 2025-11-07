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
#include <sys/epoll.h>
#include "shm_channel.h"
#include "shm_handle_fds.h"
#include "shm_channel_keeper.h"

namespace ock {
namespace hcom {

class TestShmChannelKeeper : public testing::Test {
public:
    TestShmChannelKeeper();
    virtual void SetUp(void);
    virtual void TearDown(void);

    ShmChannelKeeper *keeper = nullptr;
};

TestShmChannelKeeper::TestShmChannelKeeper()
{}

void TestShmChannelKeeper::SetUp()
{
    keeper = new (std::nothrow) ShmChannelKeeper("channel_keeper", 0);
    ASSERT_NE(keeper, nullptr);
}

void TestShmChannelKeeper::TearDown()
{
    if (keeper != nullptr) {
        delete keeper;
        keeper = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmChannelKeeper, StartMessageHandlerFail)
{
    int ret = keeper->Start();
    EXPECT_EQ(ret, SH_PARAM_INVALID);
}

TEST_F(TestShmChannelKeeper, Stop)
{
    keeper->Stop();
}

TEST_F(TestShmChannelKeeper, AddShmChannelEpollInFail)
{
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch.Get()->mFd = 0;

    int ret = keeper->AddShmChannel(ch);
    EXPECT_EQ(ret, SH_CH_ADD_FAILURE_IN_KEEPER);
}

TEST_F(TestShmChannelKeeper, RemoveShmChannelFail)
{
    int ret = keeper->RemoveShmChannel(0);
    EXPECT_EQ(ret, SH_CH_REMOVE_FAILURE_IN_KEEPER);
}

TEST_F(TestShmChannelKeeper, RemoveShmChannelSuccess)
{
    uint64_t channelId = 123;
    ShmChannelPtr channel;
    ShmChannel::CreateAndInit("TestShmChannelKeeper", channelId, NN_NO128, NN_NO4, channel);
    keeper->mShmChannels[channelId] = channel;
    MOCKER_CPP(&epoll_ctl).stubs().will(returnValue(0));
    int ret = keeper->RemoveShmChannel(channelId);
    EXPECT_EQ(ret, SH_OK);
    channel.Set(nullptr);
}

TEST_F(TestShmChannelKeeper, ExchangeFdProcessFail)
{
    ShmChKeeperMsgHeader header;
    header.msgType = EXCHANGE_USER_FD;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    int ret = keeper->ExchangeFdProcess(header, ch);
    EXPECT_EQ(ret, SH_ERROR);

    MOCKER_CPP(ShmHandleFds::ReceiveMsgFds).stubs().will(returnValue(0));
    MOCKER_CPP(ShmChannel::AddUserFds).stubs().will(returnValue(300));
    ret = keeper->ExchangeFdProcess(header, ch);
    EXPECT_EQ(ret, SH_ERROR);
}

TEST_F(TestShmChannelKeeper, ExchangeFdProcess)
{
    ShmChKeeperMsgHeader header;
    header.msgType = EXCHANGE_USER_FD;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    MOCKER_CPP(ShmHandleFds::ReceiveMsgFds).stubs().will(returnValue(0));
    int ret = keeper->ExchangeFdProcess(header, ch);
    EXPECT_EQ(ret, SH_OK);
}

}  // namespace hcom
}  // namespace ock