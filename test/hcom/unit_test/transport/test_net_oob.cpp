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
#include <sys/epoll.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "rdma_worker.h"
#include "transport/net_delay_release_timer.h"
#include "transport/net_heartbeat.h"
#include "transport/net_load_balance.h"
#include "transport/rdma/rdma_common.h"
#include "transport/rdma/verbs/net_rdma_async_endpoint.h"
#include "net_sock_driver_oob.h"

namespace ock {
namespace hcom {

class TestNetOob : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestNetOob::SetUp()
{
}

void TestNetOob::TearDown()
{
    GlobalMockObject::verify();
}

void FakeThread(OOBTCPServer *This)
{
    return;
}

TEST_F(TestNetOob, OOBTCPServerStart)
{
    uint16_t testPort = 9444;
    ock::hcom::OOBTCPServer oobServer("192.168.100.204", testPort);
    oobServer.mStarted = true;
    std::thread tmpThread(&FakeThread, &oobServer);
    oobServer.mAcceptThread = std::move(tmpThread);
    EXPECT_EQ(oobServer.Start(), 0);

    MOCKER_CPP(OOBTCPServer::CreateAndConfigSocket).stubs().will(returnValue(0));
    MOCKER_CPP(OOBTCPServer::BindAndListenAuto).stubs().will(returnValue(0));
    oobServer.mIsAutoPortSelectionEnabled = true;
    EXPECT_EQ(oobServer.CreateAndStartSocket(), 0);
}

TEST_F(TestNetOob, OOBTCPServerStop)
{
    uint16_t testPort = 9444;
    uint16_t testUdsPerm = 600;
    ock::hcom::OOBTCPServer oobServer("192.168.100.204", testPort);
    oobServer.mStarted = true;
    std::thread tmpThread(&FakeThread, &oobServer);
    oobServer.mAcceptThread = std::move(tmpThread);
    oobServer.mOobType = NET_OOB_UDS;
    oobServer.mUdsPerm = testUdsPerm;
    EXPECT_EQ(oobServer.Stop(), static_cast<int>(NN_INVALID_PARAM));
    oobServer.mStarted = false;
}

TEST_F(TestNetOob, OOBTCPServerStop1)
{
    uint16_t testPort = 9444;
    uint16_t testUdsPerm = 600;
    ock::hcom::OOBTCPServer oobServer("192.168.100.204", testPort);
    oobServer.mStarted = true;
    std::thread tmpThread(&FakeThread, &oobServer);
    oobServer.mAcceptThread = std::move(tmpThread);
    oobServer.mOobType = NET_OOB_UDS;
    oobServer.mUdsPerm = testUdsPerm;
    MOCKER_CPP(CanonicalPath).stubs().will(returnValue(true));

    EXPECT_EQ(oobServer.Stop(), static_cast<int>(NN_INVALID_PARAM));
    oobServer.mStarted = false;
}

static ssize_t MockConnSend(int socket, void const *buf, size_t size, int flags)
{
    return -1;
}

TEST_F(TestNetOob, OOBTCPServerFunc)
{
    ock::hcom::OOBTCPConnection oobConn(-1);

    void *buf = malloc(1);
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    errno = ENOMEM;
    EXPECT_EQ(oobConn.Send(buf, 1), static_cast<int>(NN_OOB_CONN_SEND_ERROR));

    if (buf != nullptr) {
        free(buf);
        buf = nullptr;
    }
}

TEST_F(TestNetOob, ConnectWithFdSocket)
{
    int fd = 0;
    int err = 0;
    uint16_t testPort = 2233;
    MOCKER(::socket).stubs().will(returnValue(static_cast<int>(-1)));

    err = OOBTCPClient::ConnectWithFd("127.0.0.1", testPort, fd);
    EXPECT_EQ(err, NN_OOB_CLIENT_SOCKET_ERROR);
}

TEST_F(TestNetOob, ConnectWithFdConnect)
{
    int fd = 0;
    int err = 0;
    uint16_t testPort = 2233;
    MOCKER(::sleep).stubs().will(returnValue(static_cast<int>(0)));
    MOCKER(::connect).stubs().will(returnValue(static_cast<int>(-1)));
    err = OOBTCPClient::ConnectWithFd("127.0.0.1", testPort, fd);
    EXPECT_EQ(err, NN_OOB_CLIENT_SOCKET_ERROR);
}

TEST_F(TestNetOob, ConnectWithFdRecv)
{
    int fd = 0;
    int err = 0;
    uint16_t testPort = 2233;
    MOCKER(::sleep).stubs().will(returnValue(static_cast<int>(0)));
    MOCKER(::connect).stubs().will(returnValue(static_cast<int>(0)));
    MOCKER(::recv)
            .stubs()
            .will(returnValue(static_cast<int>(-1)))
            .then(returnValue(static_cast<int>(NN_NO4)));

    err = OOBTCPClient::ConnectWithFd("127.0.0.1", testPort, fd);
    EXPECT_EQ(err, NN_OK);
}
}  // namespace hcom
}  // namespace ock
