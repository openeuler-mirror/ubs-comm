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
#include <semaphore.h>
#include <cstdint>
#include <cstdlib>
#include <mockcpp/mockcpp.hpp>

#include "hcom.h"
#include "net_rdma_async_endpoint.h"
#include "service_channel_imp.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {
class TestNetChannelImp : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

private:
    UBSHcomService *service = nullptr;
    HcomChannelImp *channel = nullptr;
    char *data = nullptr;
    int32_t dataSize = 1024;
    std::vector<UBSHcomNetEndpointPtr> epVector;
    NetMemPoolFixedPtr ctxMemPool = nullptr;
    HcomServiceCtxStorePtr mCtxStore = nullptr;
    HcomPeriodicManagerPtr mPeriodicMgr = nullptr;
    netPgTablePtr mPgtable = nullptr;
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = nullptr;
    NetMemPoolFixedOptions options = {};
    UBSHcomFlowCtrlOptions ctrlOptions{};
};

void TestNetChannelImp::SetUp()
{
    uint64_t id = NN_NO60;
    bool selfPoll = true;
    InnerConnectOptions connectOptions{};
    channel = new HcomChannelImp(id, selfPoll, connectOptions);
    ASSERT_NE(channel, nullptr);
    channel->SetChannelTimeOut(0, 0);

    data = new (std::nothrow) char[dataSize];
    ASSERT_NE(data, nullptr);

    ctxMemPool = new (std::nothrow) NetMemPoolFixed("test", options);
    ASSERT_NE(ctxMemPool, nullptr);
    mCtxStore = new (std::nothrow) HcomServiceCtxStore(1, ctxMemPool, UBSHcomNetDriverProtocol::RDMA);
    ASSERT_NE(mCtxStore, nullptr);
    mPgtable = new NetPgTable(HcomServiceImp::pgdAlloc, HcomServiceImp::pgdFree);
    ASSERT_NE(mPgtable, nullptr);

    mPeriodicMgr = new (std::nothrow) HcomPeriodicManager(1, "mOptions.name");
    epVector.reserve(1);
    workerIndex.Set(NN_NO4, NN_NO6, NN_NO8);
    ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, workerIndex);
    epVector.emplace_back(ep);
}

void TestNetChannelImp::TearDown()
{
    if (data != nullptr) {
        delete[] data;
        data = nullptr;
    }

    if (channel != nullptr) {
        delete channel;
        channel = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestNetChannelImp, TestSendFail)
{
    UBSHcomRequest req(data, sizeof(data), 0);
    MOCKER_CPP(&HcomChannelImp::FlowControl)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Send(req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::SendInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->Send(req, nullptr), SER_NEW_OBJECT_FAILED);

    ASSERT_EQ(channel->Send(req, nullptr), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestSendOK)
{
    UBSHcomRequest req(data, sizeof(data), 0);
    MOCKER_CPP(&HcomChannelImp::FlowControl).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::SendInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Send(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestSendInner)
{
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SendInner(req, nullptr), SER_NOT_ESTABLISHED);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); },
                                            std::placeholders::_1);
    ASSERT_EQ(channel->SendInner(req, callback), SER_INVALID_PARAM);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->SetChannelState(CH_ESTABLISHED);
    ASSERT_EQ(channel->SendInner(req, nullptr), NN_EP_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestCallFail)
{
    UBSHcomRequest req(data, dataSize, 1);
    UBSHcomResponse rsp(data, dataSize);
    MOCKER_CPP(&HcomChannelImp::FlowControl)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Call(req, rsp, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::CallInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->Call(req, rsp, nullptr), SER_NEW_OBJECT_FAILED);

    ASSERT_EQ(channel->Call(req, rsp, nullptr), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestCallOK)
{
    UBSHcomRequest req(data, dataSize, 1);
    UBSHcomResponse rsp(data, dataSize);
    MOCKER_CPP(&HcomChannelImp::FlowControl).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::CallInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Call(req, rsp, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestCallInner)
{
    UBSHcomRequest req(data, dataSize, 1);
    UBSHcomResponse rsp(data, dataSize);
    ASSERT_EQ(channel->CallInner(req, rsp, nullptr), SER_NOT_ESTABLISHED);

    int32_t ret = 0;
    sem_t sem;
    sem_init(&sem, 0, 0);
    Callback *callback = UBSHcomNewCallback(
        [&sem, &ret, &rsp](UBSHcomServiceContext &context) {
            ASSERT_EQ(context.Result(), 0);
            memcpy_s(rsp.address, rsp.size, context.MessageData(), context.MessageDataLen());
            sem_post(&sem);
        },
        std::placeholders::_1);
    ASSERT_EQ(channel->CallInner(req, rsp, callback), SER_INVALID_PARAM);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->SetChannelState(CH_ESTABLISHED);
    ASSERT_EQ(channel->CallInner(req, rsp, nullptr), NN_EP_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestReplyFail)
{
    UBSHcomReplyContext ctx;
    UBSHcomRequest req(data, dataSize, 0);
    MOCKER_CPP(&HcomChannelImp::FlowControl)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Reply(ctx, req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::ReplyInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->Reply(ctx, req, nullptr), SER_NEW_OBJECT_FAILED);

    ASSERT_EQ(channel->Reply(ctx, req, nullptr), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestReplyOK)
{
    UBSHcomReplyContext ctx;
    UBSHcomRequest req(data, dataSize, 0);
    MOCKER_CPP(&HcomChannelImp::FlowControl).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::ReplyInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Reply(ctx, req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestReplyInner)
{
    UBSHcomReplyContext ctx;
    UBSHcomRequest req(data, dataSize, 0);
    ASSERT_EQ(channel->ReplyInner(ctx, req, nullptr), SER_NOT_ESTABLISHED);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); },
                                            std::placeholders::_1);
    ASSERT_EQ(channel->ReplyInner(ctx, req, callback), SER_NOT_ESTABLISHED);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->SetChannelState(CH_ESTABLISHED);
    ASSERT_EQ(channel->ReplyInner(ctx, req, nullptr), SER_NEW_OBJECT_FAILED);
}

TEST_F(TestNetChannelImp, TestPutFail)
{
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP(&HcomChannelImp::FlowControl)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Put(req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::OneSideInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->Put(req, nullptr), SER_NEW_OBJECT_FAILED);

    ASSERT_EQ(channel->Put(req, nullptr), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestPutOK)
{
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP(&HcomChannelImp::FlowControl).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::OneSideInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Put(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestGetFail)
{
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP(&HcomChannelImp::FlowControl)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Get(req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::OneSideInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->Get(req, nullptr), SER_NEW_OBJECT_FAILED);

    ASSERT_EQ(channel->Get(req, nullptr), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestGetOK)
{
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP(&HcomChannelImp::FlowControl).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::OneSideInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Get(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideInner)
{
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;
    ASSERT_EQ(channel->OneSideInner(req, nullptr, true), SER_NOT_ESTABLISHED);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->SetChannelState(CH_ESTABLISHED);
    ASSERT_EQ(channel->OneSideInner(req, nullptr, true), NN_EP_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestInitialize)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);

    ASSERT_EQ(channel->ToString(), "Connect channel id " + std::to_string(NN_NO60) + " with 1 eps :[100]");
    ASSERT_EQ(channel->SetFlowControlConfig(ctrlOptions), SER_OK);
    channel->SetChannelTimeOut(1, 1);
    channel->UnInitialize();
}

TEST_F(TestNetChannelImp, TestInitializeFail)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()), 0, 0), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestOthers)
{
    channel->SetUuid("1");
    ASSERT_EQ(channel->GetUuid(), "1");
    ASSERT_EQ(channel->GetId(), NN_NO60);
    ASSERT_EQ(channel->GetTimerList(), 0);
    ASSERT_EQ(channel->GetDelayEraseTime(), NN_NO1);
    channel->mOptions.brokenPolicy = UBSHcomChannelBrokenPolicy::RECONNECT;
    ASSERT_EQ(channel->GetDelayEraseTime(), NN_NO60);
}

TEST_F(TestNetChannelImp, TestOthers1)
{
    ASSERT_EQ(channel->GetCtxStore(), nullptr);
    ASSERT_EQ(channel->GetCallBackType(), UBSHcomChannelCallBackType::CHANNEL_FUNC_CB);
}
TEST_F(TestNetChannelImp, TestNextWorkerPollEp)
{
    UBSHcomNetEndpoint *nextEp = nullptr;
    ASSERT_EQ(channel->NextWorkerPollEp(nextEp, 0), SER_NOT_ESTABLISHED);
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    ASSERT_EQ(channel->NextWorkerPollEp(nextEp, 0), SER_OK);
}

TEST_F(TestNetChannelImp, TestPrepareTimerCtx)
{
    HcomServiceTimer *serviceTimer = new HcomServiceTimer();
    MOCKER_CPP(&HcomServiceCtxStore::GetCtxObj<HcomServiceTimer>).stubs().will(returnValue(serviceTimer));
    MOCKER_CPP(&HcomServiceCtxStore::PutAndGetSeqNo<HcomServiceTimer>)
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomServiceCtxStore::Return<HcomServiceTimer>).stubs();
    MOCKER_CPP(&HcomPeriodicManager::AddTimer).stubs().will(returnValue(static_cast<int>(SER_OK)));

    TimerCtx TimerCtx{};
    ASSERT_EQ(channel->PrepareTimerContext(nullptr, 0, TimerCtx), 0);
    delete serviceTimer;
}

TEST_F(TestNetChannelImp, TestDestroyTimerCtx)
{
    HcomServiceTimer *timer = new HcomServiceTimer();
    TimerCtx TimerCtx{};
    TimerCtx.timer = timer;
    TimerCtx.timer->IncreaseRef();
    MOCKER_CPP(&HcomServiceTimer::EraseSeqNoWithRet).stubs().will(returnValue(false)).then(returnValue(true));
    EXPECT_NO_FATAL_FAILURE(channel->DestroyTimerContext(TimerCtx));
    EXPECT_NO_FATAL_FAILURE(channel->DestroyTimerContext(TimerCtx));
    delete timer;
}

SerResult MockPrepareTimerCtx(const Callback *cb, int16_t timeout, TimerCtx &context)
{
    if (cb != nullptr) {
        auto *mutableCb = const_cast<Callback *>(cb);
        UBSHcomServiceContext ctx{};
        ctx.mResult = SER_OK;
        mutableCb->Run(ctx);
    }
    return SER_OK;
}

TEST_F(TestNetChannelImp, TestSyncSendInner)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));

    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SyncSendInner(req), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->SyncSendInner(req), SER_OK);

    MOCKER_CPP(&HcomChannelImp::RndvInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    channel->mRndvThreshold = NN_NO10;
    ASSERT_EQ(channel->SyncSendInner(req), SER_OK);
}

TEST_F(TestNetChannelImp, TestAsyncSendInner)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->AsyncSendInner(req, nullptr), SER_NEW_OBJECT_FAILED);
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->AsyncSendInner(req, nullptr), SER_OK);

    MOCKER_CPP(&HcomChannelImp::RndvInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    channel->mRndvThreshold = NN_NO10;
    ASSERT_EQ(channel->AsyncSendInner(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestSyncSendWithSelfPoll)
{
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    channel->mOptions.selfPoll = true;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SyncSendWithSelfPoll(req), SER_OK);
}

void MockSyncCallCbForWorkerPoll(UBSHcomServiceContext &context, UBSHcomResponse *rsp,
                                 HcomServiceSelfSyncParam *syncParam)
{
    syncParam->Result(SER_OK);
    syncParam->Signal();
}

TEST_F(TestNetChannelImp, TestSyncCallInner)
{
    channel->mOptions.selfPoll = false;
    UBSHcomResponse rsp{};
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));

    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SyncCallInner(req, rsp), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->SyncCallInner(req, rsp), SER_INVALID_PARAM);

    MOCKER_CPP(&HcomChannelImp::RndvInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    channel->mRndvThreshold = NN_NO10;
    ASSERT_EQ(channel->SyncCallInner(req, rsp), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestSyncCallWithSelfPoll)
{
    channel->mOptions.selfPoll = true;
    UBSHcomResponse rsp{};
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomRequest req(data, sizeof(data), 0);
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->SyncCallWithSelfPoll(req, rsp), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestSyncCallWithSelfPollFail)
{
    channel->mOptions.selfPoll = true;
    UBSHcomResponse rsp{};
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->mUserSplitSendThreshold = NN_NO10;
    channel->mProtocol = UBC;
    UBSHcomRequest req(data, NN_NO20, 0);

    MOCKER_CPP(&HcomChannelImp::SyncCallSplitWithSelfPoll)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));

    ASSERT_EQ(channel->SyncCallWithSelfPoll(req, rsp), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestAsyncCallInner)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->AsyncCallInner(req, nullptr), SER_NEW_OBJECT_FAILED);
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->AsyncCallInner(req, nullptr), SER_OK);

    MOCKER_CPP(&HcomChannelImp::RndvInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    channel->mRndvThreshold = NN_NO10;
    ASSERT_EQ(channel->AsyncCallInner(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestPrepareCallback)
{
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));
    HcomServiceSelfSyncParam syncParam{};
    TimerCtx syncContext{};
    ASSERT_EQ(channel->PrepareCallback(syncParam, syncContext), SER_NEW_OBJECT_FAILED);
    ASSERT_EQ(channel->PrepareCallback(syncParam, syncContext), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideSyncWithWorkerPoll)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest request{};
    request.lAddress = reinterpret_cast<uintptr_t>(data);
    request.size = dataSize;
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(invoke(MockPrepareTimerCtx));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->OneSideSyncWithWorkerPoll(request, true), SER_OK);
    ASSERT_EQ(channel->OneSideSyncWithWorkerPoll(request, false), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideAsyncWithWorkerPoll)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(invoke(MockPrepareTimerCtx));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); },
                                            std::placeholders::_1);
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, true), SER_OK);
    callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); },
                                  std::placeholders::_1);
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, false), SER_OK);

    MOCKER_CPP(&HcomChannelImp::NextWorkerPollEp).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, false), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestRecvFail)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomServiceContext context{};
    UBSHcomRequest req(data, sizeof(data), 0);
    HcomServiceRndvMessage rndvMessage(NN_NO2, req);
    context.mData = reinterpret_cast<void *>(&rndvMessage);

    context.mDataLen = sizeof(HcomServiceRndvMessage) - NN_NO1;
    uintptr_t address = 0;
    uint32_t size = NN_NO16;
    ASSERT_EQ(channel->Recv(context, address, size), SER_ERROR);

    context.mDataLen = sizeof(HcomServiceRndvMessage);
    ASSERT_EQ(channel->Recv(context, address, size), SER_ERROR);

    address = reinterpret_cast<uintptr_t>(data);
    size = sizeof(data);

    PgtRegion *pgtRegion = nullptr;
    PgtRegion pgtRegion2{};
    pgtRegion2.start = reinterpret_cast<uintptr_t>(data);
    pgtRegion2.end = reinterpret_cast<uintptr_t>(data) + sizeof(data);
    MOCKER_CPP(&PgTable::Lookup).stubs().will(returnValue(pgtRegion)).then(returnValue(&pgtRegion2));
    MOCKER_CPP(&HcomServiceRndvMessage::IsTimeout).stubs().will(returnValue(false));
    ASSERT_EQ(channel->Recv(context, address, size), SER_ERROR);

    MOCKER_CPP_VIRTUAL(*channel, &HcomChannelImp::Get,
                       int32_t(HcomChannelImp::*)(const UBSHcomOneSideRequest &, const Callback *))
        .stubs()
        .will(returnValue(static_cast<int>(SER_ERROR)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->Recv(context, address, size), SER_ERROR);

    ASSERT_EQ(channel->Recv(context, address, size), SER_OK);
}

TEST_F(TestNetChannelImp, TestRndvInnerFail)
{
    UBSHcomTwoSideThreshold threshold{};
    threshold.rndvThreshold = NN_NO1024;
    channel->SetTwoSideThreshold(threshold);
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomRequest req(data, sizeof(data), 0);
    UBSHcomNetTransOpInfo transOp{};

    PgtRegion *pgtRegion = nullptr;
    PgtRegion pgtRegion2{};
    pgtRegion2.start = reinterpret_cast<uintptr_t>(data);
    pgtRegion2.end = reinterpret_cast<uintptr_t>(data) + sizeof(data) - NN_NO1;
    MOCKER_CPP(&PgTable::Lookup).stubs().will(returnValue(pgtRegion)).then(returnValue(&pgtRegion2));

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)))
        .then(returnValue(static_cast<int>(SER_ERROR)));

    ASSERT_EQ(channel->RndvInner(ep.Get(), req, transOp, true), SER_OK);

    ASSERT_EQ(channel->RndvInner(ep.Get(), req, transOp, false), SER_ERROR);

    threshold.rndvThreshold = UINT32_MAX;
    channel->SetTwoSideThreshold(threshold);
}
TEST_F(TestNetChannelImp, TestFlowControl)
{
    ASSERT_EQ(channel->FlowControl(0, 0, 0), SER_OK);
    RateLimiter *limiter = new (std::nothrow) RateLimiter;
    ASSERT_NE(limiter, nullptr);
    channel->mOptions.rateLimit = reinterpret_cast<uintptr_t>(limiter);
    MOCKER_CPP(&RateLimiter::AcquireQuota).stubs().will(returnValue(true)).then(returnValue(false));
    ASSERT_EQ(channel->FlowControl(0, 0, 0), SER_OK);
    MOCKER_CPP(&RateLimiter::InvalidateSize).stubs().will(returnValue(true)).then(returnValue(false));

    ASSERT_EQ(channel->FlowControl(0, 0, 0), SER_INVALID_PARAM);
    MOCKER_CPP(&RateLimiter::WaitUntilNextWindow).stubs();
    MOCKER_CPP(&RateLimiter::BuildNextWindow).stubs();
    ASSERT_EQ(channel->FlowControl(0, 0, 0), SER_TIMEOUT);
    channel->mOptions.rateLimit = 0;
    delete limiter;
}

TEST_F(TestNetChannelImp, TestAcquireQuotaFalse)
{
    RateLimiter *limiter = new (std::nothrow) RateLimiter;
    ASSERT_NE(limiter, nullptr);
    limiter->windowPassedByte = UINT64_MAX;
    limiter->thresholdByte = UINT64_MAX;
    auto ret = limiter->AcquireQuota(NN_NO1024);
    ASSERT_EQ(ret, false);
    delete limiter;
}

TEST_F(TestNetChannelImp, TestAcquireQuotaSuccess)
{
    RateLimiter *limiter = new (std::nothrow) RateLimiter;
    ASSERT_NE(limiter, nullptr);
    limiter->windowPassedByte = NN_NO1024;
    limiter->thresholdByte = UINT64_MAX;
    auto ret = limiter->AcquireQuota(NN_NO1024);
    ASSERT_EQ(ret, true);
    delete limiter;
}

TEST_F(TestNetChannelImp, TestSetFlowControlConfig)
{
    UBSHcomFlowCtrlOptions opt{};
    ASSERT_EQ(channel->SetFlowControlConfig(opt), SER_NOT_ESTABLISHED);
    RateLimiter *limiter = new (std::nothrow) RateLimiter;
    ASSERT_NE(limiter, nullptr);
    channel->mOptions.rateLimit = reinterpret_cast<uintptr_t>(limiter);
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    ASSERT_EQ(channel->SetFlowControlConfig(opt), SER_OK);
    channel->mOptions.rateLimit = 0;
    delete limiter;
}

TEST_F(TestNetChannelImp, TestAllEpBroken)
{
    EpInfo *info = new (std::nothrow) EpInfo;
    channel->mEpInfo = info;
    ASSERT_EQ(channel->AllEpBroken(), true);
    delete info;
    channel->mEpInfo = nullptr;

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    ASSERT_EQ(channel->AllEpBroken(), false);
}

TEST_F(TestNetChannelImp, TestNeedProcessBroken)
{
    ASSERT_EQ(channel->NeedProcessBroken(), true);
}

TEST_F(TestNetChannelImp, TestInvokeChannelBrokenCb)
{
    UBSHcomChannelPtr chPtr = channel;
    chPtr->IncreaseRef();

    EXPECT_NO_FATAL_FAILURE(channel->InvokeChannelBrokenCb(chPtr));
    channel->mOptions.brokenHandler = [](const UBSHcomChannelPtr &ch) {
        printf("enter cb\n");
        return 0;
    };
    EXPECT_NO_FATAL_FAILURE(channel->InvokeChannelBrokenCb(chPtr));
}

TEST_F(TestNetChannelImp, TestProcessIoInBroken)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    EXPECT_NO_FATAL_FAILURE(channel->ProcessIoInBroken());
}

TEST_F(TestNetChannelImp, TestCalculateOffsetAndSize)
{
    UBSHcomOneSideRequest request{};
    request.size = NN_NO1024;
    uint32_t remain = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    channel->mOptions.enableMultiRail = true;
    channel->mOptions.multiRailThresh = NN_NO1;
    EXPECT_NO_FATAL_FAILURE(channel->CalculateOffsetAndSize(request, ep.Get(), remain, offset, size));
}

TEST_F(TestNetChannelImp, TestGetRemoteUdsIdInfo)
{
    UBSHcomNetUdsIdInfo info{};
    EXPECT_EQ(channel->GetRemoteUdsIdInfo(info), static_cast<uint32_t>(SER_ERROR));

    EpInfo *epInfo = new (std::nothrow) EpInfo;
    ASSERT_NE(epInfo, nullptr);
    channel->mEpInfo = epInfo;
    EXPECT_EQ(channel->GetRemoteUdsIdInfo(info), static_cast<uint32_t>(SER_ERROR));

    channel->mEpInfo->epArr[0] = ep.Get();
    EXPECT_EQ(channel->GetRemoteUdsIdInfo(info), static_cast<uint32_t>(NN_EP_NOT_ESTABLISHED));

    channel->mEpInfo->epArr[0] = nullptr;
    channel->mEpInfo = nullptr;
    delete epInfo;
}

TEST_F(TestNetChannelImp, TestSendFds)
{
    EXPECT_EQ(channel->SendFds(nullptr, 0), static_cast<uint32_t>(SER_ERROR));

    EpInfo *epInfo = new (std::nothrow) EpInfo;
    ASSERT_NE(epInfo, nullptr);
    channel->mEpInfo = epInfo;
    EXPECT_EQ(channel->SendFds(nullptr, 0), static_cast<uint32_t>(SER_ERROR));

    channel->mEpInfo->epArr[0] = ep.Get();
    EXPECT_EQ(channel->SendFds(nullptr, 0), static_cast<uint32_t>(NN_EXCHANGE_FD_NOT_SUPPORT));

    channel->mEpInfo->epArr[0] = nullptr;
    channel->mEpInfo = nullptr;
    delete epInfo;
}

TEST_F(TestNetChannelImp, TestReceiveFds)
{
    EXPECT_EQ(channel->ReceiveFds(nullptr, 0, 0), static_cast<uint32_t>(SER_ERROR));

    EpInfo *epInfo = new (std::nothrow) EpInfo;
    ASSERT_NE(epInfo, nullptr);
    channel->mEpInfo = epInfo;
    EXPECT_EQ(channel->ReceiveFds(nullptr, 0, 0), static_cast<uint32_t>(SER_ERROR));

    channel->mEpInfo->epArr[0] = ep.Get();
    EXPECT_EQ(channel->ReceiveFds(nullptr, 0, 0), static_cast<uint32_t>(NN_EXCHANGE_FD_NOT_SUPPORT));

    channel->mEpInfo->epArr[0] = nullptr;
    channel->mEpInfo = nullptr;
    delete epInfo;
}

void MockReleaseSelfPollEp(uint32_t index) {}

TEST_F(TestNetChannelImp, TestSyncSendSplitWithWorkerPoll)
{
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();

    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();
    ASSERT_EQ(channel->SyncSendSplitWithWorkerPoll(tmpEp, req, 1), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->SyncSendSplitWithWorkerPoll(tmpEp, req, 1), SER_INVALID_PARAM);
    ASSERT_EQ(channel->SyncSendSplitWithWorkerPoll(tmpEp, req, 1), SER_OK);
}

TEST_F(TestNetChannelImp, TestSyncSendSplitWithSelfPoll)
{
    MOCKER_CPP(&HcomChannelImp::ReleaseSelfPollEp).stubs().will(invoke(MockReleaseSelfPollEp));
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();
    ASSERT_EQ(channel->SyncSendSplitWithSelfPoll(tmpEp, req, 1, 0), SER_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)))
        .then(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    ASSERT_EQ(channel->SyncSendSplitWithSelfPoll(tmpEp, req, 1, 0), SER_OK);
    ASSERT_EQ(channel->SyncSendSplitWithSelfPoll(tmpEp, req, 1, 0), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestAsyncSendSplitWithWorkerPoll)
{
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();
    ASSERT_EQ(channel->AsyncSendSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->AsyncSendSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_INVALID_PARAM);
    ASSERT_EQ(channel->AsyncSendSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessage)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data;
    uint32_t dataLen;
    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader mHeader;
    mHeader.extHeaderType = UBSHcomExtHeaderType::RAW;

    ctx.mHeader = mHeader;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    ASSERT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_INVALID_PARAM);
}

// SpliceMessage
std::shared_ptr<UBSHcomNetRequestContext> CreateNRC()
{
    alignas(UBSHcomNetMessage) static char msg_buf[sizeof(UBSHcomNetMessage)];
    auto msg = reinterpret_cast<UBSHcomNetMessage *>(msg_buf);

    alignas(UBSHcomNetEndpoint) static char ep_buf[sizeof(UBSHcomNetEndpoint)];
    auto p = reinterpret_cast<UBSHcomNetEndpoint *>(ep_buf);
    // Since we use static memory, no need to free ep_buf.
    p->IncreaseRef();

    auto sp = std::make_shared<UBSHcomNetRequestContext>();
    sp->mMessage = msg;
    sp->mEp = p;
    return sp;
}

// payloadLen = 1
UBSHcomFragmentHeader *GetFragmentHeader(int msgId, int totalLength, int offset)
{
    alignas(UBSHcomFragmentHeader) static char buf[sizeof(UBSHcomFragmentHeader) + 1];

    auto f = reinterpret_cast<UBSHcomFragmentHeader *>(buf);
    f->msgId = {0, msgId};
    f->totalLength = totalLength;
    f->offset = offset;

    return f;
}

// Typically the payload pointer refers the GetFragmentHeader::buf.
template <typename T>
void SetNRCPayload(UBSHcomNetRequestContext &ctx, T *payload, uint32_t sz = sizeof(UBSHcomFragmentHeader) + 1)
{
    ctx.mMessage->mBuf = payload;
    ctx.mMessage->mDataLen = sz;
}

std::shared_ptr<NetAsyncEndpoint> GetNetAsyncEndpoint()
{
    UBSHcomNetWorkerIndex idx;
    auto sp = std::make_shared<NetAsyncEndpoint>(0xdead, nullptr, nullptr, idx);
    return sp;
}

TEST_F(TestNetChannelImp, TestSpliceMessageMsgInvalid)
{
    auto ctx = CreateNRC();
    SetNRCPayload(*ctx, (void *)nullptr, 0);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_SPLIT_INVALID_MSG);
}

TEST_F(TestNetChannelImp, TestSpliceMessageFirstFragmentLost)
{
    auto ctx = CreateNRC();

    // offset = 1, the first fragment (offset=0) is lost.
    auto fh = GetFragmentHeader(0x11, 2, 1);
    SetNRCPayload(*ctx, fh);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_ERROR);
}

namespace internal {
void DoMockThen(mockcpp::MoreStubBuilder<> *builder) {}

template <typename... Ts>
void DoMockThen(mockcpp::MoreStubBuilder<> *builder, SerResult err, Ts... errs)
{
    builder = &builder->then(returnValue(static_cast<SerResult>(err)));
    DoMockThen(builder, errs...);
}
} // namespace internal

template <typename... Ts>
void MockPrepareTimerContext(SerResult err, Ts... errs)
{
    auto builder = MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs();
    auto *b = &builder.will(returnValue(static_cast<SerResult>(err)));
    internal::DoMockThen(b, errs...);
}

TEST_F(TestNetChannelImp, TestSpliceMessageOffsetError)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    // the first fragment of msg (id=0x11), with totalLength = 2
    auto first = GetFragmentHeader(0x11, 2, 0);
    SetNRCPayload(*ctx, first);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    // the second fragment of msg (id=0x11), but one bit of the offset flipped
    auto second = GetFragmentHeader(0x11, 2, 1 + 8);
    SetNRCPayload(*ctx, second);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_SPLIT_INVALID_MSG);
}

TEST_F(TestNetChannelImp, TestSpliceMessageLargePayload)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    // totalLength = 2, but payload length = 0xffff.
    auto first = GetFragmentHeader(0x11, 2, 0);
    SetNRCPayload(*ctx, first, 0xffff);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_SPLIT_INVALID_MSG);
}

template <typename... Ts>
void MockGetSeqNoAndRemove(SerResult err, Ts... errs)
{
    auto builder = MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<HcomServiceTimer>).stubs();
    auto *b = &builder.will(returnValue(static_cast<SerResult>(err)));
    internal::DoMockThen(b, errs...);
}

TEST_F(TestNetChannelImp, TestSpliceMessageOk)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);
    MockGetSeqNoAndRemove(SER_OK);
    MOCKER_CPP(&HcomServiceTimer::MarkFinished).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DecreaseRef).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DeleteCallBack).stubs().will(ignoreReturnValue());

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    auto first = GetFragmentHeader(0x11, 2, 0);
    SetNRCPayload(*ctx, first);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    auto second = GetFragmentHeader(0x11, 2, 1);
    SetNRCPayload(*ctx, second);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::OK);
    EXPECT_EQ(code, SER_OK);
}

TEST_F(TestNetChannelImp, TestSpliceMessageTwo)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);
    MockGetSeqNoAndRemove(SER_OK);
    MOCKER_CPP(&HcomServiceTimer::MarkFinished).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DecreaseRef).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DeleteCallBack).stubs().will(ignoreReturnValue());

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    // message1: totalLength=2, offset=0, payload=1
    auto m1First = GetFragmentHeader(0x11, 2, 0);
    SetNRCPayload(*ctx, m1First);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    // message2: totalLength=2, offset=0, payload=1
    auto m2First = GetFragmentHeader(0x12, 2, 0);
    SetNRCPayload(*ctx, m2First);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    // message1: totalLength=2, offset=1, payload=1
    auto m1Second = GetFragmentHeader(0x11, 2, 1);
    SetNRCPayload(*ctx, m1Second);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::OK);
    EXPECT_EQ(code, SER_OK);

    // message2: totalLength=2, offset=1, payload=1
    auto m2Second = GetFragmentHeader(0x12, 2, 1);
    SetNRCPayload(*ctx, m2Second);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::OK);
    EXPECT_EQ(code, SER_OK);
}

TEST_F(TestNetChannelImp, TestSpliceRespMessageOk)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);
    MockGetSeqNoAndRemove(SER_OK);
    MOCKER_CPP(&HcomServiceTimer::MarkFinished).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DecreaseRef).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomServiceTimer::DeleteCallBack).stubs().will(ignoreReturnValue());

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    auto first = GetFragmentHeader(0x11, 2, 0);
    SetNRCPayload(*ctx, first);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, true);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    auto second = GetFragmentHeader(0x11, 2, 1);
    SetNRCPayload(*ctx, second);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, true);
    EXPECT_EQ(result, SpliceMessageResultType::OK);
    EXPECT_EQ(code, SER_OK);
}

TEST_F(TestNetChannelImp, TestAsyncReplySplitWithWorkerPoll)
{
    UBSHcomReplyContext ctx;

    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    auto tmp = ep.Get();
    UBSHcomRequest req(data, NN_NO65536, 0);
    ASSERT_EQ(channel->AsyncReplySplitWithWorkerPoll(ctx, tmp, req, 1, nullptr), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->AsyncReplySplitWithWorkerPoll(ctx, tmp, req, 1, nullptr), SER_INVALID_PARAM);
    ASSERT_EQ(channel->AsyncReplySplitWithWorkerPoll(ctx, tmp, req, 1, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestSyncReplySplitWithWorkerPoll)
{
    UBSHcomReplyContext ctx;

    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    auto tmp = ep.Get();
    UBSHcomRequest req(data, NN_NO65536, 0);

    ASSERT_EQ(channel->SyncReplySplitWithWorkerPoll(ctx, tmp, req, 1), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->SyncReplySplitWithWorkerPoll(ctx, tmp, req, 1), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestSyncCallSplitWithWorkerPoll)
{
    UBSHcomResponse rsp{};
    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();

    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->SyncCallSplitWithWorkerPoll(tmpEp, req, 1, rsp), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    ASSERT_EQ(channel->SyncCallSplitWithWorkerPoll(tmpEp, req, 1, rsp), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestAsyncCallSplitWithWorkerPoll)
{
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();

    ASSERT_EQ(channel->AsyncCallSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_NEW_OBJECT_FAILED);
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->AsyncCallSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_INVALID_PARAM);
    ASSERT_EQ(channel->AsyncCallSplitWithWorkerPoll(tmpEp, req, 1, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestSyncCallSplitWithSelfPoll)
{
    UBSHcomResponse rsp{};
    UBSHcomRequest req(data, NN_NO65536, 0);
    auto tmpEp = ep.Get();

    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &,
                                         const UBSHcomExtHeaderType, const void *, uint32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));

    ASSERT_EQ(channel->SyncCallSplitWithSelfPoll(tmpEp, req, 1, 0, rsp), SER_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->SyncCallSplitWithSelfPoll(tmpEp, req, 1, 0, rsp), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestSetTraceId)
{
#ifdef build_BUILD_ENABLED
    MOCKER(HcomUrma::IsLoaded).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER(HcomUrma::LogSetThreadTag).stubs().will(ignoreReturnValue());
    std::string traceId = "This is a test trace id";

    EXPECT_NO_FATAL_FAILURE(channel->SetTraceId(traceId));
    EXPECT_NO_FATAL_FAILURE(channel->SetTraceId(traceId));
#endif
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageOne)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data;
    uint32_t dataLen;
    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader mHeader;
    mHeader.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = mHeader;

    UBSHcomFragmentHeader fHeader;
    fHeader.offset = NN_NO100;
    fHeader.totalLength = NN_NO200;
    acc.resize(NN_NO300);
    UBSHcomNetMessage message{};
    message.mBuf = &fHeader;
    message.mDataLen = NN_NO200;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_SPLIT_INVALID_MSG);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

SerResult MockReceiveSetRawAndOk(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    ctx.mHeader.extHeaderType = UBSHcomExtHeaderType::RAW;
    return SER_OK;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageRaw)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::RAW;
    ctx.mHeader = header;

    char payload[8] = {0};
    UBSHcomNetMessage message{};
    message.mBuf = payload;
    message.mDataLen = sizeof(payload);
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_OK);
    EXPECT_EQ(data, static_cast<void *>(payload));
    EXPECT_EQ(dataLen, static_cast<uint32_t>(sizeof(payload)));

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageTooSmall)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    char buf[sizeof(UBSHcomFragmentHeader)] = {0};
    UBSHcomNetMessage message{};
    message.mBuf = buf;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) - 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_ERROR);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageTotalLengthTooLarge)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    alignas(UBSHcomFragmentHeader) char fragBuf[sizeof(UBSHcomFragmentHeader) + 1] = {0};
    auto *frag = reinterpret_cast<UBSHcomFragmentHeader *>(fragBuf);
    frag->msgId = {0, 0x11};
    frag->totalLength = SERVICE_MAX_TOTAL_LENGTH;
    frag->offset = 0;

    UBSHcomNetMessage message{};
    message.mBuf = fragBuf;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) + 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_SPLIT_INVALID_MSG);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageOffsetOverflow)
{
    auto tmpEp = ep.Get();
    std::string acc;
    acc.resize(2);
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    // totalLength 与首包分配的 acc 大小一致，但 offset 越界，视为来自另一条消息
    auto *frag = GetFragmentHeader(0x11, 2, 8);
    UBSHcomNetMessage message{};
    message.mBuf = frag;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) + 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_SPLIT_INVALID_MSG);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageComplete)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    // totalLength = payloadLen = 1，单次收包即可拼完
    auto *frag = GetFragmentHeader(0x11, 1, 0);
    UBSHcomNetMessage message{};
    message.mBuf = frag;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) + 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_OK);
    EXPECT_EQ(dataLen, static_cast<uint32_t>(1));

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageRecvFailMidway)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    // totalLength = 2 但单包只带 1 字节，需要再收一次；第二次收包失败
    auto *frag = GetFragmentHeader(0x11, 2, 0);
    UBSHcomNetMessage message{};
    message.mBuf = frag;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) + 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)))
        .then(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_INVALID_PARAM);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSyncSpliceMessageRawDuringSplice)
{
    auto tmpEp = ep.Get();
    std::string acc;
    void *data = nullptr;
    uint32_t dataLen = 0;

    UBSHcomNetResponseContext ctx;
    UBSHcomNetTransHeader header;
    header.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ctx.mHeader = header;

    auto *frag = GetFragmentHeader(0x11, 2, 0);
    UBSHcomNetMessage message{};
    message.mBuf = frag;
    message.mDataLen = sizeof(UBSHcomFragmentHeader) + 1;
    ctx.mMessage = &message;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::Receive,
                       SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)))
        .then(invoke(MockReceiveSetRawAndOk));

    EXPECT_EQ(SyncSpliceMessage(ctx, tmpEp, 1, acc, data, dataLen), SER_ERROR);

    message.mBuf = nullptr;
    ctx.mMessage = nullptr;
}

TEST_F(TestNetChannelImp, TestSpliceMessageDuplicateId)
{
    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_OK);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    auto first = GetFragmentHeader(0x21, 2, 0);
    SetNRCPayload(*ctx, first);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::INDETERMINATE);
    EXPECT_EQ(code, SER_OK);

    // 同一个 msgId 再次以首包(offset=0)出现，命中去重分支，直接失败
    auto dup = GetFragmentHeader(0x21, 2, 0);
    SetNRCPayload(*ctx, dup);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_ERROR);
}

TEST_F(TestNetChannelImp, TestSpliceMessageTimerCtxFailed)
{
    channel->IncreaseRef();

    auto ctx = CreateNRC();
    MockPrepareTimerContext(SER_NEW_OBJECT_FAILED);

    SpliceMessageResultType result;
    SerResult code;
    std::string out;

    auto first = GetFragmentHeader(0x31, 2, 0);
    SetNRCPayload(*ctx, first);
    std::tie(result, code, out) = channel->SpliceMessage(*ctx, false);
    EXPECT_EQ(result, SpliceMessageResultType::ERROR);
    EXPECT_EQ(code, SER_NEW_OBJECT_FAILED);
}

TEST_F(TestNetChannelImp, TestCallWithHlcAsync)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    // 异步路径不进入同步等待，只需桩住定时器申请与发送
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSendNoCopy,
        SerResult(UBSHcomNetEndpoint::*)(int16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomResponse rsp(data, dataSize);
    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, callback), SER_OK);
    delete callback;
}

TEST_F(TestNetChannelImp, TestCallWithHlcAsyncSendFail)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSendNoCopy,
        SerResult(UBSHcomNetEndpoint::*)(int16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomResponse rsp(data, dataSize);
    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, callback), SER_INVALID_PARAM);
    // PrepareTimerContext 被 mock，回调未被 net 层接管，由用例自行释放
    delete callback;
}

TEST_F(TestNetChannelImp, TestCallWithHlcFail)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_ERROR)));

    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomResponse rsp(data, dataSize);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, nullptr), SER_ERROR);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, callback), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestCallWithHlcSelfPoll)
{
    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomResponse rsp(data, dataSize);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, nullptr), SER_INVALID_PARAM);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->CallWithHlc(req, rsp, callback), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestReplyWithHlc)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    // 异步路径不经过定时器，也不进入同步等待，仅需桩住发送
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSendNoCopy,
        SerResult(UBSHcomNetEndpoint::*)(int16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomReplyContext ctx(NN_NO1, 0);
    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->ReplyWithHlc(ctx, req, callback), SER_OK);
    delete callback;
}

TEST_F(TestNetChannelImp, TestReplyWithHlcFail)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    // 同步路径：定时器申请失败，在进入等待前返回，不会阻塞在回调等待上
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    // 异步路径：不经过定时器，由发送失败返回
    MOCKER_CPP_VIRTUAL(
        *(ep.Get()), &UBSHcomNetEndpoint::PostSendNoCopy,
        SerResult(UBSHcomNetEndpoint::*)(int16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));

    UBSHcomRequest req(data, dataSize, 0);
    UBSHcomReplyContext ctx(NN_NO1, 0);
    ASSERT_EQ(channel->ReplyWithHlc(ctx, req, nullptr), SER_ERROR);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->ReplyWithHlc(ctx, req, callback), SER_INVALID_PARAM);
    delete callback;

    // selfPoll 下 Reply 仅由参数校验记录日志(net_param_validator.h)，不做拦截，因此用
    // ep 异常（被置空）驱动出确定性的失败返回：ResponseWorkerPollEp 在 ep 为空时返回未建链
    channel->mOptions.selfPoll = true;
    channel->mEpInfo->epArr[0] = nullptr;
    SerResult nullEpRet = channel->ReplyWithHlc(ctx, req, nullptr);
    // 先还原 ep，避免断言失败时 TearDown 析构解引用空指针
    channel->mEpInfo->epArr[0] = ep.Get();
    ASSERT_EQ(nullEpRet, SER_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestOneSideSyncWithSelfPoll)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.rAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    ASSERT_EQ(channel->Put(req, nullptr), SER_OK);
    ASSERT_EQ(channel->Get(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideSyncWithSelfPollFail)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest req{};
    req.lAddress = reinterpret_cast<uintptr_t>(data);
    req.rAddress = reinterpret_cast<uintptr_t>(data);
    req.size = dataSize;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->Get(req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->Put(req, nullptr), SER_INVALID_PARAM);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->Put(req, callback), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestOneSideSglWithSelfPoll)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest iov[NN_NO2] = {};
    iov[0].lAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].rAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].size = NN_NO16;
    iov[1] = iov[0];
    UBSHcomOneSideSglRequest req{};
    req.iov = iov;
    req.iovCount = NN_NO2;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));

    ASSERT_EQ(channel->PutV(req, nullptr), SER_OK);
    ASSERT_EQ(channel->GetV(req, nullptr), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideSglWithSelfPollFail)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest iov[NN_NO2] = {};
    iov[0].lAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].rAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].size = NN_NO16;
    iov[1] = iov[0];
    UBSHcomOneSideSglRequest req{};
    req.iov = iov;
    req.iovCount = NN_NO2;

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->GetV(req, nullptr), SER_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->PutV(req, nullptr), SER_INVALID_PARAM);

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    ASSERT_EQ(channel->PutV(req, callback), SER_INVALID_PARAM);

    UBSHcomOneSideSglRequest invalid{};
    ASSERT_EQ(channel->PutV(invalid, nullptr), SER_INVALID_PARAM);
    invalid.iovCount = static_cast<uint16_t>(NET_SGE_MAX_IOV + NN_NO1);
    ASSERT_EQ(channel->OneSideSglInner(invalid, nullptr, true), SER_INVALID_PARAM);
}

TEST_F(TestNetChannelImp, TestOneSideSglSyncWithWorkerPollFail)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest iov[NN_NO1] = {};
    iov[0].lAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].rAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].size = NN_NO16;
    UBSHcomOneSideSglRequest req{};
    req.iov = iov;
    req.iovCount = NN_NO1;

    // 定时器申请失败：同步在进入发送与回调等待前返回，不会阻塞
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->PutV(req, nullptr), SER_ERROR);
    ASSERT_EQ(channel->GetV(req, nullptr), SER_ERROR);

    // ep 异常（被置空）：在申请定时器之前返回
    channel->mEpInfo->epArr[0] = nullptr;
    SerResult nullEpRet = channel->PutV(req, nullptr);
    // 先还原 ep，避免断言失败时 TearDown 析构解引用空指针
    channel->mEpInfo->epArr[0] = ep.Get();
    ASSERT_EQ(nullEpRet, SER_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestOneSideSglAsyncWithWorkerPoll)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    UBSHcomOneSideRequest iov[NN_NO1] = {};
    iov[0].lAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].rAddress = reinterpret_cast<uintptr_t>(data);
    iov[0].size = NN_NO16;
    UBSHcomOneSideSglRequest req{};
    req.iov = iov;
    req.iovCount = NN_NO1;

    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();

    Callback *callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostWrite,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)));
    ASSERT_EQ(channel->PutV(req, callback), SER_INVALID_PARAM);
    delete callback;

    callback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    ASSERT_NE(callback, nullptr);
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostRead,
                       SerResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    ASSERT_EQ(channel->GetV(req, callback), SER_OK);
    delete callback;
}

TEST_F(TestNetChannelImp, TestGetAsyncCBAndProcessRemainCallback)
{
    uint32_t runCount = 0;
    Callback *done =
        UBSHcomNewCallback([&runCount](UBSHcomServiceContext &context) { ++runCount; }, std::placeholders::_1);
    ASSERT_NE(done, nullptr);
    ASSERT_EQ(channel->GetAsyncCB(NN_NO1, done), done);

    Callback *wrapper = channel->GetAsyncCB(NN_NO3, done);
    ASSERT_NE(wrapper, nullptr);
    ASSERT_NE(wrapper, done);
    // AsyncClosureCallback 聚合 multiNum(3) 次 Run 后才触发一次用户回调，并自删除；
    // done 也是自删除回调，因此两者都不需要用例手动释放
    channel->ProcessRemainCallback(wrapper, NN_NO3);
    ASSERT_EQ(runCount, static_cast<uint32_t>(NN_NO1));

    ASSERT_NO_FATAL_FAILURE(channel->ProcessRemainCallback(nullptr, NN_NO1));
}

TEST_F(TestNetChannelImp, TestResponseWorkerPollEp)
{
    UBSHcomNetEndpoint *pollEp = nullptr;
    ASSERT_EQ(channel->ResponseWorkerPollEp(NN_NO1, pollEp), SER_NOT_ESTABLISHED);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    ASSERT_EQ(channel->ResponseWorkerPollEp(static_cast<uintptr_t>(NN_NO8) << 32, pollEp), SER_INVALID_PARAM);
    ASSERT_EQ(channel->ResponseWorkerPollEp(NN_NO1, pollEp), SER_OK);
    ASSERT_EQ(pollEp, ep.Get());

    channel->mEpInfo->epState[0].Set(SER_EP_BROKEN);
    ASSERT_EQ(channel->ResponseWorkerPollEp(NN_NO1, pollEp), SER_NOT_ESTABLISHED);
    channel->mEpInfo->epState[0].Set(SER_EP_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestChannelStatesAndSetters)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_OK);
    channel->SetPayload("channel-payload");
    ASSERT_EQ(channel->GetPeerConnectPayload(), "channel-payload");
    EXPECT_NO_FATAL_FAILURE(channel->GetLocalIp());
    channel->SetBrokenInfo(UBSHcomChannelBrokenPolicy::BROKEN_ALL, nullptr);

    channel->SetEpBroken(NN_NO8);
    ASSERT_TRUE(channel->AllEpEstablished());
    ASSERT_FALSE(channel->AllEpBroken());

    channel->SetEpBroken(NN_NO0);
    ASSERT_FALSE(channel->AllEpEstablished());
    ASSERT_FALSE(channel->AllEpBroken());
    channel->mEpInfo->epState[0].Set(SER_EP_ESTABLISHED);

    channel->UnSetEpUpCtx();
    ASSERT_EQ(ep->UpCtx(), static_cast<uint64_t>(0));
    channel->SetEpUpCtx();
    ASSERT_NE(ep->UpCtx(), static_cast<uint64_t>(0));
}

TEST_F(TestNetChannelImp, TestInitializeWithBrokenEp)
{
    ep->State().Set(NEP_BROKEN);
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
                                  reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
                                  reinterpret_cast<uintptr_t>(mPgtable.Get())),
              SER_EP_BROKEN_DURING_CONNECTING);
    ep->State().Set(NEP_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestCheckAndUpdateThreshold)
{
    MOCKER_CPP(HcomEnv::RndvThreshold)
        .stubs()
        .will(returnValue(NN_NO65536))
        .then(returnValue(NN_NO1024))
        .then(returnValue(NN_NO65536))
        .then(returnValue(NN_NO65536));

    // 使能 split send 且 rndv 阈值不小于 65536：更新 split send 阈值与 rndv 阈值
    setenv("HCOM_ENABLE_SPLIT_SEND", "1", 1);
    channel->mEnableMrCache = true;
    channel->CheckAndUpdateThreshold();
    ASSERT_EQ(channel->mUserSplitSendThreshold,
              static_cast<uint32_t>(NN_NO65536 - sizeof(UBSHcomNetTransHeader) - sizeof(UBSHcomFragmentHeader)));
    ASSERT_EQ(channel->mRndvThreshold, static_cast<uint32_t>(NN_NO65536));

    // rndv 阈值小于 65536：直接返回，不修改任何阈值
    channel->mRndvThreshold = UINT32_MAX;
    channel->CheckAndUpdateThreshold();
    ASSERT_EQ(channel->mRndvThreshold, UINT32_MAX);

    // mEnableMrCache 为 false：仅更新 split send 阈值，rndv 阈值保持不变
    channel->mEnableMrCache = false;
    channel->CheckAndUpdateThreshold();
    ASSERT_EQ(channel->mRndvThreshold, UINT32_MAX);

    // 未使能 split send：rndv 阈值取环境中的配置值
    unsetenv("HCOM_ENABLE_SPLIT_SEND");
    channel->mEnableMrCache = true;
    channel->CheckAndUpdateThreshold();
    ASSERT_EQ(channel->mRndvThreshold, static_cast<uint32_t>(NN_NO65536));
}

} // namespace hcom
} // namespace ock
