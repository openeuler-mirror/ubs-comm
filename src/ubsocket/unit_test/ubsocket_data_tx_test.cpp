/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/uio.h>
#include <cstring>

#include "core/ubsocket_data_tx.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_core_types.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_ref.h"
#include "profiling/ubsocket_prof.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

// ==================== Mock Classes ====================

class MockSocketBase : public SocketBase {
public:
    MockSocketBase(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_TCP) {}
    int GetTxFd() override { return raw_socket_; }
    bool IsBindRemote() override { return false; }
    ock::ubs::Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override { return UBS_OK; }
    ock::ubs::Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override { return UBS_OK; }
    bool ShouldRegisterTxEvent() override { return false; }
    ock::ubs::Result ProcessEpollEvent(struct epoll_event &event) override { return UBS_OK; }
    ock::ubs::Result Initialize() noexcept override { return UBS_OK; }
    void UnInitialize() noexcept override {}
};

class MockBufConverter : public UbSocketBufConverter {
public:
    uint32_t indexMoveReturn = 0;
    uint32_t IndexMove(uint32_t len) override { return indexMoveReturn; }
    bool MemCopy(uint32_t len, uintptr_t buf) override { return true; }
    void Reset() override {}
};

/* DataTxOps 已去虚化（数据面 SoA 改造）：行为控制改为 mockcpp MOCKER 钩
 * 非虚成员，fake 经 this 读 mock 字段——每用例语义与原测试逐一等价。 */
class MockDataTxOps : public DataTxOps {
public:
    ConverterPtr converter;
    bool writable = true;
    uintptr_t allocTxBufReturn = 1;
    int postSendReturn = 100;
    uint32_t iobufSize = 4096;
    uint16_t txAvail = 255;

    MockDataTxOps();
    ~MockDataTxOps();

    using DataTxOps::tx_queue_avail_num_;
};

/* mockcpp 成员钩子的 fake 不接收 this（实参即成员实参）——用"当前 mock"
 * 全局指针传递每用例控制字段；每个用例恰构造一个 mock，构造/析构自登记。 */
static MockDataTxOps *g_tx_mock = nullptr;

MockDataTxOps::MockDataTxOps() : DataTxOps(42)
{
    tx_queue_avail_num_.store(txAvail, std::memory_order_release);
    auto *raw = new MockBufConverter();
    raw->indexMoveReturn = 0;
    converter = ConverterPtr(raw);
    g_tx_mock = this;
}
MockDataTxOps::~MockDataTxOps()
{
    if (g_tx_mock == this) {
        g_tx_mock = nullptr;
    }
}

static ConverterPtr FakeBuildIovConverter(const struct iovec *, int)
{
    return g_tx_mock->converter;
}
static ConverterPtr FakeBuildBufferConverter(const void *, size_t)
{
    return g_tx_mock->converter;
}
static uintptr_t FakeAllocTxBuf(uint32_t, uint32_t)
{
    return g_tx_mock->allocTxBufReturn;
}
static int FakePostSend(const SocketPtr &, uintptr_t, uint32_t, const ConverterPtr &)
{
    return g_tx_mock->postSendReturn;
}
static int FakePollTx(Socket *) { return 0; }
static uint32_t FakeIOBufSize()
{
    return g_tx_mock->iobufSize;
}
static void FakeFlushTx(Socket *, uint32_t) {}
static void FakeWakeUpTx(Socket *) {}
static bool FakeWritable(const SocketPtr &)
{
    return g_tx_mock->writable;
}

// ==================== LibcApi Mock Helpers ====================

static ssize_t MockWritevSuccess(int fd, const struct iovec *iov, int iovcnt)
{
    return 200;
}

static ssize_t MockWritevFail(int fd, const struct iovec *iov, int iovcnt)
{
    errno = EIO;
    return -1;
}

static int MockShutdownSuccess(int fd, int how)
{
    return 0;
}

// ==================== Test Fixture ====================

class DataTxTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        savedWritev_ = LibcApi::writev_ptr;
        savedShutdown_ = LibcApi::shutdown_ptr;
        ubsocket_prof_option_t opt{};
        opt.tracepoint_count = UBSOCKET_PROF_COUNT;
        opt.enable_dump = 0;
        opt.dump_file_path = "/tmp/ubsocket/test_prof";
        opt.dump_interval_min = 1;
        ubsocket_prof_init(&opt);
        MOCKER(&DataTxOps::BuildIovConverter).stubs().will(invoke(FakeBuildIovConverter));
        MOCKER(&DataTxOps::BuildBufferConverter).stubs().will(invoke(FakeBuildBufferConverter));
        MOCKER(&DataTxOps::AllocTxBuf).stubs().will(invoke(FakeAllocTxBuf));
        MOCKER(&DataTxOps::PostSend).stubs().will(invoke(FakePostSend));
        MOCKER(&DataTxOps::PollTx).stubs().will(invoke(FakePollTx));
        MOCKER(&DataTxOps::IOBufSize).stubs().will(invoke(FakeIOBufSize));
        MOCKER(&DataTxOps::FlushTx).stubs().will(invoke(FakeFlushTx));
        MOCKER(&DataTxOps::WakeUpTx).stubs().will(invoke(FakeWakeUpTx));
        MOCKER(&DataTxOps::Writable).stubs().will(invoke(FakeWritable));
        ubsocket_prof_enabled = 1;
        savedLogLevel_ = Logger::Instance().GetLogLevel();
        Logger::Instance().SetLogLevel(LEVEL_DEBUG);
    }

    void TearDown() override
    {
        ubsocket_prof_enabled = 0;
        ubsocket_prof_uninit();
        LibcApi::writev_ptr = savedWritev_;
        LibcApi::shutdown_ptr = savedShutdown_;
        Logger::Instance().SetLogLevel(savedLogLevel_);
        GlobalMockObject::verify();
    }

    SocketPtr MakeSocket(int fd, SocketState state = SOCK_STAT_INIT)
    {
        auto *sock = new MockSocketBase(fd);
        sock->state_.store(state, std::memory_order_release);
        return SocketPtr(sock);
    }

private:
    decltype(LibcApi::writev_ptr) savedWritev_;
    decltype(LibcApi::shutdown_ptr) savedShutdown_;
    int savedLogLevel_{LEVEL_INFO};
};

// ==================== DataTx::WriteV Tests ====================

TEST_F(DataTxTest, WriteV_Established_CallsWritev)
{
    auto sock = MakeSocket(42, SOCK_STAT_RAW_ESTABLISHED);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    LibcApi::writev_ptr = MockWritevSuccess;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, 200);
}

TEST_F(DataTxTest, WriteV_Established_WritevFails)
{
    auto sock = MakeSocket(42, SOCK_STAT_RAW_ESTABLISHED);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    LibcApi::writev_ptr = MockWritevFail;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, -1);
}

TEST_F(DataTxTest, WriteV_NullIov_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    auto ret = tx.WriteV(sock, nullptr, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(DataTxTest, WriteV_ZeroIovcnt_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 0);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(DataTxTest, WriteV_SocketClosed_ReturnsEpipe)
{
    auto sock = MakeSocket(42, SOCK_STAT_CLOSE);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EPIPE);
}

TEST_F(DataTxTest, WriteV_NotWritable_ReturnsEagain)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    txOps.writable = false;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
}

TEST_F(DataTxTest, WriteV_AllocTxBufFails_ReturnsEnobufs)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    txOps.allocTxBufReturn = 0;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOBUFS);
}

TEST_F(DataTxTest, WriteV_PostSendFails_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    txOps.postSendReturn = -1;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, -1);
}

TEST_F(DataTxTest, WriteV_PostSendSuccess_ReturnsTxLen)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    txOps.postSendReturn = 500;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 5;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, 500);
}

TEST_F(DataTxTest, WriteV_BatchLoop_PostSendSuccess)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    auto *conv = new MockBufConverter();
    conv->indexMoveReturn = 100;
    txOps.converter = ConverterPtr(conv);
    txOps.iobufSize = 500;
    txOps.postSendReturn = 200;
    DataTx tx(sock, &txOps);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("hello");
    iov.iov_len = 500;
    auto ret = tx.WriteV(sock, &iov, 1);
    EXPECT_EQ(ret, 200);
}

// ==================== GetTxStatCounters ====================

TEST_F(DataTxTest, GetTxStatCounters_ReturnsCounters)
{
    bool savedStat = GlobalSetting::UBS_MONITOR_ENABLE;
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    MockDataTxOps txOps;
    EXPECT_EQ(txOps.GetTxStatCounters(), nullptr);
    GlobalSetting::UBS_MONITOR_ENABLE = savedStat;
}

TEST_F(DataTxTest, GetTxStatCounters_WithStatEnabled)
{
    bool savedStat = GlobalSetting::UBS_MONITOR_ENABLE;
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    MockDataTxOps txOps;
    auto *counters = txOps.GetTxStatCounters();
    EXPECT_NE(counters, nullptr);
    GlobalSetting::UBS_MONITOR_ENABLE = savedStat;
}

// ==================== GetTxOps ====================

TEST_F(DataTxTest, GetTxOps_ReturnsOps)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataTxOps txOps;
    DataTx tx(sock, &txOps);
    EXPECT_EQ(tx.GetTxOps(), &txOps);
}
