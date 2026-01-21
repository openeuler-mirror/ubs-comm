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
#ifdef RDMA_BUILD_ENABLED
#include <thread>

#include "test_negative_rdma_endpoint.h"
#include "mockcpp/mockcpp.hpp"
#include "ut_helper.h"

#define OVERLOAD_CONNECT 64
using namespace ock::hcom;

TestNegativeRdmaEndpoint::TestNegativeRdmaEndpoint() {}

void TestNegativeRdmaEndpoint::SetUp()
{
    MOCK_VERSION
    UTHelper::GetDriver(server, DRIVER_STATE_START, true);
    UTHelper::GetDriver(client, DRIVER_STATE_START, false);
    auto result = client->CreateMemoryRegion(NN_NO1024, clientMr);
    UT_CHECK_RESULT_OK(result)
    result = server->CreateMemoryRegion(NN_NO1024, serverMr);
    UT_CHECK_RESULT_OK(result)
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNegativeRdmaEndpoint::TearDown()
{
    GlobalMockObject::verify();
    UTHelper::ForwardDriverStateMask(server, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UTHelper::ForwardDriverStateMask(client, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UBSHcomNetDriver::DestroyInstance(server->Name());
    UBSHcomNetDriver::DestroyInstance(client->Name());
}

static void InvalidRequestForReadWrite(UBSHcomNetEndpointPtr &ep, UBSHcomNetTransRequest &req)
{
    NResult result = ep->PostWrite(req);
    UT_CHECK_RESULT_NOK(result)

    result = ep->PostRead(req);
    UT_CHECK_RESULT_NOK(result)
}

static void InvalidRequestForSend(UBSHcomNetEndpointPtr &ep, UBSHcomNetTransRequest &req, uint16_t opCode,
    uint32_t seqNo, UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = ep->PostSend(opCode, req);
    UT_CHECK_RESULT_NOK(result)

    result = ep->PostSend(opCode, req, seqNo);
    UT_CHECK_RESULT_NOK(result)

    result = ep->PostSend(opCode, req, opInfo);
    UT_CHECK_RESULT_NOK(result)
}

static void InvalidRequestForReadWriteSgl(UBSHcomNetEndpointPtr &ep, UBSHcomNetTransSglRequest &req)
{
    NResult result = ep->PostWrite(req);
    UT_CHECK_RESULT_NOK(result)

    result = ep->PostRead(req);
    UT_CHECK_RESULT_NOK(result)
}

static void InvalidRequestForSendSgl(UBSHcomNetEndpointPtr &ep, UBSHcomNetTransSglRequest &req, uint32_t seqNo)
{
    NResult result = ep->PostSendRawSgl(req, seqNo);
    UT_CHECK_RESULT_NOK(result)
}

TEST_F(TestNegativeRdmaEndpoint, AsyncEpBadReq)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep);
    ASSERT_NE(ep.Get(), nullptr);
    // invalid sgl test
    UBSHcomNetTransSglRequest reqSgl;
    reqSgl.upCtxSize = sizeof(UBSHcomNetTransSglRequest::upCtxData) + 1;
    UBSHcomNetTransSgeIov iov;
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    reqSgl.iov = &iov;
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    iov.size = 100;
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    reqSgl.iovCount = 1;
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    reqSgl.upCtxSize = 0;
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    iov.lAddress = clientMr->GetAddress();
    InvalidRequestForReadWriteSgl(ep, reqSgl);
    InvalidRequestForSendSgl(ep, reqSgl, 1);

    iov.lKey = clientMr->GetLKey();
    InvalidRequestForReadWriteSgl(ep, reqSgl);

    iov.rAddress = serverMr->GetAddress();
    InvalidRequestForReadWriteSgl(ep, reqSgl);

    // invalid request test
    UBSHcomNetTransRequest req;
    UBSHcomNetTransOpInfo opInfo;
    req.upCtxSize = sizeof(UBSHcomNetTransRequest::upCtxData) + 1;

    InvalidRequestForReadWrite(ep, req);
    InvalidRequestForSend(ep, req, 0, 0, opInfo);

    req.lAddress = clientMr->GetAddress();
    InvalidRequestForReadWrite(ep, req);
    InvalidRequestForSend(ep, req, 0, 0, opInfo);

    req.size = NN_NO100;
    InvalidRequestForReadWrite(ep, req);
    InvalidRequestForSend(ep, req, 0, 0, opInfo);

    req.upCtxSize = 0;
    InvalidRequestForReadWrite(ep, req);

    sem_t sem;
    sem_init(&sem, 0, 0);
    NResult asyncRes = NN_OK;
    // valid lkey rkey success
    client->RegisterOneSideDoneHandler([&](const UBSHcomNetRequestContext &ctx) {
        asyncRes = ctx.Result();
        sem_post(&sem);
        return 0;
    });

    req.rAddress = serverMr->GetAddress();
    req.lKey = clientMr->GetLKey();
    req.rKey = serverMr->GetLKey();
    result = ep->PostWrite(req);
    UT_CHECK_RESULT_OK(result)
    sem_wait(&sem);
    UT_CHECK_RESULT_OK(asyncRes)

    std::string value = "hello world";
    UBSHcomNetTransRequest reqValid((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    result = ep->PostSend(0, reqValid);
    UT_CHECK_RESULT_OK(result)
}

TEST_F(TestNegativeRdmaEndpoint, SyncEpBadReq)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep, NET_EP_SELF_POLLING);

    UBSHcomNetTransRequest onesideReq;
    onesideReq.lAddress = clientMr->GetAddress();
    onesideReq.rAddress = serverMr->GetAddress();
    onesideReq.lKey = 0;
    onesideReq.rKey = 0;
    onesideReq.size = NN_NO100;
    onesideReq.upCtxSize = 0;

    // invalid lkey rkey failed
    result = ep->PostWrite(onesideReq);
    UT_CHECK_RESULT_NOK(result)
    result = ep->WaitCompletion(1);
    UT_CHECK_RESULT_NOK(result)

    result = ep->PostRead(onesideReq);
    UT_CHECK_RESULT_NOK(result)
    result = ep->WaitCompletion(1);
    UT_CHECK_RESULT_NOK(result)

    // valid lkey rkey success
    onesideReq.rKey = serverMr->GetLKey();
    onesideReq.lKey = clientMr->GetLKey();
    result = ep->PostWrite(onesideReq);
    UT_CHECK_RESULT_OK(result)
    result = ep->WaitCompletion(1);
    UT_CHECK_RESULT_OK(result)

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    result = ep->PostSend(0, req);
    UT_CHECK_RESULT_OK(result)

    result = ep->WaitCompletion(1);
    UT_CHECK_RESULT_OK(result)

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(1, respCtx);
    UT_CHECK_RESULT_NOK(result)
}

TEST_F(TestNegativeRdmaEndpoint, AsyncEpUseAfterStopFailed)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    result = ep->PostSend(0, req);
    UT_CHECK_RESULT_OK(result)
    client->Stop();

    result = ep->PostSend(0, req);
    UT_CHECK_RESULT_NOK(result)
}

TEST_F(TestNegativeRdmaEndpoint, SyncEpUseAfterStopFailed)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep, NET_EP_SELF_POLLING);
    client->Stop();

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    result = ep->PostSend(0, req);
    UT_CHECK_RESULT_NOK(result)
    result = ep->WaitCompletion(2);
    UT_CHECK_RESULT_NOK(result)
}

TEST_F(TestNegativeRdmaEndpoint, AsyncOverload)
{
    NResult result;
    for (int i = 0; i < OVERLOAD_CONNECT; ++i) {
        UBSHcomNetEndpointPtr ep;
        result = client->Connect("haha", ep);
        if (result == NN_OK) {
            ASSERT_NE(ep.Get(), nullptr);
            client->Stop();
        }
    }
}

TEST_F(TestNegativeRdmaEndpoint, SyncOverload)
{
    NResult result;
    for (int i = 0; i < OVERLOAD_CONNECT; ++i) {
        UBSHcomNetEndpointPtr ep;
        result = client->Connect("haha", ep, NET_EP_EVENT_POLLING);
        if (result == NN_OK) {
            ASSERT_NE(ep.Get(), nullptr);
        }
    }
}

TEST_F(TestNegativeRdmaEndpoint, AsyncOverloadPost)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep, NET_EP_EVENT_POLLING);
    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    ASSERT_NE(ep.Get(), nullptr);
    for (int i = 0; i < OVERLOAD_CONNECT; ++i) {
        result = ep->PostSend(0, req);
        UT_CHECK_RESULT_OK(result)
    }
}

TEST_F(TestNegativeRdmaEndpoint, SyncOverloadPost)
{
    NResult result;
    UBSHcomNetEndpointPtr ep;
    client->Connect("haha", ep, NET_EP_EVENT_POLLING);
    ASSERT_NE(ep.Get(), nullptr);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    for (int i = 0; i < OVERLOAD_CONNECT; ++i) {
        result = ep->PostSend(0, req);
        UT_CHECK_RESULT_OK(result)
    }
}

#endif