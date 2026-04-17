#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "brpc_file_descriptor.h"
#include "rpc_adpt_vlog.h"

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const int SETENV_OVERWRITE = 1;
} // namespace

class BrpcConnectAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
        setenv("UBSOCKET_USE_BRPC_ZCOPY", "false", 1);
        setenv("UBSOCKET_USE_UB_FORCE", "true", 1);
        setenv("UBSOCKET_TRANS_MODE", "UB", 1);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
        int fd = 1;
        uint64_t magicNumber = 10;
        uint32_t magicNumberRecvSize = 9;
        socketfd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
        socketfd->m_tx_use_tcp = false;
    }

    void TearDown() override
    {
        unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
        unsetenv("UBSOCKET_USE_UB_FORCE");
        unsetenv("UBSOCKET_TRANS_MODE");
        if (socketfd != nullptr) {
            delete socketfd;
            socketfd = nullptr;
        }
    }

    Brpc::SocketFd *socketfd = nullptr;
};

TEST_F(BrpcConnectAdapterTest, TestConnectFailed)
{
    const struct sockaddr *address = nullptr;
    socklen_t address_len = 0;
    MOCKER_CPP(&OsAPiMgr::sendto)
            .stubs()
            .will(returnValue(int(-1)));

    // m_tx_use_tcp or m_rx_use_tcp is true,ret=-1
    socketfd->m_tx_use_tcp = true;
    int ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, -1);

    socketfd->m_tx_use_tcp = false;
    socketfd->m_rx_use_tcp = true;
    ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, -1);

    // errno is not EINPROGRESS EALREADY EISCONN
    socketfd->m_tx_use_tcp = false;
    socketfd->m_rx_use_tcp = false;
    errno = EAGAIN;
    ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, -1);

    MOCKER_CPP(&SocketFd::IsBlocking)
            .stubs()
            .will(returnValue(bool(false)));
    MOCKER_CPP(&Brpc::SocketFd::DoConnect)
            .stubs()
            .will(returnValue(int(-1)));
    errno = EINPROGRESS;
    ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, -1);
    GlobalMockObject::verify();
}

TEST_F(BrpcConnectAdapterTest, TestConnectSucceed)
{
    const struct sockaddr *address = nullptr;
    socklen_t address_len = 0;
    MOCKER_CPP(&OsAPiMgr::connect)
            .stubs()
            .will(returnValue(int(0)))
            .then(returnValue(int(-1)));
    MOCKER_CPP(&SocketFd::IsBlocking)
            .stubs()
            .will(returnValue(bool(false)));
    MOCKER_CPP(&Brpc::SocketFd::DoConnect)
            .stubs()
            .will(returnValue(int(0)));
    int ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, 0);

    errno = EINPROGRESS;
    ret = socketfd->Connect(address, address_len);
    EXPECT_EQ(ret, 0);
    GlobalMockObject::verify();
}

TEST_F(BrpcConnectAdapterTest, TestAcceptFailed)
{
    struct sockaddr *address = nullptr;
    socklen_t *address_len = nullptr;
    MOCKER_CPP(&OsAPiMgr::accept)
            .stubs()
            .will(returnValue(int(-1)));

    // m_tx_use_tcp or m_rx_use_tcp is true,ret=-1
    socketfd->m_tx_use_tcp = true;
    int ret = socketfd->Accept(address, address_len);
    EXPECT_EQ(ret, -1);

    socketfd->m_tx_use_tcp = false;
    socketfd->m_rx_use_tcp = true;
    ret = socketfd->Accept(address, address_len);
    EXPECT_EQ(ret, -1);

    socketfd->m_tx_use_tcp = false;
    socketfd->m_rx_use_tcp = false;
    errno = EINPROGRESS;
    ret = socketfd->Accept(address, address_len);
    EXPECT_EQ(ret, -1);

    errno = EAGAIN;
    ret = socketfd->Accept(address, address_len);
    EXPECT_EQ(ret, -1);
    GlobalMockObject::verify();
}

void MockAsyncEventProcess(umq_init_cfg_t cfg)
{
        return;
}

TEST_F(BrpcConnectAdapterTest, TestAcceptSucceed)
{
    setenv("UBSOCKET_TRANS_MODE", "ubmm", SETENV_OVERWRITE);
    struct sockaddr *address = nullptr;
    socklen_t *address_len = nullptr;
    MOCKER_CPP(umq_init)
            .stubs()
            .will(returnValue(int(0)));
    MOCKER_CPP(&OsAPiMgr::accept)
            .stubs()
            .will(returnValue(int(0)));
    MOCKER_CPP(&SocketFd::IsBlocking)
            .stubs()
            .will(returnValue(bool(false)));
    MOCKER_CPP(&Brpc::SocketFd::DoAccept)
            .stubs()
            .will(returnValue(int(0)));
    int ret = socketfd->Accept(address, address_len);
    EXPECT_EQ(ret, 0);
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}