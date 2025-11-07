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

#include "shm_worker.h"
#include "shm_handle.h"
#include "shm_queue.h"
#include "shm_common.h"

namespace ock {
namespace hcom {
class TestShmWorker : public testing::Test {
public:
    TestShmWorker();
    virtual void SetUp(void);
    virtual void TearDown(void);
    ShmWorker *worker = nullptr;
    ShmChannel *ch = nullptr;
    std::string name = "TestShmWorker";
    UBSHcomNetWorkerIndex index{};
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
};

TestShmWorker::TestShmWorker() {}

void TestShmWorker::SetUp()
{
    worker = new (std::nothrow) ShmWorker(name, index, options, opMemPool, opCtxMemPool, sglOpMemPool);
    ch = new (std::nothrow) ShmChannel(name, 0, NN_NO128, NN_NO4);
}

void TestShmWorker::TearDown()
{
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }

    if (ch != nullptr) {
        delete ch;
        ch = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmWorker, InitializeInitedFail)
{
    worker->mInited = true;
    HResult res = worker->Initialize();
    EXPECT_EQ(res, SH_OK);
}

TEST_F(TestShmWorker, InitializeValidateFail)
{
    worker->mInited = false;
    MOCKER_CPP(&ShmWorker::Validate).stubs().will(returnValue(1));
    HResult res = worker->Initialize();
    EXPECT_EQ(res, 1);
}

TEST_F(TestShmWorker, InitializeCreateEventQueueFail)
{
    worker->mInited = false;
    MOCKER_CPP(&ShmWorker::Validate).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmWorker::CreateEventQueue).stubs().will(returnValue(1));
    HResult res = worker->Initialize();
    EXPECT_EQ(res, 1);
}

TEST_F(TestShmWorker, CreateEventQueueAlreadyCreatedFail)
{
    worker->CreateEventQueue();
    HResult res = worker->CreateEventQueue();
    EXPECT_EQ(res, SH_ERROR);

    worker->mEventQueue->DecreaseRef();
}

TEST_F(TestShmWorker, CreateEventQueueGetPtrFail)
{
    worker->mEventQueue = nullptr;
    ShmEventQueue *eventQueueNullPtr = nullptr;
    ShmHandle *handleNullPtr = nullptr;
    MOCKER_CPP(&ShmEventQueuePtr::Get).stubs().will(returnValue(eventQueueNullPtr));

    HResult res = worker->CreateEventQueue();
    EXPECT_EQ(res, SH_NEW_OBJECT_FAILED);

    MOCKER_CPP(&ShmHandlePtr::Get).stubs().will(returnValue(handleNullPtr));
    res = worker->CreateEventQueue();
    EXPECT_EQ(res, SH_NEW_OBJECT_FAILED);
}

TEST_F(TestShmWorker, RunInThreadBusyPoll)
{
    worker->mOptions.threadPriority = 1;
    worker->mOptions.mode = SHM_BUSY_POLLING;
    MOCKER_CPP(&setpriority).stubs().will(returnValue(1));
    MOCKER_CPP(&ShmWorker::DoBusyPolling).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(worker->RunInThread(-1));
}

TEST_F(TestShmWorker, StartNotInitedErr)
{
    worker->mInited = false;
    HResult res = worker->Start();
    EXPECT_EQ(res, SH_ERROR);
}

TEST_F(TestShmWorker, StartAlreadyStarted)
{
    worker->mInited = true;
    worker->mStarted = true;

    HResult res = worker->Start();
    EXPECT_EQ(res, SH_OK);
}

TEST_F(TestShmWorker, StartNewRequestHandlerNull)
{
    worker->mInited = true;
    worker->mStarted = false;
    worker->mNewRequestHandler = nullptr;
    HResult res = worker->Start();
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, StartSendPostedHandlerNull)
{
    ShmNewReqHandler shmNewReqHandler{};
    worker->mInited = true;
    worker->mStarted = false;
    worker->RegisterNewReqHandler(shmNewReqHandler);
    worker->mSendPostedHandler = nullptr;
    HResult res = worker->Start();
    EXPECT_EQ(res, SH_PARAM_INVALID);
    worker->mNewRequestHandler = nullptr;
}

TEST_F(TestShmWorker, StartOneSideDoneHandlerNull)
{
    ShmNewReqHandler shmNewReqHandler{};
    ShmPostedHandler shmPostedHandler{};
    worker->mInited = true;
    worker->mStarted = false;
    worker->RegisterNewReqHandler(shmNewReqHandler);
    worker->RegisterReqPostedHandler(shmPostedHandler);
    worker->mOneSideDoneHandler = nullptr;
    HResult res = worker->Start();
    EXPECT_EQ(res, SH_PARAM_INVALID);
    worker->mNewRequestHandler = nullptr;
    worker->mSendPostedHandler = nullptr;
}

TEST_F(TestShmWorker, FillSglCtxNullErr)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);

    HResult res = worker->FillSglCtx(nullptr, sglReq);
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, FillSglCtxCopyErr)
{
    ShmSglOpContextInfo sglCtx{};
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 1);
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    HResult res = worker->FillSglCtx(&sglCtx, sglReq);
    EXPECT_EQ(res, SH_PARAM_INVALID);

    res = worker->FillSglCtx(&sglCtx, sglReq);
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, SendLocalEvent)
{
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_SEND;
    worker->mOptions.mode = SHM_BUSY_POLLING;

    MOCKER_CPP(&ShmEventQueue::Enqueue)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(1));

    HResult res = worker->SendLocalEvent(0, ch, type);
    EXPECT_EQ(res, 1);
    ch = nullptr;
}

TEST_F(TestShmWorker, SendLocalEventQueueFull)
{
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_SEND;
    worker->mOptions.mode = SHM_BUSY_POLLING;

    MOCKER_CPP(&ShmEventQueue::Enqueue)
        .stubs()
        .will(returnValue(-1));

    worker->mDefaultTimeout = 0;
    HResult res = worker->SendLocalEvent(0, ch, type);
    EXPECT_EQ(res, SH_SEND_COMPLETION_CALLBACK_FAILURE);
    ch = nullptr;
}

TEST_F(TestShmWorker, PostSendRawSglParamErr)
{
    UBSHcomNetTransRequest req{};
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;

    HResult res = worker->PostSendRawSgl(ch, req, sglReq, 0, 0, -1);
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, PostSendRawSglChBroken)
{
    UBSHcomNetTransRequest req{};
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    ch->mState.Set(ShmChannelState::CH_BROKEN);

    HResult res = worker->PostSendRawSgl(ch, req, sglReq, 0, 0, -1);
    EXPECT_EQ(res, SH_CH_BROKEN);
}

TEST_F(TestShmWorker, PostSendRawSglRetryFull)
{
    UBSHcomNetTransRequest req{};
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    MOCKER_CPP(&ShmChannel::EQEventEnqueue).stubs().will(returnValue(-1));

    HResult res = worker->PostSendRawSgl(ch, req, sglReq, 0, 0, -1);
    EXPECT_EQ(res, SH_RETRY_FULL);
}

TEST_F(TestShmWorker, PostReadWriteParamErr)
{
    UBSHcomNetTransRequest req{};
    ShmMRHandleMap map;
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_READ;
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;

    HResult res = worker->PostReadWrite(ch, req, map, type);
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, PostReadWriteChBroken)
{
    UBSHcomNetTransRequest req{};
    ShmMRHandleMap map;
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_READ;
    ch->mState.Set(ShmChannelState::CH_BROKEN);

    HResult res = worker->PostReadWrite(ch, req, map, type);
    EXPECT_EQ(res, SH_CH_BROKEN);
}

TEST_F(TestShmWorker, RegisterIdleHandler)
{
    ShmIdleHandler h{};
    EXPECT_NO_FATAL_FAILURE(worker->RegisterIdleHandler(h));
    worker->mIdleHandler = nullptr;
}

TEST_F(TestShmWorker, GetFinishTime)
{
    worker->mDefaultTimeout = 1;
    EXPECT_NO_FATAL_FAILURE(worker->GetFinishTime());
    worker->mDefaultTimeout = 0;
    EXPECT_EQ(worker->GetFinishTime(), 0);
}

TEST_F(TestShmWorker, NeedRetry)
{
    HResult result = -1;
    bool res = worker->NeedRetry(result, ch);
    EXPECT_EQ(res, true);

    result = 0;
    res = worker->NeedRetry(result, ch);
    EXPECT_EQ(res, false);

    ch->mState.Set(ShmChannelState::CH_BROKEN);
    res = worker->NeedRetry(result, ch);
    EXPECT_EQ(res, false);
}

TEST_F(TestShmWorker, PostSendParamErr)
{
    UBSHcomNetTransRequest req{};
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;

    HResult res = worker->PostSend(ch, req, 0, 0, -1);
    EXPECT_EQ(res, SH_PARAM_INVALID);
}

TEST_F(TestShmWorker, PostSendChBroken)
{
    UBSHcomNetTransRequest req{};
    ch->mState.Set(ShmChannelState::CH_BROKEN);

    HResult res = worker->PostSend(ch, req, 0, 0, -1);
    EXPECT_EQ(res, SH_CH_BROKEN);
}

TEST_F(TestShmWorker, PostSendRetryFull)
{
    UBSHcomNetTransRequest req{};
    MOCKER_CPP(&ShmChannel::EQEventEnqueue).stubs().will(returnValue(-1));

    HResult res = worker->PostSend(ch, req, 0, 0, -1);
    EXPECT_EQ(res, SH_RETRY_FULL);
}
}
}