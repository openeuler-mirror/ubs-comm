/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/uio.h>
#include <cstring>

#include "core/ubsocket_data_rx.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_core_types.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_ref.h"
#include "common/ubsocket_set.h"
#include "iobuf/ubsocket_iobuf.h"
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

/* DataRxOps 已去虚化（数据面 SoA 改造）：控制行为不再走子类 override，
 * 而是 fixture 里用 mockcpp MOCKER 钩非虚成员，fake 经 this 指针读本
 * mock 对象的控制字段——每用例语义与原测试逐一等价。 */
class MockDataRxOps : public DataRxOps {
public:
    MockDataRxOps();
    ~MockDataRxOps();
    int pollRxReturn = 0;
    int rearmReturn = 0;
    Block *mockBlock = nullptr;

    using DataRxOps::epoll_event_num_;
    using DataRxOps::flow_control_failed_;
    using DataRxOps::fd_;
    using DataRxOps::block_cache_;
    using DataRxOps::DataToBlock; /* 提权以便 MOCKER 取成员指针 */
};

/* mockcpp 成员钩子的 fake 不接收 this——同 tx 侧的"当前 mock"全局指针方案 */
static MockDataRxOps *g_rx_mock = nullptr;
MockDataRxOps::MockDataRxOps() : DataRxOps(42)
{
    g_rx_mock = this;
}
MockDataRxOps::~MockDataRxOps()
{
    if (g_rx_mock == this) {
        g_rx_mock = nullptr;
    }
}

static int FakePollRx(const SocketPtr &)
{
    return g_rx_mock->pollRxReturn;
}
static int FakeRearmRxInterrupt()
{
    return g_rx_mock->rearmReturn;
}
static Block *FakeDataToBlock(void *)
{
    return g_rx_mock->mockBlock;
}
static void FakeFlushRx(Socket *, uint32_t) {}

// ==================== LibcApi Mock Helpers ====================

static ssize_t MockReadvSuccess(int fd, const struct iovec *iov, int iovcnt)
{
    return 100;
}

static ssize_t MockReadvFail(int fd, const struct iovec *iov, int iovcnt)
{
    errno = EIO;
    return -1;
}

static ssize_t MockRecvPeerClosed(int fd, void *buf, size_t size, int flags)
{
    return 0;
}

static ssize_t MockRecvEagain(int fd, void *buf, size_t size, int flags)
{
    errno = EAGAIN;
    return -1;
}

// ==================== Test Fixture ====================

class DataRxTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        savedReadv_ = LibcApi::readv_ptr;
        savedRecv_ = LibcApi::recv_ptr;
        // Set recv_ptr to a safe mock (some tests reach recv path)
        LibcApi::recv_ptr = MockRecvEagain;
        ubsocket_prof_option_t opt{};
        opt.tracepoint_count = UBSOCKET_PROF_COUNT;
        opt.enable_dump = 0;
        opt.dump_file_path = "/tmp/ubsocket/test_prof";
        opt.dump_interval_min = 1;
        ubsocket_prof_init(&opt);
        ubsocket_prof_enabled = 1;
        savedLogLevel_ = Logger::Instance().GetLogLevel();
        Logger::Instance().SetLogLevel(LEVEL_DEBUG);
        MOCKER(&DataRxOps::PollRx).stubs().will(invoke(FakePollRx));
        MOCKER(&DataRxOps::RearmRxInterrupt).stubs().will(invoke(FakeRearmRxInterrupt));
        MOCKER(&MockDataRxOps::DataToBlock).stubs().will(invoke(FakeDataToBlock));
        MOCKER(&DataRxOps::FlushRx).stubs().will(invoke(FakeFlushRx));
    }

    void TearDown() override
    {
        ubsocket_prof_enabled = 0;
        ubsocket_prof_uninit();
        LibcApi::readv_ptr = savedReadv_;
        LibcApi::recv_ptr = savedRecv_;
        Logger::Instance().SetLogLevel(savedLogLevel_);
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }

    SocketPtr MakeSocket(int fd, SocketState state = SOCK_STAT_INIT)
    {
        auto *sock = new MockSocketBase(fd);
        sock->state_.store(state, std::memory_order_release);
        return SocketPtr(sock);
    }

private:
    decltype(LibcApi::readv_ptr) savedReadv_;
    decltype(LibcApi::recv_ptr) savedRecv_;
    int savedLogLevel_{LEVEL_INFO};
};

// ==================== DataRx::ReadV Tests ====================
// 注：原 OutputErrorMagicNumber 系列 3 个用例已随功能删除——协议嗅探字节
// 回放在数据面 SoA 改造中被判定为死代码移除（recv_size 生产无写点恒 0，
// 协议失配直接 close fd），相关字段不复存在。

TEST_F(DataRxTest, ReadV_Established_CallsReadv)
{
    auto sock = MakeSocket(42, SOCK_STAT_RAW_ESTABLISHED);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    LibcApi::readv_ptr = MockReadvSuccess;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, 100);
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_Established_ReadvFails)
{
    auto sock = MakeSocket(42, SOCK_STAT_RAW_ESTABLISHED);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    LibcApi::readv_ptr = MockReadvFail;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, -1);
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_NullIov_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    auto ret = rx.ReadV(sock, nullptr, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(DataRxTest, ReadV_ZeroIovcnt_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    auto ret = rx.ReadV(sock, &iov, 0);
    EXPECT_EQ(ret, UBS_ERROR);
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_NullIovBase_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = nullptr;
    iov.iov_len = 1024;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
}


TEST_F(DataRxTest, ReadV_PollFails_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    rxOps.pollRxReturn = -1;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_LT(ret, 0);
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_RxDataSetFails_ReturnsError)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    rxOps.pollRxReturn = 0;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_ReadvUnlimitedTrue_UsesMaxSize)
{
    auto sock = MakeSocket(42, SOCK_STAT_RAW_ESTABLISHED);
    GlobalSetting::UBS_READV_UNLIMITED = true;
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    LibcApi::readv_ptr = MockReadvSuccess;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, 100);
    GlobalSetting::UBS_READV_UNLIMITED = true;
    free(iov.iov_base);
}

TEST_F(DataRxTest, ReadV_OutputErrorMagicNoData_PollFails)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    rxOps.pollRxReturn = -1;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 1024;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_LT(ret, 0);
    free(iov.iov_base);
}

// ==================== ReadV: UBS_READV_UNLIMITED=false path ====================

TEST_F(DataRxTest, ReadV_ReadvUnlimitedFalse_ComputesMaxBufSize)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    GlobalSetting::UBS_READV_UNLIMITED = false;
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(malloc(1024));
    iov.iov_len = 512;
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    free(iov.iov_base);
    GlobalSetting::UBS_READV_UNLIMITED = true;
}

// ==================== RxDataSet: no-data sub-paths ====================

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_CasFailure_Eintr)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    rxOps.epoll_event_num_.store(1, std::memory_order_release);
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINTR);
}

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_FlowControlFailed_Eio)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    rxOps.flow_control_failed_ = true;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
}

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_RearmFails_Eio)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    rxOps.rearmReturn = -1;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
}

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_PeerClosed_Returns0)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    rxOps.rearmReturn = 0;
    LibcApi::recv_ptr = MockRecvPeerClosed;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, 0);
}

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_Eagain)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    rxOps.rearmReturn = 0;
    LibcApi::recv_ptr = MockRecvEagain;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EAGAIN);
}

TEST_F(DataRxTest, ReadV_RxDataSet_NoData_SocketClosed_Returns0)
{
    auto closedSock = MakeSocket(142, SOCK_STAT_CLOSE);
    ArraySet<Socket>::GetInstance().OverrideItem(142, closedSock.Get());

    auto sock = MakeSocket(142, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    rxOps.fd_ = 142; // Set DataRxOps fd_ to match ArraySet index
    char buf[1024];
    Block block(buf, sizeof(buf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_EQ(ret, 0);
}

// ==================== RxDataSet: success path ====================

TEST_F(DataRxTest, ReadV_RxDataSet_Success_ReturnsData)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    char blockBuf[1024];
    Block block(blockBuf, sizeof(blockBuf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    constexpr size_t ALLOC_SZ = sizeof(Block) + 512;
    char *raw = new char[ALLOC_SZ];
    char *dataPtr = raw + sizeof(Block);
    rxOps.block_cache_.Insert(dataPtr, 512);
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = blockBuf;
    iov.iov_len = sizeof(blockBuf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_GT(ret, 0);
}

TEST_F(DataRxTest, ReadV_RxDataSet_Success_WithTraceEnabled)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    MockDataRxOps rxOps;
    char blockBuf[1024];
    Block block(blockBuf, sizeof(blockBuf));
    rxOps.mockBlock = &block;
    rxOps.pollRxReturn = 0;
    constexpr size_t ALLOC_SZ = sizeof(Block) + 512;
    char *raw = new char[ALLOC_SZ];
    char *dataPtr = raw + sizeof(Block);
    rxOps.block_cache_.Insert(dataPtr, 512);
    DataRx rx(sock, &rxOps);
    struct iovec iov;
    iov.iov_base = blockBuf;
    iov.iov_len = sizeof(blockBuf);
    auto ret = rx.ReadV(sock, &iov, 1);
    EXPECT_GT(ret, 0);
    GlobalSetting::UBS_MONITOR_ENABLE = false;
}

// ==================== GetRxOps ====================

TEST_F(DataRxTest, GetRxOps_ReturnsOps)
{
    auto sock = MakeSocket(42, SOCK_STAT_INIT);
    MockDataRxOps rxOps;
    DataRx rx(sock, &rxOps);
    EXPECT_EQ(rx.GetRxOps(), &rxOps);
}

// ==================== OutputErrorMagicNumber: multi-iov ====================


