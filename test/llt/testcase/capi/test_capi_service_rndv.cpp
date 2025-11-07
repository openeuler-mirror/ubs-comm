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

#include "test_capi_service_rndv.h"

#include "hcom.h"
#include "hcom_service_c.h"
#include "rdma_common.h"
#include "hcom_env.h"

using namespace ock::hcom;
TestCapiServiceRndv::TestCapiServiceRndv() {}
#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"
Net_Channel serviceChannel = 0;
Net_Service serviceRndv = 0;
Net_Service clientRndv = 0;
ubs_hcom_service_type serviceRndvType = C_SERVICE_RDMA;
int32_t rndvSize = 1024;
int32_t rndvPort = 0;
int rdnvBasePort = 4900;
Service_ConnectOpt rndvOptions;
ubs_hcom_channel_callback cbRndv;


static int NewEndPoint(Net_Channel newCh, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_INFO("Channel new, payLoad: " << payLoad);
    serviceChannel = newCh;
    return 0;
}

static int EndPointBroken(Net_Channel ch, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_ERROR("Channel broken, payLoad:" << payLoad);
    return 0;
}

static int RequestReceived(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("Request received");
    return 0;
}

static int RequestPosted(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    return 0;
}

static int OneSideDone(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

Net_MemoryAllocator memAllocator = 0;
void *addressRndv = NULL;
Net_MemoryAllocator memClientAllocator = 0;
void *addressClientRndv = NULL;
uint64_t memSizeRndv = 1024 * 1024 * 128;

int PrepareMemAllocator()
{
    addressRndv = memalign(4096, memSizeRndv);
    if (addressRndv == NULL) {
        NN_LOG_ERROR("Failed to alloc memory, maybe lack of spare memory in system.");
        return -1;
    }

    Net_MemoryAllocatorOptions options1;
    options1.address = (uintptr_t)addressRndv;
    options1.size = memSizeRndv;
    options1.minBlockSize = 4096;
    options1.alignedAddress = 1;
    options1.cacheTierCount = 10;
    options1.cacheBlockCountPerTier = 8;
    options1.bucketCount = 8;
    options1.cacheTierPolicy = C_TIER_POWER;

    int result = Net_MemoryAllocatorCreate(C_DYNAMIC_SIZE_WITH_CACHE, &options1, &memAllocator);
    if (result != 0) {
        NN_LOG_ERROR("Failed to create memory allocator");
        return -1;
    }

    return 0;
}

int PrepareClientMemAllocator()
{
    addressClientRndv = memalign(4096, memSizeRndv);
    if (addressClientRndv == NULL) {
        NN_LOG_ERROR("Failed to alloc memory, maybe lack of spare memory in system.");
        return -1;
    }

    Net_MemoryAllocatorOptions options1;
    options1.address = (uintptr_t)addressClientRndv;
    options1.size = memSizeRndv;
    options1.minBlockSize = 4096;
    options1.alignedAddress = 1;
    options1.cacheTierCount = 10;
    options1.cacheBlockCountPerTier = 8;
    options1.bucketCount = 8;
    options1.cacheTierPolicy = C_TIER_POWER;

    int result = Net_MemoryAllocatorCreate(C_DYNAMIC_SIZE_WITH_CACHE, &options1, &memClientAllocator);
    if (result != 0) {
        NN_LOG_ERROR("Failed to create memory allocator");
        return -1;
    }

    return 0;
}


int MemAllocate(uint64_t rndvSize, uintptr_t *add, uint32_t *key)
{
    return Net_MemoryAllocatorAllocate(memAllocator, rndvSize, add, key);
}

int MemFree(uintptr_t add)
{
    return Net_MemoryAllocatorFree(memAllocator, add);
}

int MemClientAllocate(uint64_t rndvSize, uintptr_t *add, uint32_t *key)
{
    return Net_MemoryAllocatorAllocate(memClientAllocator, rndvSize, add, key);
}

int MemClientFree(uintptr_t add)
{
    return Net_MemoryAllocatorFree(memClientAllocator, add);
}

void CommonCbRndv(void *arg, ubs_hcom_service_context context)
{
    return;
}

int RndvHandlerC(Service_RndvContext context)
{
    // step1 direct handle message or change to other thread

    // step2 rsp message
    int ret = 0;
    Service_Message req = { &ret, sizeof(ret) };
    Service_OpInfo reqInfo = { 0 };
    cbRndv.cb = CommonCbRndv;
    ret = Service_RndvReply(context, &reqInfo, &req, &cbRndv);
    if (ret != 0) {
        NN_LOG_ERROR("Reply message failed " << ret);
    }

    // step3 free context
    Service_RndvFreeContext(context);
    return ret;
}

int CreateRndvService()
{
    int result = 0;

    if (serviceRndv != 0) {
        NN_LOG_ERROR("service already created.");
        return -1;
    }

    result = ubs_hcom_service_create(serviceRndvType, "server_rndv", 1, &serviceRndv);
    if (result != 0) {
        NN_LOG_ERROR("failed to create service already created.");
        return -1;
    }

    result = PrepareMemAllocator();
    if (result != 0) {
        NN_LOG_ERROR("failed to prepare mem.");
        return -1;
    }

    ubs_hcom_service_options options;
    bzero(&options, sizeof(ubs_hcom_service_options));
    options.enableRndv = 1;
    options.mode = C_SERVICE_BUSY_POLLING;
    options.mrSendReceiveSegSize = 1024 + rndvSize;
    options.mrSendReceiveSegCount = 8192;
    options.enableTls = false;
    strcpy(options.netDeviceIpMask, IP_SEG);

    Service_RegisterChannelHandler(serviceRndv, C_CHANNEL_NEW, &NewEndPoint, C_CHANNEL_RECONNECT, 1);
    Service_RegisterChannelHandler(serviceRndv, C_CHANNEL_BROKEN, &EndPointBroken, C_CHANNEL_RECONNECT, 1);
    Service_RegisterOpHandler(serviceRndv, 0, C_SERVICE_REQUEST_RECEIVED, &RequestReceived, 1);
    Service_RegisterOpHandler(serviceRndv, 0, C_SERVICE_REQUEST_POSTED, &RequestPosted, 1);
    Service_RegisterOpHandler(serviceRndv, 0, C_SERVICE_READWRITE_DONE, &OneSideDone, 1);
    Service_RegisterAllocateHandler(serviceRndv, &MemAllocate);
    Service_RegisterFreeHandler(serviceRndv, &MemFree);
    Service_RegisterRndvOpHandler(serviceRndv, &RndvHandlerC);
    rndvPort = ++rdnvBasePort;
    Service_SetOobIpAndPort(serviceRndv, BASE_IP, rndvPort);

    if ((result = ubs_hcom_service_start(serviceRndv, options)) != 0) {
        NN_LOG_ERROR("failed to start service " << result);
        return -1;
    }
    NN_LOG_INFO("service started");

    ubs_hcom_memory_region mr;
    result = ubs_hcom_service_register_assign_memory_region(serviceRndv, (uintptr_t)addressRndv, memSizeRndv, &mr);
    if (result != 0) {
        NN_LOG_ERROR("Register mr failed");
        return -1;
    }

    ubs_hcom_mr_info info;
    ubs_hcom_service_get_memory_region_info(mr, &info);
    Net_MemoryAllocatorSetMrKey(memAllocator, info.lKey);

    return 0;
}

int CreateRndvClient()
{
    int result = 0;

    if (clientRndv != 0) {
        NN_LOG_ERROR("service already created.");
        return -1;
    }

    result = ubs_hcom_service_create(serviceRndvType, "client_rndv", 0, &clientRndv);
    if (result != 0) {
        NN_LOG_ERROR("failed to create service already created.");
        return -1;
    }

    result = PrepareClientMemAllocator();
    if (result != 0) {
        NN_LOG_ERROR("failed to prepare mem.");
        return -1;
    }

    ubs_hcom_service_options options;
    bzero(&options, sizeof(ubs_hcom_service_options));
    options.enableRndv = 1;
    options.mode = C_SERVICE_BUSY_POLLING;
    options.mrSendReceiveSegSize = 1024 + rndvSize;
    options.mrSendReceiveSegCount = 8192;
    options.enableTls = false;
    strcpy(options.netDeviceIpMask, IP_SEG);

    Service_RegisterChannelHandler(clientRndv, C_CHANNEL_BROKEN, &EndPointBroken, C_CHANNEL_RECONNECT, 1);
    Service_RegisterOpHandler(clientRndv, 0, C_SERVICE_REQUEST_RECEIVED, &RequestReceived, 1);
    Service_RegisterOpHandler(clientRndv, 0, C_SERVICE_REQUEST_POSTED, &RequestPosted, 1);
    Service_RegisterOpHandler(clientRndv, 0, C_SERVICE_READWRITE_DONE, &OneSideDone, 1);
    Service_RegisterAllocateHandler(clientRndv, &MemClientAllocate);
    Service_RegisterFreeHandler(clientRndv, &MemClientFree);
    Service_RegisterRndvOpHandler(clientRndv, &RndvHandlerC);

    Service_SetOobIpAndPort(clientRndv, BASE_IP, rndvPort);

    if ((result = ubs_hcom_service_start(clientRndv, options)) != 0) {
        NN_LOG_ERROR("failed to start service " << result);
        return -1;
    }
    NN_LOG_INFO("client started.");

    ubs_hcom_memory_region mr;
    result = ubs_hcom_service_register_assign_memory_region(clientRndv, (uintptr_t)addressClientRndv, memSizeRndv, &mr);
    if (result != 0) {
        NN_LOG_ERROR("Register mr failed.");
        return -1;
    }

    ubs_hcom_mr_info info;
    ubs_hcom_service_get_memory_region_info(mr, &info);
    Net_MemoryAllocatorSetMrKey(memClientAllocator, info.lKey);

    return 0;
}


void TestCapiServiceRndv::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
    MOCKER(ReadRoCEVersionFromFile).stubs().will(returnValue(0));
    rndvOptions = { 0, 0, C_CHANNEL_FUNC_CB, 0, 0, 0 };
    rndvOptions.epSize = 2;
    CreateRndvService();
    CreateRndvClient();
}

void TestCapiServiceRndv::TearDown()
{
    Channel_Destroy(serviceChannel);
    Service_Stop(serviceRndv);
    ubs_hcom_service_destroy(serviceRndv);
    Service_Stop(clientRndv);
    ubs_hcom_service_destroy(clientRndv);
    serviceRndv = 0;
    clientRndv = 0;
    rndvOptions = { 0, 0, C_CHANNEL_FUNC_CB, 0, 0, 0 };
    free(addressRndv);
    addressRndv = NULL;
    free(addressClientRndv);
    addressClientRndv = NULL;
    GlobalMockObject::verify();
}

typedef struct {
    Service_Message rsp;
    sem_t sem;
    int ret;
} CallAsyncStruct;
void CallAsyncCbRdnv(void *arg, ubs_hcom_service_context context)
{
    CallAsyncStruct *param = (CallAsyncStruct *)arg;

    if (ubs_hcom_context_get_result(context, &param->ret) != 0) {
        NN_LOG_ERROR("Call async failed " << param->ret);
        return;
    }

    sem_post(&param->sem);
}

TEST_F(TestCapiServiceRndv, CallRequest)
{
    int result = 0;
    Net_Channel clientChannel = 0;
    result = ubs_hcom_service_connect(clientRndv, BASE_IP, rndvPort, "hello service c", &clientChannel, &rndvOptions);
    EXPECT_EQ(0, result);

    /* CallRequest */
    char *data = (char *)malloc(rndvSize);
    Service_Request req;
    uintptr_t addressPtr;
    uint32_t key;
    result = MemClientAllocate(rndvSize, &addressPtr, &key);
    EXPECT_EQ(0, result);

    req.lAddress = addressPtr;
    req.lKey = key;
    req.size = rndvSize;

    Service_Message rsp = { data, rndvSize };
    Service_OpInfo reqInfo = { 0, 0, 0, 0 };
    reqInfo.timeout = 1;
    Service_OpInfo rspInfo = { 0, 0, 0, 0 };

    result = Channel_SyncRndvCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    result = Channel_SyncRndvCall(0, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvCall(clientChannel, nullptr, &req, &rspInfo, &rsp);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvCall(clientChannel, &reqInfo, nullptr, &rspInfo, &rsp);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvCall(clientChannel, &reqInfo, &req, nullptr, &rsp);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvCall(clientChannel, &reqInfo, &req, &rspInfo, nullptr);
    EXPECT_EQ(501, result);

    free(data);
    MemClientFree(addressPtr);
    GlobalMockObject::verify();

    /* CallRawSglRequest */
    char *data1 = (char *)malloc(rndvSize);
    Service_Request req1[2];
    for (uint32_t i = 0; i < 2; i++) {
        uintptr_t addressPtr;
        uint32_t key;

        result = MemClientAllocate(rndvSize, &addressPtr, &key);
        EXPECT_EQ(0, result);

        req1[i].lAddress = addressPtr;
        req1[i].lKey = key;
        req1[i].size = rndvSize;
    }

    Service_SglRequest sgl1 = { req1, 1 };
    Service_Message rsp1 = { data1, rndvSize };
    Service_OpInfo reqInfo1 = { 0, 0, 0, 0 };
    Service_OpInfo rspInfo1 = { 0, 0, 0, 0 };

    result = Channel_SyncRndvSglCall(clientChannel, &reqInfo1, &sgl1, &rspInfo1, &rsp1);
    EXPECT_EQ(0, result);

    result = Channel_SyncRndvSglCall(0, &reqInfo1, &sgl1, &rspInfo1, &rsp1);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvSglCall(clientChannel, nullptr, &sgl1, &rspInfo1, &rsp1);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvSglCall(clientChannel, &reqInfo1, nullptr, &rspInfo1, &rsp1);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvSglCall(clientChannel, &reqInfo1, &sgl1, nullptr, &rsp1);
    EXPECT_EQ(501, result);

    result = Channel_SyncRndvSglCall(clientChannel, &reqInfo1, &sgl1, &rspInfo1, nullptr);
    EXPECT_EQ(501, result);

    free(data1);
    MemClientFree(req1[0].lAddress);
    MemClientFree(req1[1].lAddress);

    GlobalMockObject::verify();

    /* CallAsyncRequest */
    Service_Request req2;
    Service_OpInfo reqInfo2 = { 0, 0, 0, 0 };

    uintptr_t addressPtr2;
    uint32_t key2;

    result = MemClientAllocate(rndvSize, &addressPtr2, &key2);
    EXPECT_EQ(0, result);

    req2.lAddress = addressPtr2;
    req2.lKey = key2;
    req2.size = rndvSize;

    char data2[rndvSize];
    CallAsyncStruct asyncParam2;
    asyncParam2.rsp.data = data2;
    asyncParam2.rsp.size = rndvSize;
    sem_init(&asyncParam2.sem, 0, 0);
    asyncParam2.ret = 0;

    ubs_hcom_channel_callback cb2;
    cb2.arg = &asyncParam2;
    cb2.cb = CallAsyncCbRdnv;

    result = Channel_AsyncRndvCall(clientChannel, &reqInfo2, &req2, &cb2);
    EXPECT_EQ(0, result);
    sem_wait(&asyncParam2.sem);
    sem_destroy(&asyncParam2.sem);


    result = Channel_AsyncRndvCall(0, &reqInfo2, &req2, &cb2);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvCall(clientChannel, nullptr, &req2, &cb2);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvCall(clientChannel, &reqInfo2, nullptr, &cb2);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvCall(clientChannel, &reqInfo2, &req2, nullptr);
    EXPECT_EQ(501, result);

    MemClientFree(addressPtr2);
    EXPECT_EQ(0, asyncParam2.ret);

    GlobalMockObject::verify();

    /* CallAsyncRawSglRequest */
    Service_Request req3[2];
    for (uint32_t i = 0; i < 2; i++) {
        uintptr_t addressPtr;
        uint32_t key;

        result = MemClientAllocate(rndvSize, &addressPtr, &key);
        EXPECT_EQ(0, result);

        req3[i].lAddress = addressPtr;
        req3[i].lKey = key;
        req3[i].size = rndvSize;
    }

    Service_SglRequest sgl3 = { req3, 1 };
    Service_OpInfo reqInfo3 = { 0, 0, 0, 0 };

    CallAsyncStruct asyncParam3;
    sem_init(&asyncParam3.sem, 0, 0);
    asyncParam3.ret = 0;

    ubs_hcom_channel_callback cb3;
    cb3.arg = &asyncParam3;
    cb3.cb = CallAsyncCbRdnv;

    result = Channel_AsyncRndvSglCall(clientChannel, &reqInfo3, &sgl3, &cb3);
    EXPECT_EQ(0, result);

    result = Channel_AsyncRndvSglCall(0, &reqInfo3, &sgl3, &cb3);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvSglCall(clientChannel, nullptr, &sgl3, &cb3);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvSglCall(clientChannel, &reqInfo3, nullptr, &cb3);
    EXPECT_EQ(501, result);

    result = Channel_AsyncRndvSglCall(clientChannel, &reqInfo3, &sgl3, nullptr);
    EXPECT_EQ(501, result);

    sem_wait(&asyncParam3.sem);
    sem_destroy(&asyncParam3.sem);
    MemClientFree(req3[0].lAddress);
    MemClientFree(req3[1].lAddress);
    EXPECT_EQ(0, asyncParam3.ret);
    GlobalMockObject::verify();

    Channel_Destroy(clientChannel);
}

TEST_F(TestCapiServiceRndv, CallRequestInline)
{
    int result = 0;
    Net_Channel clientChannel = 0;
    result = ubs_hcom_service_connect(clientRndv, BASE_IP, rndvPort, "hello service c", &clientChannel, &rndvOptions);
    EXPECT_EQ(0, result);

    /* CallRequest */
    char *data = (char *)malloc(rndvSize);
    Service_Request req;
    uintptr_t addressPtr;
    uint32_t key;
    result = MemClientAllocate(rndvSize, &addressPtr, &key);
    EXPECT_EQ(0, result);

    req.lAddress = addressPtr;
    req.lKey = key;
    req.size = rndvSize;

    Service_Message rsp = { data, rndvSize };
    Service_OpInfo reqInfo = { 0, 0, 0, 0 };
    reqInfo.timeout = 1;
    Service_OpInfo rspInfo = { 0, 0, 0, 0 };

    MOCKER_CPP(&HcomEnv::InlineThreshold).stubs().will(returnValue(4096));

    result = Channel_SyncRndvCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    free(data);
    MemClientFree(addressPtr);
    GlobalMockObject::verify();
}