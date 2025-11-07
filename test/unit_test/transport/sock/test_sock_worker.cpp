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

#include "sock_worker.h"

namespace ock {
namespace hcom {
class TestSockWorker : public testing::Test {
public:
    TestSockWorker();
    virtual void SetUp(void);
    virtual void TearDown(void);
    SockWorker *mSockWorker = nullptr;
    Sock *mSock = nullptr;
};

TestSockWorker::TestSockWorker() {}

void TestSockWorker::SetUp()
{
    SockType mT = SOCK_UDS;
    std::string mName = "TestSockWorker";
    UBSHcomNetWorkerIndex mIndex{};
    NetMemPoolFixedPtr mOpCtxMemPool;
    NetMemPoolFixedPtr mSglCtxMemPool;
    NetMemPoolFixedPtr mHeaderReqMemPool;
    SockWorkerOptions mSockWorkerOptions{};
    mSockWorker = new (std::nothrow)
        SockWorker(mT, mName, mIndex, mOpCtxMemPool, mSglCtxMemPool, mHeaderReqMemPool, mSockWorkerOptions);
    ASSERT_TRUE(mSockWorker != nullptr);
    uint64_t mId = 1;
    int mFd = -1;
    SockOptions mSockOptions{};
    mSock = new (std::nothrow) Sock(mT, mName, mId, mFd, mSockOptions);
    ASSERT_TRUE(mSock != nullptr);
}

void TestSockWorker::TearDown()
{
    if (mSock != nullptr) {
        delete mSock;
        mSock = nullptr;
    }

    if (mSockWorker != nullptr) {
        delete mSockWorker;
        mSockWorker = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestSockWorker, TestInitializeInitedFail)
{
    mSockWorker->mInited = true;
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, SS_OK);
}

TEST_F(TestSockWorker, TestInitializeValidateFail)
{
    MOCKER_CPP(mSockWorker->Validate).stubs().will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestInitializeOpCtxMemPoolFail)
{
    MOCKER_CPP(&OpContextInfoPool<SockOpContextInfo>::Initialize,
        NResult(OpContextInfoPool<SockOpContextInfo>::*)(const NetMemPoolFixedPtr &, const UBSHcomNetDriverProtocol))
        .stubs()
        .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestInitializeSglCtxMemPoolFail)
{
    MOCKER_CPP(&OpContextInfoPool<SockSglContextInfo>::Initialize,
        NResult(OpContextInfoPool<SockSglContextInfo>::*)(const NetMemPoolFixedPtr &, const UBSHcomNetDriverProtocol))
        .stubs()
        .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestInitializeHeaderReqMemPoolFail)
{
    mSockWorker->mOptions.tcpSendZCopy = true;
    MOCKER_CPP(&OpContextInfoPool<SockHeaderReqInfo>::Initialize,
        NResult(OpContextInfoPool<SockHeaderReqInfo>::*)(const NetMemPoolFixedPtr &, const UBSHcomNetDriverProtocol))
        .stubs()
        .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestStartNotInitializedFail)
{
    mSockWorker->mInited = false;
    SResult res = mSockWorker->Start();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestStartStartedFail)
{
    mSockWorker->mInited = true;
    mSockWorker->mStarted = true;
    SResult res = mSockWorker->Start();
    EXPECT_EQ(res, static_cast<SResult>(SS_OK));
}

TEST_F(TestSockWorker, TestStartNewRequestHandlerFail)
{
    mSockWorker->mInited = true;
    mSockWorker->mStarted = false;
    mSockWorker->mNewRequestHandler = nullptr;
    SResult res = mSockWorker->Start();
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
}

TEST_F(TestSockWorker, TestStartSendPostedHandlerFail)
{
    mSockWorker->mInited = true;
    mSockWorker->mStarted = false;
    mSockWorker->mSendPostedHandler = nullptr;
    SResult res = mSockWorker->Start();
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
}

TEST_F(TestSockWorker, TestStartOneSideDoneHandlerFail)
{
    mSockWorker->mInited = true;
    mSockWorker->mStarted = false;
    mSockWorker->mOneSideDoneHandler = nullptr;
    SResult res = mSockWorker->Start();
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
}

TEST_F(TestSockWorker, TestPostSendSockAddQueueFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(false));
    SResult res = mSockWorker->PostSend(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
}

TEST_F(TestSockWorker, TestPostSendNoCtxLeftFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(true));
    SResult res = mSockWorker->PostSend(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_CTX_FULL));
}

TEST_F(TestSockWorker, TestPostSendRawSglSockAddQueueFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransSglRequest mReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(false));
    SResult res = mSockWorker->PostSendRawSgl(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
}

TEST_F(TestSockWorker, TestPostSendRawSglNoCtxLeftFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransSglRequest mReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(true));
    SResult res = mSockWorker->PostSendRawSgl(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_CTX_FULL));
}

TEST_F(TestSockWorker, TestPostReadSockAddQueueFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    UBSHcomNetTransSglRequest mSglReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(false));
    SResult res = mSockWorker->PostRead(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
    res = mSockWorker->PostRead(mSock, mHeader, mSglReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
}

TEST_F(TestSockWorker, TestPostReadNoCtxLeftFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    UBSHcomNetTransSglRequest mSglReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(true));
    SResult res = mSockWorker->PostRead(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
    res = mSockWorker->PostRead(mSock, mHeader, mSglReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_CTX_FULL));
}

TEST_F(TestSockWorker, TestPostWriteSockAddQueueFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    UBSHcomNetTransSglRequest mSglReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(false));
    SResult res = mSockWorker->PostWrite(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
    res = mSockWorker->PostWrite(mSock, mHeader, mSglReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_ADD_QUEUE_FAILED));
}

TEST_F(TestSockWorker, TestPostWriteNoCtxLeftFail)
{
    SockTransHeader mHeader{};
    UBSHcomNetTransRequest mReq;
    UBSHcomNetTransSglRequest mSglReq;
    MOCKER_CPP(mSock->GetQueueSpace).stubs().will(returnValue(true));
    SResult res = mSockWorker->PostWrite(mSock, mHeader, mReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_CTX_FULL));
    res = mSockWorker->PostWrite(mSock, mHeader, mSglReq);
    EXPECT_EQ(res, static_cast<SResult>(SS_CTX_FULL));
}

TEST_F(TestSockWorker, TestAddToEpollInvalidFdFail)
{
    MOCKER_CPP(&Sock::FD).stubs().will(returnValue(INVALID_FD));
    SResult res = mSockWorker->AddToEpoll(mSock, 1);
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
}

TEST_F(TestSockWorker, TestAddToEpollEpollFail)
{
    MOCKER_CPP(&Sock::FD).stubs().will(returnValue(1));
    MOCKER_CPP(&epoll_ctl).stubs().will(returnValue(1));
    SResult res = mSockWorker->AddToEpoll(mSock, 1);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_EPOLL_OP_FAILED));
}

TEST_F(TestSockWorker, TestModifyInEpollInvalidFdFail)
{
    MOCKER_CPP(&Sock::FD).stubs().will(returnValue(INVALID_FD));
    SResult res = mSockWorker->ModifyInEpoll(mSock, 1);
    EXPECT_EQ(res, static_cast<SResult>(SS_PARAM_INVALID));
}

TEST_F(TestSockWorker, TestModifyInEpollEpollFail)
{
    MOCKER_CPP(&Sock::FD).stubs().will(returnValue(1));
    MOCKER_CPP(&epoll_ctl).stubs().will(returnValue(1));
    errno = ENOENT;
    SResult res = mSockWorker->ModifyInEpoll(mSock, 1);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_EPOLL_OP_FAILED));

    errno = 0;
    res = mSockWorker->ModifyInEpoll(mSock, 1);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_EPOLL_OP_FAILED));
}

TEST_F(TestSockWorker, TestRemoveFromEpollEpollFail)
{
    MOCKER_CPP(&Sock::FD).stubs().will(returnValue(1));
    MOCKER_CPP(&epoll_ctl).stubs().will(returnValue(1));
    errno = ENOENT;
    SResult res = mSockWorker->RemoveFromEpoll(mSock);
    EXPECT_EQ(res, static_cast<SResult>(SS_OK));

    errno = 0;
    res = mSockWorker->RemoveFromEpoll(mSock);
    EXPECT_EQ(res, static_cast<SResult>(SS_SOCK_EPOLL_OP_FAILED));
}

TEST_F(TestSockWorker, TestTCPInitializeOpCtxMemPoolFail)
{
    mSockWorker->mType = SOCK_TCP;
    MOCKER_CPP(&OpContextInfoPool<SockOpContextInfo>::Initialize,
               NResult(OpContextInfoPool<SockOpContextInfo>::*)(const NetMemPoolFixedPtr &))
            .stubs()
            .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestTCPInitializeSglCtxMemPoolFail)
{
    mSockWorker->mType = SOCK_TCP;
    MOCKER_CPP(&OpContextInfoPool<SockSglContextInfo>::Initialize,
               NResult(OpContextInfoPool<SockSglContextInfo>::*)(const NetMemPoolFixedPtr &))
            .stubs()
            .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, TestTCPInitializeHeaderReqMemPoolFail)
{
    mSockWorker->mType = SOCK_TCP;
    mSockWorker->mOptions.tcpSendZCopy = true;
    MOCKER_CPP(&OpContextInfoPool<SockHeaderReqInfo>::Initialize,
               NResult(OpContextInfoPool<SockHeaderReqInfo>::*)(const NetMemPoolFixedPtr &))
            .stubs()
            .will(returnValue(static_cast<SResult>(SS_ERROR)));
    SResult res = mSockWorker->Initialize();
    EXPECT_EQ(res, static_cast<SResult>(SS_ERROR));
}

TEST_F(TestSockWorker, CheckIovLen)
{
    uint16_t iovCount = 0;
    SockOpContextInfo opCtx{};
    opCtx.dataSize = 0;
    EXPECT_EQ(mSockWorker->CheckIovLen(opCtx, iovCount), false);

    opCtx.dataSize = sizeof(UBSHcomNetTransSglRequest::iovCount);
    opCtx.dataAddress = reinterpret_cast<uintptr_t>(&iovCount);
    EXPECT_EQ(mSockWorker->CheckIovLen(opCtx, iovCount), false);

    iovCount = 1;
    EXPECT_EQ(mSockWorker->CheckIovLen(opCtx, iovCount), false);
}

TEST_F(TestSockWorker, PostReadAck)
{
    SockOpContextInfo opCtx{};
    opCtx.sock = mSock;
    mSock->mQueueVacantSize = NN_NO100;
    mSock->UpContext(NN_NO1);
    opCtx.dataSize = 0;
    EXPECT_EQ(mSockWorker->PostReadAck(opCtx), SS_PARAM_INVALID);
}

TEST_F(TestSockWorker, PostWriteAck)
{
    SockOpContextInfo opCtx{};
    opCtx.sock = mSock;
    mSock->mQueueVacantSize = NN_NO100;
    mSock->UpContext(NN_NO1);
    opCtx.dataSize = 0;
    EXPECT_EQ(mSockWorker->PostWriteAck(opCtx), SS_PARAM_INVALID);
}

TEST_F(TestSockWorker, PostWriteSglAck)
{
    SockOpContextInfo opCtx{};
    opCtx.sock = mSock;
    mSock->UpContext(NN_NO1);
    mSock->mQueueVacantSize = NN_NO100;
    opCtx.dataSize = 0;
    MOCKER_CPP(&SockWorker::CheckIovLen).stubs().will(returnValue(1));
    EXPECT_EQ(mSockWorker->PostWriteSglAck(opCtx), SS_PARAM_INVALID);
}
}
}