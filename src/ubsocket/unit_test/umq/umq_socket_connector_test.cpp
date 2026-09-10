/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_socket_connector.h"
#include "umq_conn_helper.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_setting.h"
#include "umq_socket.h"
#include "umq_socket_acceptor.h"
#include "umq_tx_helper.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

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
static const uint64_t TEST_DEFAULT_MAIN_UMQ_HANDLE = 54321;
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
        savedShareJfr_ = GlobalSetting::UBS_ENABLE_SHARE_JFR;
        savedLinkSelectionPolicy_ = GlobalSetting::LINK_SELECTION_POLICY;
        savedTpType_ = UmqSetting::UMQ_TP_TYPE;
        savedIsBonding_ = UmqSetting::UMQ_IS_BONDING;
        savedTransMode_ = UmqSetting::UMQ_UB_TRANS_MODE;
        savedLocalEid_ = UmqSetting::UMQ_LOCAL_EID;
        savedSchedulePolicy_ = UmqSetting::UMQ_DEV_SCHEDULE_POLICY;
        savedSocketIds_ = UmqSetting::UMQ_ALL_SOCKET_IDS;
        savedProcessSocketId_ = UmqSetting::UMQ_PROCESS_SOCKET_ID;
        savedSendPtr_ = LibcApi::send_ptr;
        savedRecvPtr_ = LibcApi::recv_ptr;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_ENABLE_SHARE_JFR = savedShareJfr_;
        GlobalSetting::LINK_SELECTION_POLICY = savedLinkSelectionPolicy_;
        UmqSetting::UMQ_TP_TYPE = savedTpType_;
        UmqSetting::UMQ_IS_BONDING = savedIsBonding_;
        UmqSetting::UMQ_UB_TRANS_MODE = savedTransMode_;
        UmqSetting::UMQ_LOCAL_EID = savedLocalEid_;
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY = savedSchedulePolicy_;
        UmqSetting::UMQ_ALL_SOCKET_IDS = savedSocketIds_;
        UmqSetting::UMQ_PROCESS_SOCKET_ID = savedProcessSocketId_;
        LibcApi::send_ptr = savedSendPtr_;
        LibcApi::recv_ptr = savedRecvPtr_;
        errno = 0;
    }

    UmqConnectorOps connector_{TEST_FD};
    bool savedShareJfr_{};
    LinkSelectionPolicy savedLinkSelectionPolicy_{};
    pool_type_t savedTpType_{};
    bool savedIsBonding_{};
    ub_trans_mode savedTransMode_{};
    umq_eid_t savedLocalEid_{};
    dev_schedule_policy savedSchedulePolicy_{};
    std::vector<uint32_t> savedSocketIds_;
    int savedProcessSocketId_ = -1;
    ssize_t (*savedSendPtr_)(int, const void *, size_t, int) = nullptr;
    ssize_t (*savedRecvPtr_)(int, void *, size_t, int) = nullptr;
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

TEST_F(UmqConnectorPureLogicTest, GetCpuAffinityUmqRoute_RoundRobinWithoutSocketIds_UsesAllRoutes)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
    UmqSetting::UMQ_ALL_SOCKET_IDS.clear();
    UmqSetting::UMQ_PROCESS_SOCKET_ID = -1;
    connector_.peer_all_socket_ids_.clear();
    connector_.peer_socket_id_ = -1;

    umq_route_list_t routeList{};
    routeList.route_num = 3;
    for (uint32_t i = 0; i < routeList.route_num; ++i) {
        routeList.routes[i].src_port.bs.chip_id = i;
        routeList.routes[i].dst_port.bs.chip_id = i + 1;
    }
    std::vector<umq_route_t> affineRoutes;
    std::vector<umq_route_t> nonAffineRoutes;

    EXPECT_EQ(connector_.GetCpuAffinityUmqRoute(routeList, affineRoutes, nonAffineRoutes), UBS_OK);
    ASSERT_EQ(affineRoutes.size(), routeList.route_num);
    EXPECT_TRUE(nonAffineRoutes.empty());
    EXPECT_EQ(affineRoutes[2].src_port.bs.chip_id, 2u);
}

TEST_F(UmqConnectorPureLogicTest, NewBaseUmqCreateOptions_StandalonePool_DoesNotSetShareTransport)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    UmqSetting::UMQ_TP_TYPE = POOL;
    umq_create_option_t option{};

    EXPECT_EQ(UmqConnHelper::NewBaseUmqCreateOptions(option, RM_CTP), UBS_OK);
    EXPECT_EQ(option.create_flag & UMQ_CREATE_FLAG_SHARE_TRANSPORT, 0u);
    EXPECT_EQ(option.tp_mode, UMQ_TM_RM);
    EXPECT_EQ(option.tp_type, UMQ_TP_TYPE_CTP);
}

TEST_F(UmqConnectorPureLogicTest, NewBaseUmqCreateOptions_SharedPool_SetsShareTransport)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    UmqSetting::UMQ_TP_TYPE = POOL;
    umq_create_option_t option{};

    EXPECT_EQ(UmqConnHelper::NewBaseUmqCreateOptions(option, RM_CTP), UBS_OK);
    EXPECT_NE(option.create_flag & UMQ_CREATE_FLAG_SHARE_TRANSPORT, 0u);
    EXPECT_EQ(option.tp_mode, UMQ_TM_RM);
    EXPECT_EQ(option.tp_type, UMQ_TP_TYPE_CTP);
}

TEST_F(UmqConnectorPureLogicTest, IsPortFailure_FlowControlErrorUsesUnderlyingCqeStatus)
{
    umq_buf_t qbuf{};
    auto *bufPro = reinterpret_cast<umq_buf_pro_t *>(qbuf.qbuf_ext);
    qbuf.status = UMQ_FAKE_BUF_FC_ERR;

    bufPro->rsvd1 = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    EXPECT_FALSE(UmqTxHelper::IsPortFailure(&qbuf));

    bufPro->rsvd1 = UMQ_BUF_ACK_TIMEOUT_ERR;
    EXPECT_TRUE(UmqTxHelper::IsPortFailure(&qbuf));
}

TEST_F(UmqConnectorPureLogicTest, BuildNegotiateRsp_RoundRobinWithoutSocketIds_Succeeds)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
    UmqSetting::UMQ_ALL_SOCKET_IDS.clear();
    UmqSetting::UMQ_PROCESS_SOCKET_ID = -1;
    UmqAcceptorOps acceptor(TEST_FD);
    NegotiateRsp rsp{};

    EXPECT_EQ(acceptor.BuildNegotiateRsp(rsp), UBS_OK);
    EXPECT_EQ(rsp.socket_id_count, 0u);
    EXPECT_EQ(rsp.aff_sock_id, -1);
}

TEST_F(UmqConnectorPureLogicTest, BuildNegotiateRsp_AffinityWithoutSocketIds_Fails)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    UmqSetting::UMQ_ALL_SOCKET_IDS.clear();
    UmqAcceptorOps acceptor(TEST_FD);
    NegotiateRsp rsp{};

    EXPECT_EQ(acceptor.BuildNegotiateRsp(rsp), UBS_ERROR);
}

TEST_F(UmqConnectorPureLogicTest, BuildNegotiateRsp_RawDeviceWithoutSocketIds_Succeeds)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    UmqSetting::UMQ_ALL_SOCKET_IDS.clear();
    UmqAcceptorOps acceptor(TEST_FD);
    NegotiateRsp rsp{};

    EXPECT_EQ(acceptor.BuildNegotiateRsp(rsp), UBS_OK);
    EXPECT_EQ(rsp.socket_id_count, 0u);
}

TEST_F(UmqConnectorPureLogicTest, AcceptNegotiate_NonBondingRawDevice_ReturnsSuccess)
{
    int sockets[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    auto closeSockets = MakeScopeExit([&sockets]() {
        ::close(sockets[0]);
        ::close(sockets[1]);
    });

    LockRegistry::RegisterDefaultOps();
    LibcApi::send_ptr = ::send;
    LibcApi::recv_ptr = ::recv;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSetting::UMQ_IS_BONDING = false;
    UmqSetting::UMQ_UB_TRANS_MODE = RM_CTP;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    UmqSetting::UMQ_ALL_SOCKET_IDS.clear();
    UmqSetting::UMQ_LOCAL_EID = {};
    UmqSetting::UMQ_LOCAL_EID.raw[0] = 1;

    NegotiateReq req{};
    req.trans_mode = RM_CTP;
    req.is_bonding = 0;
    req.local_eid.raw[0] = 2;
    uint32_t peerVersion = UBS_PROTOCOL_VERSION.GetWhole();
    ASSERT_EQ(SocketConnHelper::SendSocketData(sockets[1], &peerVersion, sizeof(peerVersion), 100),
              static_cast<ssize_t>(sizeof(peerVersion)));
    ASSERT_EQ(SocketConnHelper::SendLengthPrefixed(sockets[1], &req, sizeof(req), 100), UBS_OK);

    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(sockets[0]);
    SocketPtr socket = RefConvert<UmqSocket, Socket>(umqSocket);
    UmqAcceptorOps acceptor(sockets[0]);

    EXPECT_EQ(acceptor.AcceptNegotiate(socket), UBS_OK);
    EXPECT_EQ(umqSocket->GetNegotiatedVersion(), peerVersion);
    EXPECT_EQ(acceptor.umq_conn_info_.conn_eid.raw[0], 1);
    EXPECT_EQ(acceptor.umq_conn_info_.peer_eid.raw[0], 2);
}

class UmqSocketMainUmqTest : public testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        UmqEidTable::Instance().Clean();
        savedTransMode_ = UmqSetting::UMQ_UB_TRANS_MODE;
        savedLinkSelectionPolicy_ = GlobalSetting::LINK_SELECTION_POLICY;
        errno = 0;
    }

    void TearDown() override
    {
        UmqEidTable::Instance().Clean();
        UmqSetting::UMQ_UB_TRANS_MODE = savedTransMode_;
        GlobalSetting::LINK_SELECTION_POLICY = savedLinkSelectionPolicy_;
        errno = 0;
    }

    ub_trans_mode savedTransMode_ = RM_TP;
    LinkSelectionPolicy savedLinkSelectionPolicy_ = LinkSelectionPolicy::RAW_DEVICE;
};

TEST_F(UmqSocketMainUmqTest, GetOrCreateMainUmq_NegotiatedModeMissing_UsesDefaultModeHandle)
{
    umq_eid_t eid{};
    eid.raw[0] = 1;
    UmqSetting::UMQ_UB_TRANS_MODE = RM_CTP;
    UmqEidTable::Instance().Add(eid, RM_CTP, TEST_DEFAULT_MAIN_UMQ_HANDLE);
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_FD);
    socket->SetTransMode(RM_TP);
    umq_create_option_t option{};

    EXPECT_EQ(socket->GetOrCreateMainUmq(&option, &eid), TEST_DEFAULT_MAIN_UMQ_HANDLE);
}

TEST_F(UmqSocketMainUmqTest, GetOrCreateMainUmq_BondingEidMismatch_ReusesUniqueMainUmq)
{
    umq_eid_t bondingEid{};
    bondingEid.raw[0] = 1;
    umq_eid_t connectionEid{};
    connectionEid.raw[0] = 2;
    UmqSetting::UMQ_UB_TRANS_MODE = RM_CTP;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    UmqEidTable::Instance().Add(bondingEid, RM_CTP, TEST_DEFAULT_MAIN_UMQ_HANDLE);
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_FD);
    socket->SetTransMode(RM_CTP);
    umq_create_option_t option{};

    EXPECT_EQ(socket->GetOrCreateMainUmq(&option, &connectionEid), TEST_DEFAULT_MAIN_UMQ_HANDLE);
}

TEST_F(UmqSocketMainUmqTest, GetUniqueByMode_MultipleHandles_ReturnsFalse)
{
    umq_eid_t firstEid{};
    firstEid.raw[0] = 1;
    umq_eid_t secondEid{};
    secondEid.raw[0] = 2;
    UmqEidTable::Instance().Add(firstEid, RM_CTP, TEST_DEFAULT_MAIN_UMQ_HANDLE);
    UmqEidTable::Instance().Add(secondEid, RM_CTP, TEST_DEFAULT_MAIN_UMQ_HANDLE + 1);
    std::shared_ptr<MainUmqState> state;

    EXPECT_FALSE(UmqEidTable::Instance().GetUniqueByMode(RM_CTP, state));
    EXPECT_EQ(state, nullptr);
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
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

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, UBS_ERROR);

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

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    errno = 0;
    ock::ubs::Result ret =
        connector_.PrepareConnect(TEST_NEW_FD, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr), sock);
    EXPECT_EQ(ret, UBS_ERROR);

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
