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
#include "shm_channel.h"

namespace ock {
namespace hcom {

class TestShmChannel : public testing::Test {
public:
    TestShmChannel();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestShmChannel::TestShmChannel() {}

void TestShmChannel::SetUp()
{
}

void TestShmChannel::TearDown()
{
    GlobalMockObject::verify();
}

ShmChannel *MockShmChannelGet()
{
    return nullptr;
}
TEST_F(TestShmChannel, ShmChannelCreateAndInit)
{
    int ret;
    ShmChannelPtr ch;

    MOCKER_CPP(&ShmChannelPtr::Get)
        .stubs()
        .will(invoke(MockShmChannelGet));

    ret = ShmChannel::CreateAndInit("ShmChannelCreateAndInit", 0, NN_NO128, NN_NO4, ch);
    EXPECT_EQ(ret, static_cast<int>(SH_NEW_OBJECT_FAILED));
}

TEST_F(TestShmChannel, ShmChannelCreateAndInitTwo)
{
    int ret;
    ShmChannelPtr ch;

    MOCKER_CPP(&ShmChannel::Initialize)
        .stubs()
        .will(returnValue(1));

    ret = ShmChannel::CreateAndInit("ShmChannelCreateAndInit2", 0, NN_NO128, NN_NO4, ch);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmChannel, ShmChannelClose)
{
    int ret;
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("ShmChannelClose", 0, NN_NO128, NN_NO4, ch);

    ch->mFd = -1;
    ch->Close();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestShmChannel, ShmChannelAddMrFd)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelAddMrFd", 0, NN_NO128, NN_NO4, ch);

    ShmChannel::gQueueSizeCap = 0;
    ch->mFd = -1;
    ret = ch->AddMrFd(0);
    EXPECT_EQ(ret, static_cast<int>(SH_FDS_QUEUE_FULL));
}

TEST_F(TestShmChannel, ShmChannelAddUserFds)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelAddUserFds", 0, NN_NO128, NN_NO4, ch);
    // param init
    int fds[4];

    ShmChannel::gQueueSizeCap = 0;
    ret = ch->AddUserFds(fds, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_FDS_QUEUE_FULL));

    ShmChannel::gQueueSizeCap = NN_NO2;
    ret = ch->AddUserFds(fds, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));
}

TEST_F(TestShmChannel, ShmChannelRemoveUserFds)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelRemoveUserFds", 0, NN_NO128, NN_NO4, ch);
    // param init
    int fds[4];

    ch->mUserFdQueue.push(1);
    ret = ch->RemoveUserFds(fds, 1, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));
}

TEST_F(TestShmChannel, ShmChannelRemoveUserFdsTwo)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelRemoveUserFds2", 0, NN_NO128, NN_NO4, ch);
    // param init
    int fds[4];

    MOCKER_CPP(NetMonotonic::TimeUs)
        .stubs()
        .will(returnValue(static_cast<uint64_t>(0)))
        .then(returnValue(static_cast<uint64_t>(NN_NO2 * NN_NO1000000)));
    ch->mUserFdQueue.push(1);

    ret = ch->RemoveUserFds(fds, NN_NO2, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_TIME_OUT));
}

TEST_F(TestShmChannel, ShmChannelGetCtxPosted)
{
    int ret;
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("ShmChannelGetCtxPosted", 0, NN_NO128, NN_NO4, ch);
    // param init
    ShmOpContextInfo *remaining;

    ch->GetCtxPosted(remaining);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestShmChannel, ShmChannelGetCompPosted)
{
    int ret;
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("ShmChannelGetCompPosted", 0, NN_NO128, NN_NO4, ch);
    // param init
    ShmOpCompInfo *remaining;

    ch->GetCompPosted(remaining);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestShmChannel, ShmChannelGValidateExchangeInfo)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelGValidateExchangeInfo", 0, NN_NO128, NN_NO4, ch);
    // param init
    ShmConnExchangeInfo info{};

    info.qCapacity = 0;
    ret = ch->ValidateExchangeInfo(info);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    info.qCapacity = 1;
    info.queueFd = 1;
    info.qName[0] = 0;
    ret = ch->ValidateExchangeInfo(info);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmChannel, ShmChannelGValidateExchangeInfoTwo)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelGValidateExchangeInfoTwo", 0, NN_NO128, NN_NO4, ch);
    // param init
    ShmConnExchangeInfo info{};

    info.qCapacity = 1;
    info.queueFd = 1;
    strncpy_s(info.qName, NN_NO32, "test", 1);
    info.dcBuckCount = 0;
    ret = ch->ValidateExchangeInfo(info);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    info.dcBuckCount = 1;
    info.dcBuckSize = 1;
    info.dcName[0] = 0;
    ret = ch->ValidateExchangeInfo(info);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmChannel, ShmChannelGValidateExchangeInfoThree)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmChannelGValidateExchangeInfoThree", 0, NN_NO128, NN_NO4, ch);
    // param init
    ShmConnExchangeInfo info{};

    info.qCapacity = 1;
    info.queueFd = 1;
    strncpy_s(info.qName, NN_NO32, "test", 1);
    info.dcBuckCount = 1;
    info.dcBuckSize = 1;
    strncpy_s(info.dcName, NN_NO64, "test", 1);
    info.channelId = 0;
    ret = ch->ValidateExchangeInfo(info);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}
}
}