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

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <securec.h>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_scope_exit.h"
#include "common/ubsocket_version.h"
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

// TFO 模式下 IsUbsConnection 读取 TCP_INFO 的 tcpi_options，需设置 TCPI_OPT_SYN_DATA
static int MockGetsockoptTfoSynData(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (optname == TCP_INFO && optval != nullptr) {
        auto *info = static_cast<tcp_info *>(optval);
        info->tcpi_options = TCPI_OPT_SYN_DATA;
    }
    return 0;
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

// ==================== 扩展测试的 mock 控制变量与 fake ====================
static const umq_eid_t g_connEid = {};  // 零初始化 eid

static ock::ubs::Result g_createLocalUmqRet = UBS_OK;
static ock::ubs::Result g_generateCommOpsRet = UBS_OK;
static uint32_t g_bindInfoGetRet = 0;
static int g_bindRet = 0;
static ock::ubs::Result g_prefillRxRet = UBS_OK;
static ock::ubs::Result g_registerJfrRet = UBS_OK;
static ock::ubs::Result g_checkDevAddRet = UBS_OK;
static bool g_isPortInCooldown = false;
static ock::ubs::Result g_recvPeerRet = UBS_OK;
static uint32_t g_recvCpMsgBindSize = 0;
static uint32_t g_recvVersionRaw = 0;  // 0 => 默认 UBS_PROTOCOL_VERSION
static int32_t g_recvRspRetCode = 0;
static uint32_t g_recvRspSocketIdCount = 0;
static int32_t g_recvRspAffSockId = 0;
static ub_trans_mode g_recvRspPeerTransMode = RM_TP;
static uint8_t g_recvRspReserved0 = 0;

static ock::ubs::Result MockCreateLocalUmqForState(const umq_eid_t *conn_eid, umq_used_ports_t &used_ports,
                                                   umq_topo_type_t &topo_type)
{
    return g_createLocalUmqRet;
}

static ock::ubs::Result MockGenerateSocketCommOpsForState(const SocketPtr &sock)
{
    return g_generateCommOpsRet;
}

static uint32_t MockBindInfoGetForState(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    return g_bindInfoGetRet;
}

static int MockBindForState(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    return g_bindRet;
}

static ock::ubs::Result MockPrefillRxForState(uint64_t umq_handle)
{
    return g_prefillRxRet;
}

static ock::ubs::Result MockRegisterSharedJfrForReadForState(uint64_t main_umq_handle)
{
    return g_registerJfrRet;
}

static ock::ubs::Result MockCheckDevAddForState(const umq_eid_t &conn_eid)
{
    return g_checkDevAddRet;
}

static bool MockIsPortInCooldownForState(const umq_port_id_t &port)
{
    return g_isPortInCooldown;
}

static ssize_t MockSendSocketDataSize(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    return static_cast<ssize_t>(size);
}

static ssize_t MockSendSocketDataFail(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    return -1;
}

static ock::ubs::Result MockSendLengthPrefixedOk(int fd, const void *body, uint32_t obj_size, uint32_t timeout_ms)
{
    return UBS_OK;
}

static ock::ubs::Result MockRecvLengthPrefixedCpMsg(int fd, void *body, uint32_t obj_size, uint32_t timeout_ms)
{
    auto *cp_msg = static_cast<CpMsg *>(body);
    cp_msg->queue_bind_info_size = g_recvCpMsgBindSize;
    return UBS_OK;
}

static ssize_t MockRecvSocketDataPeer(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    memcpy_s(const_cast<void *>(buf), size, &g_recvPeerRet, sizeof(g_recvPeerRet));
    return static_cast<ssize_t>(size);
}

static ssize_t MockRecvSocketDataVersion(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    const uint32_t version = g_recvVersionRaw != 0 ? g_recvVersionRaw : UBS_PROTOCOL_VERSION.GetWhole();
    memcpy_s(const_cast<void *>(buf), size, &version, sizeof(version));
    return static_cast<ssize_t>(size);
}

static ock::ubs::Result MockRecvLengthPrefixedRsp(int fd, void *body, uint32_t obj_size, uint32_t timeout_ms)
{
    auto *rsp = static_cast<NegotiateRsp *>(body);
    rsp->ret_code = g_recvRspRetCode;
    rsp->aff_sock_id = g_recvRspAffSockId;
    rsp->peer_trans_mode = g_recvRspPeerTransMode;
    rsp->reserved[0] = g_recvRspReserved0;
    rsp->socket_id_count = g_recvRspSocketIdCount;
    for (uint32_t i = 0; i < g_recvRspSocketIdCount && i < NEGOTIATE_SOCKET_ID_MAX_NUM; ++i) {
        rsp->socket_ids[i] = i + 1;
    }
    return UBS_OK;
}

static int g_bindSeqCallCount = 0;
static int g_sendLenPrefixedSeqCallCount = 0;

void ResetExtMockState()
{
    g_createLocalUmqRet = UBS_OK;
    g_generateCommOpsRet = UBS_OK;
    g_bindInfoGetRet = 0;
    g_bindRet = 0;
    g_prefillRxRet = UBS_OK;
    g_registerJfrRet = UBS_OK;
    g_checkDevAddRet = UBS_OK;
    g_isPortInCooldown = false;
    g_recvPeerRet = UBS_OK;
    g_recvCpMsgBindSize = 0;
    g_recvVersionRaw = 0;
    g_recvRspRetCode = 0;
    g_recvRspSocketIdCount = 0;
    g_recvRspAffSockId = 0;
    g_recvRspPeerTransMode = RM_TP;
    g_recvRspReserved0 = 0;
    g_bindSeqCallCount = 0;
    g_sendLenPrefixedSeqCallCount = 0;
}

// 安装 DoUbConnect 成功路径所需的公共下游 mock（含 EidTable 预置）。
// SendLengthPrefixed / RecvLengthPrefixed 因不同场景需要不同 fake，由各用例自行安装。
void InstallDoUbConnectSuccessMocks(UmqSocketPtr &umq_socket)
{
    g_bindInfoGetRet = UMQ_BIND_INFO_SIZE_MAX;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(invoke(&MockBindInfoGetForState));
    MOCKER_CPP(::umq_bind).stubs().will(invoke(&MockBindForState));
    MOCKER_CPP(::umq_state_get).stubs().will(returnValue(QUEUE_STATE_READY));
    umq_socket->SetTransMode(UmqSetting::UMQ_UB_TRANS_MODE);
    UmqEidTable::Instance().Add(g_connEid, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(invoke(&MockPrefillRxForState));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead).stubs().will(invoke(&MockRegisterSharedJfrForReadForState));
}

// ---- 状态机/重试/协商路径扩展 mock ----

static int MockBindForStateSeq(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
{
    errno = 0;
    ++g_bindSeqCallCount;
    // 首轮失败以触发重试，重试轮成功
    return g_bindSeqCallCount == 1 ? -1 : 0;
}

static ock::ubs::Result MockSendLengthPrefixedOkThenFail(int fd, const void *body, uint32_t obj_size,
                                                         uint32_t timeout_ms)
{
    ++g_sendLenPrefixedSeqCallCount;
    // 首轮(kSTART DoUbConnect 控制信令)成功；第二轮(kRETRY DoUbConnectRetry 重试信令)失败
    return g_sendLenPrefixedSeqCallCount == 1 ? UBS_OK : UBS_ERROR;
}

static ssize_t MockRecvSocketDataFail(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    return -1;
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
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        UmqSetting::UMQ_IS_BONDING = false;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
        UmqSetting::UMQ_ALL_SOCKET_IDS = {0, 1};
        UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;
        UmqSetting::UMQ_DEV_NAME = "";
        UmqSetting::UMQ_DEV_IP = "";
        GlobalSetting::UBS_EARLY_ACK = false;
        GlobalSetting::UBS_CONNECT_PRECREATE = false;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
        ResetExtMockState();
        /* 方案B 的"发起连接前预建"默认开；既有 PrepareConnect 用例聚焦 TCP 层
         * 行为，这里关闭以保持它们逐字节不变。方案B 的行为由专门用例覆盖。 */
        GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO = false;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        SetLibcApiPtrsToNull();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        UmqEidTable::Instance().Clean();
        GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO = true; /* 恢复默认 */
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

// ==================== 建链快路径：能力位与降级共识合成 ====================

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_EarlyAckEnabled_SetsCapFlag)
{
    GlobalSetting::UBS_EARLY_ACK = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_NE(req.cap_flags & NEGO_CAP_EARLY_ACK, 0); /* 按位断言：cap_flags 还承载其他能力位（如 CARRY_BINDINFO） */
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_EarlyAckDisabled_ClearsCapFlag)
{
    GlobalSetting::UBS_EARLY_ACK = false;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(req.cap_flags & NEGO_CAP_EARLY_ACK, 0); /* 按位断言：其余能力位（如 CARRY_BINDINFO）独立于 EARLY_ACK */
    GlobalSetting::UBS_EARLY_ACK = true;
    GlobalMockObject::verify();
}

/* 并行 bind：本端暂存了 bind_info（走方案B）且开关开 ⇒ 声明 DEFER_BIND_RET；开关关或未暂存 ⇒ 不声明。 */
TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_ParallelBindOn_SetsDeferFlag)
{
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.local_bind_info_len_ = 32;
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_NE(req.cap_flags & NEGO_CAP_REQ_CARRY_BINDINFO, 0);
    EXPECT_NE(req.cap_flags & NEGO_CAP_DEFER_BIND_RET, 0);
    connector_.local_bind_info_len_ = 0;
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_ParallelBindOff_ClearsDeferFlag)
{
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = false;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.local_bind_info_len_ = 32;
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_NE(req.cap_flags & NEGO_CAP_REQ_CARRY_BINDINFO, 0);
    EXPECT_EQ(req.cap_flags & NEGO_CAP_DEFER_BIND_RET, 0);
    connector_.local_bind_info_len_ = 0;
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, BuildNegotiateReq_NoStash_NoDeferFlag)
{
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.local_bind_info_len_ = 0; /* 未走方案B ⇒ 也谈不上推迟 */
    NegotiateReq req{};
    EXPECT_EQ(connector_.BuildNegotiateReq(&req, umqSocket), static_cast<ock::ubs::Result>(UBS_OK));
    EXPECT_EQ(req.cap_flags & NEGO_CAP_REQ_CARRY_BINDINFO, 0);
    EXPECT_EQ(req.cap_flags & NEGO_CAP_DEFER_BIND_RET, 0);
    GlobalMockObject::verify();
}

/* 交叉 ack 的降级共识合成必须与经典回声(echo)逐组合等价：
 * 经典：服务端发出前执行 if (IsDegradable(客户端ack) && 服务端允许降级) 应答 |= DEGRADABLE，
 *       客户端 degradable_ = IsDegradable(应答)；
 * 交叉：应答为服务端原始结果（无回声），客户端 ClientDegradableVerdict 本地合成。
 * 对 服务端结果可降级 × 客户端结果可降级 × 服务端允许降级 全 8 组合断言两路一致。
 * 注：srvDeg=1 且 consent=0 的线上组合实际不可达（服务端发出前会剥掉自身掩码），
 * 此处仍纳入枚举——公式对该组合同样等价，覆盖更严。 */
TEST_F(UmqConnectorPureLogicTest, ClientDegradableVerdict_EquivalentToClassicEchoAcrossAllCombos)
{
    for (int srvDeg = 0; srvDeg <= 1; ++srvDeg) {
        for (int cliDeg = 0; cliDeg <= 1; ++cliDeg) {
            for (int srvConsent = 0; srvConsent <= 1; ++srvConsent) {
                const ock::ubs::Result server_own = srvDeg != 0 ?
                    static_cast<ock::ubs::Result>(UBS_ERROR | UBS_DEGRADABLE_MASK) :
                    static_cast<ock::ubs::Result>(UBS_OK);
                const ock::ubs::Result client_own = cliDeg != 0 ?
                    static_cast<ock::ubs::Result>(UBS_ERROR | UBS_DEGRADABLE_MASK) :
                    static_cast<ock::ubs::Result>(UBS_OK);

                // 经典路径：回声
                ock::ubs::Result classic_reply = server_own;
                if (IsDegradable(client_own) && (srvConsent != 0)) {
                    classic_reply |= UBS_DEGRADABLE_MASK;
                }
                const bool classic = IsDegradable(classic_reply);

                // 交叉路径：无回声 + 本地合成（consent 位 = 服务端允许降级）
                const bool crossed = UmqConnectorOps::ClientDegradableVerdict(server_own, client_own, true,
                                                                              srvConsent != 0);
                EXPECT_EQ(classic, crossed)
                    << "srvDeg=" << srvDeg << " cliDeg=" << cliDeg << " consent=" << srvConsent;
            }
        }
    }
}

TEST_F(UmqConnectorPureLogicTest, ClientDegradableVerdict_OldServerBitsZero_MatchesClassic)
{
    // 老服务端：能力位全 0（early=false, consent=false）——公式退化为经典 IsDegradable(peer_ret)
    const ock::ubs::Result reply_deg = static_cast<ock::ubs::Result>(UBS_ERROR | UBS_DEGRADABLE_MASK);
    const ock::ubs::Result reply_ok = static_cast<ock::ubs::Result>(UBS_OK);
    const ock::ubs::Result own_deg = static_cast<ock::ubs::Result>(UBS_ERROR | UBS_DEGRADABLE_MASK);
    EXPECT_TRUE(UmqConnectorOps::ClientDegradableVerdict(reply_deg, own_deg, false, false));
    EXPECT_FALSE(UmqConnectorOps::ClientDegradableVerdict(reply_ok, own_deg, false, false));
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

/* TFO fallback 用例说明：
 * 本用例的握手目标仅是验证 UB_SOCK_OPT 失败后模式切换到 TFO 且 TFO 路径被走到。
 * 注意:fallback 中 ConnectViaTfo 末尾的 dup3 是真实系统调用,而测试使用 fake fd
 * (新 fd=200, 旧 fd=42)并非真实 fd,dup3 必然失败,因此"dup3 成功"的完整成功 fallback
 * 分支在此用例中不可达(无法用 LibcApi 函数指针 mock dup3),本用例实际覆盖的是
 * fallback 的失败返回路径;成功路径的覆盖由真实建链/集成场景承担。 */
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
    /* peer_ip 已改为定长 char 数组（原 std::string）：用内容比较而非指针比较 */
    EXPECT_STREQ(connector_.umq_conn_info_.peer_ip, "");

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

    struct sockaddr_in addr {
    };
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

// ==================== Negotiate 成功路径 ====================

TEST_F(UmqConnectorOpsTest, Negotiate_Success_ReturnsOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = 1;
    ock::ubs::Result ret = connector_.Negotiate(TEST_NEW_FD, sock);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

// ==================== PrepareConnect - TFO 已建立 UBS 连接 ====================

TEST_F(UmqConnectorOpsTest, PrepareConnect_TfoMode_UbsConnectionExists_ReturnsOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    LibcApi::fcntl_ptr = MockFcntlBlocking;
    LibcApi::sendto_ptr = MockSendtoSuccess;
    LibcApi::connect_ptr = MockConnectSuccess;
    LibcApi::getsockopt_ptr = MockGetsockoptTfoSynData;

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

// ==================== CheckRouteDevAddForConnect ====================

TEST_F(UmqConnectorOpsTest, CheckRouteDevAddForConnect_BondingBackup_ReturnsOk)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    ock::ubs::Result ret = connector_.CheckRouteDevAddForConnect(g_connEid, umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CheckRouteDevAddForConnect_RawDevice_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    ock::ubs::Result ret = connector_.CheckRouteDevAddForConnect(g_connEid, umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CheckRouteDevAddForConnect_BondingRoute_DevAddOk_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(invoke(&MockCheckDevAddForState));
    g_checkDevAddRet = UBS_OK;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    ock::ubs::Result ret = connector_.CheckRouteDevAddForConnect(g_connEid, umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CheckRouteDevAddForConnect_BondingRoute_DevAddFail_ReturnsUmqError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(invoke(&MockCheckDevAddForState));
    g_checkDevAddRet = -1;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    ock::ubs::Result ret = connector_.CheckRouteDevAddForConnect(g_connEid, umqSocket);
    EXPECT_EQ(ret, UBS_UMQ_ERROR);
    GlobalMockObject::verify();
}

// ==================== DoUbConnect ====================

TEST_F(UmqConnectorOpsTest, DoUbConnect_Success_BondingBackup_ReturnsOk)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_CreateLocalUmqFail_ReturnsCreateError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    g_createLocalUmqRet = UBS_UMQ_CREATE;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    GlobalMockObject::verify();
}

/* CreateLocalUmq 成功、但 GenerateSocketCommOps 失败时，DoUbConnect 返回 GenerateSocketCommOps 的结果。 */
static int g_destroyLocalUmqCnt = 0;
static void MockDestroyLocalUmqCount()
{
    ++g_destroyLocalUmqCnt;
}

/* issue #49: umq 已建成而数据面装配失败 ⇒ 当场销毁（id 立刻归还），不等 socket 析构 */
TEST_F(UmqConnectorOpsTest, DoUbConnect_GenerateCommOpsFail_ReturnsOpsError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    g_createLocalUmqRet = UBS_OK;
    g_generateCommOpsRet = UBS_UMQ_ERROR;
    g_destroyLocalUmqCnt = 0;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs().will(invoke(&MockDestroyLocalUmqCount));
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    connector_.precreate_done_ = true;
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_UMQ_ERROR);
    EXPECT_EQ(g_destroyLocalUmqCnt, 1);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_BindInfoGetFail_ReturnsRetryableDegradable)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    g_bindInfoGetRet = 0;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, static_cast<ock::ubs::Result>(UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK));
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_SendCpMsgFail_ReturnsError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(
        returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_RecvCpMsgFail_ReturnsError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(
        returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_RecvCpMsgInvalidSize_ReturnsError)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = UMQ_BIND_INFO_SIZE_MAX + 1;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_ClosPortInCooldown_ReturnsDegradable)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER(&PortCooldownManager::IsPortInCooldown).stubs().will(invoke(&MockIsPortInCooldownForState));
    g_recvCpMsgBindSize = 64;
    g_isPortInCooldown = true;
    connector_.topo_type_ = UMQ_TOPO_TYPE_CLOS;
    umq_port_id_t port{};
    std::vector<umq_port_id_t> usedPortsVec = {port};
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 1};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK));
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_BindFail_ReturnsRetryableDegradable)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    g_bindRet = -1;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK));
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_BondingRouteMainUmqNull_ReturnsError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    g_bindInfoGetRet = UMQ_BIND_INFO_SIZE_MAX;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(invoke(&MockBindInfoGetForState));
    MOCKER_CPP(::umq_bind).stubs().will(invoke(&MockBindForState));
    MOCKER_CPP(::umq_state_get).stubs().will(returnValue(QUEUE_STATE_READY));
    umqSocket->SetTransMode(UmqSetting::UMQ_UB_TRANS_MODE);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    // 未预置 EidTable(conn_eid=0) → GetFirst 返回 nullptr → UBS_ERROR
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnect_BondingRouteEnsurePrefilledFail_ReturnsPrefillError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    g_prefillRxRet = UBS_PREFILL_RX;
    std::vector<umq_port_id_t> usedPortsVec;
    umq_used_ports_t usedPorts = {.port = usedPortsVec.data(), .num = 0};
    ock::ubs::Result ret = connector_.DoUbConnect(umqSocket, usedPorts, false);
    EXPECT_EQ(ret, UBS_PREFILL_RX);
    GlobalMockObject::verify();
}

// ==================== DoUbConnectRetry ====================

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_CpuAffinityDegradable_SetsDegradeState)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    connector_.degradable_ = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kDEGRADE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_CpuAffinityNotDegradable_SetsFailedState)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    connector_.degradable_ = false;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kFAILED);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_SendRetryMsgFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(
        returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_SendAckFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(0)));
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_RecvPeerRetFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    g_recvCpMsgBindSize = 64;
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_Success_SetsOkState)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    g_recvCpMsgBindSize = 64;
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kOK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_PeerDegradable_SetsDegradeState)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    g_recvCpMsgBindSize = 64;
    // peer_ret 非 OK 但带降级标记：IsOk(ack_ret)=true 但 IsOk(peer_ret)=false，
    // 且 IsDegradable(peer_ret)=true → 走 kDEGRADE（若为 UBS_OK|DEGRADABLE 则 IsOk=true 走 kOK）
    g_recvPeerRet = static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK);
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kDEGRADE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DoUbConnectRetry_NotOkNotDegradable_SetsFailedState)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    g_createLocalUmqRet = UBS_UMQ_ERROR; // ack_ret 非 OK 且非可降级
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    ock::ubs::Result ack_ret = UBS_OK;
    ock::ubs::Result peer_ret = UBS_OK;
    ock::ubs::Result ret = connector_.DoUbConnectRetry(sock, ack_ret, peer_ret);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kFAILED);
    GlobalMockObject::verify();
}

// ==================== CreateSocketResources 状态机 ====================

TEST_F(UmqConnectorOpsTest, CreateSocketResources_Success_ReturnsOk)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    g_recvCpMsgBindSize = 64;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_SendAckFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(0)));
    g_recvCpMsgBindSize = 64;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_RecvPeerAckFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    g_recvCpMsgBindSize = 64;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

/* 并行 bind：服务端确认推迟 ⇒ 腿⑥ 照收——RecvSocketData 失败必须传出 TCP_EXCHANGE（证明确实在收）。 */
TEST_F(UmqConnectorOpsTest, CreateSocketResources_DeferredCarriedAck_RecvsLegSix)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    g_recvCpMsgBindSize = 64;
    connector_.peer_req_carry_consumed_ = true;
    connector_.peer_ack_deferred_ = true;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    EXPECT_FALSE(connector_.peer_ack_deferred_); /* 一次性消费 */
    GlobalMockObject::verify();
}

/* 方案B（未推迟）：腿⑥ 已在应答里 ⇒ 不收——RecvSocketData 即使失败也不影响结果。 */
TEST_F(UmqConnectorOpsTest, CreateSocketResources_CarriedAckNotDeferred_SkipsLegSix)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    InstallDoUbConnectSuccessMocks(umqSocket);
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    g_recvCpMsgBindSize = 64;
    connector_.peer_req_carry_consumed_ = true;
    connector_.peer_ack_deferred_ = false;
    connector_.peer_carried_ack_ = UBS_OK;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_RetryableAck_RetriesAndSucceeds)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    // 手工安装：umq_bind 首轮失败 → kRETRY，重试轮成功 → kOK
    g_bindInfoGetRet = UMQ_BIND_INFO_SIZE_MAX;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(invoke(&MockBindInfoGetForState));
    MOCKER_CPP(::umq_bind).stubs().will(invoke(&MockBindForStateSeq));
    MOCKER_CPP(::umq_state_get).stubs().will(returnValue(QUEUE_STATE_READY));
    umqSocket->SetTransMode(UmqSetting::UMQ_UB_TRANS_MODE);
    UmqEidTable::Instance().Add(g_connEid, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(invoke(&MockPrefillRxForState));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead).stubs().will(invoke(&MockRegisterSharedJfrForReadForState));
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOk));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    g_recvCpMsgBindSize = 64;
    g_bindSeqCallCount = 0;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_RetrySendMsgFail_ReturnsTcpExchange)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    g_bindInfoGetRet = UMQ_BIND_INFO_SIZE_MAX;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(invoke(&MockBindInfoGetForState));
    MOCKER_CPP(::umq_bind).stubs().will(invoke(&MockBindForState)); // g_bindRet=-1 → 首轮失败触发重试
    MOCKER_CPP(::umq_state_get).stubs().will(returnValue(QUEUE_STATE_READY));
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&MockSendLengthPrefixedOkThenFail));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedCpMsg));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    g_recvCpMsgBindSize = 64;
    g_bindRet = -1;
    g_sendLenPrefixedSeqCallCount = 0;
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    // 首轮 ack retryable → kRETRY；重试信令发送失败 → UBS_TCP_EXCHANGE
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_DegradablePeer_SetsDegradeState)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    // ack_ret = UBS_UMQ_ERROR（非 OK、非可重试、非可降级）
    g_createLocalUmqRet = UBS_UMQ_ERROR;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    // peer_ret 非 OK 但带降级标记 → degradable_=true；两端均不可重试 → kDEGRADE
    g_recvPeerRet = static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK);
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    // kDEGRADE → OverrideItem(raw_fd_, nullptr) + 返回 UBS_OK
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.retry_state_, UBHandshakeState::kDEGRADE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, CreateSocketResources_NotRetryableNotDegradable_ReturnsConnRetryFailed)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSocket);
    g_createLocalUmqRet = UBS_UMQ_ERROR; // ack_ret 非重试非可降级
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(invoke(&MockGenerateSocketCommOpsForState));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataPeer));
    ock::ubs::Result ret = connector_.CreateSocketResources(sock);
    EXPECT_EQ(ret, UBS_CONN_RETRY_FAILED);
    GlobalMockObject::verify();
}

// ==================== TryPrecreateLocalUmq / DiscardPrecreatedUmq ====================

TEST_F(UmqConnectorOpsTest, TryPrecreateLocalUmq_Disabled_NoOp)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = false;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, TryPrecreateLocalUmq_BondingRoute_NoOp)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, TryPrecreateLocalUmq_RawDevice_Success)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_TRUE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, TryPrecreateLocalUmq_CreateFail_DestroysLocal)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    g_createLocalUmqRet = UBS_UMQ_CREATE;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DiscardPrecreatedUmq_NotDone_NoOp)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.precreate_done_ = false;
    connector_.DiscardPrecreatedUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, DiscardPrecreatedUmq_Done_ClearsFlag)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.precreate_done_ = true;
    connector_.DiscardPrecreatedUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

// ==================== ConnectNegotiate ====================

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_SendReqFail_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(0)));
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_RecvVersionFail_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_VersionMajorMismatch_ReturnsTcpExchange)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    const uint32_t major = (UBS_PROTOCOL_VERSION.major + 1) % kProtocolMajorMax;
    g_recvVersionRaw = UBSVersion(major, UBS_PROTOCOL_VERSION.minor, UBS_PROTOCOL_VERSION.patch).GetWhole();
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_RecvRspFail_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(
        returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_RspRetCodeNonZero_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspRetCode = -1;
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_RawDevice_ReturnsOk)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.umq_conn_info_.conn_eid.raw[0], UmqSetting::UMQ_LOCAL_EID.raw[0]);
    GlobalMockObject::verify();
}

/* 客户端跟随服务端的确认：应答带 DEFER 位 ⇒ 改收腿⑥；只带 REQ_CARRY ⇒ 方案B，用应答里的结果。 */
TEST_F(UmqConnectorOpsTest, ConnectNegotiate_ServerDefersBindRet_SetsDeferred)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    connector_.local_bind_info_len_ = 32; /* 本端确实携带了 bind_info（方案B 前提） */
    g_recvRspPeerTransMode = UmqSetting::UMQ_UB_TRANS_MODE;
    g_recvRspReserved0 = NEGO_CAP_REQ_CARRY_BINDINFO | NEGO_CAP_DEFER_BIND_RET;
    EXPECT_EQ(connector_.ConnectNegotiate(umqSocket), UBS_OK);
    EXPECT_TRUE(connector_.peer_req_carry_consumed_);
    EXPECT_TRUE(connector_.peer_ack_deferred_);
    connector_.local_bind_info_len_ = 0;
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_ServerCarriesBindRet_NotDeferred)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    connector_.local_bind_info_len_ = 32;
    g_recvRspPeerTransMode = UmqSetting::UMQ_UB_TRANS_MODE;
    g_recvRspReserved0 = NEGO_CAP_REQ_CARRY_BINDINFO; /* 老服务端/开关关：结果随应答 */
    EXPECT_EQ(connector_.ConnectNegotiate(umqSocket), UBS_OK);
    EXPECT_TRUE(connector_.peer_req_carry_consumed_);
    EXPECT_FALSE(connector_.peer_ack_deferred_);
    connector_.local_bind_info_len_ = 0;
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_InvalidSocketIdCount_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = 0;
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_TooManySocketIds_ReturnsError)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = NEGOTIATE_SOCKET_ID_MAX_NUM + 1;
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_Success_SetsPeerSocketIds)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = 2;
    g_recvRspAffSockId = 1;
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(connector_.peer_all_socket_ids_.size(), 2u);
    EXPECT_EQ(connector_.peer_all_socket_ids_[0], 1u);
    EXPECT_EQ(connector_.peer_all_socket_ids_[1], 2u);
    EXPECT_EQ(connector_.peer_socket_id_, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_CpuAffinity_DisablesRoundRobin)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::CPU_AFFINITY;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = 1;
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_FALSE(connector_.use_round_robin_);
    GlobalMockObject::verify();
}

TEST_F(UmqConnectorOpsTest, ConnectNegotiate_PrecreateTransModeMismatch_DiscardsPrecreated)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(invoke(&MockCreateLocalUmqForState));
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketDataSize));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketDataVersion));
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&MockRecvLengthPrefixedRsp));
    g_recvRspSocketIdCount = 1;
    g_recvRspPeerTransMode = RC_TP; // 对端更高优先级 → min 后低于预建假设的本端 RM_TP → 丢弃预建
    ock::ubs::Result ret = connector_.ConnectNegotiate(umqSocket);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_FALSE(connector_.precreate_done_);
    GlobalMockObject::verify();
}

// ==================== 方案B: req 携带 bind_info / 预建再入护栏 ====================

/* 再入护栏（fix_precreate_reentry）：precreate_done_ 已置位时二次调用不得再走
 * 创建路径（否则"重复创建"防御触发后，失败兜底会销毁仍然有效的 umq）。 */
TEST_F(UmqConnectorOpsTest, TryPrecreate_ReentryFlagSet_SkipsCreate)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.precreate_done_ = true;
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_TRUE(connector_.precreate_done_); /* 未被清除、未走失败兜底 */
}

/* 同上：socket 已持有有效 umq 句柄时（重试轮）同样跳过。 */
TEST_F(UmqConnectorOpsTest, TryPrecreate_HandleAlreadyValid_SkipsCreate)
{
    GlobalSetting::UBS_CONNECT_PRECREATE = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    umqSocket->umq_handle_ = 0x1234; /* -fno-access-control */
    connector_.precreate_done_ = false;
    connector_.TryPrecreateLocalUmq(umqSocket);
    EXPECT_FALSE(connector_.precreate_done_); /* 没有假装预建成功 */
    EXPECT_EQ(umqSocket->umq_handle_, 0x1234u); /* 现有 umq 未被动过 */
    umqSocket->umq_handle_ = UMQ_INVALID_HANDLE; /* 防析构走真实销毁 */
}

/* 方案B：暂存了本端 bind_info 时，请求缓冲改为 NegotiateReqExt 布局——
 * cap_flags 置 REQ_CARRY 位，body_len = offsetof(bind_info)+实际长度，尾部字节
 * 与暂存一致。 */
TEST_F(UmqConnectorOpsTest, BuildReqBuffer_WithStashedBindInfo_AppendsTail)
{
    GlobalSetting::UBS_EARLY_ACK = true;
    GlobalSetting::UBS_NEGO_CARRY_BINDINFO = true;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.local_bind_info_len_ = 32;
    for (int i = 0; i < 32; ++i) {
        connector_.local_bind_info_[i] = static_cast<uint8_t>(i + 1);
    }
    uint8_t buf[NEGOTIATE_REQ_BUFFER_SIZE];
    int buf_len = 0;
    EXPECT_EQ(connector_.BuildNegotiateReqBuffer(buf, umqSocket, buf_len), static_cast<ock::ubs::Result>(UBS_OK));
    const int head = static_cast<int>(sizeof(uint64_t) + sizeof(uint32_t)); /* magic + version */
    uint32_t body_len = 0;
    memcpy(&body_len, buf + head, sizeof(body_len));
    EXPECT_EQ(body_len, static_cast<uint32_t>(offsetof(umq::NegotiateReqExt, bind_info) + 32));
    EXPECT_EQ(buf_len, head + static_cast<int>(sizeof(uint32_t) + body_len));
    umq::NegotiateReqExt ext{};
    memcpy(&ext, buf + head + sizeof(uint32_t), body_len);
    EXPECT_NE(ext.req.cap_flags & umq::NEGO_CAP_REQ_CARRY_BINDINFO, 0);
    EXPECT_EQ(ext.bind_info_size, 32u);
    EXPECT_EQ(ext.bind_info[0], 1u);
    EXPECT_EQ(ext.bind_info[31], 32u);
    connector_.local_bind_info_len_ = 0;
}

/* 未携带时布局与经典逐字节一致（body_len == sizeof(NegotiateReq)、无 REQ_CARRY 位）。 */
TEST_F(UmqConnectorOpsTest, BuildReqBuffer_NoStash_ClassicLayout)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(TEST_FD);
    connector_.local_bind_info_len_ = 0;
    uint8_t buf[NEGOTIATE_REQ_BUFFER_SIZE];
    int buf_len = 0;
    EXPECT_EQ(connector_.BuildNegotiateReqBuffer(buf, umqSocket, buf_len), static_cast<ock::ubs::Result>(UBS_OK));
    const int head = static_cast<int>(sizeof(uint64_t) + sizeof(uint32_t));
    uint32_t body_len = 0;
    memcpy(&body_len, buf + head, sizeof(body_len));
    EXPECT_EQ(body_len, static_cast<uint32_t>(sizeof(umq::NegotiateReq)));
    umq::NegotiateReq req{};
    memcpy(&req, buf + head + sizeof(uint32_t), sizeof(req));
    EXPECT_EQ(req.cap_flags & umq::NEGO_CAP_REQ_CARRY_BINDINFO, 0);
}
