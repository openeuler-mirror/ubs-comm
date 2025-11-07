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
#include <mockcpp/mockcpp.hpp>
#include "test_rdma_heartbeat.hpp"
#include "transport/rdma/rdma_heartbeat.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace ock::hcom;
TestCaseRDMAHeartBeat::TestCaseRDMAHeartBeat() {}

void TestCaseRDMAHeartBeat::SetUp() {}

void TestCaseRDMAHeartBeat::TearDown()
{
    GlobalMockObject::verify();
}

bool TestConnBrokenCheckCB(int fd) {}

void TestConnBrokenPostCB(int fd) {}

TEST_F(TestCaseRDMAHeartBeat, InitFailed)
{
    RIPDeviceHeartbeatManager hbMgr("test init");

    auto result = hbMgr.Initialize();
    EXPECT_EQ(result, NN_PARAM_INVALID);

    hbMgr.SetConnBrokenCheckHandler(std::bind(&TestConnBrokenCheckCB, std::placeholders::_1));

    result = hbMgr.Initialize();
    EXPECT_EQ(result, NN_PARAM_INVALID);

    hbMgr.SetConnBrokenPostHandler(std::bind(&TestConnBrokenPostCB, std::placeholders::_1));

    MOCKER(epoll_create).stubs().will(returnValue(-1));
    result = hbMgr.Initialize();
    EXPECT_EQ(result, NN_HEARTBEAT_CREATE_EPOLL_FAILED);
}

TEST_F(TestCaseRDMAHeartBeat, Start)
{
    RIPDeviceHeartbeatManager hbMgr("test start");

    hbMgr.SetConnBrokenCheckHandler(std::bind(&TestConnBrokenCheckCB, std::placeholders::_1));
    hbMgr.SetConnBrokenPostHandler(std::bind(&TestConnBrokenPostCB, std::placeholders::_1));

    auto result = hbMgr.Initialize();
    EXPECT_EQ(result, NN_OK);

    result = hbMgr.Start();
    EXPECT_EQ(result, NN_OK);

    result = hbMgr.Start();
    EXPECT_EQ(result, NN_OK);

    hbMgr.Stop();
}

std::string ip = "0.0.0.0";
uint32_t port = 6323;

bool gNeedStop = false;
std::atomic<bool> mStarted;

bool Accept(RIPDeviceHeartbeatManager &hbMgr, int fd, const std::string &ip, int16_t port)
{
    int result = 0;
    if ((result = hbMgr.AddNewIP(ip, fd)) != 0) {
        NN_LOG_ERROR("Failed to add fd to heartbeat manager " << ip << "-" << fd << " result " << result);
    }
    return true;
}

void RunServer()
{
    RIPDeviceHeartbeatManager hbMgr("test server");

    hbMgr.SetConnBrokenCheckHandler(
        std::bind(&RIPDeviceHeartbeatManager::DefaultConnBrokenCheckCB, std::placeholders::_1));
    hbMgr.SetConnBrokenPostHandler(
        std::bind(&RIPDeviceHeartbeatManager::DefaultConnBrokenPostCB, std::placeholders::_1));
    hbMgr.SetKeepaliveConfig(1, 1, 1);

    int result = 0;
    if ((result = hbMgr.Initialize()) != 0 || (result = hbMgr.Start()) != 0) {
        NN_LOG_ERROR("Failed to initialize start RIPDeviceHeartbeatManager, result " << result);
        return;
    }

    auto listenFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFD < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create listen socket as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    // assign address
    struct sockaddr_in addr {};
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);

    // set option, bind and listen
    int flags = 1;
    if (::setsockopt(listenFD, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<void *>(&flags), sizeof(flags)) < 0 ||
        ::bind(listenFD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0 ||
        ::listen(listenFD, OOB_DEFAULT_LISTEN_BACKLOG) < 0) {
        ::close(listenFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set option or bind or listen on listen socket as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    struct sockaddr_in addressIn {};
    socklen_t len = sizeof(addressIn);
    mStarted.store(true);

    bzero(&addressIn, sizeof(struct sockaddr_in));
    auto fd = ::accept(listenFD, reinterpret_cast<struct sockaddr *>(&addressIn), &len);
    if (fd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_WARN("Failed to accept on new socket with "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << ", ignore and continue");
        return;
    }

    Accept(hbMgr, fd, inet_ntoa(addressIn.sin_addr), ntohs(addressIn.sin_port));


    close(listenFD);
    hbMgr.Stop();
    hbMgr.UnInitialize();
}

bool ConnBrokenCheckCB(int fd)
{
    char data[1];
    int result = recv(fd, data, 1, MSG_DONTWAIT);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // connection is still ok
            return true;
        }
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        // connection is wrong
        NN_LOG_INFO("Connection is wrong, fd " << fd << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return false;
    } else if (result == 0) {
        NN_LOG_INFO("Connection is broken, fd " << fd);
        return false; // connection really broken
    } else {
        return true;
    }
}

void ConnBrokenPostCB(int fd)
{
    NN_LOG_INFO("ConnBrokenPostCB called fd " << fd);
    close(fd);
}

bool Connect(const std::string &ip, uint16_t port, int &fd)
{
    auto tmpFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if (tmpFD < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create socket as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return false;
    }

    struct sockaddr_in addr {};
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);

    if (connect(tmpFD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to connect to " << ip << ":" << port << " as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return false;
    }
    fd = tmpFD;
    return true;
}

void RunClient()
{
    RIPDeviceHeartbeatManager hbMgr("test client");

    hbMgr.SetConnBrokenCheckHandler(std::bind(&ConnBrokenCheckCB, std::placeholders::_1));
    hbMgr.SetConnBrokenPostHandler(std::bind(&ConnBrokenPostCB, std::placeholders::_1));
    hbMgr.SetKeepaliveConfig(1, 1, 1);

    int result = 0;
    if ((result = hbMgr.Initialize()) != 0 || (result = hbMgr.Start()) != 0) {
        NN_LOG_ERROR("Failed to initialize start RIPDeviceHeartbeatManager, result " << result);
        return;
    }

    NN_LOG_INFO("Heartbeat manager started");
    int fd = -1;
    if (Connect(ip, port, fd)) {
        NN_LOG_INFO("Connected to " << ip << ":" << port << ", fd " << fd);

        if ((result = hbMgr.AddNewIP(ip, fd)) != 0) {
            NN_LOG_ERROR("Failed to add fd to heartbeat manager " << fd << " result " << result);
            return;
        }

        int getFd;
        result = hbMgr.GetFdByIP(ip, getFd);
        EXPECT_EQ(result, NN_OK);
        EXPECT_EQ(fd, getFd);

        result = hbMgr.RemoveIP(ip);
        EXPECT_EQ(result, NN_OK);

        result = hbMgr.RemoveByFD(fd);
        EXPECT_EQ(result, NN_HEARTBEAT_IP_NO_FOUND);

        close(fd);
    } else {
        NN_LOG_ERROR("Failed to connect to " << ip << ":" << port);
    }
    hbMgr.Stop();
}

TEST_F(TestCaseRDMAHeartBeat, ClientServerHb)
{
    mStarted.store(false);
    std::thread tmpThread(RunServer);
    while (!mStarted.load()) {
        usleep(10);
    }

    RunClient();
    gNeedStop = true;

    tmpThread.join();
}
#endif