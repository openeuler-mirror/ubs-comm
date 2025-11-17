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
#include <mockcpp/mockcpp.hpp>
#include <sys/epoll.h>

#include "net_mem_pool_fixed.h"
#include "service_ctx_store.h"
#include "hcom_service.h"
#include "net_service_default_imp.h"
#include "test_service_common.h"
#include "rdma_common.h"
#include "test_service_io.h"

using namespace ock::hcom;
TestCaseServiceIO::TestCaseServiceIO() {}
void TestCaseServiceIO::SetUp()
{
    MOCKER(ReadRoCEVersionFromFile).stubs().will(returnValue(0));
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}
void TestCaseServiceIO::TearDown()
{
    GlobalMockObject::verify();
}

int NewChannel(const std::string &ipPort, const NetChannelPtr &ch, const std::string &payload)
{
    NN_LOG_INFO("new channel call from " << ipPort << " payload: " << payload);
    return 0;
}

void BrokenChannel(const NetChannelPtr &ch)
{
    NN_LOG_INFO("ep broken");
}

int ReceivedRequest(NetServiceContext &context)
{
    NetServiceMessage message(context.MessageData(), context.MessageDataLen());

    if (context.OpType() == NetServiceContext::SER_RECEIVED_RAW) {
        char *receive = reinterpret_cast<char *>(message.data);
        if (receive[0] == 0) {
            // receive send message
            return 0;
        }

        // receive call message
        NetServiceMessage req = message;
        NetCallback *newCallback = NewCallback([](NetServiceContext &context) {}, std::placeholders::_1);

        // post send callback
        if ((context.Channel()->SendRaw(req, newCallback, context.RspCtx()) != 0)) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
        return 0;
    }

    if (context.OpCode() == 0) {
        NN_LOG_TRACE_INFO("receive msg, channel id " << context.Channel()->Id() << ", info " <<
            reinterpret_cast<char *>(context.MessageData()));
    } else {
        NetServiceMessage req = message;
        // send the same message back to verify
        NetCallback *newCallback = NewCallback([](NetServiceContext &context) {}, std::placeholders::_1);

        // post send callback
        if ((context.Channel()->Send(context.OpInfo(), req, newCallback, context.RspCtx())) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
    }
    return 0;
}

int PostSendRequest(NetServiceContext context)
{
    return 0;
}
int OneSideDownRequest(NetServiceContext context)
{
    return 0;
}

UBSHcomNetMemoryAllocatorPtr memPtr = nullptr;
int RndvAllocate(uint64_t size, uintptr_t &outAddress, uint32_t &outKey)
{
    outKey = memPtr->MrKey();
    return memPtr->Allocate(size, outAddress);
}

int RndvFree(uintptr_t addressFree)
{
    return memPtr->Free(addressFree);
}

int RndvHandler(NetServiceRndvContext &ctx)
{
    // step1 direct handle message

    // step2 rsp message
    int ret = 0;
    NetServiceMessage req(&ret, sizeof(ret));
    NetServiceOpInfo opInfo {};
    NetCallback *newCallback = NewCallback([](NetServiceContext &context) {}, std::placeholders::_1);
    if (ctx.ReplyRndv(opInfo, req, newCallback) != 0) {
        NN_LOG_ERROR("Reply rndv message failed");
    }

    // step3 free context
    ctx.FreeMessage();
    return SER_OK;
}

bool CreateService(NetService *&service, NetServiceProtocol protocol, const std::string &mask, const std::string &ip,
    bool startOob)
{
    if (service != nullptr) {
        NN_LOG_ERROR("service already created");
        return false;
    }

    if (protocol == UBSHcomNetDriverProtocol::SHM) {
        NN_LOG_ERROR("service not support shm protocol");
        return false;
    }

    std::string name;
    static int nameNeed = 0;
    if (startOob) {
        name = "test_service_server_";
        name += std::to_string(nameNeed++);
    } else {
        name = "test_service_client_";
        name += std::to_string(nameNeed++);
    }

    service = NetService::Instance(protocol, name, startOob);
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create service already created");
        return false;
    }

    NetServiceOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = TEST_SERVICE_SEG_SIZE;
    options.mrSendReceiveSegCount = 32;
    options.enableTls = false;

    options.SetNetDeviceIpMask(mask);
    options.SetWorkerGroups("1");

    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    service->SetOobIpAndPort(ip, TEST_SERVICE_PORT);
    service->RegisterNewChannelHandler(NewChannel);
    service->RegisterChannelBrokenHandler(BrokenChannel, ock::hcom::BROKEN_ALL);
    service->RegisterOpReceiveHandler(0, ReceivedRequest);
    service->RegisterOpSentHandler(0, PostSendRequest);
    service->RegisterOpOneSideHandler(0, OneSideDownRequest);
    int result = 0;
    if ((result = service->Start(options)) != 0) {
        NN_LOG_ERROR("failed to initialize service " << result);
        return false;
    }
    NN_LOG_INFO("service initialized");
    return true;
}

bool Connect(NetService *service, NetChannelPtr &ch, const std::string &ip, bool selfPoll)
{
    if (service == nullptr) {
        NN_LOG_ERROR("service is null");
        return false;
    }

    NetServiceConnectOptions options {};
    if (selfPoll) {
        options.flags = NET_EP_SELF_POLLING;
    }
    int result = service->Connect(ip, TEST_SERVICE_PORT, "hello service", ch, options);
    if (result != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

bool RegSglMem(NetService *client, NetService *service, uint32_t dataSize, NetServiceRequest iov[], uint16_t iovSize)
{
    for (uint16_t i = 0; i < iovSize; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = client->RegisterMemoryRegion(dataSize, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        iov[i].lAddress = mr->GetAddress();
        iov[i].lKey = mr->GetLKey();
        iov[i].size = dataSize;

        result = service->RegisterMemoryRegion(dataSize, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }

        iov[i].rAddress = mr->GetAddress();
        iov[i].rKey = mr->GetLKey();
    }

    return true;
}

static void TestServiceSends(NetChannelPtr &ch, NetServiceRequest *iov, uint16_t iovCnt)
{
    char data[128];
    NetServiceMessage message(data, sizeof(data));
    NetServiceOpInfo opInfo {};
    opInfo.opCode = 0;

    auto result = ch->Send(opInfo, message, nullptr);
    ASSERT_EQ(result, SER_OK);

    data[0] = 0; // mark send
    result = ch->SendRaw(message, nullptr);
    ASSERT_EQ(result, SER_OK);

    NetServiceSglRequest request;
    request.iov = iov;
    request.iovCount = iovCnt;

    memset(reinterpret_cast<void *>(iov[0].lAddress), 0, 1); // mark send
    result = ch->SendRawSgl(request, nullptr);
    ASSERT_EQ(result, SER_OK);
}

static void TestServiceCall(NetChannelPtr &ch, NetServiceRequest *iov, uint16_t iovCnt)
{
    char data1[128];
    char data2[128];
    NetServiceMessage req(data1, sizeof(data1));
    NetServiceMessage rsp(data2, sizeof(data2));
    NetServiceOpInfo reqInfo {};
    NetServiceOpInfo rspInfo {};
    reqInfo.opCode = 1;
    reqInfo.timeout = 1;

    auto result = ch->SyncCall(reqInfo, req, rspInfo, rsp);
    ASSERT_EQ(result, SER_OK);

    result = memcmp(&reqInfo, &rspInfo, sizeof(NetServiceOpInfo));
    ASSERT_EQ(result, SER_OK);

    data1[0] = 1; // mark call
    result = ch->SyncCallRaw(req, rsp);
    ASSERT_EQ(result, SER_OK);

    int ret = 0;
    sem_t sem;
    sem_init(&sem, 0, 0);
    NetCallback *newCallback = NewCallback(
        [&sem, &ret, &rsp](NetServiceContext &context) {
            if (context.Result() != 0 || context.MessageDataLen() != rsp.size) {
                NN_LOG_ERROR("Async call result failed or get unwanted message");
                ret = -1;
                sem_post(&sem);
                return;
            }

            memcpy(rsp.data, context.MessageData(), context.MessageDataLen());
            sem_post(&sem);
        },
        std::placeholders::_1);
    ASSERT_NE(newCallback, nullptr);

    result = ch->AsyncCall(reqInfo, req, newCallback);

    sem_wait(&sem);
    sem_destroy(&sem);
    ASSERT_EQ(ret, SER_OK);

    // validate data
    result = memcmp(data1, data2, sizeof(data1));
    ASSERT_EQ(result, SER_OK);

    NetServiceSglRequest reqSgl;
    char *buff = reinterpret_cast<char *>(iov[0].lAddress);
    buff[0] = 1; // mark call
    reqSgl.iov = iov;
    reqSgl.iovCount = iovCnt;

    result = ch->SyncCallRawSgl(reqSgl, rsp);
    ASSERT_EQ(result, SER_OK);
}

TEST_F(TestCaseServiceIO, ALL_IO)
{
    setenv("HCOM_TRACE_LEVEL", "2", 1);
    NetService *client = nullptr;
    NetService *server = nullptr;

    auto result = CreateService(client, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, false);
    ASSERT_EQ(result, true);

    result = CreateService(server, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, true);
    ASSERT_EQ(result, true);

    NetChannelPtr ch;
    result = Connect(client, ch, TEST_SERVICE_IP, false);
    ASSERT_EQ(result, true);

    NetServiceRequest iov[NET_SGE_MAX_IOV];
    result = RegSglMem(client, server, NN_NO8, iov, NET_SGE_MAX_IOV);
    ASSERT_EQ(result, true);

    TestServiceSends(ch, iov, NET_SGE_MAX_IOV);
    TestServiceCall(ch, iov, NET_SGE_MAX_IOV);

    client->Stop();
    server->Stop();
    NetService::DestroyInstance(client->Name());
    NetService::DestroyInstance(server->Name());
    std::string dumpStr = NetService::TraceLog();
    NN_LOG_INFO(dumpStr);
}

TEST_F(TestCaseServiceIO, START_FAILED)
{
    NetService *service = nullptr;

    std::string name = "SERVICE";

    service = NetService::Instance(UBSHcomNetDriverProtocol::TCP, name, true);

    NetServiceOptions options {};
    int result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    options.enableRndv = true;
    service->RegisterNewChannelHandler(NewChannel);
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterRndvAllocateHandler(RndvAllocate);
    service->RegisterRndvFreeHandler(RndvFree);
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterRndvHandler(RndvHandler);
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterChannelBrokenHandler(BrokenChannel, ock::hcom::BROKEN_ALL);
    options.maxTypeIndexSize = 17;
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    options.maxTypeIndexSize = 1;
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterOpReceiveHandler(0, ReceivedRequest);
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterOpSentHandler(0, PostSendRequest);
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    service->RegisterOpOneSideHandler(0, OneSideDownRequest);
    options.periodicThreadNum = 0;
    result = service->Start(options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    options.periodicThreadNum = 1;
    MOCKER(epoll_create).stubs().will(returnValue(-1));
    result = service->Start(options);
    ASSERT_EQ(result, NN_INVALID_IP);
    GlobalMockObject::verify();

    MOCKER(::setsockopt).defaults().will(returnValue(-1));
    options.SetNetDeviceIpMask(TEST_SERVICE_MASK);
    options.SetWorkerGroups("1");
    service->SetOobIpAndPort(TEST_SERVICE_IP, TEST_SERVICE_PORT);
    result = service->Start(options);
    ASSERT_EQ(result, NN_OOB_LISTEN_SOCKET_ERROR);
    NetService::DestroyInstance(service->Name());
    GlobalMockObject::verify();
}

TEST_F(TestCaseServiceIO, CONNECT_FAILED)
{
    NetService *client = nullptr;
    NetService *server = nullptr;

    CreateService(client, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, false);
    CreateService(server, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, true);

    NetChannelPtr ch;

    NetServiceConnectOptions options {};
    options.epSize = 0;
    int result = client->Connect(TEST_SERVICE_IP, TEST_SERVICE_PORT, "hello service", ch, options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    options.epSize = 1;
    options.index = 1;
    result = client->Connect(TEST_SERVICE_IP, TEST_SERVICE_PORT, "hello service", ch, options);
    ASSERT_EQ(result, SER_INVALID_PARAM);

    options.index = 0;
    MOCKER_CPP(&ServiceSerializeConnInfo, int (*)(ServiceConnInfo &, const std::string &, std::string &))
        .defaults()
        .will(returnObjectList(500));
    result = client->Connect(TEST_SERVICE_IP, TEST_SERVICE_PORT, "hello service", ch, options);
    ASSERT_EQ(result, SER_INVALID_PARAM);
    GlobalMockObject::verify();

    options.clientGrpNo = 1;
    result = client->Connect(TEST_SERVICE_IP, TEST_SERVICE_PORT, "hello service", ch, options);
    ASSERT_EQ(result, NN_ERROR);

    client->Stop();
    NetService::DestroyInstance(client->Name());
    server->Stop();
    NetService::DestroyInstance(server->Name());
}

TEST_F(TestCaseServiceIO, CONNECT_MULTI_PROTOCOL)
{
    NetService *client = nullptr;
    NetService *server = nullptr;

    auto result = CreateService(client, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, false);
    ASSERT_EQ(result, true);

    result = CreateService(server, UBSHcomNetDriverProtocol::TCP, TEST_SERVICE_MASK, TEST_SERVICE_IP, true);
    ASSERT_EQ(result, true);

    NetService *client1 = nullptr;
    NetService *server1 = nullptr;

    result = CreateService(client1, UBSHcomNetDriverProtocol::RDMA, TEST_SERVICE_MASK1, TEST_SERVICE_IP1, false);
    ASSERT_EQ(result, true);

    result = CreateService(server1, UBSHcomNetDriverProtocol::RDMA, TEST_SERVICE_MASK1, TEST_SERVICE_IP1, true);
    ASSERT_EQ(result, true);

    NetChannelPtr ch;
    result = Connect(client, ch, TEST_SERVICE_IP, false);
    ASSERT_EQ(result, true);

    NetChannelPtr ch1;
    result = Connect(client1, ch1, TEST_SERVICE_IP1, false);
    ASSERT_EQ(result, true);

    client->Stop();
    NetService::DestroyInstance(client->Name());
    server->Stop();
    NetService::DestroyInstance(server->Name());
    client1->Stop();
    NetService::DestroyInstance(client1->Name());
    server1->Stop();
    NetService::DestroyInstance(server1->Name());
}