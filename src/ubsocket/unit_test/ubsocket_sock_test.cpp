/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the license at:
 * http://license.coscl.org.cn/MulanPSL2
 */

#include "common/ubsocket_global_setting.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

#include <netinet/in.h>
#include <cerrno>

#include <gtest/gtest.h>

extern "C" {
int ubsocket_socket(int, int, int);
int ubsocket_shutdown(int, int);
int ubsocket_close(int);
int ubsocket_accept(int, struct sockaddr *, socklen_t *);
int ubsocket_accept4(int, struct sockaddr *, socklen_t *, int);
int ubsocket_bind(int, const struct sockaddr *, socklen_t);
int ubsocket_listen(int, int);
int ubsocket_connect(int, const struct sockaddr *, socklen_t);
ssize_t ubsocket_readv(int, const struct iovec *, int);
ssize_t ubsocket_writev(int, const struct iovec *, int);
ssize_t ubsocket_send(int, const void *, size_t, int);
ssize_t ubsocket_recv(int, void *, size_t, int);
ssize_t ubsocket_read(int, void *, size_t);
ssize_t ubsocket_write(int, const void *, size_t);
ssize_t ubsocket_sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t ubsocket_recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t ubsocket_sendmsg(int, const struct msghdr *, int);
ssize_t ubsocket_recvmsg(int, struct msghdr *, int);
ssize_t ubsocket_sendfile(int, int, off_t *, size_t);
ssize_t ubsocket_sendfile64(int, int, off64_t *, size_t);
int ubsocket_fcntl(int, int, ...);
int ubsocket_fcntl64(int, int, ...);
int ubsocket_ioctl(int, unsigned long, ...);
int ubsocket_setsockopt(int, int, int, const void *, socklen_t);
int ubsocket_getsockopt(int, int, int, void *, socklen_t *);
}

using ock::ubs::GlobalSetting;
using ock::ubs::LibcApi;

namespace {


int MockInt(int, ...)
{
    return 17;
}
ssize_t MockSsize(int, ...)
{
    return 23;
}
int MockSocket(int, int, int)
{
    return 31;
}
int MockShutdown(int, int)
{
    return 0;
}
int MockClose(int)
{
    return 0;
}
int MockAccept(int, struct sockaddr *, socklen_t *)
{
    return 41;
}
int MockAccept4(int, struct sockaddr *, socklen_t *, int)
{
    return 42;
}
int MockBind(int, const struct sockaddr *, socklen_t)
{
    return 0;
}
int MockConnect(int, const struct sockaddr *, socklen_t)
{
    return 0;
}
int MockListen(int, int)
{
    return 0;
}
ssize_t MockReadv(int, const struct iovec *, int)
{
    return 5;
}
ssize_t MockWritev(int, const struct iovec *, int)
{
    return 6;
}
ssize_t MockSend(int, const void *, size_t, int)
{
    return 7;
}
ssize_t MockRecv(int, void *, size_t, int)
{
    return 8;
}
ssize_t MockRead(int, void *, size_t)
{
    return 9;
}
ssize_t MockWrite(int, const void *, size_t)
{
    return 10;
}
ssize_t MockSendTo(int, const void *, size_t, int, const struct sockaddr *, socklen_t)
{
    return 11;
}
ssize_t MockRecvFrom(int, void *, size_t, int, struct sockaddr *, socklen_t *)
{
    return 12;
}
ssize_t MockSendMsg(int, const struct msghdr *, int)
{
    return 13;
}
ssize_t MockRecvMsg(int, struct msghdr *, int)
{
    return 14;
}
ssize_t MockSendFile(int, int, off64_t *, size_t)
{
    return 15;
}
int MockSetSockOpt(int, int, int, const void *, socklen_t)
{
    return 0;
}
int MockGetSockOpt(int, int, int, void *, socklen_t *)
{
    return 0;
}

class UbsocketSockTest : public testing::Test {
protected:
    void SetUp() override
    {
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
        GlobalSetting::UBS_INITED = true;
        LibcApi::socket_ptr = MockSocket;
        LibcApi::shutdown_ptr = MockShutdown;
        LibcApi::close_ptr = MockClose;
        LibcApi::accept_ptr = MockAccept;
        LibcApi::accept4_ptr = MockAccept4;
        LibcApi::bind_ptr = MockBind;
        LibcApi::listen_ptr = MockListen;
        LibcApi::connect_ptr = MockConnect;
        LibcApi::readv_ptr = MockReadv;
        LibcApi::writev_ptr = MockWritev;
        LibcApi::send_ptr = MockSend;
        LibcApi::recv_ptr = MockRecv;
        LibcApi::read_ptr = MockRead;
        LibcApi::write_ptr = MockWrite;
        LibcApi::sendto_ptr = MockSendTo;
        LibcApi::recvfrom_ptr = MockRecvFrom;
        LibcApi::sendmsg_ptr = MockSendMsg;
        LibcApi::recvmsg_ptr = MockRecvMsg;
        LibcApi::sendfile64_ptr = MockSendFile;
        LibcApi::setsockopt_ptr = MockSetSockOpt;
        LibcApi::getsockopt_ptr = MockGetSockOpt;
    }

    void TearDown() override
    {
        LibcApi::socket_ptr = nullptr;
        LibcApi::shutdown_ptr = nullptr;
        LibcApi::close_ptr = nullptr;
        LibcApi::accept_ptr = nullptr;
        LibcApi::accept4_ptr = nullptr;
        LibcApi::bind_ptr = nullptr;
        LibcApi::listen_ptr = nullptr;
        LibcApi::connect_ptr = nullptr;
        LibcApi::readv_ptr = nullptr;
        LibcApi::writev_ptr = nullptr;
        LibcApi::send_ptr = nullptr;
        LibcApi::recv_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        LibcApi::write_ptr = nullptr;
        LibcApi::sendto_ptr = nullptr;
        LibcApi::recvfrom_ptr = nullptr;
        LibcApi::sendmsg_ptr = nullptr;
        LibcApi::recvmsg_ptr = nullptr;
        LibcApi::sendfile64_ptr = nullptr;
        LibcApi::setsockopt_ptr = nullptr;
        LibcApi::getsockopt_ptr = nullptr;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    }
};

TEST_F(UbsocketSockTest, NativeModeDelegatesSocketApis)
{
    sockaddr_storage addr{};
    iovec iov{};
    char data[8]{};
    socklen_t len = sizeof(addr);
    EXPECT_EQ(ubsocket_socket(AF_INET, SOCK_STREAM, 0), 31);
    EXPECT_EQ(ubsocket_shutdown(1, SHUT_RDWR), 0);
    EXPECT_EQ(ubsocket_close(1), 0);
    EXPECT_EQ(ubsocket_accept(1, reinterpret_cast<sockaddr *>(&addr), &len), 41);
    EXPECT_EQ(ubsocket_accept4(1, reinterpret_cast<sockaddr *>(&addr), &len, 0), 42);
    EXPECT_EQ(ubsocket_bind(1, reinterpret_cast<sockaddr *>(&addr), len), 0);
    EXPECT_EQ(ubsocket_listen(1, 4), 0);
    EXPECT_EQ(ubsocket_connect(1, reinterpret_cast<sockaddr *>(&addr), len), 0);
    EXPECT_EQ(ubsocket_readv(1, &iov, 1), 5);
    EXPECT_EQ(ubsocket_writev(1, &iov, 1), 6);
    EXPECT_EQ(ubsocket_send(1, data, sizeof(data), 0), 7);
    EXPECT_EQ(ubsocket_recv(1, data, sizeof(data), 0), 8);
    EXPECT_EQ(ubsocket_read(1, data, sizeof(data)), 9);
    EXPECT_EQ(ubsocket_write(1, data, sizeof(data)), 10);
    EXPECT_EQ(ubsocket_sendto(1, data, sizeof(data), 0, reinterpret_cast<sockaddr *>(&addr), len), 11);
    EXPECT_EQ(ubsocket_recvfrom(1, data, sizeof(data), 0, reinterpret_cast<sockaddr *>(&addr), &len), 12);
    EXPECT_EQ(ubsocket_sendmsg(1, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_recvmsg(1, nullptr, 0), 14);
    EXPECT_EQ(ubsocket_sendfile(1, 2, nullptr, 3), 15);
    EXPECT_EQ(ubsocket_sendfile64(1, 2, nullptr, 3), 15);
    EXPECT_EQ(ubsocket_setsockopt(1, SOL_SOCKET, SO_KEEPALIVE, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_getsockopt(1, SOL_SOCKET, SO_KEEPALIVE, nullptr, &len), 0);
}

TEST_F(UbsocketSockTest, NonNativePassthroughAndStubs)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    EXPECT_EQ(ubsocket_accept4(1, reinterpret_cast<sockaddr *>(&addr), &len, 0), 0);
    EXPECT_EQ(ubsocket_sendto(1, nullptr, 0, 0, reinterpret_cast<sockaddr *>(&addr), len), 0);
    EXPECT_EQ(ubsocket_recvfrom(1, nullptr, 0, 0, reinterpret_cast<sockaddr *>(&addr), &len), 0);
    EXPECT_EQ(ubsocket_send(1, nullptr, 0, 0), 0);
    EXPECT_EQ(ubsocket_recv(1, nullptr, 0, 0), 0);
    EXPECT_EQ(ubsocket_read(1, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_write(1, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_sendmsg(1, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_recvmsg(1, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_sendfile(1, 2, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_sendfile64(1, 2, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_fcntl(1, 0), 0);
    EXPECT_EQ(ubsocket_fcntl64(1, 0), 0);
    EXPECT_EQ(ubsocket_ioctl(1, 0), 0);
}

TEST_F(UbsocketSockTest, NonNativeUnknownFdFallsBackToLibc)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    iovec iov{};
    char data[4]{};
    EXPECT_EQ(ubsocket_accept(1, reinterpret_cast<sockaddr *>(&addr), &len), 41);
    EXPECT_EQ(ubsocket_bind(1, reinterpret_cast<sockaddr *>(&addr), len), 0);
    EXPECT_EQ(ubsocket_listen(1, 4), 0);
    EXPECT_EQ(ubsocket_connect(1, reinterpret_cast<sockaddr *>(&addr), len), 0);
    EXPECT_EQ(ubsocket_readv(1, &iov, 1), 5);
    EXPECT_EQ(ubsocket_writev(1, &iov, 1), 6);
    EXPECT_EQ(ubsocket_setsockopt(1, SOL_SOCKET, SO_KEEPALIVE, nullptr, 0), 0);
    EXPECT_EQ(ubsocket_getsockopt(1, SOL_SOCKET, SO_KEEPALIVE, data, &len), 0);
}

TEST_F(UbsocketSockTest, DegradeFlagAndTransportClassification)
{
    EXPECT_EQ(ubsocket_set_degrade_enable(1), 0);
    EXPECT_TRUE(GlobalSetting::UBS_ENABLE_DEGRADE);
    EXPECT_EQ(ubsocket_set_degrade_enable(0), 0);
    EXPECT_FALSE(GlobalSetting::UBS_ENABLE_DEGRADE);
    GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    EXPECT_EQ(ubsocket_is_ub_transport(1), -1);
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    GlobalSetting::UBS_INITED = false;
    EXPECT_EQ(ubsocket_is_ub_transport(1), -1);
    GlobalSetting::UBS_INITED = true;
    EXPECT_EQ(ubsocket_is_ub_transport(-1), -1);
    EXPECT_EQ(ubsocket_is_ub_transport(9999), 0);
}

} // namespace
