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

static ssize_t MockSendVersionOk(int, const void *, size_t size, uint32_t)
{
    g_send_call_count++;
    return static_cast<ssize_t>(size);
}

static ssize_t MockSendFail(int, const void *, size_t, uint32_t)
{
    return static_cast<ssize_t>(-1);
}

static ssize_t MockSendVersionOkThenFail(int, const void *, size_t size, uint32_t)
{
    g_send_call_count++;
    if (g_send_call_count == 1) {
        return static_cast<ssize_t>(size);
    }
    return static_cast<ssize_t>(-1);
}

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
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
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
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_NE(UBS_OK, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_VersionMajorMismatch_ReturnsTcpExchange)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedMajorMismatch));

    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(returnValue(static_cast<ssize_t>(sizeof(uint32_t))));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    MOCKER_CPP(&UrmaContextPool::SelectContext).stubs().will(returnValue(tmp_context));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_SendVersionFails_ReturnsTcpExchange)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

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

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_SendRspFails_ReturnsError)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedCompatible));

    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendVersionOkThenFail));

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

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, Negotiate_Success)
{
    UrmaAcceptorOps ops(TEST_FD);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvPrefixedCompatible));

    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendVersionOk));

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    MOCKER_CPP(&UrmaContextPool::SelectContext).stubs().will(returnValue(tmp_context));

    ock::ubs::Result ret = ops.Negotiate(sockPtr);
    EXPECT_EQ(UBS_OK, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaAcceptorOpsTest, CreateSocketResources_NullSocket_ReturnsInvalidParam)
{
    UrmaAcceptorOps ops(TEST_FD);
    ock::ubs::Result ret = ops.CreateSocketResources(nullptr);
    EXPECT_EQ(UBS_INVALID_PARAM, ret);
}

TEST_F(UrmaAcceptorOpsTest, DestroySocketResources_NoCrashWhenNotCreated)
{
    UrmaAcceptorOps ops(TEST_FD);
    ops.DestroySocketResources();
}
