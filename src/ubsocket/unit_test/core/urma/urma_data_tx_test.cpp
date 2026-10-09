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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <mockcpp/mockcpp.hpp>
#include <thread>
#include <vector>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_data_tx_ops.h"
#include "core/urma/urma_io_buf_pool.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_types.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_libc_api.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const uint32_t SEND_BUF_SIZE = 8192;
static uint8_t g_send_buf[SEND_BUF_SIZE];
static urma_target_seg_t g_local_tseg = {};
static urma_target_seg_t g_remote_tseg = {};

/* bypass ubsocket/libc wrappers: set a real SO_*TIMEO on a test socketpair */
static int RawSetSockTimeout(int fd, int optname, long usec)
{
    struct timeval tv = {0, usec};
    return static_cast<int>(syscall(__NR_setsockopt, fd, SOL_SOCKET, optname, &tv, sizeof(tv)));
}

static int (*g_origFcntlPtr)(int, int, ...) = nullptr;

/* fake TEST_FD has no real kernel fd: pretend it is non-blocking so EAGAIN tests
 * do not hit a failing fcntl; real fds delegate to libc. */
static int MockFcntlForTestFd(int fd, int cmd, ...)
{
    if (fd == TEST_FD && cmd == F_GETFL) {
        return O_NONBLOCK;
    }
    va_list args;
    va_start(args, cmd);
    long arg = va_arg(args, long);
    va_end(args);
    return g_origFcntlPtr(fd, cmd, arg);
}

static urma_status_t MockPostSendWrSuccess(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_SUCCESS;
}

static urma_status_t MockPostSendWrFail(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return 1;
}

static const size_t BLOCK_SIZE = 8192;
alignas(8192) static uint8_t g_io_mem[4 * BLOCK_SIZE];
static UrmaIoBufferPool g_dummy_alloc;
static int g_next_io_block = 0;

static char *AllocIoBlock()
{
    int idx = g_next_io_block++;
    auto *block = new (g_io_mem + idx * BLOCK_SIZE)
        Block(reinterpret_cast<char *>(g_io_mem + idx * BLOCK_SIZE + sizeof(Block)), BLOCK_SIZE - sizeof(Block));
    block->nshared.store(100, std::memory_order_relaxed);
    return block->data;
}

static uintptr_t MockMiniBlockHead(uintptr_t addr)
{
    auto base = reinterpret_cast<uintptr_t>(g_io_mem);
    if (addr >= base && addr < base + sizeof(g_io_mem)) {
        return base + ((addr - base) / BLOCK_SIZE) * BLOCK_SIZE;
    }
    return 0;
}

static urma_target_seg_t *MockGetLocalTargetSeg(UrmaContextPtr ctx, uintptr_t)
{
    return &g_local_tseg;
}

/* Records the head.data_count of every posted control message. Atomics keep it race-free: the
 * blocking short-write case drives the sender from a worker thread while the test thread polls. */
static std::atomic<uint32_t> g_postedCount{0};
static std::atomic<uint32_t> g_postedDataCount0{0};
static std::atomic<uint32_t> g_postedDataCount1{0};

static urma_status_t MockPostSendWrRecordPosts(urma_jetty_t *, urma_jfs_wr_t *wr, urma_jfs_wr_t **)
{
    if (wr != nullptr && wr->rw.src.sge != nullptr) {
        const auto *ctrl = reinterpret_cast<const UrmaCtrlMessage *>(wr->rw.src.sge[0].addr);
        const uint32_t idx = g_postedCount.fetch_add(1U, std::memory_order_acq_rel);
        if (idx == 0U) {
            g_postedDataCount0.store(ctrl->head.data_count, std::memory_order_release);
        } else if (idx == 1U) {
            g_postedDataCount1.store(ctrl->head.data_count, std::memory_order_release);
        }
    }
    return URMA_SUCCESS;
}

/* deterministic payload pattern: byte i = (i * 31 + seed) & 0xFF */
static void FillTestPayload(std::vector<char> &buf, unsigned seed)
{
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<char>((i * 31U + seed) & 0xFFU);
    }
}

/* Leave exactly `available` bytes free in the send area: the pre-allocation is intentionally never
 * released, so the allocator can only satisfy what fits in the remaining room. */
static bool LeaveSendBufAvailable(const UrmaSocketPtr &urmaSock, uint64_t available)
{
    uint64_t preOffset = 0;
    if (!urmaSock->send_buf_->Allocate(SEND_BUF_SIZE - available, preOffset)) {
        return false;
    }
    return urmaSock->send_buf_->Available() == available;
}
} // namespace

class UrmaTxOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();

        dummy_jetty_ = static_cast<UrmaJetty *>(malloc(sizeof(UrmaJetty)));
        memset(dummy_jetty_, 0, sizeof(UrmaJetty));
        dummy_jetty_->ref_count_ = 1;

        MOCKER(::eventfd).stubs().will(returnValue(TEST_FD));
        MOCKER(::close).stubs().will(returnValue(0));

        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrSuccess;

        g_next_io_block = 0;
        MOCKER_CPP(&UrmaBackend::GetAllocator).stubs().will(returnValue(&g_dummy_alloc));
        MOCKER_CPP(&UrmaIoBufferPool::get_mini_block_head).stubs().will(invoke(MockMiniBlockHead));
        MOCKER_CPP(&UrmaIoBufferPool::get_local_target_seg).stubs().will(invoke(MockGetLocalTargetSeg));

        g_dummy_alloc.fixed_allocator_.mini_block_head_mask_ = ~(BLOCK_SIZE - 1UL);

        (void)LibcApi::Load(); /* ensure fcntl_ptr is not reloaded over our mock */
        g_origFcntlPtr = LibcApi::fcntl_ptr;
        LibcApi::fcntl_ptr = MockFcntlForTestFd;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        UrmaApi::urma_post_jetty_send_wr_ptr = nullptr;
        UrmaApi::urma_post_jetty_recv_wr_ptr = nullptr;
        if (dummy_jetty_ != nullptr) {
            free(dummy_jetty_);
            dummy_jetty_ = nullptr;
        }
        LibcApi::fcntl_ptr = g_origFcntlPtr;
        g_origFcntlPtr = nullptr;
    }

    SocketPtr MakeTestSocket()
    {
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->event_fd_ = 42;
        urma_sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_send_buf);
        urma_sock->send_buf_size_ = SEND_BUF_SIZE;
        urma_sock->send_buf_ = new (std::nothrow) UrmaSockRingBuf(SEND_BUF_SIZE);
        urma_sock->local_ctrl_target_seg_ = &g_local_tseg;
        urma_sock->remote_ctrl_target_seg_ = &g_remote_tseg;
        urma_sock->jetty_ = dummy_jetty_;
        urma_sock->rmt_recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_send_buf);
        return RefConvert<UrmaSocket, Socket>(urma_sock);
    }

    UrmaJetty *dummy_jetty_ = nullptr;
};

TEST_F(UrmaTxOpsTest, WriteV_InvalidIovcnt_ReturnsZero)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    struct iovec iov;
    iov.iov_base = g_send_buf;
    iov.iov_len = 1;

    auto ret = tx.WriteV(sock, &iov, 0, 0);
    EXPECT_EQ(0, ret);

    ret = tx.WriteV(sock, &iov, -1, 0);
    EXPECT_EQ(0, ret);
}

TEST_F(UrmaTxOpsTest, WriteV_AllNullIovBase_ReturnsZero)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    struct iovec iov[2];
    iov[0].iov_base = nullptr;
    iov[0].iov_len = 10;
    iov[1].iov_base = nullptr;
    iov[1].iov_len = 20;

    auto ret = tx.WriteV(sock, iov, 2, 0);
    EXPECT_EQ(0, ret);
}

TEST_F(UrmaTxOpsTest, WriteV_NullSendBuf_ReturnsEnotconn)
{
    UrmaTxOps tx(TEST_FD);
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    urma_sock->event_fd_ = 42;
    urma_sock->send_buf_ = nullptr;
    urma_sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_send_buf);
    urma_sock->local_ctrl_target_seg_ = &g_local_tseg;
    urma_sock->remote_ctrl_target_seg_ = &g_remote_tseg;
    urma_sock->jetty_ = dummy_jetty_;
    urma_sock->rmt_recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_send_buf);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);

    struct iovec iov;
    iov.iov_base = g_send_buf;
    iov.iov_len = 10;

    errno = 0;
    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOTCONN, errno);
}

TEST_F(UrmaTxOpsTest, WriteV_Closed_ReturnsEpipe)
{
    MOCKER(::eventfd_write).stubs().will(returnValue(0));

    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    ASSERT_EQ(0, urma_sock->NotifyClosed());

    struct iovec iov;
    iov.iov_base = g_send_buf;
    iov.iov_len = 10;

    errno = 0;
    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EPIPE, errno);
}

/* 对端终结 (CLOSE opcode / 连接级错误) 后本端写不可达 — 走真实触发路径锁定「对端终结 ⇒ 不可写」,
 * 而不是只依赖 NotifyClosed 注入的 HUP 位间接判断 */
TEST_F(UrmaTxOpsTest, WriteV_PeerClosedOrReset_ReturnsEpipe)
{
    MOCKER(::eventfd_write).stubs().will(returnValue(0));

    struct iovec iov;
    iov.iov_base = g_send_buf;
    iov.iov_len = 10;

    {
        SCOPED_TRACE("case: peer_close_opcode");
        UrmaTxOps tx(TEST_FD);
        auto sock = MakeTestSocket();
        auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
        WriteImmData imm;
        imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(imm);
        ASSERT_TRUE(urma_sock->IsPeerClosed());

        errno = 0;
        EXPECT_EQ(-1, tx.WriteV(sock, &iov, 1, 0));
        EXPECT_EQ(EPIPE, errno);
    }
    {
        SCOPED_TRACE("case: connection_level_error_reset");
        UrmaTxOps tx(TEST_FD);
        auto sock = MakeTestSocket();
        auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
        urma_sock->OnPeerReset();
        ASSERT_TRUE(urma_sock->IsPeerReset());

        errno = 0;
        EXPECT_EQ(-1, tx.WriteV(sock, &iov, 1, 0));
        EXPECT_EQ(EPIPE, errno);
    }
}

TEST_F(UrmaTxOpsTest, WriteV_LargeIo_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();

    /* 数据量需超过内联阈值(URMA_INLINE_DATA_THRESHOLD≈2000B)才走 PRE_WRITE 大 IO 路径 */
    const size_t data_len = 4096;
    char *data = AllocIoBlock();
    memset(data, 'X', data_len);
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = data_len;

    auto *block = reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(data) - sizeof(Block));
    int nshared_before = block->nshared.load();

    uint32_t flags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    auto ret = tx.WriteV(sock, &iov, 1, flags);
    EXPECT_EQ(static_cast<ssize_t>(data_len), ret);

    EXPECT_EQ(nshared_before + 1, block->nshared.load());

    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(1u, ctrl->head.data_count);
    auto *body = reinterpret_cast<PageBufferInMessage *>(ctrl + 1);
    EXPECT_EQ(reinterpret_cast<uint64_t>(data), body[0].addr);
    EXPECT_EQ(data_len, body[0].size);
    EXPECT_EQ(0u, body[0].flags);
}

TEST_F(UrmaTxOpsTest, WriteV_LargeIo_MultiBlock_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();

    const size_t len1 = 1500;
    const size_t len2 = 1500;
    char *data1 = AllocIoBlock();
    char *data2 = AllocIoBlock();
    memset(data1, 'A', len1);
    memset(data2, 'B', len2);
    struct iovec iov[2];
    iov[0].iov_base = data1;
    iov[0].iov_len = len1;
    iov[1].iov_base = data2;
    iov[1].iov_len = len2;

    auto *block1 = reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(data1) - sizeof(Block));
    auto *block2 = reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(data2) - sizeof(Block));

    uint32_t flags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    auto ret = tx.WriteV(sock, iov, 2, flags);
    EXPECT_EQ(static_cast<ssize_t>(len1 + len2), ret);

    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(2u, ctrl->head.data_count);
    auto *body = reinterpret_cast<PageBufferInMessage *>(ctrl + 1);
    EXPECT_EQ(reinterpret_cast<uint64_t>(data1), body[0].addr);
    EXPECT_EQ(len1, body[0].size);
    EXPECT_EQ(reinterpret_cast<uint64_t>(data2), body[1].addr);
    EXPECT_EQ(len2, body[1].size);
}

TEST_F(UrmaTxOpsTest, WriteV_LargeIo_PostWriteImmFail_ReturnsEagain)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrFail;

    char *data = AllocIoBlock();
    memset(data, 'X', 4096);
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = 4096;

    errno = 0;
    uint32_t flags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    auto ret = tx.WriteV(sock, &iov, 1, flags);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaTxOpsTest, WriteV_LargeIo_AllocFail_ReturnsEagain)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);

    char *data = AllocIoBlock();
    memset(data, 'X', 4096);
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = 4096;

    errno = 0;
    uint32_t flags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    auto ret = tx.WriteV(sock, &iov, 1, flags);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();

    char data[] = "hello";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(static_cast<ssize_t>(sizeof(data)), ret);

    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(static_cast<uint32_t>(sizeof(data)), ctrl->head.data_count);
    EXPECT_EQ(URMA_SND_BUFF_ALLOC_UNIT, ctrl->head.message_size);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data, sizeof(data)));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_NullIovSkipped_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();

    char data[] = "world";
    struct iovec iov[3];
    iov[0].iov_base = nullptr;
    iov[0].iov_len = 10;
    iov[1].iov_base = data;
    iov[1].iov_len = sizeof(data);
    iov[2].iov_base = nullptr;
    iov[2].iov_len = 20;

    auto ret = tx.WriteV(sock, iov, 3, 0);
    EXPECT_EQ(static_cast<ssize_t>(sizeof(data)), ret);

    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(static_cast<uint32_t>(sizeof(data)), ctrl->head.data_count);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data, sizeof(data)));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_MultiIov_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();

    char data1[] = "hello";
    char data2[] = "world";
    struct iovec iov[2];
    iov[0].iov_base = data1;
    iov[0].iov_len = sizeof(data1);
    iov[1].iov_base = data2;
    iov[1].iov_len = sizeof(data2);

    auto ret = tx.WriteV(sock, iov, 2, 0);
    size_t total = sizeof(data1) + sizeof(data2);
    EXPECT_EQ(static_cast<ssize_t>(total), ret);

    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(static_cast<uint32_t>(total), ctrl->head.data_count);
    char expected[sizeof(data1) + sizeof(data2)];
    memcpy(expected, data1, sizeof(data1));
    memcpy(expected + sizeof(data1), data2, sizeof(data2));
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, expected, sizeof(expected)));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_PostWriteImmFail_ReturnsEagain)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrFail;

    char data[] = "hello";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    errno = 0;
    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_RingBufWrap_Success)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);
    urma_sock->send_buf_->Release(pre_offset, SEND_BUF_SIZE - 64UL);

    char data[] = "abcde";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(static_cast<ssize_t>(sizeof(data)), ret);

    uintptr_t buf_offset = SEND_BUF_SIZE - 64UL;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + buf_offset);
    EXPECT_EQ(static_cast<uint32_t>(sizeof(data)), ctrl->head.data_count);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data, sizeof(data)));
}

TEST_F(UrmaTxOpsTest, WriteV_InlineAllocFail_ReturnsEagain)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);

    char data[] = "testdata";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    errno = 0;
    auto ret = tx.WriteV(sock, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_BlockTooSmall_SendsPayloadPrefix)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    ASSERT_TRUE(LeaveSendBufAvailable(urma_sock, 1088UL)); /* 1088 - 48 = 1040 payload bytes fit */

    std::vector<char> data(1522);
    FillTestPayload(data, 7);
    struct iovec iov;
    iov.iov_base = data.data();
    iov.iov_len = data.size();

    /* TEST_FD is mocked as non-blocking, so the caller gets the short write back */
    const auto ret = tx.WriteV(sock, &iov, 1, 0);

    EXPECT_EQ(static_cast<ssize_t>(1040), ret);
    EXPECT_EQ(0UL, urma_sock->send_buf_->Available());
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + (SEND_BUF_SIZE - 1088UL));
    EXPECT_EQ(1040U, ctrl->head.data_count); /* the copied prefix, never the requested total */
    EXPECT_EQ(1088U, ctrl->head.message_size);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data.data(), 1040U));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_BlockTooSmall_MinBlockBoundary)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    ASSERT_TRUE(LeaveSendBufAvailable(urma_sock, 1024UL));

    std::vector<char> data(2000);
    FillTestPayload(data, 11);
    struct iovec iov;
    iov.iov_base = data.data();
    iov.iov_len = data.size();

    const auto ret = tx.WriteV(sock, &iov, 1, 0);

    EXPECT_EQ(static_cast<ssize_t>(976), ret); /* 1024 - sizeof(UrmaMessageHead) */
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + (SEND_BUF_SIZE - 1024UL));
    EXPECT_EQ(976U, ctrl->head.data_count);
    EXPECT_EQ(1024U, ctrl->head.message_size);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data.data(), 976U));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_BlockEnough_SendsWholePayload)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    ASSERT_TRUE(LeaveSendBufAvailable(urma_sock, 2048UL));

    std::vector<char> data(1522);
    FillTestPayload(data, 3);
    struct iovec iov;
    iov.iov_base = data.data();
    iov.iov_len = data.size();

    const auto ret = tx.WriteV(sock, &iov, 1, 0);

    EXPECT_EQ(static_cast<ssize_t>(1522), ret);
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + (SEND_BUF_SIZE - 2048UL));
    EXPECT_EQ(1522U, ctrl->head.data_count);
    EXPECT_EQ(2048U, ctrl->head.message_size);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, data.data(), data.size()));
}

TEST_F(UrmaTxOpsTest, WriteV_Inline_BlockTooSmall_SplitsInsideSecondIov)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    ASSERT_TRUE(LeaveSendBufAvailable(urma_sock, 1088UL));

    std::vector<char> first(1000);
    std::vector<char> second(600);
    FillTestPayload(first, 1);
    FillTestPayload(second, 2);
    struct iovec iov[2];
    iov[0].iov_base = first.data();
    iov[0].iov_len = first.size();
    iov[1].iov_base = second.data();
    iov[1].iov_len = second.size();

    const auto ret = tx.WriteV(sock, iov, 2, 0);

    EXPECT_EQ(static_cast<ssize_t>(1040), ret);
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + (SEND_BUF_SIZE - 1088UL));
    EXPECT_EQ(1040U, ctrl->head.data_count);
    EXPECT_EQ(0, memcmp(ctrl->small_io.data, first.data(), first.size()));
    EXPECT_EQ(0, memcmp(ctrl->small_io.data + first.size(), second.data(), 40U));
}

/* A blocking fd must not surface the short write: the remainder goes out once the send area is
 * recycled, and the call returns only after the whole request is committed. */
TEST_F(UrmaTxOpsTest, WriteV_Blocking_ShortWrite_ContinuesUntilWholePayloadSent)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaTxOps tx(fd_pair[0]);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd for fcntl()/getsockopt() */

    const uint64_t preSize = SEND_BUF_SIZE - 1088UL;
    uint64_t preOffset = 0;
    ASSERT_TRUE(urma_sock->send_buf_->Allocate(preSize, preOffset));

    std::vector<char> data(1522);
    FillTestPayload(data, 5);
    struct iovec iov;
    iov.iov_base = data.data();
    iov.iov_len = data.size();

    g_postedCount.store(0);
    g_postedDataCount0.store(0);
    g_postedDataCount1.store(0);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrRecordPosts;

    ssize_t ret = 0;
    std::atomic<bool> done{false};
    std::thread worker([&]() {
        ret = tx.WriteV(sock, &iov, 1, 0);
        done.store(true, std::memory_order_release);
    });

    /* the first message carries the 1040 payload bytes the block can hold; the remaining 482 have
     * to wait for the send area to be recycled (peer ACK equivalent) */
    for (int i = 0; i < 500 && g_postedCount.load(std::memory_order_acquire) < 1U; i++) {
        usleep(1000);
    }
    EXPECT_EQ(1U, g_postedCount.load(std::memory_order_acquire));
    EXPECT_EQ(1040U, g_postedDataCount0.load(std::memory_order_acquire));
    EXPECT_FALSE(done.load(std::memory_order_acquire));

    urma_sock->send_buf_->Release(preOffset, preSize);
    urma_sock->NotifyBlockingWritable(); /* ACK-equivalent wakeup */
    worker.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(static_cast<ssize_t>(1522), ret); /* blocking fd: the whole request is committed */
    EXPECT_EQ(2U, g_postedCount.load(std::memory_order_acquire));
    EXPECT_EQ(482U, g_postedDataCount1.load(std::memory_order_acquire));

    /* the second message carries exactly the remaining slice - no duplicated bytes */
    auto *firstCtrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf + (SEND_BUF_SIZE - 1088UL));
    auto *secondCtrl = reinterpret_cast<UrmaCtrlMessage *>(g_send_buf);
    EXPECT_EQ(0, memcmp(firstCtrl->small_io.data, data.data(), 1040U));
    EXPECT_EQ(0, memcmp(secondCtrl->small_io.data, data.data() + 1040U, 482U));
}

TEST_F(UrmaTxOpsTest, WriteV_Blocking_WaitsForWritableThenSucceeds)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaTxOps tx(fd_pair[0]);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd for fcntl/getsockopt */

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset); /* fill send buf -> EAGAIN */

    char data[] = "blocking writev";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    ssize_t ret = 0;
    std::atomic<bool> done{false};
    std::thread worker([&]() {
        ret = tx.WriteV(sock, &iov, 1, 0);
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && urma_sock->block_waiters_.load() == 0; i++) {
        usleep(1000);
    }
    EXPECT_GT(urma_sock->block_waiters_.load(), 0U);
    EXPECT_FALSE(done.load(std::memory_order_acquire));

    urma_sock->send_buf_->Release(pre_offset, SEND_BUF_SIZE - 64UL);
    urma_sock->NotifyBlockingWritable(); /* ACK-equivalent wakeup */
    worker.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_GT(ret, 0);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());
}

TEST_F(UrmaTxOpsTest, WriteV_Blocking_LocalClose_ReturnsEpipe)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaTxOps tx(fd_pair[0]);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->raw_socket_ = fd_pair[0];

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);

    char data[] = "x";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    ssize_t ret = 0;
    int worker_errno = 0;
    std::atomic<bool> done{false};
    std::thread worker([&]() {
        errno = 0;
        ret = tx.WriteV(sock, &iov, 1, 0);
        worker_errno = errno;
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && urma_sock->block_waiters_.load() == 0; i++) {
        usleep(1000);
    }
    urma_sock->State(SOCK_STAT_CLOSE);
    urma_sock->OnLocalClose();
    worker.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EPIPE, worker_errno);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());
}

TEST_F(UrmaTxOpsTest, WriteV_Blocking_SndTimeout_ReturnsEagain)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    ASSERT_EQ(0, RawSetSockTimeout(fd_pair[0], SO_SNDTIMEO, 60000));

    UrmaTxOps tx(fd_pair[0]);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->raw_socket_ = fd_pair[0];

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);

    char data[] = "timeout";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    auto start = std::chrono::steady_clock::now();
    errno = 0;
    auto ret = tx.WriteV(sock, &iov, 1, 0);
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_GE(elapsed_ms, 40);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());
}

TEST_F(UrmaTxOpsTest, WriteV_Eagain_BufferFull_ClearsReachEpollOut)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetEvents(EPOLLOUT); /* 模拟已注册 EPOLLOUT */
    urma_sock->SetReachEpollEvents(EPOLLOUT);

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset);

    char data[] = "testdata";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    errno = 0;
    EXPECT_EQ(-1, tx.WriteV(sock, &iov, 1, 0));
    EXPECT_EQ(EAGAIN, errno);
    /* 发送缓冲仍满：EPOLLOUT 达位被清，LT 不再空转 */
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents() & EPOLLOUT);
}

TEST_F(UrmaTxOpsTest, WriteV_Eagain_PostFail_ClearsReachEpollOut_AndRearms)
{
    UrmaTxOps tx(TEST_FD);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrFail;
    MOCKER(::eventfd_write).stubs().will(returnValue(0));
    urma_sock->SetEvents(EPOLLOUT);
    urma_sock->SetReachEpollEvents(EPOLLOUT);

    char data[] = "hello";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    errno = 0;
    EXPECT_EQ(-1, tx.WriteV(sock, &iov, 1, 0));
    EXPECT_EQ(EAGAIN, errno);
    /* post 失败但发送缓冲已回滚（仍可写）：复查后 EPOLLOUT 达位重灌，LT 持续上报 */
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLOUT);
}

TEST_F(UrmaTxOpsTest, WriteV_DontWait_ReturnsEagainWithoutWaiting)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaTxOps tx(fd_pair[0]);
    auto sock = MakeTestSocket();
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd: only the flag can skip the wait */

    uint64_t pre_offset = 0;
    urma_sock->send_buf_->Allocate(SEND_BUF_SIZE - 64UL, pre_offset); /* fill send buf -> EAGAIN */

    char data[] = "dontwait writev";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    errno = 0;
    ssize_t ret = tx.WriteV(sock, &iov, 1, UBS_IO_FLAG_USE_BRPC_BLOCK | UBS_IO_FLAG_DONT_WAIT);

    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}
