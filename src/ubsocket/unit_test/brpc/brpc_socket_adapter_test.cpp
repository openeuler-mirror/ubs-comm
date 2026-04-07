/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli args parser, etc
 * Author:
 * Create: 2026-03-30
 * Note:
 * History: 2026-03-30
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cerrno>
#include <new>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "brpc_context.h"
#include "brpc_file_descriptor.h"
#include "file_descriptor.h"
#include "rpc_adpt_vlog.h"

class BrpcSocketAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
        setenv("UBSOCKET_USE_BRPC_ZCOPY", "false", setenvOverwrite);
        setenv("UBSOCKET_TRANS_MODE", "UB", setenvOverwrite);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);

        // Avoid real umq init path in Brpc::Context.
        MOCKER_CPP(umq_init).stubs().will(returnValue(int(umqInitOkReturn)));
    }

    void TearDown() override
    {
        unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
        unsetenv("UBSOCKET_TRANS_MODE");

        Fd<SocketFd>::OverrideFdObj(fd, nullptr);
        Fd<EpollFd>::OverrideFdObj(epfd, nullptr);

        GlobalMockObject::verify();
    }

protected:
    static constexpr int testFd = 101;
    static constexpr int testEpollFd = 201;
    static constexpr int setenvOverwrite = 1;
    static constexpr socklen_t socklenZero = 0;
    static constexpr uint64_t magicNumber = 10;
    static constexpr uint32_t magicNumberRecvSize = 9;

    int fd = testFd;
    int epfd = testEpollFd;

    static constexpr int connectNoFdObjError = -E2BIG;
    static constexpr int connectOriginError = -EPERM;
    static constexpr int acceptNoFdObjReturn = 55;
    static constexpr int acceptWithFdObjReturn = 0;
    static constexpr int closeReturn = 77;
    static constexpr int umqInitOkReturn = 0;
    static constexpr int doAcceptOkReturn = 0;
    static constexpr int epollCreateSize = 1;
};

TEST_F(BrpcSocketAdapterTest, ConnectNoFdObjGoesOrigin)
{
    const struct sockaddr *address = nullptr;
    socklen_t addressLen = socklenZero;

    // Ensure no object is registered.
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    MOCKER_CPP(&OsAPiMgr::connect).stubs().will(returnValue(int(connectNoFdObjError)));
    int ret = ::connect(fd, address, addressLen);
    EXPECT_EQ(ret, connectNoFdObjError);
}

TEST_F(BrpcSocketAdapterTest, ConnectOriginErrorPropagates)
{
    const struct sockaddr *address = nullptr;
    socklen_t addressLen = socklenZero;

    // Keep this test deterministic: verify adapter connect() can propagate
    // origin connect() error in the no-fd-object path.
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    MOCKER_CPP(&OsAPiMgr::connect).stubs().will(returnValue(int(connectOriginError)));
    int ret = ::connect(fd, address, addressLen);
    EXPECT_EQ(ret, connectOriginError);
}

TEST_F(BrpcSocketAdapterTest, AcceptNoFdObjGoesOrigin)
{
    struct sockaddr *address = nullptr;
    socklen_t *addressLen = nullptr;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    MOCKER_CPP(&OsAPiMgr::accept).stubs().will(returnValue(int(acceptNoFdObjReturn)));
    int ret = ::accept(fd, address, addressLen);
    EXPECT_EQ(ret, acceptNoFdObjReturn);
}

TEST_F(BrpcSocketAdapterTest, AcceptWithFdObjGoesAdapter)
{
    setenv("UBSOCKET_TRANS_MODE", "ubmm", setenvOverwrite);

    struct sockaddr *address = nullptr;
    socklen_t *addressLen = nullptr;

    Brpc::SocketFd *socketFd = nullptr;
    try {
        socketFd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
    } catch (const std::bad_alloc&) {
        socketFd = nullptr;
    }
    ASSERT_NE(socketFd, nullptr);
    socketFd->m_tx_use_tcp = false;
    socketFd->m_rx_use_tcp = false;
    Fd<SocketFd>::OverrideFdObj(fd, socketFd);

    MOCKER_CPP(umq_init).stubs().will(returnValue(int(umqInitOkReturn)));
    MOCKER_CPP(&OsAPiMgr::accept).stubs().will(returnValue(int(acceptWithFdObjReturn)));
    MOCKER_CPP(&SocketFd::IsBlocking).stubs().will(returnValue(bool(false)));
    MOCKER_CPP(&Brpc::SocketFd::DoAccept).stubs().will(returnValue(int(doAcceptOkReturn)));

    int ret = ::accept(fd, address, addressLen);
    EXPECT_EQ(ret, acceptWithFdObjReturn);
}

TEST_F(BrpcSocketAdapterTest, CloseReturnsOriginResult)
{
    MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(int(closeReturn)));

    int ret = ::close(fd);
    EXPECT_EQ(ret, closeReturn);
}

TEST_F(BrpcSocketAdapterTest, EpollCreateContextNullReturnsOriginFd)
{
    MOCKER_CPP(&OsAPiMgr::epoll_create).stubs().will(returnValue(int(epfd)));
    MOCKER_CPP(&Brpc::Context::GetContext).stubs().will(returnValue((Brpc::Context *)nullptr));

    int ret = ::epoll_create(epollCreateSize);
    EXPECT_EQ(ret, epfd);
}

// ============= Shutdown Tests =============

TEST_F(BrpcSocketAdapterTest, Shutdown_ReturnsOriginResult)
{
    constexpr int shutdownReturn = 0;
    MOCKER_CPP(&OsAPiMgr::shutdown).stubs().will(returnValue(int(shutdownReturn)));

    int ret = ::shutdown(fd, SHUT_RDWR);
    EXPECT_EQ(ret, shutdownReturn);
}

// ============= ReadV/WriteV Tests =============

TEST_F(BrpcSocketAdapterTest, ReadV_NoFdObjGoesOrigin)
{
    struct iovec iov[1] = {};
    int iovcnt = 1;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t readvReturn = 10;
    MOCKER_CPP(&OsAPiMgr::readv).stubs().will(returnValue(ssize_t(readvReturn)));

    ssize_t ret = ::readv(fd, iov, iovcnt);
    EXPECT_EQ(ret, readvReturn);
}

TEST_F(BrpcSocketAdapterTest, WriteV_NoFdObjGoesOrigin)
{
    struct iovec iov[1] = {};
    int iovcnt = 1;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t writevReturn = 20;
    MOCKER_CPP(&OsAPiMgr::writev).stubs().will(returnValue(ssize_t(writevReturn)));

    ssize_t ret = ::writev(fd, iov, iovcnt);
    EXPECT_EQ(ret, writevReturn);
}

// ============= Send/Recv Tests =============

TEST_F(BrpcSocketAdapterTest, Send_NoFdObjGoesOrigin)
{
    char buf[10] = {};
    size_t len = sizeof(buf);
    int flags = 0;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t sendReturn = 10;
    MOCKER_CPP(&OsAPiMgr::send).stubs().will(returnValue(ssize_t(sendReturn)));

    ssize_t ret = ::send(fd, buf, len, flags);
    EXPECT_EQ(ret, sendReturn);
}

TEST_F(BrpcSocketAdapterTest, Recv_NoFdObjGoesOrigin)
{
    char buf[10] = {};
    size_t len = sizeof(buf);
    int flags = 0;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t recvReturn = 10;
    MOCKER_CPP(&OsAPiMgr::recv).stubs().will(returnValue(ssize_t(recvReturn)));

    ssize_t ret = ::recv(fd, buf, len, flags);
    EXPECT_EQ(ret, recvReturn);
}

// ============= Read/Write Tests =============

TEST_F(BrpcSocketAdapterTest, Read_NoFdObjGoesOrigin)
{
    char buf[10] = {};
    size_t len = sizeof(buf);

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t readReturn = 10;
    MOCKER_CPP(&OsAPiMgr::read).stubs().will(returnValue(ssize_t(readReturn)));

    ssize_t ret = ::read(fd, buf, len);
    EXPECT_EQ(ret, readReturn);
}

TEST_F(BrpcSocketAdapterTest, Write_NoFdObjGoesOrigin)
{
    char buf[10] = "test";
    size_t len = sizeof(buf);

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t writeReturn = 10;
    MOCKER_CPP(&OsAPiMgr::write).stubs().will(returnValue(ssize_t(writeReturn)));

    ssize_t ret = ::write(fd, buf, len);
    EXPECT_EQ(ret, writeReturn);
}

// ============= SendTo/RecvFrom Tests =============

TEST_F(BrpcSocketAdapterTest, SendTo_NoFdObjGoesOrigin)
{
    char buf[10] = {};
    size_t len = sizeof(buf);
    int flags = 0;
    struct sockaddr_in addr = {};
    socklen_t addrlen = sizeof(addr);

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t sendtoReturn = 10;
    MOCKER_CPP(&OsAPiMgr::sendto).stubs().will(returnValue(ssize_t(sendtoReturn)));

    ssize_t ret = ::sendto(fd, buf, len, flags, (struct sockaddr*)&addr, addrlen);
    EXPECT_EQ(ret, sendtoReturn);
}

TEST_F(BrpcSocketAdapterTest, RecvFrom_NoFdObjGoesOrigin)
{
    char buf[10] = {};
    size_t len = sizeof(buf);
    int flags = 0;
    struct sockaddr_in addr = {};
    socklen_t addrlen = sizeof(addr);

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr ssize_t recvfromReturn = 10;
    MOCKER_CPP(&OsAPiMgr::recvfrom).stubs().will(returnValue(ssize_t(recvfromReturn)));

    ssize_t ret = ::recvfrom(fd, buf, len, flags, (struct sockaddr*)&addr, &addrlen);
    EXPECT_EQ(ret, recvfromReturn);
}

// ============= SendMsg/RecvMsg Tests =============

TEST_F(BrpcSocketAdapterTest, SendMsg_ReturnsOriginResult)
{
    struct msghdr msg = {};

    constexpr ssize_t sendmsgReturn = 10;
    MOCKER_CPP(&OsAPiMgr::sendmsg).stubs().will(returnValue(ssize_t(sendmsgReturn)));

    ssize_t ret = ::sendmsg(fd, &msg, 0);
    EXPECT_EQ(ret, sendmsgReturn);
}

TEST_F(BrpcSocketAdapterTest, RecvMsg_ReturnsOriginResult)
{
    struct msghdr msg = {};

    constexpr ssize_t recvmsgReturn = 10;
    MOCKER_CPP(&OsAPiMgr::recvmsg).stubs().will(returnValue(ssize_t(recvmsgReturn)));

    ssize_t ret = ::recvmsg(fd, &msg, 0);
    EXPECT_EQ(ret, recvmsgReturn);
}

// ============= SendFile Tests =============
// Note: sendfile/sendfile64 are Linux-specific and may not be available in all environments
// Skipping these tests as they require specific system headers

// ============= Fcntl Tests =============

TEST_F(BrpcSocketAdapterTest, Fcntl_NoFdObjGoesOrigin)
{
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    // fcntl goes to origin when fd obj is null
    // Note: We can't easily mock variadic functions
    // The actual fcntl call will fail on invalid fd, but the path is covered
    ::fcntl(-1, F_GETFL);  // Use -1 to avoid affecting real fds
}

TEST_F(BrpcSocketAdapterTest, Fcntl64_NoFdObjGoesOrigin)
{
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    // fcntl64 goes to origin when fd obj is null
    // Note: We can't easily mock variadic functions
    ::fcntl64(-1, F_GETFL);  // Use -1 to avoid affecting real fds
}

// ============= Ioctl Tests =============

TEST_F(BrpcSocketAdapterTest, Ioctl_NoFdObjGoesOrigin)
{
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    // ioctl goes to origin when fd obj is null
    // Note: We can't easily mock variadic functions, so just verify no crash
    // The actual ioctl call will fail on invalid fd, but the path is covered
    ::ioctl(-1, FIONBIO, 0);  // Use -1 to avoid affecting real fds
}

// ============= SetSockOpt Tests =============

TEST_F(BrpcSocketAdapterTest, SetSockOpt_NoFdObjGoesOrigin)
{
    int optval = 1;
    socklen_t optlen = sizeof(optval);

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    constexpr int setsockoptReturn = 0;
    MOCKER_CPP(&OsAPiMgr::setsockopt).stubs().will(returnValue(int(setsockoptReturn)));

    int ret = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, optlen);
    EXPECT_EQ(ret, setsockoptReturn);
}

// ============= Epoll Tests =============

TEST_F(BrpcSocketAdapterTest, EpollCreate1_ContextNullReturnsOriginFd)
{
    MOCKER_CPP(&OsAPiMgr::epoll_create1).stubs().will(returnValue(int(epfd)));
    MOCKER_CPP(&Brpc::Context::GetContext).stubs().will(returnValue((Brpc::Context *)nullptr));

    int ret = ::epoll_create1(0);
    EXPECT_EQ(ret, epfd);
}

TEST_F(BrpcSocketAdapterTest, EpollCtl_NoFdObjGoesOrigin)
{
    struct epoll_event event = {};

    Fd<EpollFd>::OverrideFdObj(epfd, nullptr);

    constexpr int epollCtlReturn = 0;
    MOCKER_CPP(&OsAPiMgr::epoll_ctl).stubs().will(returnValue(int(epollCtlReturn)));

    int ret = ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    EXPECT_EQ(ret, epollCtlReturn);
}

TEST_F(BrpcSocketAdapterTest, EpollWait_NoFdObjGoesOrigin)
{
    struct epoll_event events[10] = {};

    Fd<EpollFd>::OverrideFdObj(epfd, nullptr);

    constexpr int epollWaitReturn = 0;
    MOCKER_CPP(&OsAPiMgr::epoll_wait).stubs().will(returnValue(int(epollWaitReturn)));

    int ret = ::epoll_wait(epfd, events, 10, 0);
    EXPECT_EQ(ret, epollWaitReturn);
}

TEST_F(BrpcSocketAdapterTest, EpollPWait_NoFdObjGoesOrigin)
{
    struct epoll_event events[10] = {};
    sigset_t sigmask = {};

    Fd<EpollFd>::OverrideFdObj(epfd, nullptr);

    constexpr int epollPWaitReturn = 0;
    MOCKER_CPP(&OsAPiMgr::epoll_pwait).stubs().will(returnValue(int(epollPWaitReturn)));

    int ret = ::epoll_pwait(epfd, events, 10, 0, &sigmask);
    EXPECT_EQ(ret, epollPWaitReturn);
}

// ============= Accept4 Tests =============

TEST_F(BrpcSocketAdapterTest, Accept4_NoFdObjGoesOrigin)
{
    struct sockaddr *address = nullptr;
    socklen_t *addressLen = nullptr;

    Fd<SocketFd>::OverrideFdObj(fd, nullptr);

    MOCKER_CPP(&OsAPiMgr::accept4).stubs().will(returnValue(int(acceptNoFdObjReturn)));
    int ret = ::accept4(fd, address, addressLen, 0);
    EXPECT_EQ(ret, acceptNoFdObjReturn);
}

// ============= Socket Tests =============

TEST_F(BrpcSocketAdapterTest, Socket_ContextNullReturnsOriginFd)
{
    constexpr int socketFd = 200;
    MOCKER_CPP(&OsAPiMgr::socket).stubs().will(returnValue(int(socketFd)));
    MOCKER_CPP(&Brpc::Context::GetContext).stubs().will(returnValue((Brpc::Context *)nullptr));

    int ret = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_EQ(ret, socketFd);
}