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
#include "core/ubsocket_socket_connector.h"
#include "core/ubsocket_socket_helper.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_connector_ops.h"
#include "core/urma/urma_socket_types.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const int TEST_NEW_FD = 101;
static const uint32_t TEST_VERSION = 0x00000101;
static const uint64_t TEST_CONN_ID = 1;

static ock::ubs::Result MockSendPrefixedOk(int, const void *, uint32_t, uint32_t)
{
    return UBS_OK;
}

static ock::ubs::Result MockSendPrefixedFail(int, const void *, uint32_t, uint32_t)
{
    return UBS_ERROR;
}

static ssize_t MockRecvVersionCompatible(int, const void *buf, size_t size, uint32_t)
{
    *const_cast<uint32_t *>(static_cast<const uint32_t *>(buf)) = TEST_VERSION;
    return static_cast<ssize_t>(size);
}

static ssize_t MockRecvVersionMismatch(int, const void *buf, size_t size, uint32_t)
{
    *const_cast<uint32_t *>(static_cast<const uint32_t *>(buf)) = 0xFF000000;
    return static_cast<ssize_t>(size);
}

static ssize_t MockRecvFail(int, const void *, size_t, uint32_t)
{
    return static_cast<ssize_t>(-1);
}

static ock::ubs::Result MockRecvRspOk(int, void *body, uint32_t, uint32_t)
{
    UrmaNegotiateRsp *rsp = static_cast<UrmaNegotiateRsp *>(body);
    rsp->ret_code = 0;
    rsp->negotiated_version = UBS_PROTOCOL_VERSION.whole;
    rsp->trans_mode = RM_CTP;
    rsp->send_buf_kb = 16;
    rsp->recv_buf_kb = 16;
    rsp->selected_context_index = 0;
    rsp->conn_id = TEST_CONN_ID;
    return UBS_OK;
}

static ock::ubs::Result MockRecvRspFail(int, void *, uint32_t, uint32_t)
{
    return UBS_ERROR;
}
} // namespace

class UrmaConnectorOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
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

TEST_F(UrmaConnectorOpsTest, Construct_Destruct_NoCrash)
{
    UrmaConnectorOps ops(TEST_FD);
}

TEST_F(UrmaConnectorOpsTest, Negotiate_SendPrefixedFails_ReturnsError)
{
    UrmaConnectorOps ops(TEST_FD);

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    ock::ubs::Result ret = ops.Negotiate(TEST_NEW_FD, sockPtr);
    EXPECT_NE(UBS_OK, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaConnectorOpsTest, Negotiate_RecvVersionFails_ReturnsTcpExchange)
{
    UrmaConnectorOps ops(TEST_FD);

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvFail));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    ock::ubs::Result ret = ops.Negotiate(TEST_NEW_FD, sockPtr);
    EXPECT_EQ(UBS_ERROR, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaConnectorOpsTest, Negotiate_VersionMismatch_ReturnsTcpExchange)
{
    UrmaConnectorOps ops(TEST_FD);

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(MockSendPrefixedOk));

    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvVersionMismatch));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    ock::ubs::Result ret = ops.Negotiate(TEST_NEW_FD, sockPtr);
    EXPECT_EQ(UBS_ERROR, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaConnectorOpsTest, Negotiate_RecvRspFails_ReturnsError)
{
    UrmaConnectorOps ops(TEST_FD);

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(MockSendPrefixedOk));

    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvVersionCompatible));

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvRspFail));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    ock::ubs::Result ret = ops.Negotiate(TEST_NEW_FD, sockPtr);
    EXPECT_NE(UBS_OK, ret);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaConnectorOpsTest, Negotiate_Success)
{
    UrmaConnectorOps ops(TEST_FD);

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    auto sockPtr = RefConvert<UrmaSocket, Socket>(sock);

    MOCKER_CPP(&SocketConnHelper::SendLengthPrefixed).stubs().will(invoke(MockSendPrefixedOk));

    MOCKER_CPP(&SocketConnHelper::RecvLengthPrefixed).stubs().will(invoke(MockRecvRspOk));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));
    UrmaDevicePtr target_dev;
    urma_eid_info_t eid_info;
    auto tmp_context = MakeRef<UrmaContext>(nullptr, target_dev, eid_info);
    tmp_context_pool->sorted_contexts_.emplace_back(tmp_context);
    ock::ubs::Result ret = ops.Negotiate(TEST_NEW_FD, sockPtr);
    EXPECT_EQ(UBS_OK, ret);
    EXPECT_EQ(TEST_CONN_ID, sock->GetConnId());
    EXPECT_EQ(RM_CTP, sock->trans_mode);

    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaConnectorOpsTest, CreateSocketResources_NullSocket_ReturnsInvalidParam)
{
    UrmaConnectorOps ops(TEST_FD);
    SocketPtr nullSock = nullptr;
    ock::ubs::Result ret = ops.CreateSocketResources(nullSock);
    EXPECT_EQ(UBS_INVALID_PARAM, ret);
}

TEST_F(UrmaConnectorOpsTest, DestroySocketResources_NoCrashWhenNotCreated)
{
    UrmaConnectorOps ops(TEST_FD);
    ops.DestroySocketResources();
}
