/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/* ============================================================================
 * umq_socket_acceptor_test.cpp
 * ----------------------------------------------------------------------------
 * UmqAcceptorOps 单元测试：
 *   - PrepareConnect / Negotiate / CreateSocketResources / DestroySocketResources
 *   - 私有方法: ValidateProtocol / ValidateVersion / AcceptNegotiate / DoUbAccept /
 *              DoUbAcceptRetry / CheckRouteDevAddForAccept / AcceptExchangeSocketIDs /
 *              FillLocalSocketIdsForNegotiate / BuildNegotiateRsp
 *
 * 打桩策略（依赖注入点清单）：
 *   - SocketConnHelper::RecvSocketData / SendSocketData / RecvLengthPrefixed / SendLengthPrefixed
 *       静态方法（MOCKER + invoke 注入可控收发行为）
 *   - UmqSocket::CreateLocalUmq / CheckDevAdd / UpdateRxQueueAvailNum /
 *       UnbindAndFlushRemoteUmq / DestroyLocalUmq        成员方法（MOCKER_CPP）
 *   - SocketBase::GenerateSocketCommOps                   静态方法（MOCKER）
 *   - UmqConnHelper::PrefillRx / RegisterSharedJfrForRead 静态方法（MOCKER）
 *   - ::umq_bind_info_get / ::umq_bind                    全局 C 函数（MOCKER_CPP）
 *   - UmqAcceptorOps 私有成员函数 DoUbAccept / DoUbAcceptRetry （状态机驱动）
 *
 * 真实执行路径：UmqEidTable / UmqBackend(used_ports_, topo_type_) / PortCooldownManager /
 *              UBSVersion::Negotiate / UmqSocket 版本存取器
 * ============================================================================
 */

#include "umq_socket_acceptor.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_setting.h"
#include "umq_socket.h"
#include "umq_backend.h"
#include "umq_conn_helper.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_scope_exit.h"
#include "common/ubsocket_version.h"
#include "common/ubsocket_port_cooldown.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {

static const int TEST_FD = 42;
static const int TEST_SOCKET_FD = 100;
static const uint64_t TEST_MAIN_UMQ_HANDLE = 0x12345;
static const uint32_t TEST_VERSION_WHOLE = UBS_PROTOCOL_VERSION.GetWhole();

/* ======================= 可控收发 Fake ======================= */

static ssize_t g_recvRet = 0; // 0 => 自动成功（写入 + 返回 size）；<0 => 失败
static uint64_t g_recv64 = 0; // size == 8 时写入的值
static uint32_t g_recv32 = 0; // size == 4 时写入的值（首次读取，如 version）
static std::vector<uint32_t> g_recv32Seq; // size == 4 的后续逐次取值队列（首次读取后生效）
static size_t g_recv32SeqIdx = 0;
static bool g_recv32FirstReadDone = false;

static ssize_t g_sendRet = 0; // 0 => 自动成功（返回 size）；<0 => 失败
/* 控制面 I/O 顺序记录：R = RecvSocketData, S = SendSocketData, L = SendLengthPrefixed, B = BindPeerAndFinalize。
 * 并行 bind 用例靠它证明"应答先于 bind"与"先发后收"。 */
static std::string g_ioOrder;
static NegotiateRspExt g_sentRspExt;      /* 最近一次发出的扩展应答（按长度识别） */
static bool g_sentRspExtValid = false;
static ock::ubs::Result g_sendLenPrefRet = UBS_OK;
static ock::ubs::Result g_recvLenPrefRet = UBS_OK;

static NegotiateReq g_negoReq;
static uint64_t g_negoReqExtBindSize = 0; /* >0 ⇒ 按新客户端喂包：NegotiateReqExt 尾部带 bind_info */
static CpMsg g_remoteCpMsg;
static OtherRouteMessage g_otherRouteMsg;

static ssize_t FakeRecvSocketData(int, const void *buf, size_t size, uint32_t)
{
    g_ioOrder += 'R';
    if (g_recvRet != 0) {
        errno = ECONNRESET;
        return g_recvRet;
    }
    void *dst = const_cast<void *>(buf);
    if (size == sizeof(uint64_t)) {
        *static_cast<uint64_t *>(dst) = g_recv64;
        return static_cast<ssize_t>(size);
    }
    if (size == sizeof(uint32_t)) {
        uint32_t v = g_recv32;
        if (g_recv32FirstReadDone && g_recv32SeqIdx < g_recv32Seq.size()) {
            v = g_recv32Seq[g_recv32SeqIdx++];
        }
        g_recv32FirstReadDone = true;
        *static_cast<uint32_t *>(dst) = v;
        return static_cast<ssize_t>(size);
    }
    if (size > 0) {
        memset(dst, 0, size);
    }
    return static_cast<ssize_t>(size);
}

static ssize_t FakeSendSocketData(int, const void *, size_t size, uint32_t)
{
    g_ioOrder += 'S';
    return g_sendRet != 0 ? g_sendRet : static_cast<ssize_t>(size);
}

static ock::ubs::Result FakeSendLengthPrefixed(int, const void *body, uint32_t obj_size, uint32_t)
{
    g_ioOrder += 'L';
    if (obj_size >= offsetof(NegotiateRspExt, bind_info) && obj_size <= sizeof(NegotiateRspExt)) {
        memset(&g_sentRspExt, 0, sizeof(g_sentRspExt));
        memcpy(&g_sentRspExt, body, obj_size);
        g_sentRspExtValid = true;
    }
    return g_sendLenPrefRet;
}

static ock::ubs::Result FakeRecvLengthPrefixed(int, void *body, uint32_t obj_size, uint32_t)
{
    if (g_recvLenPrefRet != UBS_OK) {
        return g_recvLenPrefRet;
    }
    if (obj_size == sizeof(NegotiateReq)) {
        *static_cast<NegotiateReq *>(body) = g_negoReq;
    } else if (obj_size == sizeof(NegotiateReqExt)) {
        /* 方案B 后 accept 侧按扩展布局收请求；老客户端的短 body 会被零填充。
         * 这里按"老客户端"喂包：仅 req 有效、尾部全零（bind_info_size==0 ⇒
         * 自描述回退经典路径），既有用例的原语义因此保持不变。 */
        auto *ext = static_cast<NegotiateReqExt *>(body);
        memset(ext, 0, sizeof(*ext));
        ext->req = g_negoReq;
        if (g_negoReqExtBindSize > 0) { /* 方案B/并行 bind 用例：新客户端，尾部带 bind_info */
            ext->bind_info_size = g_negoReqExtBindSize;
            memset(ext->bind_info, 0xAB, g_negoReqExtBindSize);
        }
    } else if (obj_size == sizeof(CpMsg)) {
        *static_cast<CpMsg *>(body) = g_remoteCpMsg;
    } else if (obj_size == sizeof(OtherRouteMessage)) {
        *static_cast<OtherRouteMessage *>(body) = g_otherRouteMsg;
    } else {
        memset(body, 0, obj_size);
    }
    return UBS_OK;
}

static void InstallSocketDataFakes()
{
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&FakeSendSocketData));
    MOCKER(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(&FakeRecvLengthPrefixed));
    MOCKER(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(&FakeSendLengthPrefixed));
    g_ioOrder.clear();
    g_sentRspExtValid = false;
    g_negoReqExtBindSize = 0;
}

static void ResetFakeState()
{
    g_recvRet = 0;
    g_recv64 = 0;
    g_recv32 = 0;
    g_recv32Seq.clear();
    g_recv32SeqIdx = 0;
    g_recv32FirstReadDone = false;
    g_sendRet = 0;
    g_sendLenPrefRet = UBS_OK;
    g_recvLenPrefRet = UBS_OK;
    g_negoReq = NegotiateReq{};
    g_remoteCpMsg = CpMsg{};
    /* 收端新增 size==0 拒收后，默认桩必须携带合法长度（魔数由 CpMsg 成员默认值自带） */
    g_remoteCpMsg.queue_bind_info_size = 100;
    g_otherRouteMsg = OtherRouteMessage{};
}

} // namespace

class UmqAcceptorOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetFakeState();
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();

        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_DEGRADE = true;
        GlobalSetting::UBS_EARLY_ACK = false;
        GlobalSetting::UBS_PORT_COOLDOWN_SEC = 60;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;

        UmqSetting::UMQ_IS_BONDING = false;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY = dev_schedule_policy::ROUND_ROBIN;
        UmqSetting::UMQ_ALL_SOCKET_IDS = {0, 1};
        UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;

        UmqBackend::used_ports_ = {};
        UmqBackend::topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
        UmqEidTable::Instance().Clean();

        umqSocket_ = MakeRef<UmqSocket>(TEST_SOCKET_FD);
        umqSocket_->SetTransMode(RM_TP);
        socketPtr_ = RefConvert<UmqSocket, Socket>(umqSocket_);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        UmqEidTable::Instance().Clean();
        UmqBackend::used_ports_ = {};
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }

    UmqAcceptorOps acceptor_{TEST_FD};
    UmqSocketPtr umqSocket_;
    SocketPtr socketPtr_;
};

// ==================== PrepareConnect ====================

TEST_F(UmqAcceptorOpsTest, PrepareConnect_ReturnsOk)
{
    struct sockaddr_in addr = {};
    socklen_t len = sizeof(addr);
    EXPECT_EQ(acceptor_.PrepareConnect(TEST_SOCKET_FD, reinterpret_cast<const struct sockaddr *>(&addr), len, socketPtr_),
              0);
}

// ==================== ValidateProtocol ====================

TEST_F(UmqAcceptorOpsTest, ValidateProtocol_RecvFail_ReturnsMinusOne)
{
    InstallSocketDataFakes();
    g_recvRet = -1;
    uint64_t protocol = 0;
    ssize_t recvSize = 0;
    EXPECT_EQ(acceptor_.ValidateProtocol(TEST_FD, protocol, recvSize), -1);
}

TEST_F(UmqAcceptorOpsTest, ValidateProtocol_Mismatch_ReturnsRecvSize)
{
    InstallSocketDataFakes();
    g_recv64 = 0x1; /* 错误魔数 */
    uint64_t protocol = 0;
    ssize_t recvSize = 0;
    EXPECT_EQ(acceptor_.ValidateProtocol(TEST_FD, protocol, recvSize), static_cast<int>(sizeof(uint64_t)));
}

TEST_F(UmqAcceptorOpsTest, ValidateProtocol_Success_ReturnsZero)
{
    InstallSocketDataFakes();
    g_recv64 = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    uint64_t protocol = 0;
    ssize_t recvSize = 0;
    EXPECT_EQ(acceptor_.ValidateProtocol(TEST_FD, protocol, recvSize), 0);
    EXPECT_EQ(protocol, CONTROL_PLANE_PROTOCOL_NEGOTIATION);
}

// ==================== ValidateVersion ====================

TEST_F(UmqAcceptorOpsTest, ValidateVersion_RecvFail_ReturnsRecvFailed)
{
    InstallSocketDataFakes();
    g_recvRet = -1;
    uint32_t negotiated = 0;
    uint32_t peer = 0;
    EXPECT_EQ(acceptor_.ValidateVersion(TEST_FD, negotiated, peer), VersionCheckResult::kRecvFailed);
}

TEST_F(UmqAcceptorOpsTest, ValidateVersion_MajorMismatch_ReturnsMajorMismatch)
{
    InstallSocketDataFakes();
    g_recv32 = UBSVersion(1, 0, 0).GetWhole();
    uint32_t negotiated = 0;
    uint32_t peer = 0;
    EXPECT_EQ(acceptor_.ValidateVersion(TEST_FD, negotiated, peer), VersionCheckResult::kMajorMismatch);
}

TEST_F(UmqAcceptorOpsTest, ValidateVersion_Compatible_SetsNegotiatedVersion)
{
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    uint32_t negotiated = 0;
    uint32_t peer = 0;
    EXPECT_EQ(acceptor_.ValidateVersion(TEST_FD, negotiated, peer), VersionCheckResult::kCompatible);
    EXPECT_EQ(peer, TEST_VERSION_WHOLE);
    EXPECT_EQ(negotiated, TEST_VERSION_WHOLE);
}

// ==================== FillLocalSocketIdsForNegotiate ====================

TEST_F(UmqAcceptorOpsTest, FillLocalSocketIdsForNegotiate_Empty_ReturnsError)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {};
    uint32_t ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    uint32_t count = 0;
    EXPECT_EQ(acceptor_.FillLocalSocketIdsForNegotiate(ids, count), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, FillLocalSocketIdsForNegotiate_TooMany_ReturnsError)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS.assign(NEGOTIATE_SOCKET_ID_MAX_NUM + 1, 1);
    uint32_t ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    uint32_t count = 0;
    EXPECT_EQ(acceptor_.FillLocalSocketIdsForNegotiate(ids, count), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, FillLocalSocketIdsForNegotiate_Success_FillsIds)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {5, 6, 7};
    uint32_t ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    uint32_t count = 0;
    EXPECT_EQ(acceptor_.FillLocalSocketIdsForNegotiate(ids, count), UBS_OK);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(ids[0], 5u);
    EXPECT_EQ(ids[1], 6u);
    EXPECT_EQ(ids[2], 7u);
}

// ==================== BuildNegotiateRsp ====================

TEST_F(UmqAcceptorOpsTest, BuildNegotiateRsp_FillsFields)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {2, 4};
    UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
    UmqSetting::UMQ_PROCESS_SOCKET_ID = 3;
    NegotiateRsp rsp{};
    acceptor_.BuildNegotiateRsp(rsp);
    EXPECT_EQ(rsp.peer_trans_mode, RM_TP);
    EXPECT_EQ(rsp.aff_sock_id, 3);
    EXPECT_EQ(rsp.socket_id_count, 2u);
    EXPECT_EQ(rsp.socket_ids[0], 2u);
    EXPECT_EQ(rsp.socket_ids[1], 4u);
}

// ==================== AcceptExchangeSocketIDs ====================

TEST_F(UmqAcceptorOpsTest, AcceptExchangeSocketIDs_SendCountFail_ReturnsError)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {1, 2};
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    EXPECT_EQ(acceptor_.AcceptExchangeSocketIDs(socketPtr_, TEST_FD), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, AcceptExchangeSocketIDs_SendIdsFail_ReturnsError)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {1, 2};
    MOCKER(&SocketConnHelper::SendSocketData).stubs()
        .will(returnValue(static_cast<ssize_t>(sizeof(uint32_t)))) // count 发送成功
        .then(returnValue(static_cast<ssize_t>(-1)));              // ids 发送失败
    EXPECT_EQ(acceptor_.AcceptExchangeSocketIDs(socketPtr_, TEST_FD), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, AcceptExchangeSocketIDs_Success_ReturnsOk)
{
    UmqSetting::UMQ_ALL_SOCKET_IDS = {1, 2};
    MOCKER(&SocketConnHelper::SendSocketData).stubs()
        .will(returnValue(static_cast<ssize_t>(sizeof(uint32_t))))             // count
        .then(returnValue(static_cast<ssize_t>(2 * sizeof(uint32_t))));        // ids
    EXPECT_EQ(acceptor_.AcceptExchangeSocketIDs(socketPtr_, TEST_FD), UBS_OK);
}

// ==================== CheckRouteDevAddForAccept ====================

TEST_F(UmqAcceptorOpsTest, CheckRouteDevAddForAccept_BondingRoute_CheckDevAddFail_ReturnsUmqError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(-1)));
    EXPECT_EQ(acceptor_.CheckRouteDevAddForAccept(UmqSetting::UMQ_LOCAL_EID, umqSocket_), UBS_UMQ_ERROR);
}

TEST_F(UmqAcceptorOpsTest, CheckRouteDevAddForAccept_BondingRoute_CheckDevAddOk_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    EXPECT_EQ(acceptor_.CheckRouteDevAddForAccept(UmqSetting::UMQ_LOCAL_EID, umqSocket_), UBS_OK);
}

TEST_F(UmqAcceptorOpsTest, CheckRouteDevAddForAccept_BondingBackup_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    EXPECT_EQ(acceptor_.CheckRouteDevAddForAccept(UmqSetting::UMQ_LOCAL_EID, umqSocket_), UBS_OK);
}

TEST_F(UmqAcceptorOpsTest, CheckRouteDevAddForAccept_RawDevice_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    EXPECT_EQ(acceptor_.CheckRouteDevAddForAccept(UmqSetting::UMQ_LOCAL_EID, umqSocket_), UBS_OK);
}

// ==================== DoUbAccept ====================

TEST_F(UmqAcceptorOpsTest, DoUbAccept_CreateLocalUmqFail_ReturnsError)
{
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_CREATE)));
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_UMQ_CREATE);
}

/* CreateLocalUmq 成功但 GenerateSocketCommOps 失败时，返回 GenerateSocketCommOps 的结果，
 * 不再静默吞掉数据面重建失败。 */
static int g_acceptDestroyLocalUmqCnt = 0;
static void MockAcceptDestroyLocalUmqCount()
{
    ++g_acceptDestroyLocalUmqCnt;
}

/* issue #49: umq 已建成而数据面装配失败（现场 = ReinitTxOps 的 fd 复用拒绝）⇒ 当场销毁 */
TEST_F(UmqAcceptorOpsTest, DoUbAccept_GenerateSocketCommOpsFail_ReturnsOpsResult)
{
    InstallSocketDataFakes();
    g_acceptDestroyLocalUmqCnt = 0;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_MALLOC_FAILED)));
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs().will(invoke(&MockAcceptDestroyLocalUmqCount));
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_MALLOC_FAILED);
    EXPECT_EQ(g_acceptDestroyLocalUmqCnt, 1);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_BindInfoGetZero_ReturnsRetryableDegradable)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(0u));
    errno = EINVAL;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts),
              UBS_UMQ_BIND_INFO_GET | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_SendLengthPrefixedFail_ReturnsError)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    g_sendLenPrefRet = UBS_ERROR;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_RecvLengthPrefixedFail_ReturnsError)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    g_recvLenPrefRet = UBS_ERROR;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_RemoteBindInfoTooLarge_ReturnsError)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    g_remoteCpMsg.queue_bind_info_size = UMQ_BIND_INFO_SIZE_MAX + 1;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_MisalignedMagic_ReturnsError)
{
    /* issue#32 现网形态：错位小帧（如重试轮 OtherRouteMessage）被零填充成 CpMsg——
     * 魔数必然不符，必须在喂给 umq_bind 之前拒收 */
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    g_remoteCpMsg.protocol_negotiation = 0x2; /* 形如 UBHandshakeState 的枚举值 */
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_ZeroBindInfoSize_ReturnsError)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    g_remoteCpMsg.queue_bind_info_size = 0;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_ClosPortInCooldown_ReturnsBindDegradable)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    acceptor_.topo_type_ = UMQ_TOPO_TYPE_CLOS;

    umq_port_id_t port = {};
    port.value = 1;
    PortCooldownManager::MarkPortInCooldown(port);
    umq_used_ports_t usedPorts = {};
    usedPorts.port = &port;
    usedPorts.num = 1;
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_UMQ_BIND | UBS_DEGRADABLE_MASK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_ClosPortNoCooldown_BindSuccess_ReturnsOk)
{
    InstallSocketDataFakes();
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(&UmqSocket::UpdateRxQueueAvailNum).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    acceptor_.topo_type_ = UMQ_TOPO_TYPE_CLOS;

    acceptor_.umq_conn_info_.conn_eid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    UmqEidTable::Instance().Add(acceptor_.umq_conn_info_.conn_eid, RM_TP, TEST_MAIN_UMQ_HANDLE);
    umq_port_id_t port = {};
    port.value = 2;
    umq_used_ports_t usedPorts = {};
    usedPorts.port = &port;
    usedPorts.num = 1;
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_OK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_BindFail_ReturnsBindRetryableDegradable)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(-UMQ_ERR_EPERM));
    errno = EPERM;
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_Success_BondingBackup_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(&UmqSocket::UpdateRxQueueAvailNum).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_OK);
    EXPECT_TRUE(umqSocket_->umq_is_bind_remote_);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_Success_BondingRoute_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(&UmqSocket::UpdateRxQueueAvailNum).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    acceptor_.umq_conn_info_.conn_eid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    UmqEidTable::Instance().Add(acceptor_.umq_conn_info_.conn_eid, RM_TP, TEST_MAIN_UMQ_HANDLE);
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_OK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_MainUmqNotFound_ReturnsError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    acceptor_.umq_conn_info_.conn_eid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    /* 未在 UmqEidTable 中注册对应 eid */
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_PrefillRxFail_ReturnsPrefillRx)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_PREFILL_RX)));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    acceptor_.umq_conn_info_.conn_eid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    UmqEidTable::Instance().Add(acceptor_.umq_conn_info_.conn_eid, RM_TP, TEST_MAIN_UMQ_HANDLE);
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_PREFILL_RX);
}

TEST_F(UmqAcceptorOpsTest, DoUbAccept_RegisterSharedJfrFail_ReturnsPrefillRx)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(100u));
    MOCKER_CPP(::umq_bind).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&UmqConnHelper::RegisterSharedJfrForRead)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    acceptor_.umq_conn_info_.conn_eid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    UmqEidTable::Instance().Add(acceptor_.umq_conn_info_.conn_eid, RM_TP, TEST_MAIN_UMQ_HANDLE);
    umq_used_ports_t usedPorts = {};
    EXPECT_EQ(acceptor_.DoUbAccept(socketPtr_, usedPorts), UBS_PREFILL_RX);
}

// ==================== DoUbAcceptRetry ====================

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_CpuAffinity_Degradable_SetsDegrade)
{
    InstallSocketDataFakes();
    acceptor_.peer_schedule_policy_ = dev_schedule_policy::CPU_AFFINITY;
    acceptor_.degradable_ = true;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_CpuAffinity_NotDegradable_SetsFailed)
{
    InstallSocketDataFakes();
    acceptor_.peer_schedule_policy_ = dev_schedule_policy::CPU_AFFINITY;
    acceptor_.degradable_ = false;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_RecvLengthPrefixedFail_ReturnsTcpExchange)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_recvLenPrefRet = UBS_ERROR;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_ClientCheckOtherRouteFailed_SetsRetryFailedCheckOtherRoute)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kOK; /* != kRETRY */
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_RecvPeerRetFail_ReturnsTcpExchange)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recvRet = -1;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_SendAckFail_ReturnsTcpExchange)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = 0; /* peerRet = UBS_OK */
    g_sendRet = -1;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_Success_SetsOk)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = 0; /* peerRet = UBS_OK */
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_Degradable_SetsDegrade)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_ERROR); /* peerRet 不可降级 */
    acceptor_.degradable_ = false;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_Failed_SetsFailed)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_ERROR);
    acceptor_.degradable_ = false;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_DegradeDisabled_StripsDegradable_SetsFailed)
{
    InstallSocketDataFakes();
    GlobalSetting::UBS_ENABLE_DEGRADE = false;
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_ERROR); /* peerRet 不可降级 */
    acceptor_.degradable_ = false;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, DoUbAcceptRetry_PeerRetDegradable_MergesMask_SetsDegrade)
{
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND)));
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_OK | UBS_DEGRADABLE_MASK); /* peerRet 可降级 */
    acceptor_.degradable_ = false;
    ock::ubs::Result ackRet = UBS_ERROR;
    ock::ubs::Result peerRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.DoUbAcceptRetry(socketPtr_, ackRet, peerRet), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

// ==================== Negotiate / AcceptNegotiate ====================

TEST_F(UmqAcceptorOpsTest, Negotiate_Success_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 0;
    g_negoReq.local_eid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    EXPECT_EQ(umqSocket_->GetNegotiatedVersion(), TEST_VERSION_WHOLE);
    EXPECT_EQ(umqSocket_->GetPeerVersion(), TEST_VERSION_WHOLE);
    EXPECT_EQ(memcmp(&acceptor_.umq_conn_info_.peer_eid, &g_negoReq.local_eid, sizeof(umq_eid_t)), 0);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_Success_BondingReq_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSetting::UMQ_IS_BONDING = true;
    UmqSetting::UMQ_ALL_SOCKET_IDS = {7, 8, 9};
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 1;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_BondingMismatch_ReturnsMinusOne)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSetting::UMQ_IS_BONDING = false;
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 1; /* 本端未开 bonding，对端开启 => 不匹配 */
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), -1);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_MajorMismatch_ReturnsTcpExchangeDegradable)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    g_recv32 = UBSVersion(1, 0, 0).GetWhole(); /* peer major=1，不兼容 */
    g_recv32Seq = {0};                        /* body_len = 0 */
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_RecvVersionFail_ReturnsTcpExchange)
{
    InstallSocketDataFakes();
    g_recvRet = -1;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_RecvNegotiateReqFail_ReturnsUbsError)
{
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_recvLenPrefRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_SendVersionFail_ReturnsUbsError)
{
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_sendRet = -1; /* 发送 negotiated_version 失败 */
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_SendRspFail_ReturnsUbsError)
{
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 0;
    g_sendLenPrefRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_BondingOk_SendRspFail_ReturnsUbsError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSetting::UMQ_IS_BONDING = true;
    UmqSetting::UMQ_ALL_SOCKET_IDS = {7, 8, 9};
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 1; /* bonding 匹配 => BuildNegotiateRsp 分支 */
    g_sendLenPrefRet = UBS_ERROR;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_ERROR);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_EarlyAck_ConsentSet_ReturnsOk)
{
    GlobalSetting::UBS_EARLY_ACK = true;
    GlobalSetting::UBS_ENABLE_DEGRADE = true;
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.cap_flags |= NEGO_CAP_EARLY_ACK;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    EXPECT_TRUE(acceptor_.peer_early_ack_);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_EarlyAckDisabled_ReturnsOk)
{
    GlobalSetting::UBS_EARLY_ACK = false; /* 本端未启用 early ack */
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.cap_flags |= NEGO_CAP_EARLY_ACK;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    EXPECT_FALSE(acceptor_.peer_early_ack_);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_RawDevice_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    UmqSetting::UMQ_LOCAL_EID = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    InstallSocketDataFakes();
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.local_eid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    EXPECT_EQ(memcmp(&acceptor_.umq_conn_info_.conn_eid, &UmqSetting::UMQ_LOCAL_EID, sizeof(umq_eid_t)), 0);
    EXPECT_EQ(memcmp(&acceptor_.umq_conn_info_.peer_eid, &g_negoReq.local_eid, sizeof(umq_eid_t)), 0);
}

// ==================== CreateSocketResources ====================

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_StartSuccess_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    g_recv32 = 0; /* peerRet = UBS_OK */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_StartRecvPeerRetFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    g_recvRet = -1;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_StartSendAckFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    g_recv32 = 0;
    g_sendRet = -1;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_EarlyAck_SendFirstThenRecv_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    acceptor_.peer_early_ack_ = true;
    g_recv32 = 0;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_EarlyAck_SendAckFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    acceptor_.peer_early_ack_ = true;
    g_sendRet = -1; /* 交叉 ack：发送本端 ackRet 失败 */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_EarlyAck_RecvPeerRetFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    acceptor_.peer_early_ack_ = true;
    g_recvRet = -1; /* 交叉 ack：接收对端 ackRet 失败 */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_EarlyAck_PeerRetDegradable_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    acceptor_.peer_early_ack_ = true;
    g_recv32 = static_cast<uint32_t>(UBS_OK | UBS_DEGRADABLE_MASK); /* 对端可降级 => 合并掩码 */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

/* ==================== 并行 bind（NEGO_CAP_DEFER_BIND_RET） ====================
 * 应答先于 bind 送出，bind 结果在 kSTART 以交叉 ack 补发：必须先发本端结果、再收对端 ack
 *（两端都先发后收，链路上交叉，不互等）。 */
TEST_F(UmqAcceptorOpsTest, CreateSocketResources_DeferredBindRet_SendsThenRecvs_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    acceptor_.early_prepared_ = true;
    acceptor_.early_bound_ = true;
    acceptor_.early_ack_deferred_ = true;
    acceptor_.early_ack_ret_ = UBS_OK;
    g_recv32 = 0; /* peerRet = UBS_OK */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
    EXPECT_EQ(g_ioOrder, "SR");
    EXPECT_FALSE(acceptor_.early_bound_); /* 一次性消费 */
    EXPECT_FALSE(acceptor_.early_ack_deferred_);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_DeferredBindRet_SendFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    acceptor_.early_prepared_ = true;
    acceptor_.early_bound_ = true;
    acceptor_.early_ack_deferred_ = true;
    g_sendRet = -1;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
    EXPECT_EQ(g_ioOrder, "S");
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_DeferredBindRet_RecvFail_ReturnsTcpExchange)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    acceptor_.early_prepared_ = true;
    acceptor_.early_bound_ = true;
    acceptor_.early_ack_deferred_ = true;
    g_recvRet = -1;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
    EXPECT_EQ(g_ioOrder, "SR");
}

/* 推迟的腿⑥ 先发、后收到对端可降级 ⇒ 与交叉 ack 同款并入本端掩码，状态机仍 kOK。 */
TEST_F(UmqAcceptorOpsTest, CreateSocketResources_DeferredBindRet_PeerDegradable_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    GlobalSetting::UBS_ENABLE_DEGRADE = true;
    InstallSocketDataFakes();
    acceptor_.early_prepared_ = true;
    acceptor_.early_bound_ = true;
    acceptor_.early_ack_deferred_ = true;
    g_recv32 = static_cast<uint32_t>(UBS_OK | UBS_DEGRADABLE_MASK);
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
    EXPECT_TRUE(acceptor_.degradable_);
    EXPECT_EQ(g_ioOrder, "SR");
}

/* 方案B（未推迟）：腿⑥ 已随应答送出 ⇒ kSTART 只收、不发。 */
TEST_F(UmqAcceptorOpsTest, CreateSocketResources_CarriedBindRet_RecvOnly_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    InstallSocketDataFakes();
    acceptor_.early_prepared_ = true;
    acceptor_.early_bound_ = true;
    acceptor_.early_ack_deferred_ = false;
    g_recv32 = 0;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
    EXPECT_EQ(g_ioOrder, "R");
}

/* 协商层：并行 bind 的本质是服务端在 umq_bind 之前就把应答发出去（L 先于 B）；方案B 相反（B 先于 L）。 */
static ock::ubs::Result FakePrepareLocalUmqEarlyOk(SocketPtr, NegotiateRspExt &ext)
{
    ext.bind_info_size = 16;
    memset(ext.bind_info, 0xCD, 16);
    return UBS_OK;
}

static ock::ubs::Result FakeBindPeerAndFinalizeRecord(SocketPtr, umq_used_ports_t &, uint8_t *, uint64_t)
{
    g_ioOrder += 'B';
    return UBS_OK;
}

static void InstallParallelBindNegotiateFakes()
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    UmqSetting::UMQ_IS_BONDING = true;
    UmqSetting::UMQ_ALL_SOCKET_IDS = {7, 8, 9};
    GlobalSetting::UBS_NEGO_CARRY_BINDINFO = true;
    GlobalSetting::UBS_NEGO_REQ_CARRY_BINDINFO = true;
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqAcceptorOps::PrepareLocalUmqEarly).stubs().will(invoke(&FakePrepareLocalUmqEarlyOk));
    MOCKER_CPP(&UmqAcceptorOps::BindPeerAndFinalize).stubs().will(invoke(&FakeBindPeerAndFinalizeRecord));
    g_recv32 = TEST_VERSION_WHOLE;
    g_negoReq.is_bonding = 1;
    g_negoReq.trans_mode = UmqSetting::UMQ_UB_TRANS_MODE; /* 模式匹配 ⇒ 服务端可消费请求携带的 bind_info */
    g_negoReq.cap_flags = NEGO_CAP_CARRY_BINDINFO | NEGO_CAP_REQ_CARRY_BINDINFO;
    g_negoReqExtBindSize = 32;
}

TEST_F(UmqAcceptorOpsTest, Negotiate_ParallelBind_RepliesBeforeBind_DefersAck)
{
    InstallParallelBindNegotiateFakes();
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
    g_negoReq.cap_flags |= NEGO_CAP_DEFER_BIND_RET;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    ASSERT_NE(g_ioOrder.find('L'), std::string::npos);
    ASSERT_NE(g_ioOrder.find('B'), std::string::npos);
    EXPECT_LT(g_ioOrder.find('L'), g_ioOrder.find('B')); /* 应答先于 bind */
    EXPECT_TRUE(acceptor_.early_bound_);
    EXPECT_TRUE(acceptor_.early_ack_deferred_);
    ASSERT_TRUE(g_sentRspExtValid);
    EXPECT_NE(g_sentRspExt.rsp.reserved[0] & NEGO_CAP_REQ_CARRY_BINDINFO, 0);
    EXPECT_NE(g_sentRspExt.rsp.reserved[0] & NEGO_CAP_DEFER_BIND_RET, 0); /* 客户端据此改收腿⑥ */
    EXPECT_EQ(g_sentRspExt.bind_info_size, 16u);
}

TEST_F(UmqAcceptorOpsTest, Negotiate_ParallelBindSwitchOff_BindsBeforeReply)
{
    InstallParallelBindNegotiateFakes();
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = false;
    g_negoReq.cap_flags |= NEGO_CAP_DEFER_BIND_RET; /* 客户端声明了，但本端开关关 ⇒ 方案B */
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    ASSERT_NE(g_ioOrder.find('L'), std::string::npos);
    ASSERT_NE(g_ioOrder.find('B'), std::string::npos);
    EXPECT_LT(g_ioOrder.find('B'), g_ioOrder.find('L')); /* bind 先于应答 */
    EXPECT_TRUE(acceptor_.early_bound_);
    EXPECT_FALSE(acceptor_.early_ack_deferred_);
    ASSERT_TRUE(g_sentRspExtValid);
    EXPECT_EQ(g_sentRspExt.rsp.reserved[0] & NEGO_CAP_DEFER_BIND_RET, 0);
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
}

TEST_F(UmqAcceptorOpsTest, Negotiate_ClientWithoutDeferCap_BindsBeforeReply)
{
    InstallParallelBindNegotiateFakes(); /* 老客户端：无 DEFER 位 ⇒ 方案B 不变 */
    GlobalSetting::UBS_NEGO_PARALLEL_BIND = true;
    EXPECT_EQ(acceptor_.Negotiate(socketPtr_), UBS_OK);
    EXPECT_LT(g_ioOrder.find('B'), g_ioOrder.find('L'));
    EXPECT_FALSE(acceptor_.early_ack_deferred_);
    ASSERT_TRUE(g_sentRspExtValid);
    EXPECT_EQ(g_sentRspExt.rsp.reserved[0] & NEGO_CAP_DEFER_BIND_RET, 0);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_PeerRetDegradable_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    g_recv32 = static_cast<uint32_t>(UBS_OK | UBS_DEGRADABLE_MASK); /* 经典路径：对端可降级 */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_DegradeDisabled_StripsDegradable_ReturnsUbAccept)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    GlobalSetting::UBS_ENABLE_DEGRADE = false;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK)));
    InstallSocketDataFakes();
    g_recv32 = 0; /* peerRet = UBS_OK */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryThenSuccess_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    /* 第一次(初次) retryable，第二次(重试内) 成功 */
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK)))
        .then(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = 0; /* peerRet = UBS_OK */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_OK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kOK);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryThenDegrade_ReturnsUbAcceptDegradable)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    /* 第一次 retryable+degradable，重试内 degradable */
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK)))
        .then(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK)));
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_ERROR); /* peerRet 失败 */
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT | UBS_DEGRADABLE_MASK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryThenFailed_ReturnsUbAccept)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    /* 第一次 retryable(不可降级)，重试内普通错误 */
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK)))
        .then(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND)));
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kRETRY;
    g_recv32 = static_cast<uint32_t>(UBS_ERROR);
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryReturnsError_ReturnsRet)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAcceptRetry).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_TCP_EXCHANGE)));
    InstallSocketDataFakes();
    g_recv32 = 0;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_TCP_EXCHANGE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_Degrade_ReturnsUbAcceptDegradable)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    /* 可降级但不可重试 */
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_DEGRADABLE_MASK)));
    InstallSocketDataFakes();
    g_recv32 = static_cast<uint32_t>(UBS_ERROR);
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT | UBS_DEGRADABLE_MASK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_Failed_ReturnsUbAccept)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    /* 普通错误：不可重试、不可降级 */
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND)));
    InstallSocketDataFakes();
    g_recv32 = static_cast<uint32_t>(UBS_ERROR);
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryFailedCheckOtherRoute_Degradable_ReturnsUbAcceptDegradable)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK)));
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kOK; /* 客户端 CheckOtherRoute 失败 */
    g_recv32 = 0;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT | UBS_DEGRADABLE_MASK);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kDEGRADE);
}

TEST_F(UmqAcceptorOpsTest, CreateSocketResources_RetryFailedCheckOtherRoute_NotDegradable_ReturnsUbAccept)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_ROUTE;
    MOCKER_CPP(&UmqSocket::CheckDevAdd).stubs().will(returnValue(static_cast<ock::ubs::Result>(0)));
    MOCKER_CPP(&UmqAcceptorOps::DoUbAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_UMQ_BIND | UBS_RETRYABLE_MASK)));
    InstallSocketDataFakes();
    MOCKER_CPP(&UmqSocket::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&UmqSocket::DestroyLocalUmq).stubs();
    g_otherRouteMsg.ub_handshake_state = UBHandshakeState::kOK;
    g_recv32 = 0;
    EXPECT_EQ(acceptor_.CreateSocketResources(socketPtr_), UBS_UB_ACCEPT);
    EXPECT_EQ(acceptor_.retry_state_, UBHandshakeState::kFAILED);
}

// ==================== DestroySocketResources ====================

TEST_F(UmqAcceptorOpsTest, DestroySocketResources_NoThrow)
{
    ASSERT_NO_THROW(acceptor_.DestroySocketResources());
}
