/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_socket_connector.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_setting.h"
#include "umq_socket.h"

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <gtest/gtest.h>
#include <securec.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_scope_exit.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const int TEST_FD = 42;
static const int TEST_NEW_FD = 100;
static const uint64_t TEST_UMQ_HANDLE = 12345;
static const uint32_t TEST_DEPTH = 64;

static int g_mockSetsockoptCallCount = 0;
static int g_mockConnectCallCount = 0;
static int g_mockFcntlCallCount = 0;

static int MockSetsockoptSuccess(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    g_mockSetsockoptCallCount++;
    return 0;
}

static int MockSetsockoptFailEnoprotoopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    g_mockSetsockoptCallCount++;
    errno = ENOPROTOOPT;
    return -1;
}

static int MockSetsockoptFailEopnotsupp(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    g_mockSetsockoptCallCount++;
    errno = EOPNOTSUPP;
    return -1;
}

static int MockSetsockoptFailEio(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    g_mockSetsockoptCallCount++;
    errno = EIO;
    return -1;
}

static int MockConnectSuccess(int socket, const struct sockaddr *address, socklen_t address_len)
{
    g_mockConnectCallCount++;
    return 0;
}

static int MockConnectFailEinprogress(int socket, const struct sockaddr *address, socklen_t address_len)
{
    g_mockConnectCallCount++;
    errno = EINPROGRESS;
    return -1;
}

static int MockConnectFailEalready(int socket, const struct sockaddr *address, socklen_t address_len)
{
    g_mockConnectCallCount++;
    errno = EALREADY;
    return -1;
}

static int MockConnectFailEisconn(int socket, const struct sockaddr *address, socklen_t address_len)
{
    g_mockConnectCallCount++;
    errno = EISCONN;
    return -1;
}

static int MockConnectFailEconnrefused(int socket, const struct sockaddr *address, socklen_t address_len)
{
    g_mockConnectCallCount++;
    errno = ECONNREFUSED;
    return -1;
}

static int MockFcntlBlocking(int fd, int cmd, ...)
{
    g_mockFcntlCallCount++;
    if (cmd == F_GETFL) {
        return 0;
    }
    return 0;
}

static int MockFcntlNonBlocking(int fd, int cmd, ...)
{
    g_mockFcntlCallCount++;
    if (cmd == F_GETFL) {
        return O_NONBLOCK;
    }
    if (cmd == F_SETFL) {
        return 0;
    }
    return 0;
}

static int MockGetsockoptUbsConnection(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (optname == TCP_UB_SOCKET_HANDSHAKE && optval != nullptr) {
        *static_cast<int *>(optval) = 1;
    }
    return 0;
}

static int MockGetsockoptNoUbsConnection(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (optname == TCP_UB_SOCKET_HANDSHAKE && optval != nullptr) {
        *static_cast<int *>(optval) = 0;
    }
    return 0;
}

static int MockGetsockoptFail(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    errno = ENOPROTOOPT;
    return -1;
}

static int MockSocketSuccess(int domain, int type, int protocol)
{
    return 200;
}

static int MockCloseSuccess(int fd)
{
    return 0;
}

static ssize_t MockSendtoSuccess(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to,
                                 socklen_t tolen)
{
    return static_cast<ssize_t>(n);
}

static ssize_t MockSendtoFail(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t tolen)
{
    errno = ECONNREFUSED;
    return -1;
}

static uint32_t MockBindInfoGetSuccess(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    return bind_info_size;
}

static uint32_t MockBindInfoGetFailZero(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = EINVAL;
    return 0;
}

static int MockBindSuccess(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    return 0;
}

static int MockBindFailEperm(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    return -UMQ_ERR_EPERM;
}

static int MockBindFailEnodev(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = EIO;
    return -UMQ_ERR_ENODEV;
}

static int MockGetRouteListFail(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    return 0;
}

static ock::ubs::Result MockCreateLocalUmqSuccess(umq_eid_t *conn_eid, umq_used_ports_t &used_ports,
                                                  umq_eid_t *conn_eid_used, umq_topo_type_t &topo_type)
{
    return UBS_OK;
}

static ock::ubs::Result MockCreateLocalUmqFail(umq_eid_t *conn_eid, umq_used_ports_t &used_ports,
                                               umq_eid_t *conn_eid_used, umq_topo_type_t &topo_type)
{
    return UBS_UMQ_CREATE;
}

static ock::ubs::Result MockGenerateSocketCommOpsSuccess(const SocketPtr &sock)
{
    return UBS_OK;
}

static ock::ubs::Result MockPrefillRxSuccess()
{
    return UBS_OK;
}

static ock::ubs::Result MockPrefillRxFail()
{
    return UBS_PREFILL_RX;
}

void ResetCallCounts()
{
    g_mockSetsockoptCallCount = 0;
    g_mockConnectCallCount = 0;
    g_mockFcntlCallCount = 0;
}

void SaveAndResetLibcApiPtrs() {}

void RestoreLibcApiPtrs() {}

void SetLibcApiPtrsToNull()
{
    LibcApi::setsockopt_ptr = nullptr;
    LibcApi::connect_ptr = nullptr;
    LibcApi::fcntl_ptr = nullptr;
    LibcApi::getsockopt_ptr = nullptr;
    LibcApi::socket_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
    LibcApi::sendto_ptr = nullptr;
    LibcApi::send_ptr = nullptr;
    LibcApi::recv_ptr = nullptr;
}
} // namespace

class UmqConnectorOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetCallCounts();
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
        GlobalSetting::UBS_BACKUP_LINK_ENABLED = false;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        UmqSetting::UMQ_IS_BONDING = false;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
        UmqSetting::UMQ_ALL_SOCKET_IDS = {0, 1};
        UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        SetLibcApiPtrsToNull();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }

    UmqConnectorOps connector_{TEST_FD};
};

class UmqConnectorPureLogicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
    }

    void TearDown() override
    {
        errno = 0;
    }

    UmqConnectorOps connector_{TEST_FD};
};

// ==================== GetTargetChipId ====================

TEST_F(UmqConnectorPureLogicTest, GetTargetChipId_FoundInSocketIds_ReturnsChipId)
{
    std::vector<uint32_t> socketIds = {0, 1, 2};
    std::vector<uint32_t> chipIdList = {10, 20, 30};
    EXPECT_EQ(connector_.GetTargetChipId(socketIds, chipIdList, 1), 20u);
}

TEST_F(UmqConnectorPureLogicTest, GetTargetChipId_NotFoundInSocketIds_ReturnsUint32Max)
{
    std::vector<uint32_t> socketIds = {0, 1, 2};
    std::vector<uint32_t> chipIdList = {10, 20, 30};
    EXPECT_EQ(connector_.GetTargetChipId(socketIds, chipIdList, 5), UINT32_MAX);
}

TEST_F(UmqConnectorPureLogicTest, GetTargetChipId_IndexOutOfBounds_ReturnsUint32Max)
{
    std::vector<uint32_t> socketIds = {0};
    std::vector<uint32_t> chipIdList;
    EXPECT_EQ(connector_.GetTargetChipId(socketIds, chipIdList, 0), UINT32_MAX);
}

TEST_F(UmqConnectorPureLogicTest, GetTargetChipId_EmptySocketIds_ReturnsUint32Max)
{
    std::vector<uint32_t> socketIds;
    std::vector<uint32_t> chipIdList = {10};
    EXPECT_EQ(connector_.GetTargetChipId(socketIds, chipIdList, 0), UINT32_MAX);
}

TEST_F(UmqConnectorPureLogicTest, GetTargetChipId_EmptyChipIdList_ReturnsUint32Max)
{
    std::vector<uint32_t> socketIds = {0};
    std::vector<uint32_t> chipIdList;
    EXPECT_EQ(connector_.GetTargetChipId(socketIds, chipIdList, 0), UINT32_MAX);
}

// ==================== BuildNegotiateReq ====================

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_SetsFieldsCorrectly)
{
    UmqSetting::UMQ_LOCAL_EID = {};
    UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
    UmqSetting::UMQ_IS_BONDING = false;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(req.trans_mode, RM_TP);
    EXPECT_EQ(req.is_bonding, 0);
    EXPECT_EQ(req.enable_share_jfr, 0);
    EXPECT_EQ(req.schedule_policy, static_cast<uint8_t>(dev_schedule_policy::ROUND_ROBIN));

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_ShareJfrEnabled_SetsEnableShareJfr)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    UmqSetting::UMQ_IS_BONDING = true;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(req.enable_share_jfr, 1);
    EXPECT_EQ(req.is_bonding, 1);
    EXPECT_EQ(req.schedule_policy, static_cast<uint8_t>(dev_schedule_policy::CPU_AFFINITY));

    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    UmqSetting::UMQ_IS_BONDING = false;
    GlobalMockObject::verify();
}

// ==================== PrepareConnect - UB_SOCK_OPT mode ====================

TEST_F(UmqConnectorOpsTest, PrepareConnect_HandshakeOpt_SetsockoptSuccess_ConnectSuccess)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, UBS_OK);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_HandshakeOpt_SetsockoptFailEnoprotoopt_FallbackTfo)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptFailEnoprotoopt;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::socket_ptr = MockSocketSuccess;
    LibcApi::close_ptr = MockCloseSuccess;
    LibcApi::sendto_ptr = MockSendtoSuccess;
    LibcApi::getsockopt_ptr = MockGetsockoptNoUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::TFO);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_HandshakeOpt_SetsockoptFailEopnotsupp_FallbackTfo)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptFailEopnotsupp;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::socket_ptr = MockSocketSuccess;
    LibcApi::close_ptr = MockCloseSuccess;
    LibcApi::sendto_ptr = MockSendtoSuccess;
    LibcApi::getsockopt_ptr = MockGetsockoptNoUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::TFO);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_HandshakeOpt_SetsockoptFailOtherErrno_NoFallback)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptFailEio;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(GlobalSetting::UBS_HAND_SHAKE_MODE, UBHandshakeMode::UB_SOCK_OPT);

    GlobalMockObject::verify();
}

// ==================== PrepareConnect - errno handling ====================

TEST_F(UmqConnectorOpsTest, PrepareConnect_Einprogress_RetCorrectedToOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEinprogress;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, EINPROGRESS);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_Ealready_RetCorrectedToOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEalready;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, EALREADY);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_Eisconn_ContinuesWithOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEisconn;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_OtherErrno_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEconnrefused;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ECONNREFUSED);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_RawEstablishedState_ReturnsEarly)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEinprogress;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_NotUbsConnection_ReturnsEarly)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectFailEinprogress;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::getsockopt_ptr = MockGetsockoptNoUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

// ==================== PrepareConnect - null address ====================

TEST_F(UmqConnectorOpsTest, PrepareConnect_NullAddress_NoPeerIp)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    errno = 0;
    ock::ubs::Result ret = connector_.PrepareConnect(TEST_NEW_FD, nullptr, 0, sock);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.umq_conn_info_.peer_ip, "");

    GlobalMockObject::verify();
}

// ==================== PrepareConnect - TFO mode ====================

TEST_F(UmqConnectorOpsTest, PrepareConnect_TfoMode_SendtoSuccessButDup3Fail_ReturnsMinus1)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::socket_ptr = MockSocketSuccess;
    LibcApi::close_ptr = MockCloseSuccess;
    LibcApi::sendto_ptr = MockSendtoSuccess;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::getsockopt_ptr = MockGetsockoptNoUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, PrepareConnect_TfoMode_SendtoFail_ReturnsMinus1)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::socket_ptr = MockSocketSuccess;
    LibcApi::close_ptr = MockCloseSuccess;
    LibcApi::sendto_ptr = MockSendtoFail;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::getsockopt_ptr = MockGetsockoptNoUbsConnection;

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    struct sockaddr_in addr {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

// ==================== Negotiate ====================

TEST_F(UmqConnectorOpsTest, Negotiate_RecvFail_ReturnsUbsError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);

    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(sizeof(NegotiateReq))));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));

    errno = 0;
    ock::ubs::Result ret = connector_.Negotiate(TEST_NEW_FD, sock);
    EXPECT_EQ(ret, UBS_ERROR);

    GlobalMockObject::verify();
}