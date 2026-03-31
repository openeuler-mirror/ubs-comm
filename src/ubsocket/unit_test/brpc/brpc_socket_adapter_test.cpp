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
        setenv("UBSOCKET_USE_UB_FORCE", "true", setenvOverwrite);
        setenv("UBSOCKET_TRANS_MODE", "UB", setenvOverwrite);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);

        // Avoid real umq init path in Brpc::Context.
        MOCKER_CPP(umq_init).stubs().will(returnValue(int(umqInitOkReturn)));
    }

    void TearDown() override
    {
        unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
        unsetenv("UBSOCKET_USE_UB_FORCE");
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

