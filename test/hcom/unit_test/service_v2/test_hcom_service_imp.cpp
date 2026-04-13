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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom.h"
#include "service_imp.h"
#include "service_channel_imp.h"
#include "net_rdma_driver_oob.h"
#include "net_rdma_async_endpoint.h"

namespace ock {
namespace hcom {
std::string name = "service1";
std::string serviceIpInfo = "127.0.0.1";
std::string serviceUdsPath = "/home/udsPath";
std::string oobPort = "1234";

class TestHcomServiceImp : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
    HcomServiceImp *service;
};

void TestHcomServiceImp::SetUp()
{
    UBSHcomServiceOptions options{};
    service = new HcomServiceImp(UBSHcomNetDriverProtocol::RDMA, name, options);
    service->mOptions.enableMultiRail = false;
    service->mEnableMrCache = true;
    ASSERT_NE(service, nullptr);
}

void TestHcomServiceImp::TearDown()
{
    if (service != nullptr) {
        delete service;
    }
    GlobalMockObject::verify();
}

int NewChannel(const std::string &ipPort, const UBSHcomChannelPtr &ch, const std::string &payload)
{
    return 0;
}

TEST_F(TestHcomServiceImp, TestServiceBind)
{
    EXPECT_EQ(service->Bind("tcp://" + serviceIpInfo + ":" + oobPort, NewChannel), static_cast<int>(SER_OK));
    EXPECT_EQ(service->Bind("uds://" + serviceUdsPath, NewChannel), static_cast<int>(SER_OK));
    EXPECT_EQ(service->Bind("abc://" + serviceIpInfo, NewChannel), static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->Bind("abc" + serviceIpInfo, NewChannel), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceAddListener)
{
    EXPECT_NO_FATAL_FAILURE(service->AddListener("tcp://" + serviceIpInfo + ":" + oobPort, 1));
    EXPECT_NO_FATAL_FAILURE(service->AddListener("uds://" + serviceUdsPath, 1));
    EXPECT_NO_FATAL_FAILURE(service->AddListener("abc://" + serviceIpInfo, 1));
    EXPECT_NO_FATAL_FAILURE(service->AddListener("abc" + serviceIpInfo, 1));
}

TEST_F(TestHcomServiceImp, TestServiceAddTcpOobListener)
{
    EXPECT_EQ(service->AddTcpOobListener(serviceIpInfo + ":" + oobPort, 1), static_cast<int>(SER_OK));
    EXPECT_EQ(service->AddTcpOobListener(serviceIpInfo + oobPort, 1), static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->AddTcpOobListener("127.127.127.127.127:" + oobPort, 1), static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->AddTcpOobListener(serviceIpInfo + ":" + oobPort, 1), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceAddUdsOobListener)
{
    EXPECT_EQ(service->AddUdsOobListener(serviceUdsPath, 1), 0);
    EXPECT_EQ(service->AddUdsOobListener(serviceUdsPath, 1), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceStart)
{
    EXPECT_EQ(service->Start(), static_cast<int>(SER_INVALID_PARAM));

    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {},
        UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterRecvHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterIdleHandler([](const UBSHcomNetWorkerIndex &ctx) {return 0;});
    EXPECT_EQ(service->Start(), static_cast<int>(NN_INVALID_IP));

    UBSHcomNetDriver *driver = new (std::nothrow) NetDriverRDMAWithOob(name, false, UBSHcomNetDriverProtocol::RDMA);
    MOCKER_CPP(&UBSHcomNetDriver::IsStarted).stubs().will(returnValue(true));
    MOCKER_CPP_VIRTUAL(driver, &UBSHcomNetDriver::Initialize).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&UBSHcomNetDriver::Instance).stubs().will(returnValue(driver));
    EXPECT_EQ(service->Start(), static_cast<int>(NN_ERROR));
}

TEST_F(TestHcomServiceImp, TestServiceStartSuccess)
{
    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {},
        UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterRecvHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterIdleHandler([](const UBSHcomNetWorkerIndex &ctx) {return 0;});
    service->SetDeviceIpMask({serviceIpInfo});
    UBSHcomNetDriver *driver = new (std::nothrow) NetDriverRDMAWithOob(name, false, UBSHcomNetDriverProtocol::RDMA);
    MOCKER_CPP(&UBSHcomNetDriver::Instance).stubs().will(returnValue(driver));
    MOCKER_CPP_VIRTUAL(driver, &UBSHcomNetDriver::Initialize).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP_VIRTUAL(driver, &UBSHcomNetDriver::Start).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&UBSHcomNetDriver::IsStarted).stubs().will(returnValue(true));
    EXPECT_EQ(service->Start(), 0);
    EXPECT_EQ(service->Start(), 0);
}

TEST_F(TestHcomServiceImp, TestServiceStartFailed)
{
    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {},
        UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterRecvHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    EXPECT_EQ(service->Start(), static_cast<int>(NN_INVALID_IP));

    MOCKER_CPP(&HcomServiceImp::CreatePeriodicMgr)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomServiceImp::CreateCtxMemPool)
        .stubs()
        .will(returnValue(static_cast<int>(SER_NEW_OBJECT_FAILED)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->Start(), static_cast<int>(SER_NEW_OBJECT_FAILED));
    EXPECT_EQ(service->Start(), static_cast<int>(SER_NEW_OBJECT_FAILED));
}

SerResult MockGetEnableDevCnt(std::string ipMask, uint16_t &enableDevCount, std::vector<std::string> &enableIps,
    std::string ipGroup)
{
    enableDevCount = 2;
    return SER_OK;
}

TEST_F(TestHcomServiceImp, TestServiceStartMultirail)
{
    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {},
        UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterRecvHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});

    UBSHcomNetDriver *driver = new (std::nothrow) NetDriverRDMAWithOob(name, false, UBSHcomNetDriverProtocol::RDMA);
    MOCKER_CPP(&UBSHcomNetDriver::IsStarted).stubs().will(returnValue(true));
    MOCKER_CPP_VIRTUAL(driver, &UBSHcomNetDriver::Initialize).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&UBSHcomNetDriver::Instance).stubs().will(returnValue(driver));

    service->mOptions.startOobSvr = true;
    service->mOptions.enableMultiRail = true;
    UBSHcomWorkerGroupInfo groupInfo;
    groupInfo.cpuIdsRange = {1, 1};
    service->mOptions.workerGroupInfos = {{groupInfo}, {groupInfo}};

    service->SetDeviceIpGroups({serviceIpInfo, serviceIpInfo});
    MOCKER_CPP_VIRTUAL(driver, &UBSHcomNetDriver::Start).stubs().will(returnValue(static_cast<int>(SER_OK)));

    MOCKER_CPP(&RDMADeviceHelper::GetEnableDeviceCount)
        .stubs()
        .will(invoke(MockGetEnableDevCnt));
    EXPECT_EQ(service->Bind("tcp://" + serviceIpInfo + ":" + oobPort, NewChannel), 0);
    EXPECT_EQ(service->Start(), 0);
}

TEST_F(TestHcomServiceImp, TestServiceCreateOobListenersFailed)
{
    UBSHcomNetOobListenerOptions option {};
    UBSHcomNetDriverOptions opt {};
    opt.oobType = NetDriverOobType::NET_OOB_TCP;
    for (int i = 0; i < NN_NO65536; i++) {
    service->mOptions.oobOption[std::to_string(i)] = option;
    }
    EXPECT_EQ(service->CreateOobListeners(opt), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceCreateOobUdsListenersFailed)
{
    UBSHcomNetOobUDSListenerOptions option {};
    UBSHcomNetDriverOptions opt {};
    opt.oobType = NetDriverOobType::NET_OOB_UDS;
    for (int i = 0; i < NN_NO65536; i++) {
        service->mOptions.udsOobOption[std::to_string(i)] = option;
    }
    EXPECT_EQ(service->CreateOobUdsListeners(opt), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceCreateOobUdsListeners)
{
    UBSHcomNetOobUDSListenerOptions option {};
    EXPECT_EQ(option.Set(serviceUdsPath, 1), true);

    UBSHcomNetDriverOptions opt {};
    opt.oobType = NetDriverOobType::NET_OOB_UDS;
    EXPECT_EQ(service->CreateOobListeners(opt), static_cast<int>(SER_INVALID_PARAM));
    service->mOptions.udsOobOption[serviceUdsPath] = option;
    EXPECT_EQ(service->CreateOobListeners(opt), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceDoDestroy)
{
    EXPECT_EQ(service->DoDestroy(name), static_cast<int>(SER_OK));

    service->mStarted = true;
    EXPECT_EQ(service->DoDestroy(name), static_cast<int>(SER_OK));
}


SerResult MockDoConnect(const std::string &serverUrl, SerConnInfo &opt,
    const std::string &payLoad, UBSHcomChannelPtr &channel)
{
    channel = new (std::nothrow) HcomChannelImp(opt.channelId, false, opt.options);
    return SER_OK;
}

TEST_F(TestHcomServiceImp, TestServiceConnect)
{
    UBSHcomChannelPtr ch;
    UBSHcomConnectOptions opt;
    opt.linkCount = NN_NO1;
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt), static_cast<int>(SER_STOP));

    MOCKER_CPP(&HcomServiceImp::DoConnect)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(invoke(MockDoConnect));
    service->mStarted = true;
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt), static_cast<int>(SER_INVALID_PARAM));

    MOCKER_CPP(HcomEnv::RndvThreshold).stubs().will(returnValue(NN_NO65536));
    MOCKER_CPP(&HcomServiceImp::ExchangeTimestamp)
        .stubs()
        .will(returnValue(static_cast<int>(SER_TIMEOUT)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt), static_cast<int>(SER_TIMEOUT));
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestExchangeTimestamp)
{
    UBSHcomChannel *ch = nullptr;
    EXPECT_EQ(service->ExchangeTimestamp(ch), static_cast<int>(SER_ERROR));
    InnerConnectOptions opt{};
    ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    MOCKER_CPP(&HcomChannelImp::SyncCallInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->ExchangeTimestamp(ch), static_cast<int>(SER_OK));
    if (ch != nullptr) {
        delete ch;
        ch = nullptr;
    }
}

TEST_F(TestHcomServiceImp, TestExchangeTimestamp2)
{
    InnerConnectOptions opt{};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    MOCKER_CPP(&HcomChannelImp::SyncCallInner).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    EXPECT_EQ(service->ExchangeTimestamp(ch), static_cast<int>(SER_ERROR));
    if (ch != nullptr) {
        delete ch;
        ch = nullptr;
    }
}

TEST_F(TestHcomServiceImp, TestServiceExchangeTimeStampHandle)
{
    UBSHcomServiceContext ctx{};
    ctx.mResult = SER_ERROR;
    EXPECT_EQ(service->ServiceExchangeTimeStampHandle(ctx), static_cast<int>(SER_ERROR));

    ctx.mResult = SER_OK;
    MOCKER_CPP(&UBSHcomServiceContext::MessageDataLen)
        .stubs()
        .will(returnValue(static_cast<uint32_t>(sizeof(HcomExchangeTimestamp) - NN_NO1)))
        .then(returnValue(static_cast<uint32_t>(sizeof(HcomExchangeTimestamp))));
    EXPECT_EQ(service->ServiceExchangeTimeStampHandle(ctx), static_cast<int>(SER_INVALID_PARAM));

    HcomExchangeTimestamp timestamp{};
    timestamp.deltaTimeStamp = NN_NO0;

    HcomExchangeTimestamp timestamp2{};
    timestamp2.deltaTimeStamp = NN_NO1024;
    MOCKER_CPP(&UBSHcomServiceContext::MessageData)
        .stubs()
        .will(returnValue(static_cast<void *>(&timestamp)))
        .then(returnValue(static_cast<void *>(&timestamp2)));
    EXPECT_EQ(service->ServiceExchangeTimeStampHandle(ctx), static_cast<int>(SER_INVALID_PARAM));

    UBSHcomChannelPtr ch1 = nullptr;
    InnerConnectOptions opt{};
    UBSHcomChannelPtr ch2 = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch2.Get(), nullptr);
    MOCKER_CPP(&UBSHcomServiceContext::Channel).stubs().will(returnValue(ch1)).then(returnValue(ch2));
    EXPECT_EQ(service->ServiceExchangeTimeStampHandle(ctx), static_cast<int>(SER_INVALID_PARAM));

    MOCKER_CPP(&HcomChannelImp::ReplyInner).stubs().will(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->ServiceExchangeTimeStampHandle(ctx), static_cast<int>(SER_OK));
}

SerResult MockDoConnectInner(const std::string &serverUrl, SerConnInfo &opt,
    const std::string &payLoad, std::vector<UBSHcomNetEndpointPtr> &epVector, uint32_t &totalBandWidth)
{
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    epVector.push_back(ep);
    return SER_OK;
}

TEST_F(TestHcomServiceImp, TestServiceDoConnect)
{
    UBSHcomChannelPtr ch;
    UBSHcomConnectOptions opt;
    opt.linkCount = NN_NO1;
    NetMemPoolFixedOptions options = {};
    service->mContextMemPool = new (std::nothrow) NetMemPoolFixed("ServiceContextTimer-test", options);
    service->mPeriodicMgr = new (std::nothrow) HcomPeriodicManager(NN_NO1, "mName");
    service->mPgtable = new NetPgTable(HcomServiceImp::pgdAlloc, HcomServiceImp::pgdFree);
    SerConnInfo connInfo(0, NetUuid::GenerateUuid(serviceIpInfo), NN_NO1, service->mOptions.chBrokenPolicy, opt);
    MOCKER_CPP(&HcomServiceImp::DoConnectInner)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)))
        .then(invoke(MockDoConnectInner));
    EXPECT_EQ(service->DoConnect("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", ch),
        static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->DoConnect("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", ch),
        static_cast<int>(SER_NEW_OBJECT_FAILED));
    EXPECT_EQ(service->DoConnect("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", ch),
        static_cast<int>(SER_OK));
    service->mPeriodicMgr.Set(nullptr);
    service->mContextMemPool.Set(nullptr);
    service->mPgtable.Set(nullptr);
    ch.Set(nullptr);
}

TEST_F(TestHcomServiceImp, TestServiceDoConnectInner)
{
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    UBSHcomConnectOptions opt;
    opt.linkCount = NN_NO1;
    SerConnInfo connInfo(0, NetUuid::GenerateUuid(serviceIpInfo), NN_NO1, service->mOptions.chBrokenPolicy, opt);
    std::vector<UBSHcomNetEndpointPtr> epVector;
    uint32_t bandWidth = 0;
    MOCKER_CPP(&SerConnInfo::Serialize)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->DoConnectInner("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", epVector, bandWidth),
        static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->DoConnectInner("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", epVector, bandWidth),
        static_cast<int>(NN_NOT_INITIALIZED));
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::Connect,
        SerResult(UBSHcomNetDriver::*)(const std::string &, const std::string &, UBSHcomNetEndpointPtr &,
        uint32_t, uint8_t, uint8_t, uint64_t))
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->DoConnectInner("tcp://" + serviceIpInfo + ":" + oobPort, connInfo, "", epVector, bandWidth),
        static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceDoChooseDriver)
{
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    int8_t selectDevIndex = 0;
    uint8_t selectBandWidth = 0;
    UBSHcomNetDriver *driver = nullptr;
    EXPECT_NO_FATAL_FAILURE(service->DoChooseDriver(0, 0, selectDevIndex, selectBandWidth, driver));
}

TEST_F(TestHcomServiceImp, TestServiceChooseDriver)
{
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    UBSHcomNetDriver *driver = nullptr;
    OOBTCPConnection conn(NN_NO6);
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Receive)
        .stubs()
        .will(returnValue(static_cast<int>(NN_PARAM_INVALID)))
        .then(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send)
        .stubs()
        .will(returnValue(static_cast<int>(NN_PARAM_INVALID)))
        .then(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP(&HcomServiceImp::DoChooseDriver)
        .stubs();
    EXPECT_EQ(service->ChooseDriver(conn, driver), static_cast<int>(NN_PARAM_INVALID));
    EXPECT_EQ(service->ChooseDriver(conn, driver), static_cast<int>(SER_ERROR));
    driver = driverPtr.Get();
    EXPECT_EQ(service->ChooseDriver(conn, driver), static_cast<int>(NN_PARAM_INVALID));
    EXPECT_EQ(service->ChooseDriver(conn, driver), static_cast<int>(SER_OK));
    driver = nullptr;
}

TEST_F(TestHcomServiceImp, TestServiceDisconnect)
{
    EXPECT_NO_FATAL_FAILURE(service->Disconnect(nullptr));
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NO_FATAL_FAILURE(service->Disconnect(ch));
}

TEST_F(TestHcomServiceImp, TestServiceRegisterMemoryRegion)
{
    UBSHcomRegMemoryRegion mr {};
    EXPECT_EQ(service->RegisterMemoryRegion(NN_NO1024, mr), static_cast<int>(NN_ERROR));

    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::CreateMemoryRegion,
        SerResult(UBSHcomNetDriver::*)(uint64_t, UBSHcomNetMemoryRegionPtr &))
        .stubs()
        .will(returnValue(static_cast<int>(NN_ERROR)));
    EXPECT_EQ(service->RegisterMemoryRegion(NN_NO1024, mr), static_cast<int>(NN_ERROR));
}

NResult MockCreateMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr)
{
    mr = new (std::nothrow) RDMAMemoryRegion(name, nullptr, address, size);
    return NN_OK;
}

NResult MockCreateMemoryRegion2(uint64_t size, UBSHcomNetMemoryRegionPtr &mr)
{
    mr = new (std::nothrow) RDMAMemoryRegion(name, nullptr, size);
    return NN_OK;
}

TEST_F(TestHcomServiceImp, TestServiceRegisterMemoryRegion2)
{
    UBSHcomRegMemoryRegion mr {};
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::CreateMemoryRegion,
        SerResult(UBSHcomNetDriver::*)(uint64_t, UBSHcomNetMemoryRegionPtr &))
        .stubs()
        .will(invoke(MockCreateMemoryRegion2));

    MOCKER_CPP(&PgTable::Insert).stubs().will(returnValue(0)).then(returnValue(static_cast<int>(NN_ERROR)));
    EXPECT_EQ(service->RegisterMemoryRegion(NN_NO1024, mr), static_cast<int>(NN_OK));
    EXPECT_EQ(service->RegisterMemoryRegion(NN_NO1024, mr), static_cast<int>(NN_ERROR));
}

TEST_F(TestHcomServiceImp, TestServiceRegisterMemoryRegion3)
{
    UBSHcomRegMemoryRegion mr {};
    uintptr_t addr = 0;
    EXPECT_EQ(service->RegisterMemoryRegion(addr, NN_NO1024, mr), static_cast<int>(NN_ERROR));

    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::CreateMemoryRegion,
        SerResult(UBSHcomNetDriver::*)(uintptr_t, uint64_t, UBSHcomNetMemoryRegionPtr &))
        .stubs()
        .will(returnValue(static_cast<int>(NN_ERROR)));
    EXPECT_EQ(service->RegisterMemoryRegion(addr, NN_NO1024, mr), static_cast<int>(NN_ERROR));
}

TEST_F(TestHcomServiceImp, TestServiceRegisterMemoryRegion4)
{
    UBSHcomRegMemoryRegion mr {};
    uintptr_t addr = 0;
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::CreateMemoryRegion,
        SerResult(UBSHcomNetDriver::*)(uintptr_t, uint64_t, UBSHcomNetMemoryRegionPtr &))
        .stubs()
        .will(invoke(MockCreateMemoryRegion));

    MOCKER_CPP(&PgTable::Insert).stubs().will(returnValue(0)).then(returnValue(static_cast<int>(NN_ERROR)));
    EXPECT_EQ(service->RegisterMemoryRegion(addr, NN_NO1024, mr), static_cast<int>(NN_OK));
    EXPECT_EQ(service->RegisterMemoryRegion(addr, NN_NO1024, mr), static_cast<int>(NN_ERROR));
}

TEST_F(TestHcomServiceImp, TestServiceDestroyMemoryRegion)
{
    UBSHcomRegMemoryRegion mr {};
    EXPECT_NO_FATAL_FAILURE(service->DestroyMemoryRegion(mr));
    mr.mHcomMrs.resize(1);
    EXPECT_NO_FATAL_FAILURE(service->DestroyMemoryRegion(mr));
    NetDriverPtr driverPtr = new (std::nothrow) NetDriverRDMAWithOob(name, false, RDMA);
    service->mDriverPtrs.push_back(driverPtr);
    MOCKER_CPP_VIRTUAL(*(service->mDriverPtrs[0].Get()), &UBSHcomNetDriver::DestroyMemoryRegion)
        .stubs();

    mr.mHcomMrs.resize(0);
    UBSHcomMemoryRegionPtr mrPtr = new (std::nothrow) RDMAMemoryRegion(name, nullptr, 0, 0);
    PgtRegion *pgtRegion = new PgtRegion();
    mrPtr->mPgRegion = reinterpret_cast<uintptr_t>(pgtRegion);
    mr.mHcomMrs.emplace_back(mrPtr);
    EXPECT_NO_FATAL_FAILURE(service->DestroyMemoryRegion(mr));
}

TEST_F(TestHcomServiceImp, TestServiceSetOptions)
{
    std::pair<uint32_t, uint32_t> cpuIdsPair;
    EXPECT_NO_FATAL_FAILURE(service->AddWorkerGroup(0, 0, cpuIdsPair));
    EXPECT_NO_FATAL_FAILURE(service->AddWorkerGroup(0, 0, cpuIdsPair, 0, NN_NO4));
    UBSHcomServiceLBPolicy lbPolicy {};
    EXPECT_NO_FATAL_FAILURE(service->SetConnectLBPolicy(lbPolicy));
    UBSHcomTlsOptions opt {};
    EXPECT_NO_FATAL_FAILURE(service->SetTlsOptions(opt));
    UBSHcomConnSecureOptions secureOpt {};
    EXPECT_NO_FATAL_FAILURE(service->SetConnSecureOpt(secureOpt));
    uint16_t timeOutSec = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetTcpUserTimeOutSec(timeOutSec));
}

TEST_F(TestHcomServiceImp, TestServiceSetOptions2)
{
    bool tcpSendZCopy = false;
    EXPECT_NO_FATAL_FAILURE(service->SetTcpSendZCopy(tcpSendZCopy));
    uint16_t depth = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetCompletionQueueDepth(depth));
    uint32_t sqSize = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetSendQueueSize(sqSize));
    uint32_t rqSize = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetRecvQueueSize(rqSize));
    uint32_t prePostSize = 10;
    EXPECT_NO_FATAL_FAILURE(service->SetQueuePrePostSize(prePostSize));
    uint16_t pollSize = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetPollingBatchSize(pollSize));
}

TEST_F(TestHcomServiceImp, TestServiceSetOptions3)
{
    uint16_t pollTimeout = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetEventPollingTimeOutUs(pollTimeout));
    uint32_t threadNum = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetTimeOutDetectionThreadNum(threadNum));
    uint32_t maxConnCount = 0;
    EXPECT_NO_FATAL_FAILURE(service->SetMaxConnectionCount(maxConnCount));
    UBSHcomHeartBeatOptions opt {};
    EXPECT_NO_FATAL_FAILURE(service->SetHeartBeatOptions(opt));
    UBSHcomMultiRailOptions multiRailOpt {};
    EXPECT_NO_FATAL_FAILURE(service->SetMultiRailOptions(multiRailOpt));
}

TEST_F(TestHcomServiceImp, TestServiceGenerateUuid)
{
    std::string uuid;
    MOCKER_CPP(&HcomServiceImp::GetIpAddressByIpPort)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    MOCKER(&BuffToHexString)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true))
        .then(returnValue(false))
        .then(returnValue(true));
    EXPECT_EQ(service->GenerateUuid(serviceIpInfo, NN_NO1, uuid), static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->GenerateUuid(serviceIpInfo, NN_NO1, uuid), static_cast<int>(SER_ERROR));
    EXPECT_EQ(service->GenerateUuid(serviceIpInfo, NN_NO1, uuid), static_cast<int>(SER_OK));
    EXPECT_EQ(service->GenerateUuid(NN_NO123, NN_NO1, uuid), static_cast<int>(SER_ERROR));
    EXPECT_EQ(service->GenerateUuid(NN_NO123, NN_NO1, uuid), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceEmplaceNewEndpoint)
{
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    ConnectingEpInfoPtr epInfo {};
    SerConnInfo connInfo {};
    connInfo.totalLinkCount = 1;
    connInfo.options.linkCount = 1;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_OK));
    connInfo.index = NN_NO1;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_OK));
    connInfo.totalLinkCount = NN_NO17;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));

    service->mNewEpMap.insert(std::make_pair(name, epInfo));
    MOCKER_CPP(&HcomConnectingEpInfo::Compare)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));
    MOCKER_CPP(&HcomConnectingEpInfo::AddEp)
        .stubs()
        .will(returnValue(false));
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name),
        static_cast<int>(SER_EP_BROKEN_DURING_CONNECTING));
}

TEST_F(TestHcomServiceImp, TestServiceEmplaceNewEndpointInvalidLinkCount)
{
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    ConnectingEpInfoPtr epInfo {};
    SerConnInfo connInfo {};
    connInfo.totalLinkCount = 1;
    connInfo.options.linkCount = 0;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));
    connInfo.options.linkCount = NN_NO20;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceEmplaceNewEndpointInvalidTotalLinkCount)
{
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    ConnectingEpInfoPtr epInfo {};
    SerConnInfo connInfo {};
    connInfo.totalLinkCount = NN_NO0;
    connInfo.options.linkCount = 1;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));
    connInfo.totalLinkCount = NN_NO100;
    EXPECT_EQ(service->EmplaceNewEndpoint(ep, epInfo, connInfo, name), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceServiceHandleNewEndPoint)
{
    EXPECT_EQ(service->ServiceHandleNewEndPoint(serviceIpInfo, nullptr, ""), static_cast<int>(SER_INVALID_PARAM));
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);

    UBSHcomConnectOptions opt;
    SerConnInfo connInfo(0, NetUuid::GenerateUuid(serviceIpInfo), NN_NO1, service->mOptions.chBrokenPolicy, opt);
    connInfo.options.linkCount = 1;
    MOCKER_CPP(&SerConnInfo::Deserialize)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->ServiceHandleNewEndPoint(serviceIpInfo, ep, ""), static_cast<int>(SER_INVALID_PARAM));
    MOCKER_CPP(&HcomServiceImp::GenerateUuid,
        SerResult(HcomServiceImp::*)(const std::string &, uint64_t, std::string &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->ServiceHandleNewEndPoint(serviceIpInfo, ep, ""), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceServiceNewChannel)
{
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    std::vector<UBSHcomNetEndpointPtr> epVector;
    UBSHcomConnectOptions opt;
    SerConnInfo connInfo(0, NetUuid::GenerateUuid(serviceIpInfo), NN_NO1, service->mOptions.chBrokenPolicy, opt);
    connInfo.options.linkCount = 1;
    EXPECT_EQ(service->ServiceNewChannel(serviceIpInfo, connInfo, "", epVector),
        static_cast<int>(SER_NEW_OBJECT_FAILED));
    MOCKER_CPP(&SerConnInfo::Deserialize)
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    NetMemPoolFixedOptions options = {};
    service->mContextMemPool = new (std::nothrow) NetMemPoolFixed("ServiceContextTimer-test", options);
    service->mPeriodicMgr = new (std::nothrow) HcomPeriodicManager(NN_NO1, name);
    service->mPgtable = new NetPgTable(HcomServiceImp::pgdAlloc, HcomServiceImp::pgdFree);
    epVector.push_back(ep);
    MOCKER_CPP(&HcomServiceImp::GenerateUuid,
        SerResult(HcomServiceImp::*)(const std::string &, uint64_t, std::string &))
        .stubs()
        .will(returnValue(static_cast<int>(SER_INVALID_PARAM)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->ServiceNewChannel(serviceIpInfo, connInfo, "", epVector),
        static_cast<int>(SER_INVALID_PARAM));
    EXPECT_EQ(service->ServiceNewChannel(serviceIpInfo, connInfo, "", epVector),
        static_cast<int>(SER_INVALID_PARAM));
    service->mOptions.chNewHandler = [](const std::string &ipPort, const UBSHcomChannelPtr &,
        const std::string &payload) {
        return SER_OK;
    };
    EXPECT_EQ(service->ServiceNewChannel(serviceIpInfo, connInfo, "", epVector),
        static_cast<int>(SER_OK));
    service->mPeriodicMgr.Set(nullptr);
    service->mContextMemPool.Set(nullptr);
}

TEST_F(TestHcomServiceImp, TestServiceDelayEraseChannel)
{
    InnerConnectOptions opt {};
    HcomChannelImp *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    ch->mCtxStore = new (std::nothrow) HcomServiceCtxStore(NN_NO2097152, nullptr, UBSHcomNetDriverProtocol::RDMA);
    UBSHcomChannelPtr chPtr = ch;
    NetMemPoolFixedOptions options = {};
    service->mContextMemPool = new (std::nothrow) NetMemPoolFixed("ServiceContextTimer-test", options);
    service->mPeriodicMgr = new (std::nothrow) HcomPeriodicManager(NN_NO1, name);
    HcomServiceTimer *timer = new HcomServiceTimer();
    MOCKER_CPP(&HcomServiceCtxStore::GetCtxObj<HcomServiceTimer>)
        .stubs()
        .will(returnValue(timer));
    MOCKER_CPP(&HcomServiceCtxStore::PutAndGetSeqNo<HcomServiceTimer>)
        .stubs()
        .will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomServiceCtxStore::Return<HcomServiceTimer>)
        .stubs();
    MOCKER_CPP(&HcomPeriodicManager::AddTimer).stubs().will(returnValue(static_cast<int>(SER_OK)));

    EXPECT_EQ(service->DelayEraseChannel(chPtr, 0), static_cast<int>(SER_OK));
    delete timer;
    service->mPeriodicMgr.Set(nullptr);
    service->mContextMemPool.Set(nullptr);
}

TEST_F(TestHcomServiceImp, TestServiceEraseChannel)
{
    InnerConnectOptions opt {};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NO_FATAL_FAILURE(service->EraseChannel(reinterpret_cast<uintptr_t>(ch)));
}

TEST_F(TestHcomServiceImp, TestServiceServiceEndPointBroken)
{
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(nullptr));
    UBSHcomNetWorkerIndex idx {};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, idx);
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));
}

TEST_F(TestHcomServiceImp, TestServiceEndPointBrokenFail2)
{
    MOCKER_CPP(&HcomConnectingEpInfo::AllEPBroken).stubs().will(returnValue(false)).then(returnValue(true));
    UBSHcomNetWorkerIndex workerIndex{};
    workerIndex.Set(NN_NO6, NN_NO4, NN_NO8);
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO8, nullptr, nullptr, workerIndex);
    ConnectingEpInfoPtr epInfo = new (std::nothrow) HcomConnectingEpInfo();
    Ep2ChanUpCtx ctx(NN_NO0, reinterpret_cast<uint64_t>(epInfo.Get()), NN_NO4);
    ep->UpCtx(ctx.wholeUpCtx);
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));

    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    Ep2ChanUpCtx ctx1(NN_NO1, reinterpret_cast<uint64_t>(ch.Get()), NN_NO4);
    ep->UpCtx(ctx1.wholeUpCtx);
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::AllEpBroken)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::NeedProcessBroken)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::SetChannelState).stubs().will(returnValue(true));
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::ProcessIoInBroken).stubs();
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::InvokeChannelBrokenCb).stubs();
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::GetDelayEraseTime)
        .stubs()
        .will(returnValue(static_cast<uint16_t>(0)));
    MOCKER_CPP(&HcomServiceImp::DelayEraseChannel).stubs().will(returnValue(static_cast<int>(SER_OK)));
    MOCKER_CPP(&HcomServiceImp::EraseChannel).stubs();
    EXPECT_NO_FATAL_FAILURE(service->ServiceEndPointBroken(ep));
}

TEST_F(TestHcomServiceImp, TestServiceServiceRequestReceived)
{
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    Ep2ChanUpCtx ep2ChUpCtx(NN_NO1, reinterpret_cast<uint64_t>(ch.Get()), NN_NO0);
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO8, nullptr, nullptr, workerIndex);
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    UBSHcomNetRequestContext ctx{};
    ctx.mEp = ep;
    ctx.mHeader.opCode = NN_NO8192;
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_ERROR));
    Ep2ChanUpCtx ep2ChUpCtx1(NN_NO1, reinterpret_cast<uint64_t>(nullptr), NN_NO0);
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    ctx.mEp = ep;
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_ERROR));
    ctx.mHeader.opCode = NN_NO1;
    service->mOptions.recvHandler = [](const UBSHcomServiceContext &ctx) {return 0;};
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_OK));

    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<uintptr_t>)
        .stubs()
        .will(returnValue(static_cast<int>(SER_STORE_SEQ_NO_FOUND)));
    MOCKER_CPP(&HcomSeqNo::IsResp).stubs().will(returnValue(true));
    HcomServiceCtxStore *store = new (std::nothrow) HcomServiceCtxStore(NN_NO2097152, nullptr,
        UBSHcomNetDriverProtocol::RDMA);
    ASSERT_NE(store, nullptr);
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::GetCtxStore)
        .stubs()
        .will(returnValue(store));
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    ctx.mEp = ep;
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_ERROR));
    if (store != nullptr) {
        delete store;
    }
}

TEST_F(TestHcomServiceImp, TestServiceServiceRequestReceivedSplit)
{
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    Ep2ChanUpCtx ep2ChUpCtx(NN_NO1, reinterpret_cast<uint64_t>(ch.Get()), NN_NO0);
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO8, nullptr, nullptr, workerIndex);
    UBSHcomNetRequestContext ctx{};
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    ctx.mEp = ep;
    ctx.mHeader.opCode = NN_NO1;
    ctx.extHeaderType = UBSHcomExtHeaderType::RAW;
    service->mOptions.recvHandler = [](const UBSHcomServiceContext &ctx) {return 0;};
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_OK));

    ctx.extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    SpliceMessageResultType result = SpliceMessageResultType::INDETERMINATE;
    SerResult code = SER_OK;
    std::string out = "";
    auto tmp = std::make_tuple(result, code, out);
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::SpliceMessage)
        .stubs()
        .will(returnValue(tmp));
    EXPECT_EQ(service->ServiceRequestReceived(ctx), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceRunRequestCallback)
{
    UBSHcomNetRequestContext ctx{};
    ctx.mOpType = UBSHcomNetRequestContext::NN_INVALID_OP_TYPE;
    UBSHcomServiceContext context{};
    EXPECT_EQ(service->RunRequestCallback(nullptr, ctx, context), false);

    ctx.mOpType = UBSHcomNetRequestContext::NN_SENT;
    Callback *newCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    SerTransContext upCtx {};
    upCtx.callback = newCallback;
    memcpy_s(ctx.mOriginalReq.upCtxData, NN_NO16, reinterpret_cast<char *>(&upCtx), NN_NO16);
    EXPECT_EQ(service->RunRequestCallback(nullptr, ctx, context), true);

    upCtx.callback = nullptr;
    memcpy_s(ctx.mOriginalReq.upCtxData, NN_NO16, reinterpret_cast<char *>(&upCtx), NN_NO16);
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    ASSERT_NE(ch.Get(), nullptr);
    HcomServiceCtxStore *store = new (std::nothrow) HcomServiceCtxStore(NN_NO2097152, nullptr,
        UBSHcomNetDriverProtocol::RDMA);
    ASSERT_NE(store, nullptr);
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::GetCtxStore)
        .stubs()
        .will(returnValue(store));
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<uintptr_t>)
        .stubs()
        .will(returnValue(static_cast<int>(SER_STORE_SEQ_NO_FOUND)));
    EXPECT_EQ(service->RunRequestCallback(ch.Get(), ctx, context), false);
}

TEST_F(TestHcomServiceImp, TestServiceServiceRequestPosted)
{
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    Ep2ChanUpCtx ep2ChUpCtx(NN_NO1, reinterpret_cast<uint64_t>(ch.Get()), NN_NO0);
    UBSHcomNetWorkerIndex workerIndex{};
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::GetCallBackType)
        .stubs()
        .will(returnValue(UBSHcomChannelCallBackType::CHANNEL_GLOBAL_CB));
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO8, nullptr, nullptr, workerIndex);
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    UBSHcomNetRequestContext ctx{};
    ctx.mEp = ep;
    ctx.mOpType = UBSHcomNetRequestContext::NN_INVALID_OP_TYPE;
    EXPECT_EQ(service->ServiceRequestPosted(ctx), static_cast<int>(SER_ERROR));
    service->mOptions.sendHandler = [](const UBSHcomServiceContext &ctx) {return 0;};
    EXPECT_EQ(service->ServiceRequestPosted(ctx), static_cast<int>(SER_OK));
    MOCKER_CPP(&HcomServiceImp::RunRequestCallback)
        .stubs()
        .will(returnValue(true));
    EXPECT_EQ(service->ServiceRequestPosted(ctx), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceServiceOneSideDone)
{
    InnerConnectOptions opt {};
    UBSHcomChannelPtr ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    Ep2ChanUpCtx ep2ChUpCtx(NN_NO1, reinterpret_cast<uint64_t>(ch.Get()), NN_NO0);
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO8, nullptr, nullptr, workerIndex);
    ep->UpCtx(ep2ChUpCtx.wholeUpCtx);
    UBSHcomNetRequestContext ctx{};
    ctx.mEp = ep;
    ctx.mOpType = UBSHcomNetRequestContext::NN_INVALID_OP_TYPE;
    MOCKER_CPP_VIRTUAL(*(ch.Get()), &UBSHcomChannel::GetCallBackType)
        .stubs()
        .will(returnValue(UBSHcomChannelCallBackType::CHANNEL_GLOBAL_CB));
    EXPECT_EQ(service->ServiceOneSideDone(ctx), static_cast<int>(SER_ERROR));
    service->mOptions.oneSideDoneHandler = [](const UBSHcomServiceContext &ctx) {return 0;};
    EXPECT_EQ(service->ServiceOneSideDone(ctx), static_cast<int>(SER_OK));
    MOCKER_CPP(&HcomServiceImp::RunRequestCallback)
        .stubs()
        .will(returnValue(true));
    EXPECT_EQ(service->ServiceOneSideDone(ctx), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceServiceSecInfoProvider)
{
    int64_t flag = 0;
    UBSHcomNetDriverSecType type;
    char *output = nullptr;
    uint32_t outLen = 0;
    bool needAutoFree = false;
    EXPECT_EQ(service->ServiceSecInfoProvider(0, flag, type, output, outLen, needAutoFree),
        static_cast<int>(SER_ERROR));
    service->mOptions.connSecOption.provider = [](uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type,
        char *&output, uint32_t &outLen, bool &needAutoFree) {return 0;};
    MOCKER_CPP(&ConnectingSecInfo::Initialize).stubs();
    EXPECT_EQ(service->ServiceSecInfoProvider(0, flag, type, output, outLen, needAutoFree), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceServiceSecInfoValidator)
{
    uint64_t ctx = 0;
    int64_t flag = 0;
    char *input = nullptr;
    uint32_t inputLen = 0;
    EXPECT_EQ(service->ServiceSecInfoValidator(ctx, flag, input, inputLen), static_cast<int>(SER_ERROR));
    service->mOptions.connSecOption.validator = [](uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen) {
        return 0;
    };
    MOCKER_CPP(&ConnectingSecInfo::Initialize).stubs();
    EXPECT_EQ(service->ServiceSecInfoValidator(ctx, flag, input, inputLen), static_cast<int>(SER_OK));
}

TEST_F(TestHcomServiceImp, TestServiceProtocol)
{
    EXPECT_NO_FATAL_FAILURE(service->Protocol());
}

TEST_F(TestHcomServiceImp, TestServiceGetIpAddressByIpPort)
{
    service->mOptions.protocol = SHM;
    uint32_t ip;
    EXPECT_EQ(service->GetIpAddressByIpPort(serviceIpInfo, ip), static_cast<int>(SER_OK));
    service->mOptions.protocol = RDMA;
    EXPECT_EQ(service->GetIpAddressByIpPort(serviceIpInfo, ip), static_cast<int>(SER_INVALID_PARAM));
}

TEST_F(TestHcomServiceImp, TestServiceRegisterDriverCb)
{
    service->mOptions.tlsOption.enableTls = true;
    EXPECT_NO_FATAL_FAILURE(service->RegisterDriverCb());
}

TEST_F(TestHcomServiceImp, TestServiceServicePrivateOpHandle)
{
    UBSHcomServiceContext context{};
    InnerConnectOptions opt {};
    context.mCh = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_EQ(service->ServicePrivateOpHandle(context), static_cast<int>(SER_ERROR));
    context.mCh.Set(nullptr);
}

TEST_F(TestHcomServiceImp, TestServiceAddTimerCtx)
{
    SerTimerListHeader header {};
    HcomServiceTimer timer {};
    EXPECT_NO_FATAL_FAILURE(header.AddTimerCtx(&timer));
    EXPECT_NO_FATAL_FAILURE(header.RemoveTimerCtx(&timer));
}

TEST_F(TestHcomServiceImp, TestServiceSetServiceTransCtx)
{
    SerTransContext ctx {};
    char *ctxData = reinterpret_cast<char *>(&ctx);
    EXPECT_NO_FATAL_FAILURE(SetServiceTransCtx(ctxData, 1));
    EXPECT_NO_FATAL_FAILURE(SetServiceTransCtx(ctxData, nullptr));
    EXPECT_NO_FATAL_FAILURE(SetServiceTransCtx(ctxData, 0, false));
}

TEST_F(TestHcomServiceImp, TestServiceSetMaxSendRecvDataCount)
{
    EXPECT_NO_FATAL_FAILURE(service->SetMaxSendRecvDataCount(1));
}

TEST_F(TestHcomServiceImp, TestServiceConnectFailed)
{
    UBSHcomChannelPtr ch;
    UBSHcomConnectOptions opt;
    opt.linkCount = NN_NO1;

    MOCKER_CPP(&HcomServiceImp::DoConnect)
        .stubs()
        .will(invoke(MockDoConnect));
    service->mStarted = true;
    MOCKER_CPP(HcomEnv::RndvThreshold).stubs().will(returnValue(NN_NO65536));
    MOCKER_CPP(&HcomServiceImp::ExchangeTimestamp)
        .stubs()
        .will(returnValue(static_cast<int>(SER_TIMEOUT)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt), static_cast<int>(SER_TIMEOUT));

    MOCKER_CPP(&HcomServiceImp::EmplaceChannelUuid)
        .stubs()
        .will(returnValue(static_cast<int>(SER_ERROR)))
        .then(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(service->Connect("tcp://" + serviceIpInfo + ":" + oobPort, ch, opt),
        static_cast<int>(SER_CHANNEL_ID_DUP));
}
}
}
