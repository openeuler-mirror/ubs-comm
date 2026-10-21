/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_acceptor.h"
#include "core/ubsocket_socket_helper.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_acceptor_ops.h"
#include "core/urma/urma_socket_types.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const uint32_t TEST_VERSION = 0x00000101;

static int g_send_call_count = 0;
static int g_recv_call_count = 0;

static ock::ubs::Result MockRecvPrefixedFail(int, void *, uint32_t, uint32_t)
{
    return UBS_ERROR;
}

static ock::ubs::Result MockRecvPrefixedMajorMismatch(int, void *body, uint32_t, uint32_t)
{
    UrmaNegotiateReq *req = static_cast<UrmaNegotiateReq *>(body);
    req->peer_version = 0xFF000000;
    req->trans_mode = RM_CTP;
    req->send_buf_kb = 16;
    req->recv_buf_kb = 16;
    return UBS_OK;
}

static ock::ubs::Result MockRecvPrefixedCompatible(int, void *body, uint32_t, uint32_t)
{
    UrmaNegotiateReq *req = static_cast<UrmaNegotiateReq *>(body);
    req->peer_version = TEST_VERSION;
    req->trans_mode = RM_CTP;
    req->send_buf_kb = 16;
    req->recv_buf_kb = 16;
    return UBS_OK;
}

static ock::ubs::Result MockRecvPrefixedWithStaleConn(int fd, void *body, uint32_t size, uint32_t timeout)
{
    auto result = MockRecvPrefixedCompatible(fd, body, size, timeout);
    static_cast<UrmaNegotiateReq *>(body)->last_conn_id = 0x1234;
    return result;
}

/* 捕获 SendLengthPrefixed 发出的 rsp 内容，供 mismatch 用例断言 */
static UrmaNegotiateRsp g_captured_rsp = {};
static ock::ubs::Result MockSendPrefixedCaptureRsp(int, const void *body, uint32_t, uint32_t)
{
    if (body != nullptr) {
        g_captured_rsp = *static_cast<const UrmaNegotiateRsp *>(body);
    }
    return UBS_OK;
}

static ock::ubs::Result MockSendPrefixedOk(int, const void *, uint32_t, uint32_t)
{
    return UBS_OK;
}

static ock::ubs::Result MockSendPrefixedFail(int, const void *, uint32_t, uint32_t)
{
    return UBS_ERROR;
}
} // namespace

class UrmaAcceptorOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_send_call_count = 0;
        g_recv_call_count = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();
        UrmaBackend::Instance().context_pool_ = MakeRef<UrmaContextPool>();
        urma_device_attr_t dev_attr = {};
        urma_eid_info_t eid_info = {};
        auto dev = MakeRef<UrmaDevice>("ut_dev", "", dev_attr);
        auto ctx = MakeRef<UrmaContext>(nullptr, dev, eid_info);
        ctx->affinity_ = false;
        UrmaBackend::Instance().context_pool_->sorted_contexts_.push_back(ctx);
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        UrmaBackend::Instance().context_pool_ = nullptr;
    }
};

TEST_F(UrmaAcceptorOpsTest, Construct_Destruct_NoCrash)
{
    UrmaAcceptorOps ops(TEST_FD);
}

TEST_F(UrmaAcceptorOpsTest, PrepareConnect_ReturnsOk)
{
    UrmaAcceptorOps ops(TEST_FD);
    ock::ubs::Result ret = ops.PrepareConnect(TEST_FD);
    EXPECT_EQ(UBS_OK, ret);
}

TEST_F(UrmaAcceptorOpsTest, ValidateProtocol_ReturnsZero)
{
    UrmaAcceptorOps ops(TEST_FD);
    uint64_t protocol = 0;
    ssize_t recv_size = 0;
    int ret = ops.ValidateProtocol(TEST_FD, protocol, recv_size);
    EXPECT_EQ(0, ret);
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_NullSocket_ReturnsInvalidParam)
{
    UrmaAcceptorOps ops(TEST_FD);
    ock::ubs::Result ret = ops.Negotiate(nullptr);
    EXPECT_EQ(UBS_INVALID_PARAM, ret);
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_RecvPrefixedFails_ReturnsError)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_NE(UBS_OK, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_VersionMajorMismatch_ReturnsDegradableTcpExchange)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedMajorMismatch));

    /* mismatch 回帧：ret_code=0，negotiated_version=本端版本，供对端判定降级 */
    g_captured_rsp = {};
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(MockSendPrefixedCaptureRsp));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_TCP_EXCHANGE | UBS_DEGRADABLE_MASK, ret);
    EXPECT_EQ(0, g_captured_rsp.ret_code);
    EXPECT_EQ(UBS_PROTOCOL_VERSION.GetWhole(), g_captured_rsp.negotiated_version);
    EXPECT_EQ(-1, g_captured_rsp.selected_context_index); /* mismatch 通知不携带 context */

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_NoContext_ReturnsTcpExchange)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);
    UrmaBackend::Instance().context_pool_->sorted_contexts_.clear();

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedCompatible));

    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    MOCKER_CPP(&UrmaContextPool::SelectContext).stubs().will(returnValue(tmp_context));
    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_ERROR, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_MismatchSendRspFails_ReturnsError)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedMajorMismatch));

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_ERROR, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_SendRspFails_ReturnsError)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedCompatible));

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    MOCKER_CPP(&UrmaContextPool::SelectContext).stubs().will(returnValue(tmp_context));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_NE(UBS_OK, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_Success)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedCompatible));

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    MOCKER_CPP(&UrmaContextPool::SelectContext).stubs().will(returnValue(tmp_context));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_OK, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, CreateSocketResources_NullSocket_ReturnsInvalidParam)
{
    UrmaAcceptorOps ops(TEST_FD);
    ock::ubs::Result ret = ops.CreateSocketResources(nullptr);
    EXPECT_EQ(UBS_INVALID_PARAM, ret);
}

TEST_F(UrmaAcceptorOpsTest, CreateSocketResources_GenerateOpsFails_ReturnsError)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    SocketPtr sockPtr = RefConvert<UrmaSocket, Socket>(sock);
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    EXPECT_EQ(UBS_ERROR, ops.CreateSocketResources(sockPtr));
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, CreateSocketResources_PropagatesExchangeResult)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    SocketPtr sockPtr = RefConvert<UrmaSocket, Socket>(sock);
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::ExchangeSocketResources)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_TCP_EXCHANGE)));

    EXPECT_EQ(UBS_TCP_EXCHANGE, ops.CreateSocketResources(sockPtr));
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, DestroySocketResources_NoCrashWhenNotCreated)
{
    UrmaAcceptorOps ops(TEST_FD);
    ops.DestroySocketResources();
}

TEST_F(UrmaAcceptorOpsTest, CleanStaleSockBreaksMatchingAcceptedSocket)
{
    constexpr int STALE_FD = 101;
    constexpr uint64_t STALE_CONN_ID = 0x1234;
    UrmaAcceptorOps ops(TEST_FD);
    auto current = MakeRef<UrmaSocket>(TEST_FD);
    auto stale = MakeRef<UrmaSocket>(STALE_FD);
    current->SetConnId(0x5678);
    stale->SetConnId(STALE_CONN_ID);
    stale->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
    stale->event_fd_ = 102;
    stale->State(SOCK_STAT_ESTABLISHED);
    ArraySet<Socket>::GetInstance().OverrideItem(STALE_FD, stale.Get());

    MOCKER_CPP(&UrmaIoBufferPool::rm_remote_segments).stubs();
    MOCKER(eventfd_write).stubs().will(returnValue(0));

    SocketPtr currentSocket = RefConvert<UrmaSocket, Socket>(current);
    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedWithStaleConn));
    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    EXPECT_EQ(UBS_OK, ops.Negotiate(currentSocket));

    EXPECT_EQ(SOCK_STAT_BROKEN, stale->State());
}
