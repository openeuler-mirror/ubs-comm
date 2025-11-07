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
#include "hcom.h"
#include "test_net_oob.h"
#include "transport/rdma/rdma_common.h"
#include <thread>
#include <sys/poll.h>
#include <unistd.h>

namespace ock {
namespace hcom {

#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"
char sendTemp[] = "SendSuccess";

int testNewConnectionHandler(ock::hcom::OOBTCPConnection &conn)
{
    char *sendBuff = sendTemp;
    NResult ret = conn.Send(sendBuff, strlen(sendBuff));
    EXPECT_EQ(ret, ock::hcom::NN_OK);
    return ret;
}

int testNewConnectionHandlerEmpty(ock::hcom::OOBTCPConnection &conn)
{
    NResult ret = conn.Send(nullptr, 0);
    EXPECT_EQ(ret, ock::hcom::NN_PARAM_INVALID);
    return ret;
}

int testNewConnectionHandlerFailure(ock::hcom::OOBTCPConnection &conn)
{
    return -1;
}

std::string GetFilPrefixEnv()
{
    char path[255];
    getcwd(path, 255);
    std::string pathStr = path;
    std::string envString = "HCOM_FILE_PATH_PREFIX=" + pathStr;
    return envString;
}

TEST_F(TestNetOob, ConnectSuccess)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, ConnectSuccessWithFailToAcceptSocket)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::accept).defaults().will(returnValue(-1));
    MOCKER(::recv).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, SendSuccess)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    ASSERT_EQ(ret1, ock::hcom::NN_OK);
    char revTemp[1024];
    void *revBuff = (void *)revTemp;
    NResult ret3 = conn->Receive(revBuff, strlen(sendTemp));
    EXPECT_EQ(ret3, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, SendFailureWithFailToHandshakeWithClient)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandlerFailure, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OK);
    auto connFd = conn->TransferFd();
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestNetOob, SendFailureWithEmptyContent)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandlerEmpty, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OK);
    auto connFd = conn->TransferFd();
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestNetOob, ReceiveFailureWithEmptyContent)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    ASSERT_EQ(ret1, ock::hcom::NN_OK);
    void *revBuff = nullptr;
    NResult ret3 = conn->Receive(revBuff, strlen(sendTemp));
    EXPECT_EQ(ret3, ock::hcom::NN_PARAM_INVALID);
    auto connFd = conn->TransferFd();
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestNetOob, ReceiveFailureWithUnmatchedSize)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    ASSERT_EQ(ret1, ock::hcom::NN_OK);
    char revTemp[1024];
    void *revBuff = (void *)revTemp;
    NResult ret3 = conn->Receive(revBuff, strlen(sendTemp) + 1);
    EXPECT_EQ(ret3, ock::hcom::NN_OOB_CONN_RECEIVE_ERROR);
    auto connFd = conn->TransferFd();
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestNetOob, ConnectFailure)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    MOCKER(::connect).defaults().will(returnValue(-1));
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OOB_CLIENT_SOCKET_ERROR);
}

TEST_F(TestNetOob, ConnectFailureWithFailToCreateSocketInClient)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) ock::hcom::OOBTCPClient(BASE_IP, 9444);
    MOCKER(::socket).defaults().will(returnValue(-1));
    NResult ret1 = client->Connect(BASE_IP, 9444, conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OOB_CLIENT_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartSuccess)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, StartSuccessWithFailToSetThreadNameOfOobServer)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(pthread_setname_np).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, StartFailureWithFailedToSetNewConnectionCallBack)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_CONN_CB_NOT_SET);
}

TEST_F(TestNetOob, StartFailureWithFailedToSetLoadBalancer)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartFailureWithInvalidOobType)
{
    ock::hcom::OOBTCPServer oobServer("", 0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartFailureWithFailedToCreateListenSocket)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::socket).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartFailureWithFailedToSetOption)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::setsockopt).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartFailureWithFailedToBind)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::bind).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartFailureWithFailedToListen)
{
    ock::hcom::OOBTCPServer oobServer(BASE_IP, 9444);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::listen).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartForUdsFailureWithEmptyFilePath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "", 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartForUdsFailureWithLongFileName)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS,
        "ThisFileNameIs107InLengthThisFileNameIs107InLengthThisFileNameIs107InLengthThisFileNameIs107InLengthXXXXXXX",
        0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartForUdsSuccessWithAbstractPath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "server.socket", 0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, StartForUdsFailureWithInvalidPath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "/xxx/xxx/fake.socket", 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartForUdsSuccess)
{
    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, StartForUdsFailureWithSlashAbstractPath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "/fake.socket", 0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartForUdsSuccessWithFailToSetThreadNameOfOobServer)
{
    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(pthread_setname_np).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
}

TEST_F(TestNetOob, StartForUdsFailureWithFailToUnlinkFile)
{
    std::ofstream file(testFile);
    file.close();

    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(unlink).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_INVALID_PARAM);
}

TEST_F(TestNetOob, StartForUdsFailureWithFailToCreateListenSocket)
{
    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::socket).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartForUdsFailureWithFailToBind)
{
    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::bind).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, StartForUdsFailureWithFailToListen)
{
    std::string envString = GetFilPrefixEnv();
    ::putenv(const_cast<char *>(envString.c_str()));

    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, testFile, 640);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    MOCKER(::listen).defaults().will(returnValue(-1));
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OOB_LISTEN_SOCKET_ERROR);
}

TEST_F(TestNetOob, ConnectForUdsSuccessWithAbstractPath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "server.socket", 0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    EXPECT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client =
        new (std::nothrow) ock::hcom::OOBTCPClient(ock::hcom::NET_OOB_UDS, "client.socket", 0);
    NResult ret1 = client->Connect("server.socket", conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OK);
    auto connFd = conn->TransferFd();
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestNetOob, ConnectForUdsFailureWithEmptyPath)
{
    ock::hcom::OOBTCPServer oobServer(ock::hcom::NET_OOB_UDS, "server.socket", 0);
    oobServer.SetNewConnCB(std::bind(&testNewConnectionHandler, std::placeholders::_1));
    ock::hcom::UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    NResult ret = oobServer.Start();
    ASSERT_EQ(ret, ock::hcom::NN_OK);
    ock::hcom::OOBTCPConnection *conn = nullptr;
    ock::hcom::OOBTCPClientPtr client =
        new (std::nothrow) ock::hcom::OOBTCPClient(ock::hcom::NET_OOB_UDS, "client.socket", 0);
    NResult ret1 = client->Connect("", conn);
    EXPECT_EQ(ret1, ock::hcom::NN_OOB_CLIENT_SOCKET_ERROR);
}

/* ep id UT */
static UBSHcomNetEndpointPtr serverEp = nullptr;
static std::string udsName = "server-conn-epId-";
static std::atomic_uint64_t driverIndex(0);
static sem_t sem;

static int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    serverEp = newEP;
    sem_post(&sem);
    return 0;
}
static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id() << " broken");
}
static bool CreateDriver(UBSHcomNetDriver *&driver, uint16_t port, bool isServer, NetDriverOobType oobType)
{
    std::string driverName = "";
    if (isServer) {
        driverName = "server-epId-";
    } else {
        driverName = "client-epId-";
    }

    if (port > 0) {
        driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, driverName + std::to_string(port), isServer);
        driver->OobIpAndPort(BASE_IP, 10000);
    } else {
        driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM,
            driverName + std::to_string(driverIndex.fetch_add(1)), isServer);
    }

    if (oobType == NET_OOB_UDS && isServer) {
        UBSHcomNetOobUDSListenerOptions listenOpt;
        listenOpt.Name(udsName);
        listenOpt.perm = 0;
        driver->AddOobUdsOptions(listenOpt);
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.oobType = oobType;
    options.SetNetDeviceIpMask(IP_SEG);
    options.pollingBatchSize = 16;
    options.SetWorkerGroups("1");
    options.SetWorkerGroupsCpuSet("12-12");
    options.enableTls = false;

    if (isServer) {
        driver->RegisterNewEPHandler(
            std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    }
    driver->Initialize(options);
    driver->Start();
    return true;
}

static void CloseDriver(UBSHcomNetDriver *&driver)
{
    if (driver->IsStarted()) {
        driver->Stop();
        driver->UnInitialize();
    }
}

TEST_F(TestNetOob, ConnectShmEpId)
{
    MOCKER_CPP(&UBSHcomNetDriver::ValidateHandlesCheck).stubs().will(returnValue(static_cast<int>(SER_OK)));
    int count = 40;
    UBSHcomNetDriver *serverDriver = nullptr;
    CreateDriver(serverDriver, 0, true, NET_OOB_UDS);

    std::unordered_set<uint64_t> set;
    std::mutex locker;
    std::vector<std::thread> ths;
    std::atomic_uint16_t cnt(0);

    for (int i = 0; i < count; ++i) {
        std::thread th([&]() {
            auto index = cnt.fetch_add(1);
            UBSHcomNetDriver *clientDriver = nullptr;
            UBSHcomNetEndpointPtr clientEp = nullptr;
            CreateDriver(clientDriver, 0, false, NET_OOB_UDS);
            locker.lock();
            if (index & 1) {
                clientDriver->Connect(udsName, 0, "hello world", clientEp, 0);
            } else {
                clientDriver->Connect(udsName, 0, "hello world", clientEp, NET_EP_SELF_POLLING);
            }
            if (serverEp->Id() == clientEp->Id()) {
                set.insert(clientEp->Id());
            }
            locker.unlock();
            CloseDriver(clientDriver);
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
    EXPECT_EQ(set.size(), count);
    CloseDriver(serverDriver);
    if (serverEp.Get() != nullptr) {
        serverEp.Set(nullptr);
    }
}

TEST_F(TestNetOob, TestClientTcpConnect)
{
    std::string oobIp = "255.255.255.255";
    OOBTCPClientPtr client = new (std::nothrow) OOBTCPClient(NET_OOB_TCP, oobIp, NN_NO8192);
    OOBTCPConnection *conn = nullptr;
    int result = client->Connect(conn);
    EXPECT_EQ(NN_INVALID_IP, result);
}
}
}