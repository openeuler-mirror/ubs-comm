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
#include <unistd.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "transport/net_oob_secure.h"

namespace ock {
namespace hcom {

class TestNetOobSecure : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestNetOobSecure::SetUp()
{
}

void TestNetOobSecure::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetOobSecure, SecProcessCompareEpNum)
{
    OOBSecureProcess proc {};
    std::vector<NetOOBServer *> oobServers;
    NetOOBServer *server = new (std::nothrow) OOBTCPServer("127.0.0.1", 9980);
    server->mStarted = false;
    oobServers.push_back(server);
    EXPECT_EQ(proc.SecProcessCompareEpNum(0, 0, "0", oobServers), static_cast<int>(NN_OK));
    EXPECT_EQ(proc.SecProcessCompareEpNum("udsName", "IpPort", oobServers), static_cast<int>(NN_OK));
    EXPECT_NO_FATAL_FAILURE(proc.SecProcessAddEpNum(0, 0, "0", oobServers));
    EXPECT_NO_FATAL_FAILURE(proc.SecProcessAddEpNum("udsName", "IpPort", oobServers));
    EXPECT_NO_FATAL_FAILURE(proc.SecProcessDelEpNum(0, 0, "0", oobServers));

    if (server != nullptr) {
        delete server;
        server = nullptr;
    }
}

TEST_F(TestNetOobSecure, SecProcessDelEpNum)
{
    OOBSecureProcess proc {};
    std::vector<NetOOBServer *> oobServers;
    NetOOBServer *server = new (std::nothrow) OOBTCPServer("127.0.0.1", 9980);
    server->mStarted = false;
    oobServers.push_back(server);
    EXPECT_NO_FATAL_FAILURE(proc.SecProcessDelEpNum("udsName", "IpPort", oobServers));

    server->mStarted = true;
    server->mOobType = NET_OOB_UDS;
    server->mUdsName = "udsName";
    EXPECT_NO_FATAL_FAILURE(proc.SecProcessDelEpNum("udsName", "IpPort", oobServers));
}

TEST_F(TestNetOobSecure, SecProcessInOOBServer)
{
    OOBSecureProcess proc {};
    UBSHcomNetDriverEndpointSecInfoProvider provider = nullptr;
    UBSHcomNetDriverEndpointSecInfoValidator validator = nullptr;
    OOBTCPConnection *tcpConn = new (std::nothrow) OOBTCPConnection(-1);
    MOCKER_CPP(OOBSecureProcess::ValidateSecInfo).stubs()
        .will(returnValue(static_cast<int>(NN_OOB_SEC_PROCESS_ERROR)))
        .then(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Send)
        .stubs()
        .will(returnValue(static_cast<int>((NN_ERROR))));
    EXPECT_EQ(proc.SecProcessInOOBServer(provider, validator, *tcpConn, "name",
        UBSHcomNetDriverSecType::NET_SEC_DISABLED),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
    EXPECT_EQ(proc.SecProcessInOOBServer(provider, validator, *tcpConn, "name",
        UBSHcomNetDriverSecType::NET_SEC_DISABLED),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    if (tcpConn != nullptr) {
        delete tcpConn;
        tcpConn = nullptr;
    }
}

TEST_F(TestNetOobSecure, SecProcessInOOBClient)
{
    OOBSecureProcess proc {};
    UBSHcomNetDriverEndpointSecInfoProvider provider = nullptr;
    UBSHcomNetDriverEndpointSecInfoValidator validator = nullptr;
    OOBTCPConnection *tcpConn = new (std::nothrow) OOBTCPConnection(-1);
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Send)
        .stubs()
        .will(returnValue(static_cast<int>((NN_ERROR))))
        .then(returnValue(static_cast<int>((NN_OK))));
    EXPECT_EQ(proc.SecProcessInOOBClient(provider, validator, tcpConn, "name", 0,
        UBSHcomNetDriverSecType::NET_SEC_DISABLED),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Receive)
        .stubs()
        .will(returnValue(static_cast<int>((NN_ERROR))));

    EXPECT_EQ(proc.SecProcessInOOBClient(provider, validator, tcpConn, "name", 0,
        UBSHcomNetDriverSecType::NET_SEC_DISABLED),
        static_cast<int>(NN_ERROR));

    if (tcpConn != nullptr) {
        delete tcpConn;
        tcpConn = nullptr;
    }
}

TEST_F(TestNetOobSecure, SendSecInfo)
{
    OOBSecureProcess proc {};
    UBSHcomNetDriverEndpointSecInfoProvider provider =
        [](uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen,
            bool &needAutoFree) {
            outLen = NN_NO2147483646 + 1;
            return 0;
        };
    UBSHcomNetDriverEndpointSecInfoValidator validator = nullptr;
    OOBTCPConnection *tcpConn = new (std::nothrow) OOBTCPConnection(-1);
    UBSHcomNetDriverSecType type = UBSHcomNetDriverSecType::NET_SEC_DISABLED;
    EXPECT_EQ(proc.SendSecInfo(provider, validator, nullptr, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    provider =
        [](uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen,
            bool &needAutoFree) {
            outLen = 0;
            type = UBSHcomNetDriverSecType::NET_SEC_VALID_ONE_WAY;
            return 0;
        };
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Send)
        .stubs()
        .will(returnValue(static_cast<int>((NN_ERROR))))
        .then(returnValue(static_cast<int>((NN_OK))))
        .then(returnValue(static_cast<int>((NN_ERROR))));

    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    if (tcpConn != nullptr) {
        delete tcpConn;
        tcpConn = nullptr;
    }
}

NResult FakeReceive(void *&buf, uint32_t size)
{
    ConnSecHeader *header = static_cast<ConnSecHeader *>(buf);
    header->type = 3;
    return 0;
}

NResult FakeReceive1(void *&buf, uint32_t size)
{
    ConnSecHeader *header = static_cast<ConnSecHeader *>(buf);
    header->secInfoLen = NN_NO2147483646 + 1;
    header->type = UBSHcomNetDriverSecType::NET_SEC_VALID_TWO_WAY;
    return 0;
}

NResult FakeReceive2(void *&buf, uint32_t size)
{
    ConnSecHeader *header = static_cast<ConnSecHeader *>(buf);
    header->type = UBSHcomNetDriverSecType::NET_SEC_VALID_TWO_WAY;
    return 0;
}

TEST_F(TestNetOobSecure, ValidateSecInfo)
{
    OOBSecureProcess proc {};
    OOBTCPConnection *tcpConn = new (std::nothrow) OOBTCPConnection(-1);
    UBSHcomNetDriverSecType type = UBSHcomNetDriverSecType::NET_SEC_DISABLED;
    UBSHcomNetDriverEndpointSecInfoProvider provider = nullptr;
    UBSHcomNetDriverEndpointSecInfoValidator validator = nullptr;

    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Receive)
        .stubs()
        .will(invoke(FakeReceive))
        .then(returnValue(0))
        .then(invoke(FakeReceive1))
        .then(invoke(FakeReceive2))
        .then(returnValue(1));
    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
    EXPECT_EQ(proc.SendSecInfo(provider, validator, tcpConn, "name", type, 0),
        static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));
}

TEST_F(TestNetOobSecure, SecCheckConnectionHeader)
{
    OOBSecureProcess proc {};
    ConnectHeader header {};
    UBSHcomNetDriverOptions options {};
    bool enableTls = true;
    UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::TCP;
    uint32_t majorVersion = 1;
    uint32_t minorVersion = 0;
    ConnRespWithUId resp {};

    options.magic = header.magic;
    header.protocol = UBSHcomNetDriverProtocol::RDMA;
    EXPECT_EQ(proc.SecCheckConnectionHeader(header, options, enableTls, protocol, majorVersion, minorVersion, resp),
        static_cast<int>(NN_ERROR));

    header.protocol = UBSHcomNetDriverProtocol::TCP;
    header.majorVersion = majorVersion + 1;
    EXPECT_EQ(proc.SecCheckConnectionHeader(header, options, enableTls, protocol, majorVersion, minorVersion, resp),
        static_cast<int>(VERSION_MISMATCH));

    header.majorVersion = majorVersion;
    header.minorVersion = minorVersion + 1;
    EXPECT_EQ(proc.SecCheckConnectionHeader(header, options, enableTls, protocol, majorVersion, minorVersion, resp),
        static_cast<int>(VERSION_MISMATCH));

    header.minorVersion = minorVersion;
    header.tlsVersion = TLS_1_3 + 1;
    EXPECT_EQ(proc.SecCheckConnectionHeader(header, options, enableTls, protocol, majorVersion, minorVersion, resp),
        static_cast<int>(NN_ERROR));
}
}
}