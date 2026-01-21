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
#include "shm_composed_endpoint.h"
#include "shm_queue.h"
#include "shm_mr_pool.h"

namespace ock {
namespace hcom {
class TestShmComposedEndpoint : public testing::Test {
public:
    TestShmComposedEndpoint();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestShmComposedEndpoint::TestShmComposedEndpoint() {}

void TestShmComposedEndpoint::SetUp()
{
}

void TestShmComposedEndpoint::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointCreate)
{
    int ret;
    // ShmChannel create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmSyncEndpointCreate", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;

    MOCKER_CPP(&ShmSyncEndpoint::CreateEventQueue)
        .stubs()
        .will(returnValue(1));

    ret = ShmSyncEndpoint::Create("ShmSyncEndpointCreate", NN_NO128, SHM_EVENT_POLLING, shmEp);
    EXPECT_EQ(ret, 1);
}


TEST_F(TestShmComposedEndpoint, ShmSyncEndpointCreateFail)
{
    int ret;
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint *nullEp = nullptr;

    MOCKER_CPP(&ShmSyncEndpointPtr::Get)
        .stubs()
        .will(returnValue(nullEp));

    ret = ShmSyncEndpoint::Create("ShmSyncEndpointCreate", NN_NO128, SHM_EVENT_POLLING, shmEp);
    EXPECT_EQ(ret, static_cast<int>(SH_NEW_OBJECT_FAILED));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointCreateEventQueueFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostSend", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    ShmHandle *nullShmHandle = nullptr;
    ShmEventQueue *nullEventQueue = nullptr;

    MOCKER_CPP(&ShmEventQueuePtr::Get)
        .stubs()
        .will(returnValue(nullEventQueue));
    ret = ep->CreateEventQueue();
    EXPECT_EQ(ret, static_cast<int>(SH_NEW_OBJECT_FAILED));

    MOCKER_CPP(&ShmHandlePtr::Get)
        .stubs()
        .will(returnValue(nullShmHandle));
    ret = ep->CreateEventQueue();
    EXPECT_EQ(ret, static_cast<int>(SH_NEW_OBJECT_FAILED));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostSend)
{
    int ret;
    // ShmChannel create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmSyncEndpointPostSend", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostSend", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();

    UBSHcomNetTransRequest req{};
    uint64_t offset = 0;
    uint32_t immData = 0;
    int32_t defaultTimeout = 0;
    UBSHcomNetTransHeader header{};

    MOCKER_CPP(&ShmChannel::EQEventEnqueue)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER_CPP(&ShmEventQueue::EnqueueAndNotify)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1))
        .then(returnValue(1));

    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ret = ep->PostSend(ch.Get(), req, offset, immData, defaultTimeout);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx);
    req.lAddress = reinterpret_cast<uintptr_t>(&header);
    ret = ep->PostSend(ch.Get(), req, offset, immData, defaultTimeout);
    EXPECT_EQ(ret, static_cast<int>(SH_RETRY_FULL));

    ret = ep->PostSend(ch.Get(), req, offset, immData, defaultTimeout);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));

    ret = ep->PostSend(ch.Get(), req, offset, immData, defaultTimeout);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    ret = ep->PostSend(ch.Get(), req, offset, immData, defaultTimeout);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointFillSglCtx)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointFillSglCtx", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // request create
    ShmSglOpContextInfo sglCtx{};
    UBSHcomNetTransSglRequest request{};
    int data;
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&data);
    request.upCtxSize = 1;

    ret = ep->FillSglCtx(nullptr, request);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    ret = ep->FillSglCtx(&sglCtx, request);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));
    ret = ep->FillSglCtx(&sglCtx, request);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    ret = ep->FillSglCtx(&sglCtx, request);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostSendRawSgl)
{
    int ret;
    // ShmChannel create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmSyncEndpointPostSendRawSgl", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostSendRawSgl", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // request create
    UBSHcomNetTransRequest req{};
    UBSHcomNetTransSglRequest request{};
    int data;
    UBSHcomNetTransHeader header{};
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&data);
    request.upCtxSize = 1;
    req.lAddress = reinterpret_cast<uintptr_t>(&header);

    MOCKER_CPP(&ShmChannel::EQEventEnqueue)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER_CPP(&ShmEventQueue::EnqueueAndNotify)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1))
        .then(returnValue(1));

    request.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ret = ep->PostSendRawSgl(ch.Get(), req, request, 0, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));

    request.upCtxSize = sizeof(ShmOpContextInfo::upCtx);
    ret = ep->PostSendRawSgl(ch.Get(), req, request, 0, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_RETRY_FULL));

    ret = ep->PostSendRawSgl(ch.Get(), req, request, 0, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));

    ret = ep->PostSendRawSgl(ch.Get(), req, request, 0, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    ret = ep->PostSendRawSgl(ch.Get(), req, request, 0, 0, 0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointSendLocalEventForOneSideDone)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointSendLocalEventForOneSideDone", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    ShmOpContextInfo ctx{};
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_READ;

    MOCKER_CPP(&ShmEventQueue::EnqueueAndNotify)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1))
        .then(returnValue(1));

    ret = ep->SendLocalEventForOneSideDone(&ctx, type);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));

    ret = ep->SendLocalEventForOneSideDone(&ctx, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointReceive)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointReceive", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    ShmOpContextInfo opCtx{};
    uint32_t immData = 0;

    MOCKER_CPP(&ShmSyncEndpoint::DequeueEvent)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(1));

    ret = ep->Receive(0, opCtx, immData);
    EXPECT_EQ(ret, 1);

    ret = ep->Receive(0, opCtx, immData);
    EXPECT_EQ(ret, static_cast<int>(SH_ERROR));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointDequeueEvent)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointDequeueEvent", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    ShmEvent opEvent{};

    MOCKER_CPP(&ShmEventQueue::DequeueOrWait)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(1));

    ret = ep->DequeueEvent(0, opEvent);
    EXPECT_EQ(ret, 1);

    ret = ep->DequeueEvent(0, opEvent);
    EXPECT_EQ(ret, static_cast<int>(SH_OK));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostRead)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostRead", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    UBSHcomNetTransRequest req{};
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ShmMRHandleMap mrHandleMap{};

    ret = ep->PostRead(0, req, mrHandleMap);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostReadTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostRead2", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    UBSHcomNetTransSglRequest req{};
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ShmMRHandleMap mrHandleMap{};

    ret = ep->PostRead(0, req, mrHandleMap);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostWrite)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostWrite", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    UBSHcomNetTransRequest req{};
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ShmMRHandleMap mrHandleMap{};

    ret = ep->PostWrite(0, req, mrHandleMap);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, ShmSyncEndpointPostWriteTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("ShmSyncEndpointPostWrite2", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param create
    UBSHcomNetTransSglRequest req{};
    req.upCtxSize = sizeof(ShmOpContextInfo::upCtx) + 1;
    ShmMRHandleMap mrHandleMap{};

    ret = ep->PostWrite(0, req, mrHandleMap);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, SyncReadWriteProcess)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("SyncReadWriteProcess", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReadWriteProcess", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::SH_SEND;
    ShmHandlePtr localMrHandle = nullptr;
    UBSHcomNetTransRequest req{};

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));

    ret = ep->PostReadWrite(ch.Get(), req, mrHandleMap, type);
    EXPECT_EQ(ret, static_cast<int>(SH_ERROR));
}

TEST_F(TestShmComposedEndpoint, SyncReadWriteProcessTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("SyncReadWriteProcess2", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReadWriteProcess2", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::SH_SEND;
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("SyncReadWriteProcess2", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle;
    UBSHcomNetTransRequest req{};

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&ShmChannel::GetRemoteMrHandle)
        .stubs()
        .will(returnValue(1));

    ret = ep->PostReadWrite(ch.Get(), req, mrHandleMap, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, SyncReadWriteProcessLkeyFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("SyncReadWriteProcess", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReadWriteProcess", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::SH_SEND;
    ShmHandlePtr localMrHandle = nullptr;
    UBSHcomNetTransRequest req{};
    req.lKey  = UINT64_MAX;
    ret = ep->PostReadWrite(ch.Get(), req, mrHandleMap, type);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, PostReadWriteCopyFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("PostReadWriteCopyFail", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("PostReadWriteCopyFail", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_READ;
    UBSHcomNetTransRequest req{};
    req.upCtxSize  = 1;
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("localMrHandle", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle = new (std::nothrow) ShmHandle("remoteMrHandle", "", 0, 0, false);
    ASSERT_NE(remoteMrHandle, nullptr);

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    ret = ep->PostReadWrite(ch.Get(), req, mrHandleMap, type);
    EXPECT_EQ(ret, static_cast<int>(SH_PARAM_INVALID));
}

TEST_F(TestShmComposedEndpoint, PostReadWriteSendLocalEventFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("PostReadWriteSendLocalEventFail", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("PostReadWriteSendLocalEventFail", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_READ;
    UBSHcomNetTransRequest req{};
    req.upCtxSize  = 0;
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("localMrHandle", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle = new (std::nothrow) ShmHandle("remoteMrHandle", "", 0, 0, false);
    ASSERT_NE(remoteMrHandle, nullptr);

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::SendLocalEventForOneSideDone).stubs().will(returnValue(1));

    ret = ep->PostReadWrite(ch.Get(), req, mrHandleMap, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, PostReadWriteSglSyncReadWriteProcessFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("PostReadWriteSglSyncReadWriteProcessFail", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("PostReadWriteSglSyncReadWriteProcessFail", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_SGL_READ;
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("localMrHandle", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle;

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&ShmChannel::GetRemoteMrHandle)
        .stubs()
        .will(returnValue(1));

    ret = ep->PostReadWriteSgl(ch.Get(), sglReq, mrHandleMap, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, PostReadWriteSglFillSglCtxFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("PostReadWriteSglFillSglCtxFail", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("PostReadWriteSglFillSglCtxFail", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_SGL_WRITE;
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("localMrHandle", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle = new (std::nothrow) ShmHandle("remoteMrHandle", "", 0, 0, false);
    ASSERT_NE(remoteMrHandle, nullptr);

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::FillSglCtx).stubs().will(returnValue(1));
    ret = ep->PostReadWriteSgl(ch.Get(), sglReq, mrHandleMap, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, PostReadWriteSglSendLocalEventFail)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpointPtr shmEp;
    ShmSyncEndpoint::Create("PostReadWriteSglSendLocalEventFail", NN_NO128, SHM_EVENT_POLLING, shmEp);
    ShmSyncEndpoint *ep = shmEp.Get();
    // param init
    ShmMRHandleMap mrHandleMap{};
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("PostReadWriteSglSendLocalEventFail", 0, NN_NO128, NN_NO4, ch);
    ShmOpContextInfo::ShmOpType type = ShmOpContextInfo::ShmOpType::SH_SGL_READ;
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    ShmHandlePtr localMrHandle = new (std::nothrow) ShmHandle("localMrHandle", "", 0, 0, false);
    ASSERT_NE(localMrHandle, nullptr);
    ShmHandlePtr remoteMrHandle = new (std::nothrow) ShmHandle("remoteMrHandle", "", 0, 0, false);
    ASSERT_NE(remoteMrHandle, nullptr);

    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap)
        .stubs()
        .will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmMRHandleMap::GetFromRemoteMap)
        .stubs()
        .will(returnValue(remoteMrHandle));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::FillSglCtx).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::SendLocalEventForOneSideDone).stubs().will(returnValue(1));
    ret = ep->PostReadWriteSgl(ch.Get(), sglReq, mrHandleMap, type);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmComposedEndpoint, CreateEventQueueFail)
{
    int ret;
    ShmSyncEndpoint *ep = new ShmSyncEndpoint("shm", 0, SHM_EVENT_POLLING);
    EXPECT_NE(ep->CreateEventQueue(), SH_OK);
 
    delete ep;
}

TEST_F(TestShmComposedEndpoint, ShmMemoryRegionCreateFail)
{
    int ret;
    ShmMemoryRegion *mr = nullptr;
    
    ret = ShmMemoryRegion::Create("shmMr", 0, mr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
 
    ret = ShmMemoryRegion::Create("shmMr", 0, 0, mr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}
}
}