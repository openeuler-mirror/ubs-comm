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

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_data_rx_ops.h"
#include "core/urma/urma_io_buf_pool.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_types.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const size_t BUF_SIZE = 4 * 1024 * 1024;
static const size_t ZC_BLOCK_SIZE = 8192;
constexpr size_t CTRL_OFF1 = URMA_SND_BUFF_ALLOC_UNIT;
constexpr size_t CTRL_OFF2 = 2 * URMA_SND_BUFF_ALLOC_UNIT;
static uint8_t g_recv_buf[BUF_SIZE];
alignas(8192) static uint8_t g_zc_mem[2 * ZC_BLOCK_SIZE];
static UrmaIoBufferPool g_dummy_alloc;
static int g_zc_block_idx = 0;
static urma_target_seg_t g_target_seg;

static urma_status_t MockPostReadWrSuccess(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_SUCCESS;
}

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

static Block *AllocZcBlock()
{
    int idx = g_zc_block_idx++;
    auto *block = new (g_zc_mem + idx * ZC_BLOCK_SIZE)
        Block(reinterpret_cast<char *>(g_zc_mem + idx * ZC_BLOCK_SIZE + sizeof(Block)), ZC_BLOCK_SIZE - sizeof(Block));
    block->nshared.store(100, std::memory_order_relaxed);
    return block;
}

static uintptr_t MockMiniBlockHead(uintptr_t addr)
{
    auto base = reinterpret_cast<uintptr_t>(g_zc_mem);
    if (addr >= base && addr < base + sizeof(g_zc_mem)) {
        return base + ((addr - base) / ZC_BLOCK_SIZE) * ZC_BLOCK_SIZE;
    }
    return g_dummy_alloc.fixed_allocator_.MiniBlockHead(addr);
}
} // namespace

class UrmaDataRxTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;

        memset(g_recv_buf, 0, BUF_SIZE);

        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->recv_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        urma_sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->send_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        sock_ = RefConvert<UrmaSocket, Socket>(urma_sock);

        MOCKER(::eventfd_write).stubs().will(returnValue(0));
        MOCKER(::eventfd).stubs().will(returnValue(42));
        MOCKER_CPP(&UrmaRxOps::ResponseCtrlMessage).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
        MOCKER_CPP(&UrmaBackend::GetAllocator).stubs().will(returnValue(&g_dummy_alloc));
        MOCKER_CPP(&UrmaIoBufferPool::get_mini_block_head).stubs().will(invoke(MockMiniBlockHead));

        g_dummy_alloc.fixed_allocator_.mini_block_head_mask_ = ~(ZC_BLOCK_SIZE - 1UL);

        g_zc_block_idx = 0;

        (void)LibcApi::Load(); /* ensure fcntl_ptr is not reloaded over our mock */
        g_origFcntlPtr = LibcApi::fcntl_ptr;
        LibcApi::fcntl_ptr = MockFcntlForTestFd;
    }

    void TearDown() override
    {
        g_dummy_alloc.fixed_allocator_.UnInit();
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sock_ = nullptr;
        LibcApi::fcntl_ptr = g_origFcntlPtr;
        g_origFcntlPtr = nullptr;
    }

    void PrepareSlot(uint64_t seq_no, uint64_t write_imm)
    {
        UrmaRxOps rx(TEST_FD);
        rx.DataReached(sock_, seq_no, write_imm);
    }

    SocketPtr sock_{nullptr};
};

TEST_F(UrmaDataRxTest, DataReached_SlotFree_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    auto result = rx.DataReached(sock_, 0, imm.data);
    EXPECT_EQ(UBS_OK, result);
}

TEST_F(UrmaDataRxTest, DataReached_SlotOccupied_ReturnsPrefillRx)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));
    auto result = rx.DataReached(sock_, 256, 0x2000);
    EXPECT_EQ(UBS_PREFILL_RX, result);
}

TEST_F(UrmaDataRxTest, DataReached_OutOfOrder_ReturnsDataOrder)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    auto result = rx.DataReached(sock_, 1, imm.data);
    EXPECT_EQ(UBS_LATER_SEQ, result);
}

TEST_F(UrmaDataRxTest, DataReached_StaleSlot_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));
    rx.consume_seq_no_.store(1, std::memory_order_release);

    auto result = rx.DataReached(sock_, 0, imm.data);
    EXPECT_EQ(UBS_LATER_SEQ, result);
}

TEST_F(UrmaDataRxTest, DataReached_SameSeqAsConsumed_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    auto result = rx.DataReached(sock_, 0, imm.data);
    EXPECT_EQ(UBS_OK, result);
}

TEST_F(UrmaDataRxTest, ReadV_NullIov_ReturnsEINVAL)
{
    UrmaRxOps rx(TEST_FD);
    errno = 0;
    auto ret = rx.ReadV(sock_, nullptr, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaDataRxTest, ReadV_ZeroIovcnt_ReturnsEINVAL)
{
    UrmaRxOps rx(TEST_FD);
    struct iovec iov;
    iov.iov_base = g_recv_buf + 128;
    iov.iov_len = 128;
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 0, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaDataRxTest, ReadV_NoData_ReturnsEAGAIN)
{
    UrmaRxOps rx(TEST_FD);
    uint8_t buf[128];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaDataRxTest, ReadV_ClosedNoData_ReturnsZeroEof)
{
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    ASSERT_EQ(0, urma_sock->NotifyClosed());

    UrmaRxOps rx(TEST_FD);
    uint8_t buf[128];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(0, ret);
}

TEST_F(UrmaDataRxTest, ReadV_ClosedWithData_DrainsThenEof)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    ctrl->head.data_count = body_size;
    const char test_data[] = "0123456789";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = CTRL_OFF1 / URMA_SND_BUFF_ALLOC_UNIT;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    ASSERT_EQ(0, urma_sock->NotifyClosed());

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    EXPECT_EQ(static_cast<ssize_t>(body_size), rx.ReadV(sock_, &iov, 1, 0));

    errno = 0;
    EXPECT_EQ(0, rx.ReadV(sock_, &iov, 1, 0));
}

TEST_F(UrmaDataRxTest, ReadV_SingleMsgSingleIov_ReturnsFullData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    const uint32_t buf_unit = 1; /* ctrl 消息按 URMA_SND_BUFF_ALLOC_UNIT 对齐放置 */
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl->head.data_count = body_size;
    const char test_data[] = "0123456789";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = buf_unit;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(body_size, ret);
    EXPECT_EQ(0, memcmp(buf, test_data, body_size));
}

TEST_F(UrmaDataRxTest, ReadV_MultiMsgMultiIov_ReturnsTotalData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body1 = 5;
    const uint32_t body2 = 5;
    const uint32_t buf_unit1 = 1;
    const uint32_t buf_unit2 = 2;
    auto *ctrl1 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit1 * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl1->head.data_count = body1;
    memcpy(ctrl1->small_io.data, "hello", body1);
    WriteImmData imm1;
    imm1.ubsocket.opcode = WRITE_IN_BAND;
    imm1.ubsocket.buffer_offset = buf_unit1;

    auto *ctrl2 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit2 * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl2->head.data_count = body2;
    memcpy(ctrl2->small_io.data, "world", body2);
    WriteImmData imm2;
    imm2.ubsocket.opcode = WRITE_IN_BAND;
    imm2.ubsocket.buffer_offset = buf_unit2;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm1.data));
    ASSERT_EQ(UBS_LATER_SEQ, rx.DataReached(sock_, 1, imm2.data));

    uint8_t buf1[3] = {};
    uint8_t buf2[64] = {};
    struct iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);

    auto ret = rx.ReadV(sock_, iov, 2, 0);
    EXPECT_EQ(static_cast<ssize_t>(body1 + body2), ret);
    EXPECT_EQ(0, memcmp(buf1, "hel", 3));
    EXPECT_EQ(0, memcmp(buf2, "loworld", 7));
}

TEST_F(UrmaDataRxTest, ReadV_PartialRead_ReturnsPartialData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 20;
    const uint32_t buf_unit = 1;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl->head.data_count = body_size;
    const char test_data[] = "0123456789ABCDEFGHIJ";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = buf_unit;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    uint8_t buf[10] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(10, ret);
    EXPECT_EQ(0, memcmp(buf, test_data, 10));
}

TEST_F(UrmaDataRxTest, ReadV_LaterSeqArrivedFirst_ReturnsEAGAIN)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF2);
    ctrl->head.data_count = body_size;
    memcpy(ctrl->small_io.data, "ABCDEFGHIJ", body_size);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = CTRL_OFF2 / URMA_SND_BUFF_ALLOC_UNIT;

    ASSERT_EQ(UBS_LATER_SEQ, rx.DataReached(sock_, 1, imm.data));

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaDataRxTest, ZeroCopyRead_Success_LinksBlocksAndSendsPostWrite)
{
    auto *cache_block = AllocZcBlock();
    auto *iov_block = AllocZcBlock();
    const char test_data[] = "hello_zero_copy!";
    memcpy(cache_block->data, test_data, sizeof(test_data));
    cache_block->size = sizeof(test_data);
    cache_block->cap = sizeof(test_data);

    UrmaRxOps rx(TEST_FD);

    auto *slot = rx.InsertSlot(0);
    ASSERT_NE(slot, nullptr);
    WriteImmData imm;
    imm.ubsocket.opcode = PRE_WRITE;
    imm.ubsocket.buffer_offset = 0;
    slot->write_imm = imm.data;
    slot->data_block = cache_block;
    slot->rx_state.store(URMA_RX_STS_DATA_READY, std::memory_order_release);
    rx.consume_seq_no_.store(0, std::memory_order_relaxed);

    struct iovec zc_iov;
    zc_iov.iov_base = iov_block->data;
    zc_iov.iov_len = 64;

    auto zc_ret = rx.ReadV(sock_, &zc_iov, 1, UBS_IO_FLAG_USE_BRPC_BLOCK);
    EXPECT_GT(zc_ret, 0);
    EXPECT_NE(iov_block->GetNext(), static_cast<void *>(nullptr));
    /* fully drained: the head slot is retired and the sequence advances */
    EXPECT_EQ(rx.FindSlot(0), nullptr);
    EXPECT_EQ(rx.consume_seq_no_.load(), 1ULL);
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, ZeroCopyRead_NotDataReady_ReturnsEagain)
{
    auto *iov_block = AllocZcBlock();

    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = PRE_WRITE;
    auto *slot = rx.InsertSlot(0);
    ASSERT_NE(slot, nullptr);
    slot->write_imm = imm.data;
    slot->rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);
    rx.consume_seq_no_.store(0, std::memory_order_relaxed);

    struct iovec zc_iov;
    zc_iov.iov_base = iov_block->data;
    zc_iov.iov_len = 64;

    errno = 0;
    auto zc_ret = rx.ReadV(sock_, &zc_iov, 1, UBS_IO_FLAG_USE_BRPC_BLOCK);
    EXPECT_EQ(-1, zc_ret);
    EXPECT_EQ(EAGAIN, errno);
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, DataReadFinished_WrongState_ReturnsError)
{
    UrmaRxOps rx(TEST_FD);
    ASSERT_NE(rx.InsertSlot(0), nullptr); /* defaults to IDLE */

    auto result = rx.DataReadFinished(sock_, 0);
    EXPECT_EQ(UBS_ERROR, result);
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, DataReadFinished_SeqMatchesConsumed_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    rx.consume_seq_no_.store(5, std::memory_order_relaxed);
    auto *slot = rx.InsertSlot(5);
    ASSERT_NE(slot, nullptr);
    slot->rx_state.store(URMA_RX_STS_READING, std::memory_order_release);

    auto result = rx.DataReadFinished(sock_, 5);
    EXPECT_EQ(UBS_OK, result);
    EXPECT_EQ(URMA_RX_STS_DATA_READY, slot->rx_state.load());
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, DataReadFinished_SeqNotConsumed_ReturnsLaterSeq)
{
    UrmaRxOps rx(TEST_FD);
    rx.consume_seq_no_.store(3, std::memory_order_relaxed);
    auto *slot = rx.InsertSlot(7);
    ASSERT_NE(slot, nullptr);
    slot->rx_state.store(URMA_RX_STS_READING, std::memory_order_release);

    auto result = rx.DataReadFinished(sock_, 7);
    EXPECT_EQ(UBS_LATER_SEQ, result);
    EXPECT_EQ(URMA_RX_STS_DATA_READY, slot->rx_state.load());
    GlobalMockObject::verify();
}

/*
 * Dispatcher (QueueRxDispatcher) mode: poller-side entries only enqueue + wake (UBS_DEFERRED);
 * ReadV drains the queue and performs the real processing on the consumer thread.
 */
class UrmaDataRxDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;

        memset(g_recv_buf, 0, BUF_SIZE);

        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->recv_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        urma_sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->send_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        sock_ = RefConvert<UrmaSocket, Socket>(urma_sock);

        saved_mode_ = UrmaSetting::UB_EPOLL_HANDLE_MODE;
        UrmaSetting::UB_EPOLL_HANDLE_MODE = DISPATCHER_THREAD_POOL;

        MOCKER(::eventfd_write).stubs().will(returnValue(0));
        MOCKER(::eventfd).stubs().will(returnValue(42));
        MOCKER_CPP(&UrmaRxOps::ResponseCtrlMessage).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
        MOCKER_CPP(&UrmaBackend::GetAllocator).stubs().will(returnValue(&g_dummy_alloc));
        MOCKER_CPP(&UrmaIoBufferPool::get_mini_block_head).stubs().will(invoke(MockMiniBlockHead));

        g_dummy_alloc.fixed_allocator_.mini_block_head_mask_ = ~(ZC_BLOCK_SIZE - 1UL);
        g_zc_block_idx = 0;

        (void)LibcApi::Load(); /* ensure fcntl_ptr is not reloaded over our mock */
        g_origFcntlPtr = LibcApi::fcntl_ptr;
        LibcApi::fcntl_ptr = MockFcntlForTestFd;
    }

    void TearDown() override
    {
        UrmaSetting::UB_EPOLL_HANDLE_MODE = saved_mode_;
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sock_ = nullptr;
        LibcApi::fcntl_ptr = g_origFcntlPtr;
        g_origFcntlPtr = nullptr;
    }

    EpollHandleMode saved_mode_;
    SocketPtr sock_{nullptr};
};

TEST_F(UrmaDataRxDispatcherTest, DataReached_DefersAndLeavesSlotIdle)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;

    auto result = rx.DataReached(sock_, 0, imm.data);
    EXPECT_EQ(UBS_DEFERRED, result);
    /* poller side never creates the slot: state machine is advanced only by the ReadV drain */
    EXPECT_EQ(rx.FindSlot(0), nullptr);
}

TEST_F(UrmaDataRxDispatcherTest, DataReadFinished_DefersReturnsDeferred)
{
    UrmaRxOps rx(TEST_FD);

    auto result = rx.DataReadFinished(sock_, 0);
    EXPECT_EQ(UBS_DEFERRED, result);

    /* drain runs on ReadV; the deferred event has no READING slot, so ReadV yields EAGAIN */
    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaDataRxDispatcherTest, ReadV_DrainsDeferredInlineMessage_ReturnsData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    const uint32_t buf_unit = 1;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl->head.data_count = body_size;
    ctrl->head.sequence_no = 0; /* seq re-read by the drain from recv_buf */
    const char test_data[] = "0123456789";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = buf_unit;

    ASSERT_EQ(UBS_DEFERRED, rx.DataReached(sock_, 0, imm.data));
    ASSERT_EQ(rx.FindSlot(0), nullptr);

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(body_size, ret);
    EXPECT_EQ(0, memcmp(buf, test_data, body_size));
}

TEST_F(UrmaDataRxDispatcherTest, ReadV_DrainsMultipleDeferredMessages_InOrder)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t buf_unit1 = 1;
    const uint32_t buf_unit2 = 2;
    auto *ctrl1 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit1 * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl1->head.data_count = 5;
    ctrl1->head.sequence_no = 0;
    memcpy(ctrl1->small_io.data, "hello", 5);
    WriteImmData imm1;
    imm1.ubsocket.opcode = WRITE_IN_BAND;
    imm1.ubsocket.buffer_offset = buf_unit1;

    auto *ctrl2 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit2 * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl2->head.data_count = 5;
    ctrl2->head.sequence_no = 1;
    memcpy(ctrl2->small_io.data, "world", 5);
    WriteImmData imm2;
    imm2.ubsocket.opcode = WRITE_IN_BAND;
    imm2.ubsocket.buffer_offset = buf_unit2;

    ASSERT_EQ(UBS_DEFERRED, rx.DataReached(sock_, 0, imm1.data));
    ASSERT_EQ(UBS_DEFERRED, rx.DataReached(sock_, 1, imm2.data));

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(10, ret);
    EXPECT_EQ(0, memcmp(buf, "helloworld", 10));
}

TEST_F(UrmaDataRxDispatcherTest, ReadV_Blocking_WakesAfterDeferredDataReached)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaRxOps rx(fd_pair[0]);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd for fcntl/getsockopt */

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    ssize_t ret = 0;
    std::atomic<bool> done{false};
    std::thread reader([&]() {
        ret = rx.ReadV(sock_, &iov, 1, 0);
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && urma_sock->block_waiters_.load() == 0; i++) {
        usleep(1000);
    }
    EXPECT_GT(urma_sock->block_waiters_.load(), 0U);
    EXPECT_FALSE(done.load(std::memory_order_acquire));

    const uint32_t buf_unit = 1;
    const char test_data[] = "0123456789";
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl->head.data_count = sizeof(test_data) - 1;
    ctrl->head.sequence_no = 0; /* re-read by the drain from recv_buf */
    memcpy(ctrl->small_io.data, test_data, sizeof(test_data) - 1);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = buf_unit;

    /* Queue dispatcher: only enqueues + wakes; slot is advanced by the ReadV drain. */
    ASSERT_EQ(UBS_DEFERRED, rx.DataReached(sock_, 0, imm.data));
    reader.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(static_cast<ssize_t>(sizeof(test_data) - 1), ret);
    EXPECT_EQ(0, memcmp(buf, test_data, sizeof(test_data) - 1));
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}

TEST_F(UrmaDataRxTest, ReadV_Blocking_WaitsForDataThenReturns)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaRxOps rx(fd_pair[0]);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd for fcntl/getsockopt */

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    ssize_t ret = 0;
    std::atomic<bool> done{false};
    std::thread reader([&]() {
        ret = rx.ReadV(sock_, &iov, 1, 0);
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && urma_sock->block_waiters_.load() == 0; i++) {
        usleep(1000);
    }
    EXPECT_GT(urma_sock->block_waiters_.load(), 0U);
    EXPECT_FALSE(done.load(std::memory_order_acquire));

    const uint32_t buf_unit = 1;
    const char test_data[] = "0123456789";
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + buf_unit * URMA_SND_BUFF_ALLOC_UNIT);
    ctrl->head.data_count = sizeof(test_data) - 1;
    memcpy(ctrl->small_io.data, test_data, sizeof(test_data) - 1);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = buf_unit;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));
    urma_sock->NotifySocketReadable(); /* data-arrival wakeup */
    reader.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(static_cast<ssize_t>(sizeof(test_data) - 1), ret);
    EXPECT_EQ(0, memcmp(buf, test_data, sizeof(test_data) - 1));
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}

TEST_F(UrmaDataRxTest, ReadV_Blocking_LocalClose_ReturnsEbadf)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaRxOps rx(fd_pair[0]);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->raw_socket_ = fd_pair[0];

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    ssize_t ret = 0;
    int worker_errno = 0;
    std::atomic<bool> done{false};
    std::thread reader([&]() {
        errno = 0;
        ret = rx.ReadV(sock_, &iov, 1, 0);
        worker_errno = errno;
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && urma_sock->block_waiters_.load() == 0; i++) {
        usleep(1000);
    }
    urma_sock->State(SOCK_STAT_CLOSE);
    urma_sock->OnLocalClose();
    reader.join();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EBADF, worker_errno);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}

TEST_F(UrmaDataRxTest, ReadV_Blocking_RcvTimeout_ReturnsEagain)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    ASSERT_EQ(0, RawSetSockTimeout(fd_pair[0], SO_RCVTIMEO, 60000));

    UrmaRxOps rx(fd_pair[0]);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->raw_socket_ = fd_pair[0];

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    auto start = std::chrono::steady_clock::now();
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_GE(elapsed_ms, 40);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}

TEST_F(UrmaDataRxTest, ReadV_NoData_ClearsReachEpollIn)
{
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->SetEvents(EPOLLIN); /* 模拟已注册 EPOLLIN */
    urma_sock->SetReachEpollEvents(EPOLLIN);
    UrmaRxOps rx(TEST_FD);
    uint8_t buf[128];
    struct iovec iov {
        buf, sizeof(buf)
    };
    errno = 0;
    EXPECT_EQ(-1, rx.ReadV(sock_, &iov, 1, 0));
    EXPECT_EQ(EAGAIN, errno);
    /* 无数据：EPOLLIN 达位被清，LT 不再空转 */
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents() & EPOLLIN);
}

TEST_F(UrmaDataRxTest, ReadV_ConsumeAll_ClearsReachEpollIn)
{
    UrmaRxOps rx(TEST_FD);
    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    ctrl->head.data_count = body_size;
    memcpy(ctrl->small_io.data, "0123456789", body_size);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = CTRL_OFF1 / URMA_SND_BUFF_ALLOC_UNIT;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->SetEvents(EPOLLIN);
    urma_sock->SetReachEpollEvents(EPOLLIN);

    uint8_t buf[64] = {};
    struct iovec iov {
        buf, sizeof(buf)
    };
    EXPECT_EQ(static_cast<ssize_t>(body_size), rx.ReadV(sock_, &iov, 1, 0));
    /* 一次读完：EPOLLIN 达位被清 */
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents() & EPOLLIN);
}

TEST_F(UrmaDataRxTest, ReadV_PartialRead_KeepsReachEpollIn)
{
    UrmaRxOps rx(TEST_FD);
    const uint32_t body_size = 20;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    ctrl->head.data_count = body_size;
    memcpy(ctrl->small_io.data, "0123456789ABCDEFGHIJ", body_size);
    WriteImmData imm;
    imm.ubsocket.opcode = WRITE_IN_BAND;
    imm.ubsocket.buffer_offset = CTRL_OFF1 / URMA_SND_BUFF_ALLOC_UNIT;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->SetEvents(EPOLLIN);
    urma_sock->SetReachEpollEvents(EPOLLIN);

    uint8_t buf[10] = {};
    struct iovec iov {
        buf, sizeof(buf)
    };
    EXPECT_EQ(10, rx.ReadV(sock_, &iov, 1, 0));
    /* 剩余数据未读完：EPOLLIN 达位经复查补灌保留（LT 持续上报直至读完） */
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLIN);
}

TEST_F(UrmaDataRxTest, ReadV_DontWait_ReturnsEagainWithoutWaiting)
{
    int fd_pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair));

    UrmaRxOps rx(fd_pair[0]);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->raw_socket_ = fd_pair[0]; /* real blocking fd: only the flag can skip the wait */

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    errno = 0;
    ssize_t ret = rx.ReadV(sock_, &iov, 1, UBS_IO_FLAG_USE_BRPC_BLOCK | UBS_IO_FLAG_DONT_WAIT);

    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_EQ(0U, urma_sock->block_waiters_.load());

    ASSERT_EQ(0, ::close(fd_pair[0]));
    ASSERT_EQ(0, ::close(fd_pair[1]));
}

/* ===================== CLOSE opcode 的 ReadV 语义 ===================== */

/* graceful CLOSE 到达且无数据: readv 返回 0 (对齐 TCP EOF) */
TEST_F(UrmaDataRxTest, ReadV_PeerClosedNoData_ReturnsZeroEof)
{
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    WriteImmData imm;
    imm.ubsocket.opcode = CLOSE;
    urma_sock->HandleRecvCqe(imm);

    UrmaRxOps rx(TEST_FD);
    uint8_t buf[64] = {};
    struct iovec iov {
        buf, sizeof(buf)
    };
    errno = 0;
    EXPECT_EQ(0, rx.ReadV(sock_, &iov, 1, 0));
}

/* 有序关闭: CLOSE 前有滞留数据 — 先读尽数据, 再收 EOF (对齐 TCP FIN 语义) */
TEST_F(UrmaDataRxTest, ReadV_PeerClosedWithData_DrainsInOrderThenEof)
{
    UrmaRxOps rx(TEST_FD);

    /* 数据先到 (seq 0) */
    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    ctrl->head.data_count = body_size;
    memcpy(ctrl->small_io.data, "0123456789", body_size);
    WriteImmData data_imm;
    data_imm.ubsocket.opcode = WRITE_IN_BAND;
    data_imm.ubsocket.buffer_offset = CTRL_OFF1 / URMA_SND_BUFF_ALLOC_UNIT;
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, data_imm.data));

    /* CLOSE 后到 (RDMA 队列有序, 必然排在数据之后) */
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    WriteImmData close_imm;
    close_imm.ubsocket.opcode = CLOSE;
    urma_sock->HandleRecvCqe(close_imm);

    uint8_t buf[64] = {};
    struct iovec iov {
        buf, sizeof(buf)
    };
    EXPECT_EQ(static_cast<ssize_t>(body_size), rx.ReadV(sock_, &iov, 1, 0)); /* 先读尽滞留数据 */
    EXPECT_EQ(0, memcmp(buf, "0123456789", body_size));

    errno = 0;
    EXPECT_EQ(0, rx.ReadV(sock_, &iov, 1, 0)); /* 数据排空后 EOF */
}

/* 错误 CQE (reset) 路径: readv 返回 -1/ECONNRESET, 不误报 EOF */
TEST_F(UrmaDataRxTest, ReadV_PeerReset_ReturnsEconnreset)
{
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock_);
    urma_sock->OnPeerReset();

    UrmaRxOps rx(TEST_FD);
    uint8_t buf[64] = {};
    struct iovec iov {
        buf, sizeof(buf)
    };
    errno = 0;
    EXPECT_EQ(-1, rx.ReadV(sock_, &iov, 1, 0));
    EXPECT_EQ(ECONNRESET, errno);
}

TEST_F(UrmaDataRxTest, SlotList_InsertFindRemoveAndRetireInOrder)
{
    UrmaRxOps rx(TEST_FD);
    auto *slot2 = rx.InsertSlot(2);
    auto *slot0 = rx.InsertSlot(0);
    auto *slot1 = rx.InsertSlot(1);
    ASSERT_NE(nullptr, slot0);
    ASSERT_NE(nullptr, slot1);
    ASSERT_NE(nullptr, slot2);
    EXPECT_EQ(slot0, rx.HeadSlot());
    EXPECT_EQ(slot1, rx.FindSlot(1));
    EXPECT_EQ(slot1, rx.InsertSlot(1));
    EXPECT_EQ(nullptr, rx.FindSlot(3));

    rx.RemoveSlot(slot1);
    EXPECT_EQ(nullptr, rx.FindSlot(1));
    rx.RetireHead(slot2);
    EXPECT_EQ(slot0, rx.HeadSlot());
    rx.RetireHead(slot0);
    EXPECT_EQ(1u, rx.consume_seq_no_.load());
    EXPECT_EQ(slot2, rx.HeadSlot());
    rx.ReleaseAllSlots();
    EXPECT_EQ(nullptr, rx.HeadSlot());
}

TEST_F(UrmaDataRxTest, SlotList_NullOperationsAreNoOps)
{
    UrmaRxOps rx(TEST_FD);
    rx.ReleaseSlot(nullptr);
    rx.RemoveSlot(nullptr);
    rx.RetireHead(nullptr);
    rx.ReleaseAllSlots();
    EXPECT_EQ(nullptr, rx.HeadSlot());
}

TEST_F(UrmaDataRxTest, ConsumeBlockDataSupportsPartialAndFullReads)
{
    UrmaRxOps rx(TEST_FD);
    uint8_t source[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    Block block(reinterpret_cast<char *>(source), sizeof(source));
    block.nshared.store(2, std::memory_order_relaxed);
    UrmaRxOps::DataSlot slot;
    slot.seq_no = 0;
    slot.data_block = &block;
    WriteImmData imm{PRE_WRITE, 0};
    slot.write_imm = imm.data;

    uint8_t first[5]{};
    struct iovec firstIov = {first, sizeof(first)};
    UrmaRxOps::IovWriter firstWriter{};
    firstWriter.iov = &firstIov;
    firstWriter.iovcnt = 1;
    EXPECT_FALSE(rx.ConsumeBlockData(sock_, slot, firstWriter, 0));
    EXPECT_EQ(5u, firstWriter.written);
    EXPECT_EQ(5u, rx.consume_offset_);

    uint8_t second[7]{};
    struct iovec secondIov = {second, sizeof(second)};
    UrmaRxOps::IovWriter secondWriter{};
    secondWriter.iov = &secondIov;
    secondWriter.iovcnt = 1;
    EXPECT_TRUE(rx.ConsumeBlockData(sock_, slot, secondWriter, 0));
    EXPECT_EQ(7u, secondWriter.written);
    EXPECT_EQ(0, std::memcmp(source, first, sizeof(first)));
    EXPECT_EQ(0, std::memcmp(source + sizeof(first), second, sizeof(second)));
    slot.data_block = nullptr;
}

TEST_F(UrmaDataRxTest, ConsumeBlockDataRejectsMissingBlock)
{
    UrmaRxOps rx(TEST_FD);
    UrmaRxOps::DataSlot slot;
    uint8_t output[4]{};
    struct iovec iov = {output, sizeof(output)};
    UrmaRxOps::IovWriter writer{};
    writer.iov = &iov;
    writer.iovcnt = 1;
    EXPECT_FALSE(rx.ConsumeBlockData(sock_, slot, writer, 0));
}

TEST_F(UrmaDataRxTest, ConsumeInlineDataSupportsPartialAndFullReads)
{
    UrmaRxOps rx(TEST_FD);
    WriteImmData imm{WRITE_IN_BAND, 1};
    auto *message = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    message->head.data_count = 10;
    auto *body = g_recv_buf + CTRL_OFF1 + URMA_INLINE_MESSAGE_CTRL_COST;
    std::memcpy(body, "0123456789", 10);
    UrmaRxOps::DataSlot slot;
    slot.seq_no = 0;
    slot.write_imm = imm.data;

    char first[4]{};
    struct iovec firstIov = {first, sizeof(first)};
    UrmaRxOps::IovWriter firstWriter{};
    firstWriter.iov = &firstIov;
    firstWriter.iovcnt = 1;
    EXPECT_FALSE(rx.ConsumeInlineData(sock_, slot, firstWriter, 0));
    EXPECT_EQ(4u, rx.consume_offset_);

    char second[6]{};
    struct iovec secondIov = {second, sizeof(second)};
    UrmaRxOps::IovWriter secondWriter{};
    secondWriter.iov = &secondIov;
    secondWriter.iovcnt = 1;
    EXPECT_TRUE(rx.ConsumeInlineData(sock_, slot, secondWriter, 0));
    EXPECT_EQ(std::string("0123"), std::string(first, sizeof(first)));
    EXPECT_EQ(std::string("456789"), std::string(second, sizeof(second)));
}

TEST_F(UrmaDataRxTest, TriggerPollInAndTraceTimeCoverStateTransitions)
{
    UrmaRxOps rx(TEST_FD);
    EXPECT_EQ(UBS_OK, rx.TriggerPollIn(sock_, 0));
    auto saved = ubsocket_prof_enabled;
    ubsocket_prof_enabled = 1;
    auto *slot = rx.InsertSlot(0);
    ASSERT_NE(nullptr, slot);
    slot->start_read_time = 1234;
    slot->rx_state.store(URMA_RX_STS_DATA_READY, std::memory_order_release);
    EXPECT_EQ(1234u, rx.CurrentTraceNsTime());
    slot->rx_state.store(URMA_RX_STS_READING, std::memory_order_release);
    EXPECT_EQ(0u, rx.CurrentTraceNsTime());
    ubsocket_prof_enabled = saved;
}

TEST_F(UrmaDataRxTest, AllocBlocksForRequestReturnsNullWhenAllocatorIsNotInitialized)
{
    UrmaRxOps rx(TEST_FD);
    EXPECT_EQ(nullptr, rx.AllocBlocksForReq(300));
}

TEST_F(UrmaDataRxTest, BuildReadWrsSplitsRemotePagesAcrossLocalBlocks)
{
    using GetLocalSeg = urma_target_seg_t *(UrmaIoBufferPool::*)(UrmaContextPtr, uintptr_t) const noexcept;
    using GetRemoteSeg = urma_target_seg_t *(UrmaIoBufferPool::*)(RemoteSegmentsPtr &, uintptr_t) const noexcept;
    MOCKER_CPP((static_cast<GetLocalSeg>(&UrmaIoBufferPool::get_local_target_seg)))
        .stubs()
        .will(returnValue(&g_target_seg));
    MOCKER_CPP((static_cast<GetRemoteSeg>(&UrmaIoBufferPool::get_remote_target_seg)))
        .stubs()
        .will(returnValue(&g_target_seg));

    auto *message = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    message->head.data_count = 2;
    auto *pages = reinterpret_cast<PageBufferInMessage *>(g_recv_buf + CTRL_OFF1 + sizeof(UrmaMessageHead));
    pages[0].addr = 0x1000;
    pages[0].size = 7;
    pages[1].addr = 0x2000;
    pages[1].size = 5;

    uint8_t localData1[8]{};
    uint8_t localData2[8]{};
    Block block1(reinterpret_cast<char *>(localData1), sizeof(localData1));
    Block block2(reinterpret_cast<char *>(localData2), sizeof(localData2));
    block1.SetNext(&block2);
    block1.nshared.store(100, std::memory_order_relaxed);
    block2.nshared.store(100, std::memory_order_relaxed);

    auto *urmaSocket = static_cast<UrmaSocket *>(sock_.Get());
    urmaSocket->remote_target_segs_ = MakeRef<RemoteSegments>();
    UrmaRxOps::BuildReadWrCtx ctx{&g_dummy_alloc, urmaSocket, 123};
    UrmaSetting::UB_USING_BOUNDING_DEV = true;
    UrmaRxOps rx(TEST_FD);
    rx.BuildReadWrs(2, CTRL_OFF1, &block1, ctx);

    ASSERT_EQ(3u, ctx.wrs.size());
    EXPECT_EQ(&ctx.wrs[1], ctx.wrs[0].next);
    EXPECT_EQ(&ctx.wrs[2], ctx.wrs[1].next);
    EXPECT_EQ(nullptr, ctx.wrs[2].next);
    EXPECT_EQ(123u, ctx.wrs[2].user_ctx);
    EXPECT_EQ(1u, ctx.wrs[2].flag.bs.complete_enable);
    EXPECT_EQ(12u, ctx.src_sges[0].len + ctx.src_sges[1].len + ctx.src_sges[2].len);
}

TEST_F(UrmaDataRxTest, IssueReadRequestAllocatesBlocksBuildsWrsAndPosts)
{
    using GetLocalSeg = urma_target_seg_t *(UrmaIoBufferPool::*)(UrmaContextPtr, uintptr_t) const noexcept;
    using GetRemoteSeg = urma_target_seg_t *(UrmaIoBufferPool::*)(RemoteSegmentsPtr &, uintptr_t) const noexcept;
    using StoreRemoteSeg = ock::ubs::Result (UrmaIoBufferPool::*)(UrmaContextPtr, RemoteSegmentsPtr &, urma_eid_t &,
                                                                  uint32_t, uintptr_t, uint32_t) noexcept;
    MOCKER_CPP((static_cast<GetLocalSeg>(&UrmaIoBufferPool::get_local_target_seg)))
        .stubs()
        .will(returnValue(&g_target_seg));
    MOCKER_CPP((static_cast<GetRemoteSeg>(&UrmaIoBufferPool::get_remote_target_seg)))
        .stubs()
        .will(returnValue(&g_target_seg));
    MOCKER_CPP((static_cast<StoreRemoteSeg>(&UrmaIoBufferPool::store_remote_target_seg)))
        .stubs()
        .will(returnValue(static_cast<int32_t>(UBS_OK)));

    allocator::FixedAllocatorOptions options;
    options.pool_space_start_tb = 104;
    options.pool_space_size_gb = 2;
    options.block_size_mb = 2;
    options.init_block_count = 1;
    options.mini_block_size_kb = 8;
    options.expanded_callback = [](uintptr_t, uint64_t, uint32_t) {
        return UBS_OK;
    };
    g_dummy_alloc.options_ = options;
    ASSERT_EQ(UBS_OK, g_dummy_alloc.fixed_allocator_.Init(options));

    auto *urmaSocket = static_cast<UrmaSocket *>(sock_.Get());
    auto *rawJetty = static_cast<UrmaJetty *>(malloc(sizeof(UrmaJetty)));
    ASSERT_NE(nullptr, rawJetty);
    memset(rawJetty, 0, sizeof(UrmaJetty));
    rawJetty->ref_count_ = 1;
    urmaSocket->jetty_ = rawJetty;
    urmaSocket->remote_target_segs_ = MakeRef<RemoteSegments>();
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostReadWrSuccess;

    auto *message = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + CTRL_OFF1);
    message->head.data_count = 2;
    auto *pages = reinterpret_cast<PageBufferInMessage *>(g_recv_buf + CTRL_OFF1 + sizeof(UrmaMessageHead));
    pages[0].addr = 0x100000;
    pages[0].size = 5000;
    pages[0].flags = 7;
    pages[1].addr = 0x200000;
    pages[1].size = 5000;
    pages[1].flags = 8;

    UrmaRxOps rx(TEST_FD);
    UrmaRxOps::DataSlot slot;
    WriteImmData imm{PRE_WRITE, CTRL_OFF1 / URMA_SND_BUFF_ALLOC_UNIT};
    EXPECT_EQ(UBS_OK, rx.IssueReadRequest(sock_, 9, imm.data, slot));
    EXPECT_EQ(URMA_RX_STS_READING, slot.rx_state.load(std::memory_order_acquire));
    ASSERT_NE(nullptr, slot.data_block);
    EXPECT_NE(nullptr, slot.data_block->GetNext());

    rx.ReleaseBlockInSlot(slot);
    urmaSocket->jetty_ = nullptr;
    free(rawJetty);
    UrmaApi::urma_post_jetty_send_wr_ptr = nullptr;
}
