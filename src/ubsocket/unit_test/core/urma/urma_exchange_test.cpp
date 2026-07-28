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
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_types.h"
#include "core/urma/urma_wrapper.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;

static urma_target_seg_t g_fake_urg_seg;
static RemoteSegmentsPtr g_fake_remote;

static urma_status_t MockUrmaDeleteJfc(urma_jfc_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaDeleteJfce(urma_jfce_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaDeleteJfs(urma_jfs_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaDeleteJfr(urma_jfr_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaDeleteJetty(urma_jetty_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaUnbindJetty(urma_jetty_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaUnimportJetty(urma_target_jetty_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaUnregisterSeg(urma_target_seg_t *)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaUnimportSeg(urma_target_seg_t *)
{
    return URMA_SUCCESS;
}

static ssize_t MockSendSuccess(int, const void *buf, size_t size, uint32_t)
{
    (void)buf;
    return static_cast<ssize_t>(size);
}

static ssize_t MockSendFail(int, const void *buf, size_t, uint32_t)
{
    (void)buf;
    return -1;
}

static ssize_t MockRecvSuccess(int, const void *buf, size_t size, uint32_t)
{
    memset(const_cast<void *>(buf), 0, size);
    return static_cast<ssize_t>(size);
}

static ssize_t MockRecvFail(int, const void *buf, size_t, uint32_t)
{
    (void)buf;
    return -1;
}

static RemoteSegmentsPtr MockGetRemoteSegments(const IdentifyNode &)
{
    return g_fake_remote;
}

static int g_sendCall = 0;
static int g_recvCall = 0;

static ssize_t MockSendAckFail(int, const void *, size_t size, uint32_t)
{
    g_sendCall++;
    if (g_sendCall == 2) {
        return -1;
    }
    return static_cast<ssize_t>(size);
}

static ssize_t MockRecvAckFail(int, const void *, size_t size, uint32_t)
{
    g_recvCall++;
    if (g_recvCall == 2) {
        return -1;
    }
    return static_cast<ssize_t>(size);
}
static urma_status_t MockUrmaPostRecvWrSuccess(urma_jetty_t *, urma_jfr_wr_t *, urma_jfr_wr_t **)
{
    return URMA_SUCCESS;
}
} // namespace

class ExchangeSocketResourcesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();

        memset(&g_fake_urg_seg, 0, sizeof(g_fake_urg_seg));

        g_fake_remote = MakeRef<RemoteSegments>();
        g_fake_remote->remote_target_segs_.resize(1024);

        UrmaApi::urma_delete_jfc_ptr = MockUrmaDeleteJfc;
        UrmaApi::urma_delete_jfce_ptr = MockUrmaDeleteJfce;
        UrmaApi::urma_delete_jfs_ptr = MockUrmaDeleteJfs;
        UrmaApi::urma_delete_jfr_ptr = MockUrmaDeleteJfr;
        UrmaApi::urma_delete_jetty_ptr = MockUrmaDeleteJetty;
        UrmaApi::urma_unbind_jetty_ptr = MockUrmaUnbindJetty;
        UrmaApi::urma_unimport_jetty_ptr = MockUrmaUnimportJetty;
        UrmaApi::urma_delete_context_ptr = MockUrmaDeleteContext;
        UrmaApi::urma_unregister_seg_ptr = MockUrmaUnregisterSeg;
        UrmaApi::urma_unimport_seg_ptr = MockUrmaUnimportSeg;
        UrmaApi::urma_post_jetty_recv_wr_ptr = MockUrmaPostRecvWrSuccess;

        urma_device_attr_t device_attr{};
        auto device = MakeRef<UrmaDevice>("test_dev", "/sys/test", device_attr);
        urma_eid_info_t eid_info{};
        ctx_ = MakeRef<UrmaContext>(nullptr, device, eid_info);
        jfc_ = MakeRef<UrmaJfc>(BUSY_POLLING, nullptr, nullptr, ctx_);
        jfs_ = MakeRef<UrmaJfs>(nullptr, ctx_, jfc_);
        jfr_ = MakeRef<UrmaJfr>(nullptr, 0, ctx_, jfc_);
        jetty_ = MakeRef<UrmaJetty>(URMA_CTP, 1, nullptr, ctx_, jfs_, jfr_);

        sock_ = MakeRef<UrmaSocket>(TEST_FD);
        sock_->send_buf_addr_ = 0x1000;
        sock_->recv_buf_addr_ = 0x2000;
        sock_->send_buf_size_ = 4096 * 1024;
        sock_->recv_buf_size_ = 4096 * 1024;
        sock_->jetty_ = jetty_;

        UrmaBackend::GetAllocator()->local_start_addr_ = 0x1000;
        UrmaBackend::local_identify_node_.eid = {};
        UrmaBackend::local_identify_node_.pid = 1234;
        memset(UrmaBackend::local_identify_node_.ns, 0, sizeof(UrmaBackend::local_identify_node_.ns));
        strncpy(UrmaBackend::local_identify_node_.ns, "test_ns", NAME_MAX + 1);
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sock_ = nullptr;
        jetty_ = nullptr;
        jfr_ = nullptr;
        jfs_ = nullptr;
        jfc_ = nullptr;
        ctx_ = nullptr;
        g_fake_remote = nullptr;

        UrmaApi::urma_delete_jfc_ptr = nullptr;
        UrmaApi::urma_delete_jfce_ptr = nullptr;
        UrmaApi::urma_delete_jfs_ptr = nullptr;
        UrmaApi::urma_delete_jfr_ptr = nullptr;
        UrmaApi::urma_delete_jetty_ptr = nullptr;
        UrmaApi::urma_unbind_jetty_ptr = nullptr;
        UrmaApi::urma_unimport_jetty_ptr = nullptr;
        UrmaApi::urma_delete_context_ptr = nullptr;
        UrmaApi::urma_unregister_seg_ptr = nullptr;
        UrmaApi::urma_unimport_seg_ptr = nullptr;
        UrmaApi::urma_post_jetty_recv_wr_ptr = nullptr;
    }

    UrmaContextPtr ctx_{nullptr};
    UrmaJfcPtr jfc_{nullptr};
    UrmaJfsPtr jfs_{nullptr};
    UrmaJfrPtr jfr_{nullptr};
    UrmaJettyPtr jetty_{nullptr};
    Ref<UrmaSocket> sock_{nullptr};
};

TEST_F(ExchangeSocketResourcesTest, CreateUrmaResources_Fails_ReturnsError)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get());
    EXPECT_EQ(UBS_ERROR, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, PoolStartAddrZero_ReturnsTcpExchange)
{
    UrmaBackend::GetAllocator()->local_start_addr_ = 0;

    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get());
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, LocalSegNull_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg)
        .stubs()
        .will(returnValue(static_cast<urma_target_seg_t *>(nullptr)));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get());
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_SendSocketDataFails_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendFail));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_RecvSocketDataFails_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvFail));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_ImportRemoteJettyFails_ReturnsError)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_ERROR, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_StoreRemoteTargetSegFails_ReturnsError)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_ERROR, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_AckSendFails_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));

    g_sendCall = 0;
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendAckFail));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_AckRecvFails_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));

    g_recvCall = 0;
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvAckFail));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, SendFirst_Success_ReturnsOk)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), true);
    EXPECT_EQ(UBS_OK, ret);
    EXPECT_TRUE(sock_->IsBindRemote());
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, NotSendFirst_SendSocketDataFails_ReturnsTcpExchange)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendFail));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), false);
    EXPECT_EQ(UBS_TCP_EXCHANGE, ret);
    GlobalMockObject::verify();
}

TEST_F(ExchangeSocketResourcesTest, NotSendFirst_Success_ReturnsOk)
{
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(returnValue(&g_fake_urg_seg));
    MOCKER_CPP(&UrmaJetty::ImportRemoteJetty).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaIoBufferPool::get_remote_segments).stubs().will(invoke(MockGetRemoteSegments));
    MOCKER_CPP(&UrmaIoBufferPool::store_remote_target_seg).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&SocketConnHelper::RecvSocketData).stubs().will(invoke(MockRecvSuccess));
    MOCKER_CPP(&SocketConnHelper::SendSocketData).stubs().will(invoke(MockSendSuccess));

    ock::ubs::Result ret = sock_->ExchangeSocketResources(TEST_FD, *sock_.Get(), false);
    EXPECT_EQ(UBS_OK, ret);
    EXPECT_TRUE(sock_->IsBindRemote());
    GlobalMockObject::verify();
}
