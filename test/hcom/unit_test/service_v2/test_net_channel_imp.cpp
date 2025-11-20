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
#include <cstdint>
#include <semaphore.h>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom.h"
#include "service_channel_imp.h"
#include "net_rdma_async_endpoint.h"
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

    Callback *callback = UBSHcomNewCallback([]
        (UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); }, std::placeholders::_1);
    ASSERT_EQ(channel->SendInner(req, callback), SER_INVALID_PARAM);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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

    Callback *callback = UBSHcomNewCallback([]
        (UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); }, std::placeholders::_1);
    ASSERT_EQ(channel->ReplyInner(ctx, req, callback), SER_NOT_ESTABLISHED);

    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    channel->SetChannelState(CH_ESTABLISHED);
    ASSERT_EQ(channel->OneSideInner(req, nullptr, true), NN_EP_NOT_ESTABLISHED);
}

TEST_F(TestNetChannelImp, TestInitialize)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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

    TimerCtx TimerCtx {};
    ASSERT_EQ(channel->PrepareTimerContext(nullptr, 0, TimerCtx), 0);
    delete serviceTimer;
}

TEST_F(TestNetChannelImp, TestDestroyTimerCtx)
{
    HcomServiceTimer *timer = new HcomServiceTimer();
    TimerCtx TimerCtx {};
    TimerCtx.timer = timer;
    TimerCtx.timer->IncreaseRef();
    MOCKER_CPP(&HcomServiceTimer::EraseSeqNoWithRet).stubs().will(returnValue(false)).then(returnValue(true));
    EXPECT_NO_FATAL_FAILURE(channel->DestroyTimerContext(TimerCtx));
    EXPECT_NO_FATAL_FAILURE(channel->DestroyTimerContext(TimerCtx));
    delete timer;
}

SerResult MockPrepareTimerCtx(Callback *cb, int16_t timeout, TimerCtx &context)
{
    if (cb != nullptr) {
        UBSHcomServiceContext ctx {};
        ctx.mResult = SER_OK;
        cb->Run(ctx);
    }
    return SER_OK;
}

TEST_F(TestNetChannelImp, TestSyncSendInner)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));

    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SyncSendInner(req), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->AsyncSendInner(req, nullptr), SER_NEW_OBJECT_FAILED);
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
        SerResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::WaitCompletion, SerResult(UBSHcomNetEndpoint::*)(int32_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    channel->mOptions.selfPoll = true;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(invoke(MockPrepareTimerCtx));

    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->SyncCallInner(req, rsp), SER_NEW_OBJECT_FAILED);

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    UBSHcomRequest req(data, sizeof(data), 0);
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

TEST_F(TestNetChannelImp, TestAsyncCallInner)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    MOCKER_CPP(&HcomChannelImp::PrepareTimerContext)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomChannelImp::DestroyTimerContext).stubs();
    UBSHcomRequest req(data, sizeof(data), 0);
    ASSERT_EQ(channel->AsyncCallInner(req, nullptr), SER_NEW_OBJECT_FAILED);
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
    HcomServiceSelfSyncParam syncParam {};
    TimerCtx syncContext {};
    ASSERT_EQ(channel->PrepareCallback(syncParam, syncContext), SER_NEW_OBJECT_FAILED);
    ASSERT_EQ(channel->PrepareCallback(syncParam, syncContext), SER_OK);
}

TEST_F(TestNetChannelImp, TestOneSideSyncWithWorkerPoll)
{
    channel->mOptions.selfPoll = false;
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    UBSHcomOneSideRequest request {};
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    UBSHcomOneSideRequest req {};
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
    Callback *callback = UBSHcomNewCallback([]
        (UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); }, std::placeholders::_1);
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, true), SER_OK);
    callback = UBSHcomNewCallback([]
        (UBSHcomServiceContext &context) { ASSERT_EQ(context.Result(), 0); }, std::placeholders::_1);
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, false), SER_OK);

    MOCKER_CPP(&HcomChannelImp::NextWorkerPollEp).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    ASSERT_EQ(channel->OneSideAsyncWithWorkerPoll(req, callback, false), SER_ERROR);
}

TEST_F(TestNetChannelImp, TestRecvFail)
{
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    UBSHcomRequest req(data, sizeof(data), 0);
    UBSHcomNetTransOpInfo transOp{};

    PgtRegion *pgtRegion = nullptr;
    PgtRegion pgtRegion2{};
    pgtRegion2.start = reinterpret_cast<uintptr_t>(data);
    pgtRegion2.end = reinterpret_cast<uintptr_t>(data) + sizeof(data) - NN_NO1;
    MOCKER_CPP(&PgTable::Lookup).stubs().will(returnValue(pgtRegion)).then(returnValue(&pgtRegion2));

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
    UBSHcomFlowCtrlOptions opt {};
    ASSERT_EQ(channel->SetFlowControlConfig(opt), SER_NOT_ESTABLISHED);
    RateLimiter *limiter = new (std::nothrow) RateLimiter;
    ASSERT_NE(limiter, nullptr);
    channel->mOptions.rateLimit = reinterpret_cast<uintptr_t>(limiter);
    ASSERT_EQ(channel->Initialize(epVector, reinterpret_cast<uintptr_t>(ctxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
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
        reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()), reinterpret_cast<uintptr_t>(mPgtable.Get())),
        SER_OK);
    EXPECT_NO_FATAL_FAILURE(channel->ProcessIoInBroken());
}

TEST_F(TestNetChannelImp, TestCalculateOffsetAndSize)
{
    UBSHcomOneSideRequest request {};
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
    UBSHcomNetUdsIdInfo info {};
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
        SerResult(UBSHcomNetEndpoint::*)(int32_t, UBSHcomNetResponseContext&))
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
template<typename T> void SetNRCPayload(UBSHcomNetRequestContext &ctx, T *payload,
    uint32_t sz = sizeof(UBSHcomFragmentHeader) + 1)
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
void DoMockThen(mockcpp::MoreStubBuilder<> *builder)
{
}

template<typename... Ts> void DoMockThen(mockcpp::MoreStubBuilder<> *builder, SerResult err, Ts... errs)
{
    builder = &builder->then(returnValue(static_cast<SerResult>(err)));
    DoMockThen(builder, errs...);
}
}  // namespace internal

template<typename... Ts> void MockPrepareTimerContext(SerResult err, Ts... errs)
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

template<typename... Ts> void MockGetSeqNoAndRemove(SerResult err, Ts... errs)
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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
    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

    MOCKER_CPP_VIRTUAL(*(ep.Get()), &UBSHcomNetEndpoint::PostSend,
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

}  // namespace HCOM
}  // namespace OCK
