/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "core/ubsocket_core_types.h"
#include "core/umq/umq_backend.h"
#include "core/umq/umq_eid_table.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_share_jfr_epoll_runner_ops.h"
#include "core/umq/umq_socket.h"
#include "core/umq/umq_socket_acceptor.h"
#include "core/umq/umq_socket_connector.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {

int g_uninit_calls = 0;
int g_destroy_calls = 0;
int g_ack_calls = 0;
int g_freed_qbuf_calls = 0;
umq_buf_t *g_freed_qbuf = nullptr;

void MockUmqUninit()
{
    ++g_uninit_calls;
}

int MockUmqDestroy(uint64_t)
{
    ++g_destroy_calls;
    return UMQ_SUCCESS;
}

int MockGetCqEvent(uint64_t, umq_interrupt_option_t *)
{
    return 1;
}

int MockRearmFail(uint64_t, bool, umq_interrupt_option_t *)
{
    return -1;
}

void MockAck(uint64_t, uint32_t, umq_interrupt_option_t *)
{
    ++g_ack_calls;
}

void MockBufFree(umq_buf_t *qbuf)
{
    ++g_freed_qbuf_calls;
    g_freed_qbuf = qbuf;
}

} // namespace

class UmqMidRiskRegressionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UmqEidTable::Instance().Clean();
        g_uninit_calls = 0;
        g_destroy_calls = 0;
        g_ack_calls = 0;
        g_freed_qbuf_calls = 0;
        g_freed_qbuf = nullptr;
        UmqBackend::UMQ_INITED = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
        UmqSetting::UMQ_TRANS_MODE = UMQ_TRANS_MODE_UB;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_DEV_NAME.clear();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        UmqEidTable::Instance().Clean();
        UmqBackend::UMQ_INITED = false;
    }
};

// G: a failed main-UMQ creation must not poison the shared EidTable with
// UMQ_INVALID_HANDLE, otherwise later sockets reuse an invalid handle.
TEST_F(UmqMidRiskRegressionTest, CreateShareMainUmq_CreateFailure_DoesNotCacheInvalidHandle)
{
    umq_eid_t eid{};
    eid.raw[0] = 1;
    UmqSetting::UMQ_LOCAL_EID = eid;
    MOCKER_CPP(&UmqApi::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));

    EXPECT_EQ(UmqBackend::CreateShareMainUmq(eid), UMQ_INVALID_HANDLE);
    EXPECT_EQ(UmqEidTable::Instance().GetFirst(eid, UmqSetting::UMQ_UB_TRANS_MODE).get(), nullptr);
}

// H: once umq_init succeeds, every later Init failure must balance it with
// umq_uninit. An unsupported transport mode is a deterministic post-init exit.
TEST_F(UmqMidRiskRegressionTest, Init_PostInitFailure_CallsUmqUninit)
{
    MOCKER_CPP(&UmqSetting::Init).stubs().will(returnValue(static_cast<int>(UBS_OK)));
    MOCKER_CPP(&UmqApi::umq_init).stubs().will(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(&UmqApi::umq_uninit).stubs().will(invoke(MockUmqUninit));
    UmqSetting::UMQ_TRANS_MODE = static_cast<umq_trans_mode>(-1);

    EXPECT_NE(UmqBackend::Init(), UBS_OK);
    EXPECT_EQ(g_uninit_calls, 1);
}

// K: GenerateSocketCommOps failure must be returned instead of returning the
// already-successful CreateLocalUmq result and exposing an incomplete socket.
TEST_F(UmqMidRiskRegressionTest, DoUbConnect_GenerateCommOpsFailure_IsPropagated)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(101);
    UmqConnectorOps connector(101);
    umq_used_ports_t ports{};

    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<int>(UBS_OK)));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<int>(UBS_ERROR)));

    EXPECT_NE(connector.DoUbConnect(umqSocket, ports), UBS_OK);
}

TEST_F(UmqMidRiskRegressionTest, DoUbAccept_GenerateCommOpsFailure_IsPropagated)
{
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(102);
    SocketPtr socket = RefConvert<UmqSocket, Socket>(umqSocket);
    UmqAcceptorOps acceptor(102);
    umq_used_ports_t ports{};

    MOCKER_CPP(&UmqSocket::CreateLocalUmq).stubs().will(returnValue(static_cast<int>(UBS_OK)));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<int>(UBS_ERROR)));

    EXPECT_NE(acceptor.DoUbAccept(socket, ports), UBS_OK);
}

// J: after umq_create succeeds, a flow-control event setup failure must
// destroy the local handle before returning.
TEST_F(UmqMidRiskRegressionTest, CreateLocalUmq_RegisterFcFailure_DestroysCreatedHandle)
{
    UmqSocket umqSocket(303);
    umq_eid_t eid{};
    umq_used_ports_t ports{};
    umq_topo_type_t topoType{};
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;

    MOCKER_CPP(&UmqApi::umq_create).stubs().will(returnValue(static_cast<uint64_t>(123)));
    MOCKER_CPP(&UmqApi::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    MOCKER_CPP(&UmqApi::umq_destroy).stubs().will(invoke(MockUmqDestroy));

    EXPECT_NE(umqSocket.CreateLocalUmq(&eid, ports, topoType), UBS_OK);
    EXPECT_EQ(g_destroy_calls, 1);
    umqSocket.umq_handle_ = UMQ_INVALID_HANDLE;
}

// O: a failed rearm must not be treated as a successful RX interrupt cycle or
// acknowledged, otherwise the RX flow can silently stop.

// E: if delivery into the per-socket RX queue fails, the polled buffer still
// belongs to the runner and must be returned to UMQ instead of being dropped.
TEST_F(UmqMidRiskRegressionTest, SiftSocketEvents_AddQbufFailure_FreesPolledBuffer)
{
    const int fd = 202;
    UmqSocketPtr umqSocket = MakeRef<UmqSocket>(fd);
    SocketPtr socket = RefConvert<UmqSocket, Socket>(umqSocket);
    ArraySet<Socket>::GetInstance().OverrideItem(fd, socket.Get());

    umq_buf_t qbuf{};
    qbuf.status = 0;
    auto *qbufPro = reinterpret_cast<umq_buf_pro_t *>(qbuf.qbuf_ext);
    qbufPro->umq_ctx = static_cast<uint64_t>(fd);
    umq_buf_t *bufs[] = {&qbuf};
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> socketPtrs;
    UmqShareJfrEpollRunnerOps ops;
    MOCKER_CPP(&UmqApi::umq_buf_free).stubs().will(invoke(MockBufFree));

    ops.SiftSocketEventsWithUmqBuffers(bufs, 1, socketFds, socketPtrs);

    EXPECT_EQ(g_freed_qbuf_calls, 1);
    EXPECT_EQ(g_freed_qbuf, &qbuf);
}
