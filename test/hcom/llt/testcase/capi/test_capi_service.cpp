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

#include <cstdlib>
#include <semaphore.h>
#define protected public
#include "hcom_service.h"
#include "hcom_service_c.h"
#include "net_service_default_imp.h"
#include "test_capi_service.h"

using namespace ock::hcom;
TestCapiService::TestCapiService() {}

char oobIp[24] = "127.0.0.1";
uint16_t oobPort = 9981;
char capiIpSeg[24] = "127.0.0.0/16";

uint32_t dataSize = 1024;

ubs_hcom_service_type serviceType = C_SERVICE_SHM;
Net_Service service = 0;
Net_Service client = 0;
Net_Channel channel = 0;
Service_ConnectOpt capiOptions;
const char *udsName = "SHM_UDS";
typedef struct {
    uintptr_t lAddress;
    uint32_t lKey;
    uint32_t size;
} TestRegMrInfo;

TestRegMrInfo capiRemoteMrInfo[4];

char *data = NULL;
char *data1 = NULL;

ubs_hcom_channel_callback cb;
void CommonCb(void *arg, ubs_hcom_service_context context)
{
    return;
}

static int NewEndPoint(Net_Channel newCh, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_INFO("Channel new, payLoad: " << payLoad);
    channel = newCh;
    return 0;
}

static int EndPointBroken(Net_Channel ch, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_ERROR("Channel broken, payLoad:" << payLoad);
    return 0;
}

static int RequestReceived(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("Get context type start");
    Service_Message message;
    message.data = ubs_hcom_context_get_data(ctx);
    message.size = ubs_hcom_context_get_datalen(ctx);
    if (message.data == NULL) {
        NN_LOG_ERROR("failed to get message");
        return -1;
    }

    ubs_hcom_service_context_type type;
    if (ubs_hcom_context_get_type(ctx, &type) != 0) {
        NN_LOG_ERROR("Get context type failed");
        return -1;
    }

    Net_Channel tmpChannel;
    if (ubs_hcom_context_get_channel(ctx, &tmpChannel) != 0) {
        NN_LOG_ERROR("Get channel failed");
        return -1;
    }

    cb.cb = CommonCb;
    cb.arg = NULL;
    if (type == SERVICE_RECEIVED_RAW) {
        char *receive = (char *)message.data;
        if (receive[0] == 0) {
            // receive send message
            return 0;
        }

        // receive call message, need send response
        Service_Message req = message;

        Service_OpInfo sendOpInfo = { 0, 0, 0, 0 };
        Service_RspCtx rsp = 0;
        if (ubs_hcom_context_get_rspctx(ctx, &rsp) != 0) {
            NN_LOG_ERROR("Get response ctx failed");
            return -1;
        }

        // in hcom receive thread, need async send message; in user thread, there is no limit
        if (Channel_PostResponse(tmpChannel, rsp, &sendOpInfo, &req, &cb) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
        return 0;
    }

    // SERVICE_RECEIVED type
    Service_OpInfo opInfo;
    if (Service_GetOpInfo(ctx, &opInfo) != 0) {
        NN_LOG_ERROR("Get op info failed");
        return -1;
    }
    if (opInfo.opCode == 0) {
        printf("receive msg, op code 0");
    } else if (opInfo.opCode == NN_NO2) {
        Service_Message req = { capiRemoteMrInfo, sizeof(capiRemoteMrInfo) };
        Service_OpInfo sendOpInfo = opInfo;

        // post send callback
        Service_RspCtx rsp = 0;
        if (ubs_hcom_context_get_rspctx(ctx, &rsp) != 0) {
            NN_LOG_ERROR("Get response ctx failed");
            return -1;
        }
        // in hcom receive thread, need async send message; in user thread, there is no limit
        if (Channel_PostResponse(tmpChannel, rsp, &sendOpInfo, &req, &cb) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
    } else {
        Service_Message req = message;
        // send the same message back to verify

        Service_OpInfo sendOpInfo = opInfo;

        // post send callback
        Service_RspCtx rsp = 0;
        if (ubs_hcom_context_get_rspctx(ctx, &rsp) != 0) {
            NN_LOG_ERROR("Get response ctx failed");
            return -1;
        }

        // in hcom receive thread, need async send message; in user thread, there is no limit
        if (Channel_PostResponse(tmpChannel, rsp, &sendOpInfo, &req, &cb) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
    }
    return 0;
}

static int RequestPosted(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("posted");
    return 0;
}

static int OneSideDone(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

int RegCapiSglMem(Net_Service driver, TestRegMrInfo capiMrInfo[], std::vector<ubs_hcom_memory_region> &mrs)
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        ubs_hcom_memory_region mrArray;
        int result = ubs_hcom_service_register_memory_region(driver, dataSize, &mrArray);
        if (result != 0) {
            NN_LOG_ERROR("reg mr failed");
            return -1;
        }

        ubs_hcom_mr_info mrInfo;
        result = ubs_hcom_service_get_memory_region_info(mrArray, &mrInfo);
        if (result != 0) {
            NN_LOG_ERROR("parse mr failed");
            return -1;
        }
        capiMrInfo[i].lAddress = mrInfo.lAddress;
        capiMrInfo[i].lKey = mrInfo.lKey;
        capiMrInfo[i].size = mrInfo.size;
        mrs.push_back(mrArray);
        memset(reinterpret_cast<void *>(capiMrInfo[i].lAddress), ' ', capiMrInfo[i].size);
    }

    return 0;
}

void DestoryCapiSglMem(Net_Service driver, std::vector<ubs_hcom_memory_region> &mrs)
{
    while (!mrs.empty()) {
        ubs_hcom_service_destroy_memory_region(driver, mrs.back());
        mrs.pop_back();
    }
}

typedef struct {
    Service_Message rsp;
    sem_t sem;
    int ret;
} CallAsyncStruct;
void CallAsyncCb(void *arg, ubs_hcom_service_context context)
{
    CallAsyncStruct *param = (CallAsyncStruct *)arg;

    if (ubs_hcom_context_get_result(context, &param->ret) != 0) {
        NN_LOG_ERROR("Call async failed " << param->ret);
        return;
    }

    Service_Message message;
    message.data = ubs_hcom_context_get_data(context);
    message.size = ubs_hcom_context_get_datalen(context);
    if (message.data == NULL) {
        sem_post(&param->sem);
        param->ret = -1;
        NN_LOG_ERROR("failed to get message");
        return;
    }

    if (message.size != param->rsp.size) {
        sem_post(&param->sem);
        param->ret = -1;
        NN_LOG_ERROR("Receive unwanted message");
        return;
    }

    memcpy(param->rsp.data, message.data, message.size);
    sem_post(&param->sem);
}

int CreateCapiService(ubs_hcom_service_request_handler receiveCb)
{
    int result = 0;

    if (service != 0) {
        NN_LOG_ERROR("service already created");
        return -1;
    }

    result = ubs_hcom_service_create(serviceType, "server_capi", 1, &service);
    if (result != 0) {
        NN_LOG_ERROR("failed to create service already created");
        return -1;
    }

    ubs_hcom_service_options capiOptions;
    bzero(&capiOptions, sizeof(ubs_hcom_service_options));
    capiOptions.mode = C_SERVICE_BUSY_POLLING;
    capiOptions.mrSendReceiveSegSize = NN_NO1024 + dataSize;
    capiOptions.mrSendReceiveSegCount = NN_NO8192;
    capiOptions.enableTls = false;
    strcpy(capiOptions.netDeviceIpMask, capiIpSeg);
    sprintf(capiOptions.workerGroups, "%u", 1);
    if (serviceType == C_SERVICE_SHM) {
        capiOptions.oobType = C_SERVICE_OOB_UDS;
        capiOptions.mode = C_SERVICE_EVENT_POLLING;
        Service_OobUDSListenerOptions listenOpt;
        strcpy(listenOpt.name, udsName);
        listenOpt.perm = 0;
        Service_AddOobUdsOptions(service, listenOpt);
    }

    Service_RegisterChannelHandler(service, C_CHANNEL_NEW, &NewEndPoint, C_CHANNEL_RECONNECT, 1);
    Service_RegisterChannelHandler(service, C_CHANNEL_BROKEN, &EndPointBroken, C_CHANNEL_RECONNECT, 1);
    Service_RegisterOpHandler(service, 0, C_SERVICE_REQUEST_RECEIVED, receiveCb, 1);
    Service_RegisterOpHandler(service, 0, C_SERVICE_REQUEST_POSTED, &RequestPosted, 1);
    Service_RegisterOpHandler(service, 0, C_SERVICE_READWRITE_DONE, &OneSideDone, 1);

    Service_SetOobIpAndPort(service, oobIp, oobPort);

    if ((result = ubs_hcom_service_start(service, capiOptions)) != 0) {
        NN_LOG_ERROR("failed to start service " << result);
        return -1;
    }
    NN_LOG_INFO("service started");

    return 0;
}

int CreateCapiClient()
{
    int result = 0;

    if (client != 0) {
        NN_LOG_ERROR("service already created");
        return -1;
    }

    result = ubs_hcom_service_create(serviceType, "client_capi", 0, &client);
    if (result != 0) {
        NN_LOG_ERROR("failed to create service already created");
        return -1;
    }

    ubs_hcom_service_options capiOptions;
    bzero(&capiOptions, sizeof(ubs_hcom_service_options));
    capiOptions.mode = C_SERVICE_BUSY_POLLING;
    capiOptions.mrSendReceiveSegSize = NN_NO1024 + dataSize;
    capiOptions.mrSendReceiveSegCount = NN_NO8192;
    capiOptions.enableTls = false;
    strcpy(capiOptions.netDeviceIpMask, capiIpSeg);
    sprintf(capiOptions.workerGroups, "%u", 1);
    if (serviceType == C_SERVICE_SHM) {
        capiOptions.oobType = C_SERVICE_OOB_UDS;
        capiOptions.mode = C_SERVICE_EVENT_POLLING;
    }

    Service_RegisterChannelHandler(client, C_CHANNEL_BROKEN, &EndPointBroken, C_CHANNEL_RECONNECT, 1);
    Service_RegisterOpHandler(client, 0, C_SERVICE_REQUEST_RECEIVED, &RequestReceived, 1);
    Service_RegisterOpHandler(client, 0, C_SERVICE_REQUEST_POSTED, &RequestPosted, 1);
    Service_RegisterOpHandler(client, 0, C_SERVICE_READWRITE_DONE, &OneSideDone, 1);

    Service_SetOobIpAndPort(client, oobIp, oobPort);

    if ((result = ubs_hcom_service_start(client, capiOptions)) != 0) {
        NN_LOG_ERROR("failed to start service " << result);
        return -1;
    }
    NN_LOG_INFO("service started");

    return 0;
}


void TestCapiService::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
    setenv("HCOM_CONNECTION_RECV_TIMEOUT_SEC", "1", 1);
    setenv("HCOM_CONNECTION_SEND_TIMEOUT_SEC", "1", 1);
    capiOptions = { 0, 0, C_CHANNEL_FUNC_CB, 0, 0, 0 };
    capiOptions.epSize = 1;
}

void TestCapiService::TearDown()
{
    service = 0;
    client = 0;
    capiOptions = { 0, 0, C_CHANNEL_FUNC_CB, 0, 0, 0 };
    GlobalMockObject::verify();
}

TEST_F(TestCapiService, ServiceGetMemoryRegionInfo)
{
    CreateCapiService(&RequestReceived);
    ubs_hcom_memory_region mr;
    int result = ubs_hcom_service_register_memory_region(0, dataSize, &mr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = ubs_hcom_service_register_memory_region(service, dataSize, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = ubs_hcom_service_register_memory_region(service, 0, &mr);
    EXPECT_EQ(NN_INVALID_PARAM, result);

    result = ubs_hcom_service_register_memory_region(service, dataSize, &mr);
    EXPECT_EQ(0, result);

    ubs_hcom_mr_info mrInfo;
    result = ubs_hcom_service_get_memory_region_info(0, &mrInfo);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = ubs_hcom_service_get_memory_region_info(mr, &mrInfo);
    EXPECT_EQ(0, result);

    ubs_hcom_service_destroy_memory_region(service, mr);
    Service_Stop(service);
    ubs_hcom_service_destroy(service);
}

TEST_F(TestCapiService, ChannelPostSend)
{
    CreateCapiService(&RequestReceived);
    CreateCapiClient();
    int result = 0;
    Net_Channel clientChannel = 0;

    result = ubs_hcom_service_connect(0, udsName, oobPort, "hello service c", &clientChannel, &capiOptions);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = ubs_hcom_service_connect(client, udsName, oobPort, nullptr, &clientChannel, &capiOptions);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = ubs_hcom_service_connect(client, udsName, oobPort, "hello service c", nullptr, &capiOptions);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    capiOptions.epSize = NN_NO19;
    result = ubs_hcom_service_connect(client, udsName, oobPort, "hello service c", &clientChannel, &capiOptions);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    capiOptions.epSize = 1;
    result = ubs_hcom_service_connect(client, udsName, oobPort, "hello service c", &clientChannel, &capiOptions);
    EXPECT_EQ(0, result);

    data = (char *)malloc(dataSize);
    Service_Message message = { data, dataSize };

    Service_OpInfo opInfo = { 0, 0, 0, 0 };
    opInfo.opCode = 0;
    opInfo.timeout = 1;

    /* postSend */
    result = Channel_PostSend(clientChannel, &opInfo, &message, NULL);
    EXPECT_EQ(0, result);

    /* creat callback */
    char data2[dataSize];
    CallAsyncStruct asyncParam;
    asyncParam.rsp.data = data2;
    asyncParam.rsp.size = dataSize;
    sem_init(&asyncParam.sem, 0, 0);
    asyncParam.ret = 0;

    ubs_hcom_channel_callback cb;
    cb.arg = &asyncParam;
    cb.cb = CallAsyncCb;
    result = Channel_PostSend(clientChannel, &opInfo, &message, &cb);
    EXPECT_EQ(0, result);
    sem_wait(&asyncParam.sem);
    sem_destroy(&asyncParam.sem);

    result = Channel_PostSend(0, &opInfo, &message, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_PostSend(clientChannel, nullptr, &message, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_PostSend(clientChannel, &opInfo, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SendInner).stubs().will(returnValue(SER_INVALID_PARAM));
    result = Channel_PostSend(clientChannel, &opInfo, &message, &cb);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    /* postsendRaw */
    result = Channel_PostSendRaw(clientChannel, &message, NULL);
    EXPECT_EQ(0, result);

    result = Channel_PostSendRaw(0, &message, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_PostSendRaw(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SendRawInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_PostSendRaw(clientChannel, &message, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* postSendRawSgl */
    TestRegMrInfo capiLocalMrInfo[4];
    std::vector<ubs_hcom_memory_region> mrClient;
    result = RegCapiSglMem(client, capiLocalMrInfo, mrClient);
    EXPECT_EQ(0, result);
    std::vector<ubs_hcom_memory_region> mrService;
    result = RegCapiSglMem(service, capiRemoteMrInfo, mrService);
    EXPECT_EQ(0, result);

    Service_Message req;
    req.data = data;
    req.size = dataSize;

    Service_Message rsp;
    TestRegMrInfo getRemoteMrInfo[4];
    rsp.data = getRemoteMrInfo;
    rsp.size = sizeof(getRemoteMrInfo);

    Service_OpInfo reqInfo = { 0, 0, 0, 0 };
    reqInfo.opCode = NN_NO2;

    Service_OpInfo rspInfo = { 0, 0, 0, 0 };

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    Service_Request reqRawSgl;
    reqRawSgl.lAddress = capiLocalMrInfo[0].lAddress;
    reqRawSgl.rAddress = getRemoteMrInfo[0].lAddress;
    reqRawSgl.lKey = capiLocalMrInfo[0].lKey;
    reqRawSgl.rKey = getRemoteMrInfo[0].lKey;
    reqRawSgl.size = capiLocalMrInfo[0].size;

    Service_SglRequest request;
    request.iov = &reqRawSgl;
    request.iovCount = 1;
    memset((void *)(reqRawSgl.lAddress), 0, 1);

    result = Channel_PostSendRawSgl(clientChannel, &request, NULL);
    EXPECT_EQ(0, result);

    result = Channel_PostSendRawSgl(0, &request, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_PostSendRawSgl(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SendRawSglInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_PostSendRawSgl(clientChannel, &request, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);
    DestoryCapiSglMem(client, mrClient);
    DestoryCapiSglMem(service, mrService);

    GlobalMockObject::verify();

    /* SyncCall */
    req = { data, dataSize };
    rsp = { data, dataSize };
    reqInfo = { 0, 0, 0, 0 };
    rspInfo = { 0, 0, 0, 0 };
    reqInfo.opCode = 1;
    reqInfo.timeout = 0;
    reqInfo.errorCode = 0xff;

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    result = Channel_SyncCall(0, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(clientChannel, nullptr, &req, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(clientChannel, &reqInfo, nullptr, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SyncCallInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* AsyncCall */
    req = { data, dataSize };
    opInfo = { 0, 0, 0, 0 };
    opInfo.opCode = 1;

    // CallAsyncStruct asyncParam
    asyncParam.rsp.data = data2;
    asyncParam.rsp.size = dataSize;
    sem_init(&asyncParam.sem, 0, 0);
    asyncParam.ret = 0;

    // ubs_hcom_channel_callback cb
    cb.arg = &asyncParam;
    cb.cb = CallAsyncCb;

    result = Channel_AsyncCall(clientChannel, &opInfo, &req, &cb);
    EXPECT_EQ(0, result);

    result = Channel_AsyncCall(0, &opInfo, &req, &cb);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_AsyncCall(clientChannel, nullptr, &req, &cb);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_AsyncCall(clientChannel, &opInfo, nullptr, &cb);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_AsyncCall(clientChannel, &opInfo, &req, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    sem_wait(&asyncParam.sem);
    sem_destroy(&asyncParam.sem);

    MOCKER_CPP(&NetChannel::AsyncCallInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_AsyncCall(channel, &opInfo, &req, &cb);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* Channel_SyncCallRaw */

    req = { data, dataSize };
    rsp = { data, dataSize };
    data[0] = 1;

    result = Channel_SyncCallRaw(clientChannel, &req, &rsp);
    EXPECT_EQ(0, result);

    result = Channel_SyncCallRaw(0, &req, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCallRaw(clientChannel, nullptr, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCallRaw(clientChannel, &req, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SyncCallRawInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_SyncCallRaw(clientChannel, &req, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* Channel_SyncCallRawSgl */
    result = RegCapiSglMem(client, capiLocalMrInfo, mrClient);
    EXPECT_EQ(0, result);
    result = RegCapiSglMem(service, capiRemoteMrInfo, mrService);
    EXPECT_EQ(0, result);

    data = (char *)malloc(dataSize);
    // Service_Message req
    req.data = data;
    req.size = dataSize;

    rsp.data = getRemoteMrInfo;
    rsp.size = sizeof(getRemoteMrInfo);

    reqInfo = { 0, 0, 0, 0 };
    reqInfo.opCode = NN_NO2;
    rspInfo = { 0, 0, 0, 0 };

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    Service_Request reqIov;
    reqIov.lAddress = capiLocalMrInfo[0].lAddress;
    reqIov.rAddress = getRemoteMrInfo[0].lAddress;
    reqIov.lKey = capiLocalMrInfo[0].lKey;
    reqIov.rKey = getRemoteMrInfo[0].lKey;
    reqIov.size = capiLocalMrInfo[0].size;

    Service_SglRequest reqRawSgl2;
    char *buff = (char *)(reqIov.lAddress);
    buff[0] = 1; // mark call
    reqRawSgl2.iov = &reqIov;
    reqRawSgl2.iovCount = 1;

    Service_Message rspRawSgl = { data, dataSize };
    data[0] = 1; // mark call
    result = Channel_SyncCallRawSgl(clientChannel, &reqRawSgl2, &rspRawSgl);
    EXPECT_EQ(0, result);

    result = Channel_SyncCallRawSgl(0, &reqRawSgl2, &rspRawSgl);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCallRawSgl(clientChannel, nullptr, &rspRawSgl);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCallRawSgl(clientChannel, &reqRawSgl2, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::SyncCallRawSglInner).defaults().will(returnValue(SER_INVALID_PARAM));
    result = Channel_SyncCallRawSgl(clientChannel, &reqRawSgl2, &rspRawSgl);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    DestoryCapiSglMem(client, mrClient);
    DestoryCapiSglMem(service, mrService);

    GlobalMockObject::verify();

    /* Channel_WriteRead */

    result = RegCapiSglMem(client, capiLocalMrInfo, mrClient);
    EXPECT_EQ(0, result);
    result = RegCapiSglMem(service, capiRemoteMrInfo, mrService);
    EXPECT_EQ(0, result);

    req.data = data;
    req.size = dataSize;

    rsp.data = getRemoteMrInfo;
    rsp.size = sizeof(getRemoteMrInfo);

    reqInfo = { 0, 0, 0, 0 };
    reqInfo.opCode = NN_NO2;
    rspInfo = { 0, 0, 0, 0 };

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, nullptr);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(clientChannel, nullptr, &req, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(0, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_SyncCall(clientChannel, &reqInfo, nullptr, &rspInfo, &rsp);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    Service_Request reqWrite;
    reqWrite.lAddress = capiLocalMrInfo[0].lAddress;
    reqWrite.rAddress = getRemoteMrInfo[0].lAddress;
    reqWrite.lKey = capiLocalMrInfo[0].lKey;
    reqWrite.rKey = getRemoteMrInfo[0].lKey;
    reqWrite.size = capiLocalMrInfo[0].size;

    /* write */
    result = Channel_Write(clientChannel, &reqWrite, NULL);
    EXPECT_EQ(0, result);

    result = Channel_Write(0, &reqWrite, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_Write(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::WriteInner, int (NetChannel::*)(const NetServiceRequest &, const NetCallback *))
        .defaults()
        .will(returnValue(SER_INVALID_PARAM));
    result = Channel_Write(clientChannel, &reqWrite, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    Service_SglRequest reqSgl;
    reqSgl.iov = &reqWrite;
    reqSgl.iovCount = 1;

    /* write sgl */
    result = Channel_WriteSgl(clientChannel, &reqSgl, NULL);
    EXPECT_EQ(0, result);

    result = Channel_WriteSgl(0, &reqSgl, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_WriteSgl(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::WriteSglInner, int (NetChannel::*)(const NetServiceSglRequest &, const NetCallback *))
        .defaults()
        .will(returnValue(SER_INVALID_PARAM));
    result = Channel_WriteSgl(clientChannel, &reqSgl, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* read */
    result = Channel_Read(clientChannel, &reqWrite, NULL);
    EXPECT_EQ(0, result);

    result = Channel_Read(0, &reqWrite, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_Read(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::ReadInner, int (NetChannel::*)(const NetServiceRequest &, const NetCallback *))
        .defaults()
        .will(returnValue(SER_INVALID_PARAM));
    result = Channel_Read(clientChannel, &reqWrite, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    GlobalMockObject::verify();

    /* read sgl */
    result = Channel_ReadSgl(clientChannel, &reqSgl, NULL);
    EXPECT_EQ(0, result);

    result = Channel_ReadSgl(0, &reqSgl, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    result = Channel_ReadSgl(clientChannel, nullptr, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    MOCKER_CPP(&NetChannel::ReadSglInner, int (NetChannel::*)(const NetServiceSglRequest &, const NetCallback *))
        .defaults()
        .will(returnValue(SER_INVALID_PARAM));
    result = Channel_ReadSgl(clientChannel, &reqSgl, NULL);
    EXPECT_EQ(SER_INVALID_PARAM, result);

    DestoryCapiSglMem(client, mrClient);
    DestoryCapiSglMem(service, mrService);
    GlobalMockObject::verify();

    free(data);
    data = nullptr;
    Channel_Destroy(clientChannel);
    Channel_Destroy(channel);
    Service_Stop(service);
    ubs_hcom_service_destroy(service);
    Service_Stop(client);
    ubs_hcom_service_destroy(client);
}


static int RequestReceivedContext(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    NN_LOG_INFO("Get context type start");
    Service_Message message;
    message.data = ubs_hcom_context_get_data(ctx);
    message.size = ubs_hcom_context_get_datalen(ctx);
    if (message.data == NULL) {
        NN_LOG_ERROR("failed to get message");
        return -1;
    }

    ubs_hcom_service_context_type type;
    if (ubs_hcom_context_get_type(ctx, &type) != 0) {
        NN_LOG_ERROR("Get context type failed");
        return -1;
    }

    cb.cb = CommonCb;
    cb.arg = NULL;
    if (type == SERVICE_RECEIVED_RAW) {
        // receive call message, need send response
        Service_Message req = message;
        Service_OpInfo sendOpInfo = { 0, 0, 0, 0 };

        ubs_hcom_service_context cloneCtx = Service_ContextClone(ctx);
        EXPECT_NE(0ul, cloneCtx);

        // in hcom receive thread, need async send message; in user thread, there is no limit
        auto ret = Service_ContextReplyRaw(cloneCtx, &req, &cb);
        Service_ContextDeClone(cloneCtx);
        EXPECT_EQ(ret, 0);
        return 0;
    }

    // SERVICE_RECEIVED type
    Service_OpInfo opInfo;
    Service_Message req = message;
    // send the same message back to verify
    Service_OpInfo sendOpInfo = opInfo;
    ubs_hcom_service_context cloneCtx = Service_ContextClone(ctx);
    EXPECT_NE(0ul, cloneCtx);

    // in hcom receive thread, need async send message; in user thread, there is no limit
    auto ret = Service_ContextReply(cloneCtx, &sendOpInfo, &req, &cb);
    Service_ContextDeClone(cloneCtx);
    EXPECT_EQ(ret, 0);

    return 0;
}

TEST_F(TestCapiService, ServiceContextTest)
{
    CreateCapiService(&RequestReceivedContext);
    CreateCapiClient();

    Net_Channel clientChannel;
    capiOptions.epSize = 1;
    auto result = ubs_hcom_service_connect(client, udsName, oobPort, "hello service c context", &clientChannel,
        &capiOptions);
    EXPECT_EQ(0, result);

    char testData[512];
    Service_Message req = { testData, sizeof(testData) };
    Service_Message rsp = { testData, sizeof(testData) };
    Service_OpInfo reqInfo = { 0, 0, 0, 0 };
    Service_OpInfo rspInfo = { 0, 0, 0, 0 };
    reqInfo.opCode = 1;
    reqInfo.timeout = 0;
    reqInfo.errorCode = 0xff;

    result = Channel_SyncCall(clientChannel, &reqInfo, &req, &rspInfo, &rsp);
    EXPECT_EQ(0, result);

    result = Channel_SyncCallRaw(clientChannel, &req, &rsp);
    EXPECT_EQ(0, result);

    Channel_Destroy(clientChannel);
    Channel_Destroy(channel);
    Service_Stop(service);
    ubs_hcom_service_destroy(service);
    Service_Stop(client);
    ubs_hcom_service_destroy(client);
}