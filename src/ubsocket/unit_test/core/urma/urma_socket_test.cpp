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
#include "core/urma/urma_backend.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_types.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static char g_mock_buf[256 * 1024];

static urma_status_t MockUrmaPostSendWrSuccess(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaPostRecvWrSuccess(urma_jetty_t *, urma_jfr_wr_t *, urma_jfr_wr_t **)
{
    return URMA_SUCCESS;
}
static void *MockDirectAllocate(size_t)
{
    return g_mock_buf;
}
static void MockDirectRelease(void *) {}
} // namespace

class UrmaSocketTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();

        UrmaApi::urma_post_jetty_send_wr_ptr = MockUrmaPostSendWrSuccess;
        UrmaApi::urma_post_jetty_recv_wr_ptr = MockUrmaPostRecvWrSuccess;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();

        UrmaApi::urma_post_jetty_send_wr_ptr = nullptr;
        UrmaApi::urma_post_jetty_recv_wr_ptr = nullptr;
    }
};

TEST_F(UrmaSocketTest, Construct_Destruct_NoCrash)
{
    UrmaSocket sock(TEST_FD);
    EXPECT_EQ(TEST_FD, sock.Fd());
    EXPECT_EQ(SocketType::SOCK_TYPE_URMA, sock.Type());
}

TEST_F(UrmaSocketTest, Initialize_EventfdFail_ReturnsErrno)
{
    MOCKER(eventfd).stubs().will(returnValue(-1));
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_NE(nullptr, sock.Get());
    ock::ubs::Result ret = sock->Initialize();
    EXPECT_NE(UBS_OK, ret);
    GlobalMockObject::verify();
}

TEST_F(UrmaSocketTest, Initialize_Success_ReturnsOk)
{
    MOCKER_CPP(&UrmaIoBufferPool::direct_allocate).stubs().will(invoke(MockDirectAllocate));
    MOCKER_CPP(&UrmaIoBufferPool::direct_release).stubs().will(invoke(MockDirectRelease));
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_NE(nullptr, sock.Get());
    ock::ubs::Result ret = sock->Initialize();
    EXPECT_EQ(UBS_OK, ret);
    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaSocketTest, IsBindRemote_DefaultFalse)
{
    UrmaSocket sock(TEST_FD);
    EXPECT_FALSE(sock.IsBindRemote());
}

TEST_F(UrmaSocketTest, SetBindRemote_True_IsBindRemoteReturnsTrue)
{
    UrmaSocket sock(TEST_FD);
    sock.SetBindRemote(true);
    EXPECT_TRUE(sock.IsBindRemote());
}

TEST_F(UrmaSocketTest, WriteV_NullIov_ReturnsEinval)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    sock->SetBindRemote(true);
    errno = 0;
    int ret = sock->WriteV(RefConvert<UrmaSocket, Socket>(sock), nullptr, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
    sock->UnInitialize();
}

TEST_F(UrmaSocketTest, WriteV_NotBound_ReturnsEnotconn)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    struct iovec iov;
    iov.iov_base = const_cast<char *>("test");
    iov.iov_len = 4;
    errno = 0;
    int ret = sock->WriteV(RefConvert<UrmaSocket, Socket>(sock), &iov, 1);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOTCONN, errno);
    sock->UnInitialize();
}

TEST_F(UrmaSocketTest, ReadV_NoRecvBuf_ReturnsEnotconn)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("");
    iov.iov_len = 4;
    errno = 0;
    int ret = sock->ReadV(RefConvert<UrmaSocket, Socket>(sock), &iov, 1);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOTCONN, errno);
}

TEST_F(UrmaSocketTest, LocalEid_SetAndGet)
{
    UrmaSocket sock(TEST_FD);
    urma_eid_t eid = {};
    eid.raw[0] = 0x42;
    sock.local_eid_ = eid;
    urma_eid_t result = sock.local_eid_;
    EXPECT_EQ(0x42, result.raw[0]);
}

TEST_F(UrmaSocketTest, PeerEid_SetNoCrash)
{
    UrmaSocket sock(TEST_FD);
    urma_eid_t eid = {};
    eid.raw[0] = 0x77;
    sock.SetPeerEid(eid);
}

TEST_F(UrmaSocketTest, NegVersion_SetNoCrash)
{
    UrmaSocket sock(TEST_FD);
    sock.SetNegotiatedVersion(0x010203);
    sock.SetPeerVersion(0x040506);
}
