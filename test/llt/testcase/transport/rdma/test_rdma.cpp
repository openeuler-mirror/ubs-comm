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
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "test_rdma.hpp"
#include "string.h"
#include "hcom.h"
#include "common/net_util.h"
#include "transport/rdma/verbs/net_rdma_sync_endpoint.h"
#include "transport/rdma/verbs/net_rdma_async_endpoint.h"
#include "transport/rdma/rdma_mr_dm_buf.h"
#include "transport/rdma/rdma_mr_fixed_buf.h"
#include "transport/rdma/verbs/rdma_worker.h"
#include "fake_ibv.h"
#include "transport/rdma/verbs/net_rdma_driver.h"
#include "ut_helper.h"

TestCaseRdma::TestCaseRdma() {}

void TestCaseRdma::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestCaseRdma::TearDown()
{
    GlobalMockObject::verify();
}

using namespace ock::hcom;
#ifdef MOCK_VERBS
#ifdef __cplusplus
extern "C" {
#endif
int fake_ibv_post_send(fake_qp_t *my_qp, struct ibv_send_wr *wr);
int fake_post_read(fake_qp_t *my_qp, struct ibv_send_wr *wr);
int fake_post_write(fake_qp_t *my_qp, struct ibv_send_wr *wr);
#ifdef __cplusplus
}
#endif
#endif
// cpp case
using TestOpCode = enum {
    GET_MR = 1,
    CHECK_SYNC_RESPONSE,
    SEND_RAW,
    RECEIVE_RAW,
    POST_SEND_FAIL,
    SET_MR,
};

#define CHECK_RESULT_TRUE(result) \
    EXPECT_EQ(true, result);      \
    if (!result) {                \
        return;                   \
    }

#define CLEAN_UP_ALL_STUBS() GlobalMockObject::verify()

constexpr uint64_t SYNC_SEND_VALUE = 0xffff0000;
constexpr uint64_t SYNC_RECEIVE_VALUE = 0x0000ffff;
constexpr uint64_t ASYNC_RW_COUNT = 4;
constexpr uint64_t RDMA_LISTEN_PORT = 22222;
// server
UBSHcomNetDriver *serverDriver = nullptr;

std::string ipSeg = IP_SEG;

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));
TestRegMrInfo serverLocalMrInfo[4];

int ServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    return 0;
}

void ServerEndPointBroken(const UBSHcomNetEndpointPtr &serverEp)
{
    NN_LOG_TRACE_INFO("end point " << serverEp->Id());
}

int ServerRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    std::string req((char *)ctx.Message()->Data(), ctx.Header().dataLength);
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED) {
        if (ctx.Header().opCode == GET_MR) {
            UBSHcomNetTransRequest rsp((void *)(serverLocalMrInfo), sizeof(serverLocalMrInfo), 0);
            if ((result = ctx.EndPoint()->PostSend(ctx.Header().opCode, rsp)) != 0) {
                NN_LOG_ERROR("failed to post message to data to server, result " << result);
                return result;
            }

            NN_LOG_INFO("request rsp Mr info");
            for (uint16_t i = 0; i < 4; i++) {
                NN_LOG_TRACE_INFO("idx:" << i << " key:" << serverLocalMrInfo[i].lKey << " address:" <<
                    serverLocalMrInfo[i].lAddress << " size" << serverLocalMrInfo[i].size);
            }
        } else if (ctx.Header().opCode == CHECK_SYNC_RESPONSE) {
            uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ctx.Message()->Data()));
            EXPECT_EQ(SYNC_SEND_VALUE, *readValue);

            uint64_t rspData = SYNC_RECEIVE_VALUE;
            UBSHcomNetTransRequest rsp((void *)(&rspData), sizeof(rspData), 0);
            if ((result = ctx.EndPoint()->PostSend(ctx.Header().opCode, rsp)) != 0) {
                NN_LOG_ERROR("failed to post message to data to server, result " << result);
                return result;
            }
        } else if (ctx.Header().opCode == SET_MR) {
            for (uint16_t i = 0; i < 4; i++) {
                memset(reinterpret_cast<void *>(serverLocalMrInfo[i].lAddress), 0, NN_NO16);
            }
            uint64_t rspData = 0;
            UBSHcomNetTransRequest rsp((void *)(&rspData), sizeof(rspData), 0);
            if ((result = ctx.EndPoint()->PostSend(ctx.Header().opCode, rsp)) != 0) {
                NN_LOG_ERROR("failed to post message to data to server, result " << result);
                return result;
            }
        }
    } else if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED_RAW) {
        int32_t *readValue = reinterpret_cast<int32_t *>((void *)(ctx.Message()->Data()));
        EXPECT_EQ(SEND_RAW, *readValue);
        int32_t *localAddress = reinterpret_cast<int32_t *>(serverLocalMrInfo[0].lAddress);
        *localAddress = RECEIVE_RAW;
        UBSHcomNetTransRequest req((void *)(serverLocalMrInfo[0].lAddress), NN_NO4, 0);
        if ((result = ctx.EndPoint()->PostSendRaw(req, 1)) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return result;
        }
    }

    return 0;
}

int ServerRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_TRACE_INFO("request posted");
    return 0;
}

int ServerOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_TRACE_INFO("one side done");
    return 0;
}


bool ServerCreateDriver()
{
    if (serverDriver != nullptr) {
        NN_LOG_ERROR("serverDriver already created");
        return false;
    }

    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaServer", true);
    if (serverDriver == nullptr) {
        NN_LOG_ERROR("failed to create serverDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING; // 只支持EVENT模式
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 1024;
    options.enableTls = false;
    options.SetNetDeviceIpMask(ipSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);
    options.prePostReceiveSizePerQP = 32;

    serverDriver->RegisterNewEPHandler(
        std::bind(&ServerNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    serverDriver->RegisterEPBrokenHandler(std::bind(&ServerEndPointBroken, std::placeholders::_1));
    serverDriver->RegisterNewReqHandler(std::bind(&ServerRequestReceived, std::placeholders::_1));
    serverDriver->RegisterReqPostedHandler(std::bind(&ServerRequestPosted, std::placeholders::_1));
    serverDriver->RegisterOneSideDoneHandler(std::bind(&ServerOneSideDone, std::placeholders::_1));

    serverDriver->OobIpAndPort(BASE_IP, RDMA_LISTEN_PORT);

    int result = 0;
    if ((result = serverDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize serverDriver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver initialized");

    if ((result = serverDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start serverDriver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver started");

    return true;
}

bool ServerRegSglMem()
{
    for (uint16_t i = 0; i < 4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = serverDriver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        serverLocalMrInfo[i].lAddress = mr->GetAddress();
        serverLocalMrInfo[i].lKey = mr->GetLKey();
        serverLocalMrInfo[i].size = NN_NO16;
        memset(reinterpret_cast<void *>(serverLocalMrInfo[i].lAddress), 0, NN_NO16);
    }

    return true;
}

// client
UBSHcomNetDriver *clientDriver = nullptr;
UBSHcomNetEndpointPtr clientAsyncEp = nullptr;
UBSHcomNetEndpointPtr clientSyncEp = nullptr;
TestRegMrInfo localMrInfo[NN_NO4];
TestRegMrInfo remoteMrInfo[NN_NO4];
sem_t sem;

uint32_t execCount = 0;

void ClientEndPointBroken(const UBSHcomNetEndpointPtr &clientEp)
{
    if (clientSyncEp != nullptr && clientEp->Id() == clientSyncEp->Id()) {
        NN_LOG_INFO("client sync end point " << clientEp->Id() << " broken");
        clientSyncEp.Set(nullptr);
    } else if (clientAsyncEp != nullptr && clientEp->Id() == clientAsyncEp->Id()) {
        NN_LOG_INFO("client async end point " << clientEp->Id() << " broken");
        clientAsyncEp.Set(nullptr);
    }
}

int ClientRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED) {
        if (ctx.Header().opCode == GET_MR) {
            memcpy(remoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
            NN_LOG_INFO("get remote Mr info");
            for (uint16_t i = 0; i < NN_NO4; i++) {
                NN_LOG_TRACE_INFO("idx:" << i << " key:" << remoteMrInfo[i].lKey << " address:" <<
                    remoteMrInfo[i].lAddress << " size" << remoteMrInfo[i].size);
            }
            sem_post(&sem);
        } else if (ctx.Header().opCode == CHECK_SYNC_RESPONSE) {
            uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ctx.Message()->Data()));
            EXPECT_EQ(SYNC_RECEIVE_VALUE, *readValue);
        }
    } else if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED_RAW) {
        int32_t *readValue = reinterpret_cast<int32_t *>((void *)(ctx.Message()->Data()));
        EXPECT_EQ(RECEIVE_RAW, *readValue);
        sem_post(&sem);
    }

    return 0;
}

int ClientRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int ClientOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    sem_post(&sem);
    return 0;
}

bool ClientCreateDriver()
{
    if (clientDriver != nullptr) {
        NN_LOG_ERROR("clientDriver already created");
        return false;
    }

    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaClient", false);
    if (clientDriver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING; // 只支持EVENT模式
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 1024;
    options.heartBeatIdleTime = 1;
    options.heartBeatProbeInterval = 1;
    options.enableTls = false;
    options.SetNetDeviceIpMask(ipSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    clientDriver->RegisterEPBrokenHandler(std::bind(&ClientEndPointBroken, std::placeholders::_1));
    clientDriver->RegisterNewReqHandler(std::bind(&ClientRequestReceived, std::placeholders::_1));
    clientDriver->RegisterReqPostedHandler(std::bind(&ClientRequestPosted, std::placeholders::_1));
    clientDriver->RegisterOneSideDoneHandler(std::bind(&ClientOneSideDone, std::placeholders::_1));

    clientDriver->OobIpAndPort(BASE_IP, RDMA_LISTEN_PORT);

    int result = 0;
    if ((result = clientDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver initialized");

    if ((result = clientDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver started");

    return true;
}

bool AsyncClientConnect()
{
    if (clientDriver == nullptr) {
        NN_LOG_ERROR("clientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = clientDriver->Connect("hello world", clientAsyncEp, 0)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }
    clientAsyncEp->PeerIpAndPort();
    sem_init(&sem, 0, 0);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = clientAsyncEp->PostSend(GET_MR, req)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return false;
    }

    sem_wait(&sem);
    return true;
}

bool SyncClientConnect()
{
    if (clientDriver == nullptr) {
        NN_LOG_ERROR("clientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = clientDriver->Connect("hello world", clientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    clientSyncEp->PeerIpAndPort();
    return true;
}

void SendAsyncOneSideRequest(UBSHcomNetTransSgeIov *iov, uint64_t index)
{
    int result = 0;
    MOCKER(fake_post_read).stubs().will(returnValue(0));
    MOCKER(fake_post_write).stubs().will(returnValue(0));
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    result = clientAsyncEp->PostRead(sglReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read sgl data from server");
        return;
    }

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, 0);
    result = clientAsyncEp->PostWrite(sglReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to write sgl data from server");
        return;
    }

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, 0);
    result = clientAsyncEp->PostRead(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }

    result = clientAsyncEp->PostWrite(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to write data from server");
        return;
    }
}

void AsyncPostSendRawRequest()
{
    int result = 0;
    int32_t *localAddress = reinterpret_cast<int32_t *>(localMrInfo[0].lAddress);
    *localAddress = SEND_RAW;
    UBSHcomNetTransRequest req((void *)(localMrInfo[0].lAddress), NN_NO4, 0);
    if ((result = clientAsyncEp->PostSendRaw(req, 1)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return;
    }
    sem_wait(&sem);
    EXPECT_EQ(result, NN_OK);
}

void AsyncPostSendFailRequest()
{
    int result = 0;
    uint64_t data = 0;
    UBSHcomNetTransRequest req((void *)(localMrInfo[0].lAddress), NN_NO4, 0);
    clientAsyncEp->DefaultTimeout(0);

    MOCKER(RDMAMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(returnValue(false));
    result = clientAsyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, NN_GET_BUFF_FAILED);

    result = clientAsyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, NN_GET_BUFF_FAILED);
    CLEAN_UP_ALL_STUBS();

    MOCKER(RDMAQp::GetPostSendWr).stubs().will(returnValue(false));
    result = clientAsyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, RR_QP_POST_SEND_WR_FULL);

    result = clientAsyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, RR_QP_POST_SEND_WR_FULL);
    CLEAN_UP_ALL_STUBS();

    req.upCtxSize = NN_NO100;
    result = clientAsyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, NN_PARAM_INVALID);

    result = clientAsyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, NN_PARAM_INVALID);
    req.upCtxSize = 0;
#ifdef MOCK_VERBS
    MOCKER(fake_ibv_post_send).stubs().will(returnValue(-1));
    result = clientAsyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, RR_QP_POST_SEND_FAILED);

    result = clientAsyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, RR_QP_POST_SEND_FAILED);
#endif
    CLEAN_UP_ALL_STUBS();
}

void AsyncReadRequestCheckResult(UBSHcomNetTransSgeIov *iov, int checkResult, uint16_t upCtxSize)
{
    int result = 0;

    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, upCtxSize);
    result = clientAsyncEp->PostRead(sglReq);
    EXPECT_EQ(result, checkResult);

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, upCtxSize);
    result = clientAsyncEp->PostRead(buffReq);
    EXPECT_EQ(result, checkResult);
}

void AsyncWriteRequestCheckResult(UBSHcomNetTransSgeIov *iov, int checkResult, uint16_t upCtxSize)
{
    int result = 0;

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, upCtxSize);
    result = clientAsyncEp->PostWrite(reqWrite);
    EXPECT_EQ(result, checkResult);

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, upCtxSize);
    result = clientAsyncEp->PostWrite(buffReq);
    EXPECT_EQ(result, checkResult);
}

void AsyncOneSideFailRequest(UBSHcomNetTransSgeIov *iov)
{
    uint16_t upCtxSize = 0;
    clientAsyncEp->DefaultTimeout(0);
    MOCKER(NetDriverRDMA::ValidateMemoryRegion).stubs().will(returnValue(-1));
    AsyncReadRequestCheckResult(iov, NN_INVALID_LKEY, upCtxSize);
    AsyncWriteRequestCheckResult(iov, NN_INVALID_LKEY, upCtxSize);
    CLEAN_UP_ALL_STUBS();

    MOCKER(RDMAQp::GetOneSideWr).stubs().will(returnValue(false));
    AsyncReadRequestCheckResult(iov, RR_QP_ONE_SIDE_WR_FULL, upCtxSize);
    AsyncWriteRequestCheckResult(iov, RR_QP_ONE_SIDE_WR_FULL, upCtxSize);
    CLEAN_UP_ALL_STUBS();

    upCtxSize = NN_NO100;
    AsyncReadRequestCheckResult(iov, NN_PARAM_INVALID, upCtxSize);
    AsyncWriteRequestCheckResult(iov, NN_PARAM_INVALID, upCtxSize);
    upCtxSize = 0;
#ifdef MOCK_VERBS
    MOCKER(fake_post_read).stubs().will(returnValue(-1));
    AsyncReadRequestCheckResult(iov, RR_QP_POST_READ_FAILED, upCtxSize);
    MOCKER(fake_post_write).stubs().will(returnValue(-1));
    AsyncWriteRequestCheckResult(iov, RR_QP_POST_WRITE_FAILED, upCtxSize);
#endif
    CLEAN_UP_ALL_STUBS();
}

void AsyncNotSupportOperation()
{
    EXPECT_EQ(clientAsyncEp->WaitCompletion(), NN_INVALID_OPERATION);

    UBSHcomNetResponseContext ctx;
    EXPECT_EQ(clientAsyncEp->Receive(ctx), NN_INVALID_OPERATION);
    EXPECT_EQ(clientAsyncEp->ReceiveRaw(ctx), NN_INVALID_OPERATION);
}

void AsyncQpErrorHandle()
{
    MOCKER(RDMAOpContextInfo::OpResult).stubs().will(returnValue(RDMAOpContextInfo::ERR_IO_ERROR));
    int result = 0;
    int32_t *localAddress = reinterpret_cast<int32_t *>(localMrInfo[0].lAddress);
    *localAddress = SEND_RAW;
    UBSHcomNetTransRequest req((void *)(localMrInfo[0].lAddress), NN_NO4, 0);
    if ((result = clientAsyncEp->PostSendRaw(req, 1)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return;
    }
    sleep(1);
    CLEAN_UP_ALL_STUBS();
}

void AsyncRequest()
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = localMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = localMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO8;
    }
    sem_init(&sem, 0, 0);
    SendAsyncOneSideRequest(iov, 0);

    AsyncPostSendRawRequest();
    AsyncPostSendFailRequest();
    AsyncOneSideFailRequest(iov);
    AsyncNotSupportOperation();
    AsyncQpErrorHandle();
    // clientAsyncEp destroy when broken handle, do not use anymore
}

void SyncPostSendFailRequest()
{
    int result = 0;
    uint64_t data = 0;
    UBSHcomNetTransRequest req((void *)(localMrInfo[0].lAddress), NN_NO4, 0);
    clientSyncEp->DefaultTimeout(0);

    MOCKER(RDMAMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(returnValue(false));
    result = clientSyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, NN_GET_BUFF_FAILED);

    result = clientSyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, NN_GET_BUFF_FAILED);
    CLEAN_UP_ALL_STUBS();

    req.upCtxSize = NN_NO100;
    result = clientSyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, NN_PARAM_INVALID);

    result = clientSyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, NN_PARAM_INVALID);
    req.upCtxSize = 0;
#ifdef MOCK_VERBS
    MOCKER(fake_ibv_post_send).stubs().will(returnValue(-1));
    result = clientSyncEp->PostSend(POST_SEND_FAIL, req);
    EXPECT_EQ(result, RR_QP_POST_SEND_FAILED);

    result = clientSyncEp->PostSendRaw(req, 1);
    EXPECT_EQ(result, RR_QP_POST_SEND_FAILED);
#endif
    CLEAN_UP_ALL_STUBS();
}

void SyncReadRequestCheckResult(UBSHcomNetTransSgeIov *iov, int checkResult, uint16_t upCtxSize)
{
    int result = 0;

    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, upCtxSize);
    result = clientSyncEp->PostRead(sglReq);
    EXPECT_EQ(result, checkResult);

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, upCtxSize);
    result = clientSyncEp->PostRead(buffReq);
    EXPECT_EQ(result, checkResult);
}

void SyncWriteRequestCheckResult(UBSHcomNetTransSgeIov *iov, int checkResult, uint16_t upCtxSize)
{
    int result = 0;

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, upCtxSize);
    result = clientSyncEp->PostWrite(reqWrite);
    EXPECT_EQ(result, checkResult);

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, upCtxSize);
    result = clientSyncEp->PostWrite(buffReq);
    EXPECT_EQ(result, checkResult);
}

void SyncOneSideFailRequest(UBSHcomNetTransSgeIov *iov)
{
    uint16_t upCtxSize = 0;
    clientSyncEp->DefaultTimeout(0);
    MOCKER(NetDriverRDMA::ValidateMemoryRegion).stubs().will(returnValue(-1));
    SyncReadRequestCheckResult(iov, NN_INVALID_LKEY, upCtxSize);
    SyncWriteRequestCheckResult(iov, NN_INVALID_LKEY, upCtxSize);
    CLEAN_UP_ALL_STUBS();

    upCtxSize = NN_NO100;
    SyncReadRequestCheckResult(iov, NN_PARAM_INVALID, upCtxSize);
    SyncWriteRequestCheckResult(iov, NN_PARAM_INVALID, upCtxSize);
    upCtxSize = 0;
#ifdef MOCK_VERBS
    MOCKER(fake_post_read).stubs().will(returnValue(-1));
    SyncReadRequestCheckResult(iov, RR_QP_POST_READ_FAILED, upCtxSize);
    MOCKER(fake_post_write).stubs().will(returnValue(-1));
    SyncWriteRequestCheckResult(iov, RR_QP_POST_WRITE_FAILED, upCtxSize);
#endif
    CLEAN_UP_ALL_STUBS();
}

void SyncRequestsSuccess()
{
    // get one mr seg from pool
    uint64_t data = 0;
    UBSHcomNetResponseContext respCtx {};

    int result = 0;
    data = SYNC_SEND_VALUE;
    UBSHcomNetTransRequest req((void *)(&data), sizeof(data), 0);
    result = clientSyncEp->PostSend(CHECK_SYNC_RESPONSE, req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return;
    }

    result = clientSyncEp->WaitCompletion(-1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait completion, result " << result);
        return;
    }

    result = clientSyncEp->Receive(respCtx);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to get response, result " << result);
        return;
    }

    UBSHcomNetTransRequest buffReq(localMrInfo[0].lAddress, remoteMrInfo[0].lAddress, localMrInfo[0].lKey,
        remoteMrInfo[0].lKey, localMrInfo[0].size, 0);
    result = clientSyncEp->PostRead(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }

    result = clientSyncEp->WaitCompletion(-1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait completion, result " << result);
        return;
    }
    uint64_t *readBuff = reinterpret_cast<uint64_t *>((void *)(localMrInfo[0].lAddress));
    uint64_t readValue = *readBuff;
    EXPECT_EQ(readValue, ASYNC_RW_COUNT);

    result = clientSyncEp->PostWrite(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to write data from server");
        return;
    }

    result = clientSyncEp->WaitCompletion(-1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait completion, result " << result);
        return;
    }

    int32_t *localAddress = reinterpret_cast<int32_t *>(localMrInfo[0].lAddress);
    *localAddress = SEND_RAW;
    UBSHcomNetTransRequest reqRaw((void *)(localMrInfo[0].lAddress), NN_NO4, 0);
    result = clientSyncEp->PostSendRaw(reqRaw, 1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return;
    }
    result = clientSyncEp->WaitCompletion(-1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait completion, result " << result);
        return;
    }

    UBSHcomNetResponseContext rawCtx;
    result = clientSyncEp->ReceiveRaw(rawCtx);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to get response, result " << result);
        return;
    }
}

void SendSyncOneSideRequest(UBSHcomNetTransSgeIov *iov, uint64_t index)
{
    int result = 0;

    UBSHcomNetTransSglRequest sglReq(iov, NN_NO1, 0);
    result = clientSyncEp->PostRead(sglReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read sgl data from server");
        return;
    }

    result = clientSyncEp->WaitCompletion();
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait read sgl from server");
        return;
    }
    NN_LOG_TRACE_INFO("sgl read value idx:" << execCount++);
    for (uint16_t i = 0; i < NN_NO1; i++) {
        uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(localMrInfo[i].lAddress));
        uint64_t value = *readValue;
        NN_LOG_TRACE_INFO("value[" << i << "]=" << *readValue);
        EXPECT_EQ(value, index);
        *readValue = ++value;
    }

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO1, 0);
    result = clientSyncEp->PostWrite(sglReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to write sgl data from server");
        return;
    }

    result = clientSyncEp->WaitCompletion();
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait write sgl from server");
        return;
    }
}

void SyncSetRemoteMrZero()
{
    uint64_t data = 0;
    UBSHcomNetResponseContext respCtx {};

    int result = 0;
    data = SYNC_SEND_VALUE;
    UBSHcomNetTransRequest req((void *)(&data), sizeof(data), 0);
    result = clientSyncEp->PostSend(SET_MR, req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return;
    }

    result = clientSyncEp->WaitCompletion(-1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait completion, result " << result);
        return;
    }

    result = clientSyncEp->Receive(respCtx);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to get response, result " << result);
        return;
    }
}

void SyncRequests()
{
    SyncRequestsSuccess();
    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = localMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = localMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO8;
    }

    SyncSetRemoteMrZero();
    SendSyncOneSideRequest(iov, 0);

    SyncPostSendFailRequest();
    SyncOneSideFailRequest(iov);
}

bool ClientRegSglMem()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = clientDriver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        localMrInfo[i].lAddress = mr->GetAddress();
        localMrInfo[i].lKey = mr->GetLKey();
        localMrInfo[i].size = NN_NO16;
        memset(reinterpret_cast<void *>(localMrInfo[i].lAddress), 0, NN_NO16);
    }

    return true;
}

TEST_F(TestCaseRdma, RDMA_BASIC_OPERATE)
{
#ifdef MOCK_VERBS
    MOCK_VERSION
#endif
    bool result = ServerCreateDriver();
    CHECK_RESULT_TRUE(result);

    result = ServerRegSglMem();
    CHECK_RESULT_TRUE(result);
#ifdef MOCK_VERBS
    MOCK_VERSION
#endif
    result = ClientCreateDriver();
    CHECK_RESULT_TRUE(result);
    result = AsyncClientConnect();
    CHECK_RESULT_TRUE(result);
    result = ClientRegSglMem();
    CHECK_RESULT_TRUE(result);
    AsyncRequest();
    // clientAsyncEp destroy when broken handle, do not use anymore

    result = SyncClientConnect();
    CHECK_RESULT_TRUE(result);
    SyncRequests();

    if (clientDriver->IsStarted()) {
        clientDriver->Stop();
    }
    if (clientDriver->IsInited()) {
        clientDriver->UnInitialize();
    }
    if (serverDriver->IsStarted()) {
        serverDriver->Stop();
    }
    if (serverDriver->IsInited()) {
        serverDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(clientDriver->Name());
    UBSHcomNetDriver::DestroyInstance(serverDriver->Name());
}
#endif