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
#include "hcom.h"
#include "net_oob.h"

namespace ock {
namespace hcom {

const std::string SERVER_IP("127.0.0.1");
constexpr uint16_t SERVER_PORT_ZERO = 0;


class TestNetOob : public testing::Test {
public:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}
    virtual void SetUp(void) {}
    virtual void TearDown(void)
    {
        GlobalMockObject::verify();
    }
};

// tcp server is already started
TEST_F(TestNetOob, EnableAutoPortSelectionFailed1)
{
    uint16_t minPort = 2000;
    uint16_t maxPort = 3000;
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = true;
    server.mOobType = NetDriverOobType::NET_OOB_TCP;
    NResult ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);

    // make sure the server exits correctly
    server.mStarted = false;
}

// tcp server oob is not tcp
TEST_F(TestNetOob, EnableAutoPortSelectionFailed2)
{
    uint16_t minPort = 2000;
    uint16_t maxPort = 3000;
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    server.mOobType = NetDriverOobType::NET_OOB_UDS;
    NResult ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);
}

// port range error
TEST_F(TestNetOob, EnableAutoPortSelectionFailed3)
{
    uint16_t minPort = 0;
    uint16_t maxPort = 3000;
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    server.mOobType = NetDriverOobType::NET_OOB_TCP;
    NResult ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);

    minPort = 1;
    maxPort = 1000;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);

    minPort = 2000;
    maxPort = 1000;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);

    minPort = 3000;
    maxPort = 2000;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_ERROR);
}

// port range error
TEST_F(TestNetOob, EnableAutoPortSelectionSuccess)
{
    uint16_t minPort = 2000;
    uint16_t maxPort = 3000;
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    server.mOobType = NetDriverOobType::NET_OOB_TCP;
    NResult ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_OK);

    server.mListenPort = 2500;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestNetOob, GetListenPortFailed)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    uint16_t port = 0;
    NResult ret = server.GetListenPort(port);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetOob, GetListenIpFailed)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    std::string listenIp;
    NResult ret = server.GetListenIp(listenIp);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetOob, GetUdsNameFailed)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    std::string udsName;
    NResult ret = server.GetUdsName(udsName);
    EXPECT_EQ(ret, NN_ERROR);

    server.mStarted = true;
    ret = server.GetUdsName(udsName);
    EXPECT_EQ(ret, NN_ERROR);

    // make sure the server exits correctly
    server.mStarted = false;
}

TEST_F(TestNetOob, BindAndListenAutoSuccess)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    int socketFD = 0;
    int ret = server.CreateAndConfigSocket(socketFD);
    EXPECT_EQ(ret, NN_OK);
    uint16_t minPort = 2000;
    uint16_t maxPort = 3000;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_OK);

    ret = server.BindAndListenCommon(socketFD);
    EXPECT_EQ(ret, NN_OK);

    NetFunc::NN_SafeCloseFd(socketFD);
}

// bind always failed
TEST_F(TestNetOob, BindAndListenAutoFailed1)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    int socketFD = 0;
    int ret = server.CreateAndConfigSocket(socketFD);
    EXPECT_EQ(ret, NN_OK);
    uint16_t minPort = 2000;
    uint16_t maxPort = 2003;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(::bind).stubs().will(returnValue(int(-1)));
    ret = server.BindAndListenAuto(socketFD);
    EXPECT_NE(ret, NN_OK);
}

// listen always failed
TEST_F(TestNetOob, BindAndListenAutoFailed2)
{
    OOBTCPServer server(SERVER_IP, SERVER_PORT_ZERO);
    server.mStarted = false;
    int socketFD = 0;
    int ret = server.CreateAndConfigSocket(socketFD);
    EXPECT_EQ(ret, NN_OK);
    uint16_t minPort = 2000;
    uint16_t maxPort = 2003;
    ret = server.EnableAutoPortSelection(minPort, maxPort);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(::listen).stubs().will(returnValue(int(-1)));
    ret = server.BindAndListenAuto(socketFD);
    EXPECT_NE(ret, NN_OK);
}

}
}
