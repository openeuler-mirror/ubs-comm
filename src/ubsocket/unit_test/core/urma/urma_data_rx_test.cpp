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
    return 0;
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

    auto &slot = rx.data_slots_[0];
    WriteImmData imm;
    imm.ubsocket.opcode = PRE_WRITE;
    imm.ubsocket.buffer_offset = 0;
    slot.write_imm = imm.data;
    slot.seq_no = 0;
    slot.data_block = cache_block;
    slot.rx_state.store(URMA_RX_STS_DATA_READY, std::memory_order_release);
    rx.consume_seq_no_.store(0, std::memory_order_relaxed);

    struct iovec zc_iov;
    zc_iov.iov_base = iov_block->data;
    zc_iov.iov_len = 64;

    auto zc_ret = rx.ReadV(sock_, &zc_iov, 1, UBS_IO_FLAG_USE_BRPC_BLOCK);
    EXPECT_GT(zc_ret, 0);
    EXPECT_NE(iov_block->GetNext(), static_cast<void *>(nullptr));
    EXPECT_EQ(slot.rx_state.load(), URMA_RX_STS_IDLE);
    EXPECT_EQ(rx.consume_seq_no_.load(), 1ULL);
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, ZeroCopyRead_NotDataReady_ReturnsEagain)
{
    auto *iov_block = AllocZcBlock();

    UrmaRxOps rx(TEST_FD);
    WriteImmData imm;
    imm.ubsocket.opcode = PRE_WRITE;
    rx.data_slots_[0].write_imm = imm.data;
    rx.data_slots_[0].rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);
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
    rx.data_slots_[0].rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);

    auto result = rx.DataReadFinished(sock_, 0);
    EXPECT_EQ(UBS_ERROR, result);
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, DataReadFinished_SeqMatchesConsumed_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    rx.consume_seq_no_.store(5, std::memory_order_relaxed);
    auto &slot = rx.data_slots_[5 % 256];
    slot.rx_state.store(URMA_RX_STS_READING, std::memory_order_release);

    auto result = rx.DataReadFinished(sock_, 5);
    EXPECT_EQ(UBS_OK, result);
    EXPECT_EQ(URMA_RX_STS_DATA_READY, slot.rx_state.load());
    GlobalMockObject::verify();
}

TEST_F(UrmaDataRxTest, DataReadFinished_SeqNotConsumed_ReturnsLaterSeq)
{
    UrmaRxOps rx(TEST_FD);
    rx.consume_seq_no_.store(3, std::memory_order_relaxed);
    auto &slot = rx.data_slots_[7 % 256];
    slot.rx_state.store(URMA_RX_STS_READING, std::memory_order_release);

    auto result = rx.DataReadFinished(sock_, 7);
    EXPECT_EQ(UBS_LATER_SEQ, result);
    EXPECT_EQ(URMA_RX_STS_DATA_READY, slot.rx_state.load());
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
    /* poller side never touches the slot: state machine is advanced only by the ReadV drain */
    EXPECT_EQ(URMA_RX_STS_IDLE, rx.data_slots_[0].rx_state.load(std::memory_order_acquire));
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
    ASSERT_EQ(URMA_RX_STS_IDLE, rx.data_slots_[0].rx_state.load(std::memory_order_acquire));

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
    urma_sock->NotifyLocalClose();
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
    struct iovec iov { buf, sizeof(buf) };
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
    struct iovec iov { buf, sizeof(buf) };
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
    struct iovec iov { buf, sizeof(buf) };
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
