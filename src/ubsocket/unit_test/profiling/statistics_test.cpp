/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "profiling/statistics/statistics_statsmgr.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <securec.h>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "core/umq/umq_socket.h"
#include "profiling/statistics/statistics.h"
#include "profiling/statistics/ubsocket_print_stats_mgr.h"
#include "umq_dfx_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;
using namespace Statistics;

namespace {

constexpr umq_trans_mode_t TEST_TRANS_MODE_UB = UMQ_TRANS_MODE_UB;
constexpr uint32_t TEST_PERF_BUF_SIZE = 4096;
constexpr uint32_t TEST_RETRY_COUNT_42 = 42;
constexpr uint32_t TEST_RETRY_COUNT_7 = 7;
constexpr int TEST_FD_42 = 42;
constexpr uint32_t TEST_PID = 1234;

static char g_fakePerfBuf[TEST_PERF_BUF_SIZE] = {0};
static uint32_t g_fakePerfBufLen = 0;
static int g_fakeInfoGetRet = 0;

void SetFakePerfContent(const char *content)
{
    g_fakePerfBufLen = static_cast<uint32_t>(strlen(content));
    if (g_fakePerfBufLen > 0 && g_fakePerfBufLen <= TEST_PERF_BUF_SIZE) {
        memcpy_s(g_fakePerfBuf, sizeof(g_fakePerfBuf), content, g_fakePerfBufLen);
    }
}

int FakeTpPerfInfoGet(umq_trans_mode_t transMode, char *perfBuf, uint32_t *length)
{
    (void)transMode;
    if (perfBuf == nullptr || length == nullptr) {
        return -1;
    }
    *length = g_fakePerfBufLen;
    if (g_fakePerfBufLen > 0 && g_fakePerfBufLen <= TEST_PERF_BUF_SIZE) {
        memcpy_s(perfBuf, TEST_PERF_BUF_SIZE, g_fakePerfBuf, g_fakePerfBufLen);
    }
    return g_fakeInfoGetRet;
}

static struct tm g_fakeLocaltimeTm = {};
static int g_fakeLocaltimeRCall = 0;

struct tm *FakeLocaltimeRAlternating(const time_t *timep, struct tm *result)
{
    (void)timep;
    (void)result;
    ++g_fakeLocaltimeRCall;
    /* 奇数次调用返回有效 tm(走 strftime 分支)，偶数次返回 nullptr(走 else 分支)，
     * 使单个 OutputAllStats 调用点在循环内同时覆盖两条路径。 */
    return (g_fakeLocaltimeRCall % 2 == 1) ? &g_fakeLocaltimeTm : nullptr;
}

constexpr int TEST_FD_43 = 43;
constexpr int TEST_FD_44 = 44;
constexpr int TEST_FD_45 = 45;
constexpr uint64_t TEST_UMQ_HANDLE = 12345;
constexpr uint64_t TEST_EXT_MAX_BYTES_1 = 1;
const std::string TEST_PRINT_STATS_DIR = "/tmp/ubsocket/unitest/print_stats_mgr";
const std::string TEST_LISTENER_ARCHIVE_SUFFIX = ".1.json";

static int g_mockCloseCount = 0;

int MockSocketSuccess(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return TEST_FD_42;
}

int MockBindSuccess(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd;
    (void)addr;
    (void)len;
    return 0;
}

int MockListenSuccess(int fd, int backlog)
{
    (void)fd;
    (void)backlog;
    return 0;
}

int MockCloseCount(int fd)
{
    (void)fd;
    g_mockCloseCount++;
    return 0;
}

void RemoveTestDir()
{
    std::error_code ec;
    std::filesystem::remove_all(TEST_PRINT_STATS_DIR, ec);
}

void CreateTestDir()
{
    std::error_code ec;
    std::filesystem::create_directories(TEST_PRINT_STATS_DIR, ec);
}

std::string ReadFileContent(const std::string &path)
{
    std::ifstream ifs(path);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

constexpr int TEST_ACCEPT_FD = 7;
constexpr int TEST_EPOLL_FD = 8;
constexpr int TEST_WAKEUP_FD = 9;

static int g_mockSetsockoptRet = 0;
static int g_mockSetsockoptCall = 0;
static int g_mockSetsockoptFailOnCall = -1;
static int g_mockEpollCreateRet = TEST_EPOLL_FD;
static int g_mockEventfdRet = TEST_WAKEUP_FD;
static int g_mockEpollCtlRet = 0;
static int g_mockEpollCtlCall = 0;
static int g_mockEpollCtlFailOnCall = -1;
static int g_mockEpollWaitRet = 0;
static int g_mockEventFd = -1;
static uint32_t g_mockEventMask = 0;
static ssize_t g_mockWriteRet = static_cast<ssize_t>(sizeof(uint64_t));
static ssize_t g_mockReadRet = static_cast<ssize_t>(sizeof(uint64_t));
static int g_mockTransportPoolGetRet = 0;
static int g_mockGetRouteListRet = 0;
static uint32_t g_mockRouteNum = 0;
static int g_mockProfCombineRet = 0;
static int g_mockProfInitRet = 0;
static CLICommand g_mockRecvCmdId = CLICommand::INVALID;
static CLITypeParam g_mockRecvType = CLITypeParam::INVALID;
static uint32_t g_mockCtrlDataSize = 0;
static double g_mockRecvMValue = 0.0;
static int g_mockRecvSocketDataFailCall = -1;
static int g_mockRecvSocketDataCall = 0;
static int g_mockSendSocketDataMode = 0;
static int g_mockSendSocketDataCall = 0;

/* ProcessDelayRequest 的 PROF_OP_PATH 载荷读取 mock: 单次使能后, 下一次
 * RecvSocketData 调用把 pathPayload 拷入接收缓冲区。 */
static bool g_mockRecvPathEnable = false;
static std::string g_mockRecvPathPayload;

int MockSocketFail(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    errno = EACCES;
    return -1;
}

int MockBindFail(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd;
    (void)addr;
    (void)len;
    errno = EADDRINUSE;
    return -1;
}

int MockListenFail(int fd, int backlog)
{
    (void)fd;
    (void)backlog;
    errno = EADDRINUSE;
    return -1;
}

int MockAcceptSuccess(int socket, struct sockaddr *address, socklen_t *address_len)
{
    (void)socket;
    (void)address;
    (void)address_len;
    return TEST_ACCEPT_FD;
}

int MockAcceptFail(int socket, struct sockaddr *address, socklen_t *address_len)
{
    (void)socket;
    (void)address;
    (void)address_len;
    errno = EMFILE;
    return -1;
}

int MockSetsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    ++g_mockSetsockoptCall;
    if (g_mockSetsockoptCall == g_mockSetsockoptFailOnCall) {
        errno = EINVAL;
        return -1;
    }
    return g_mockSetsockoptRet;
}

int MockEpollCreate(int size)
{
    (void)size;
    return g_mockEpollCreateRet;
}

int MockEpollCtl(int epfd, int op, int fd, struct epoll_event *event)
{
    (void)epfd;
    (void)op;
    (void)fd;
    (void)event;
    ++g_mockEpollCtlCall;
    if (g_mockEpollCtlCall == g_mockEpollCtlFailOnCall) {
        errno = EPERM;
        return -1;
    }
    return g_mockEpollCtlRet;
}

int MockEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    (void)epfd;
    (void)timeout;
    if (g_mockEpollWaitRet > 0 && events != nullptr && maxevents > 0) {
        events[0].events = g_mockEventMask;
        events[0].data.fd = g_mockEventFd;
    }
    return g_mockEpollWaitRet;
}

int MockEventfd(unsigned int initval, int flags)
{
    (void)initval;
    (void)flags;
    return g_mockEventfdRet;
}

ssize_t MockWrite(int fd, const void *buf, size_t n)
{
    (void)fd;
    (void)buf;
    (void)n;
    if (g_mockWriteRet < 0) {
        errno = EIO;
    }
    return g_mockWriteRet;
}

ssize_t MockRead(int fd, void *buf, size_t n)
{
    (void)fd;
    (void)buf;
    (void)n;
    if (g_mockReadRet < 0) {
        errno = EIO;
    }
    return g_mockReadRet;
}

int MockTransportPoolGet(uint64_t umqh, umq_transport_pool_stats_t *stats)
{
    (void)umqh;
    if (stats != nullptr) {
        stats->total_num = 10;
        stats->global_num = 3;
        stats->cache_num = 2;
        stats->in_use_num = 5;
    }
    return g_mockTransportPoolGetRet;
}

int MockGetRouteList(const umq_route_key_t *route, umq_trans_mode_t transMode, umq_route_list_t *routeList)
{
    (void)route;
    (void)transMode;
    if (routeList != nullptr) {
        routeList->route_num = g_mockRouteNum;
    }
    return g_mockGetRouteListRet;
}

int MockProfCombine(std::string &out)
{
    out = "trace-data";
    return g_mockProfCombineRet;
}

void MockProfReset()
{
}

int MockProfInit(uint32_t tracepointCount, const char *dumpPath, uint16_t dumpIntervalMin)
{
    (void)tracepointCount;
    (void)dumpPath;
    (void)dumpIntervalMin;
    return g_mockProfInitRet;
}

ssize_t MockSendSocketData(int fd, const void *buf, size_t size, uint32_t timeoutMs)
{
    (void)fd;
    (void)buf;
    (void)timeoutMs;
    ++g_mockSendSocketDataCall;
    if (g_mockSendSocketDataMode == 1 && g_mockSendSocketDataCall == 1) {
        return 0;
    }
    if (g_mockSendSocketDataMode == 2 && g_mockSendSocketDataCall == 2) {
        return 0;
    }
    if (g_mockSendSocketDataMode == 3) {
        return 0;
    }
    if (g_mockSendSocketDataMode == 4 && g_mockSendSocketDataCall >= 2) {
        return 0;
    }
    return static_cast<ssize_t>(size);
}

ssize_t MockRecvSocketData(int fd, const void *buf, size_t size, uint32_t timeoutMs)
{
    (void)fd;
    (void)timeoutMs;
    ++g_mockRecvSocketDataCall;
    if (g_mockRecvSocketDataCall == g_mockRecvSocketDataFailCall) {
        return 0;
    }
    if (g_mockRecvPathEnable) {
        g_mockRecvPathEnable = false;
        const size_t copyLen = std::min(size, g_mockRecvPathPayload.size());
        if (copyLen > 0) {
            memcpy_s(const_cast<void *>(buf), size, g_mockRecvPathPayload.data(), copyLen);
        }
        return static_cast<ssize_t>(size);
    }
    if (size == sizeof(CLIControlHeader)) {
        CLIControlHeader header{};
        header.mCmdId = g_mockRecvCmdId;
        header.mType = g_mockRecvType;
        header.mDataSize = g_mockCtrlDataSize;
        header.mValue = g_mockRecvMValue;
        memcpy_s(const_cast<void *>(buf), size, &header, sizeof(CLIControlHeader));
    } else if (size == sizeof(Statistics::Listener::CtrlHead)) {
        Statistics::Listener::CtrlHead ctrl{};
        ctrl.m_data_size = g_mockCtrlDataSize;
        memcpy_s(const_cast<void *>(buf), size, &ctrl, sizeof(Statistics::Listener::CtrlHead));
    }
    return static_cast<ssize_t>(size);
}

class TestableListener : public Statistics::Listener {
public:
    using Statistics::Listener::Listener;

    int TestRecvCmd(int fd, Statistics::Listener::CtrlHead &ipcCtl)
    {
        return RecvCmd(fd, ipcCtl);
    }

    int TestSendCmd(int fd, Statistics::Listener::CtrlHead &ipcCtl, const char *inData)
    {
        return SendCmd(fd, ipcCtl, inData);
    }

    void TestProcessStats()
    {
        ProcessStats();
    }
};

} // namespace

class StatisticsStatsMgrTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_PROF_ENABLE = false;
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        StatsMgr::mReTxCount.store(0);
        StatsMgr::mConnCount.store(0);
        StatsMgr::mActiveConnCount.store(0);
        g_fakePerfBufLen = 0;
        g_fakeInfoGetRet = 0;
        memset_s(g_fakePerfBuf, sizeof(g_fakePerfBuf), 0, sizeof(g_fakePerfBuf));
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        errno = 0;
    }
};

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ProfDisabledStartFail_ReturnsEarly)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(-1));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).expects(exactly(0)).will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_stop).expects(exactly(0)).will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_InfoGetFail_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    g_fakeInfoGetRet = -1;
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_PerfLenZero_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseSuccess_StoresRetryCount)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: 42\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), TEST_RETRY_COUNT_42);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_StopFail_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: 42\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(-1));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), TEST_RETRY_COUNT_42);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseNoPrefix_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("total: 100\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseNoNewline_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: 12");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseEmptyDigit_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: \n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseDigitTooLong_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: 123456789012345678901\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseInvalidDigit_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: abc\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ParseOverflow_LogsError)
{
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    SetFakePerfContent("retry_count: 99999999999999999999999999\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

// ==================== Recorder ====================

TEST_F(StatisticsStatsMgrTest, Recorder_UpdateReset_ReflectsCount)
{
    Recorder recorder;
    recorder.Update(5);
    EXPECT_EQ(recorder.GetCnt(), 5u);
    recorder.Update(3);
    EXPECT_EQ(recorder.GetCnt(), 8u);
    recorder.Reset();
    EXPECT_EQ(recorder.GetCnt(), 0u);
}

TEST_F(StatisticsStatsMgrTest, Recorder_GetInfo_ZeroCountOutputsDash)
{
    Recorder recorder;
    std::ostringstream oss;
    recorder.GetInfo(TEST_FD_42, "totalConnections", oss);
    EXPECT_NE(oss.str().find("42"), std::string::npos);
    EXPECT_NE(oss.str().find("-"), std::string::npos);
}

TEST_F(StatisticsStatsMgrTest, Recorder_GetInfo_NonZeroCountOutputsValue)
{
    Recorder recorder;
    recorder.Update(7);
    std::ostringstream oss;
    recorder.GetInfo(TEST_FD_42, "totalConnections", oss);
    EXPECT_NE(oss.str().find("42"), std::string::npos);
    EXPECT_NE(oss.str().find("7"), std::string::npos);
    EXPECT_EQ(oss.str().find("-"), std::string::npos);
}

TEST_F(StatisticsStatsMgrTest, Recorder_GetTitle_OutputsHeader)
{
    std::ostringstream oss;
    Recorder::GetTitle(oss);
    EXPECT_NE(oss.str().find("fd"), std::string::npos);
    EXPECT_NE(oss.str().find("type"), std::string::npos);
    EXPECT_NE(oss.str().find("total"), std::string::npos);
}

TEST_F(StatisticsStatsMgrTest, Recorder_FillEmptyForm_MatchingLengthAppendsEmptyRow)
{
    std::ostringstream oss;
    Recorder::GetTitle(oss);
    const size_t before = oss.str().length();
    Recorder::FillEmptyForm(oss);
    EXPECT_GT(oss.str().length(), before);
    EXPECT_NE(oss.str().find("-"), std::string::npos);
}

TEST_F(StatisticsStatsMgrTest, Recorder_FillEmptyForm_MismatchedLengthNoop)
{
    std::ostringstream oss;
    Recorder::FillEmptyForm(oss);
    EXPECT_TRUE(oss.str().empty());
}

// ==================== StatsMgr: Init / Update ====================

TEST_F(StatisticsStatsMgrTest, InitStatsMgr_StatEnable_AllocatesRecorderVec)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    ASSERT_NE(mgr.m_recorder_vec, nullptr);
    EXPECT_TRUE(mgr.m_stats_enable);
}

TEST_F(StatisticsStatsMgrTest, InitStatsMgr_StatDisable_SkipsAlloc)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    EXPECT_EQ(mgr.m_recorder_vec, nullptr);
    EXPECT_FALSE(mgr.m_stats_enable);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_ConnCountAndActiveCount_FetchAdd)
{
    StatsMgr mgr;
    /* 单一调用点遍历全部枚举值 + default，使该调用点覆盖 switch 全部分支 */
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 2);
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 2u);
    EXPECT_EQ(StatsMgr::GetActiveConnCount(), 2u);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_PacketTypes_RecorderUpdated)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    uint64_t rxPacket0 = 0;
    uint64_t txPacket0 = 0;
    uint64_t rxByte0 = 0;
    uint64_t txByte0 = 0;
    /* 同一调用点先覆盖 recorder 非空(累加)，再覆盖 recorder 为空(跳过) 两条路径 */
    for (int round = 0; round < 2; ++round) {
        for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
            mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
        }
        if (round == 0) {
            rxPacket0 = (*mgr.m_recorder_vec)[StatsMgr::RX_PACKET_COUNT].GetCnt();
            txPacket0 = (*mgr.m_recorder_vec)[StatsMgr::TX_PACKET_COUNT].GetCnt();
            rxByte0 = (*mgr.m_recorder_vec)[StatsMgr::RX_BYTE_COUNT].GetCnt();
            txByte0 = (*mgr.m_recorder_vec)[StatsMgr::TX_BYTE_COUNT].GetCnt();
            mgr.m_recorder_vec.reset();
        }
    }
    EXPECT_EQ(rxPacket0, 1u);
    EXPECT_EQ(txPacket0, 1u);
    EXPECT_EQ(rxByte0, 1u);
    EXPECT_EQ(txByte0, 1u);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_RecorderNull_PacketTypeNoop)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        EXPECT_NO_FATAL_FAILURE(mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1));
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 1u);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_UnknownType_Noop)
{
    StatsMgr mgr;
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 1u);
}

// ==================== StatsMgr: Sub / Get ====================

TEST_F(StatisticsStatsMgrTest, SubMConnCount_Zero_StaysZero)
{
    for (uint32_t cnt : {0u, 1u}) {
        StatsMgr::mConnCount.store(cnt);
        StatsMgr::SubMConnCount();
        EXPECT_EQ(StatsMgr::GetConnCount(), cnt == 0 ? 0u : 0u);
    }
}

TEST_F(StatisticsStatsMgrTest, SubMConnCount_Positive_Decrements)
{
    for (uint32_t cnt : {3u, 0u, 1u}) {
        StatsMgr::mConnCount.store(cnt);
        StatsMgr::SubMConnCount();
        EXPECT_EQ(StatsMgr::GetConnCount(), cnt == 0 ? 0u : (cnt - 1));
    }
}

TEST_F(StatisticsStatsMgrTest, SubMActiveConnCount_Zero_StaysZero)
{
    for (uint32_t cnt : {0u, 1u}) {
        StatsMgr::mActiveConnCount.store(cnt);
        StatsMgr::SubMActiveConnCount();
        EXPECT_EQ(StatsMgr::GetActiveConnCount(), cnt == 0 ? 0u : 0u);
    }
}

TEST_F(StatisticsStatsMgrTest, SubMActiveConnCount_Positive_Decrements)
{
    for (uint32_t cnt : {3u, 0u, 1u}) {
        StatsMgr::mActiveConnCount.store(cnt);
        StatsMgr::SubMActiveConnCount();
        EXPECT_EQ(StatsMgr::GetActiveConnCount(), cnt == 0 ? 0u : (cnt - 1));
    }
}

TEST_F(StatisticsStatsMgrTest, GetStatsStr_AllTypes_ReturnsExpectedNames)
{
    StatsMgr mgr;
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::CONN_COUNT), "totalConnections");
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::ACTIVE_OPEN_COUNT), "activeConnections");
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::RX_PACKET_COUNT), "sendPackets");
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::TX_PACKET_COUNT), "receivePackets");
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::RX_BYTE_COUNT), "sendBytes");
    EXPECT_STREQ(mgr.GetStatsStr(StatsMgr::TX_BYTE_COUNT), "receiveBytes");
}

// ==================== StatsMgr: Output ====================

TEST_F(StatisticsStatsMgrTest, OutputAllStats_LocaltimeOk_OutputsJson)
{
    g_fakeLocaltimeRCall = 0;
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&FakeLocaltimeRAlternating));
    for (int round = 0; round < 4; ++round) {
        std::ostringstream oss;
        StatsMgr::AggregatedStats agg;
        StatsMgr::OutputAllStats(oss, TEST_PID, agg);
        const std::string s = oss.str();
        EXPECT_NE(s.find("\"pid\":\"1234\""), std::string::npos);
        EXPECT_NE(s.find("totalConnections"), std::string::npos);
        EXPECT_NE(s.find("activeConnections"), std::string::npos);
        EXPECT_NE(s.find("reTxCount"), std::string::npos);
        if (round % 2 == 0) {
            EXPECT_NE(s.find("timeStamp"), std::string::npos);
        } else {
            EXPECT_NE(s.find("\"timeStamp\":\"\""), std::string::npos);
        }
    }
}

TEST_F(StatisticsStatsMgrTest, OutputAllStats_LocaltimeFail_OutputsEmptyTimestamp)
{
    g_fakeLocaltimeRCall = 0;
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&FakeLocaltimeRAlternating));
    for (int round = 0; round < 4; ++round) {
        if (round == 1) {
            /* 失败且日志使能的轮次，模拟 gettimeofday 失败，覆盖 LogDefault 异常分支 */
            MOCKER_CPP(::gettimeofday).stubs().will(returnValue(-1));
        }
        if (round == 3) {
            /* 最后一轮(失败)关闭日志，覆盖 UBS_VLOG_ERR 使能/禁用两条路径 */
            ock::ubs::Logger::Instance().logLevel =
                static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);
        }
        std::ostringstream oss;
        StatsMgr::AggregatedStats agg;
        StatsMgr::OutputAllStats(oss, TEST_PID, agg);
        const std::string s = oss.str();
        EXPECT_NE(s.find("totalConnections"), std::string::npos);
        if (round % 2 == 1) {
            EXPECT_NE(s.find("\"timeStamp\":\"\""), std::string::npos);
        } else {
            EXPECT_NE(s.find("timeStamp"), std::string::npos);
        }
    }
    ock::ubs::Logger::Instance().logLevel = ock::ubs::LEVEL_INFO;
}

TEST_F(StatisticsStatsMgrTest, OutputAllStats_ErrLogDisabled_EmptyTimestamp)
{
    g_fakeLocaltimeRCall = 0;
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&FakeLocaltimeRAlternating));
    for (int round = 0; round < 4; ++round) {
        if (round == 3) {
            ock::ubs::Logger::Instance().logLevel =
                static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);
        }
        std::ostringstream oss;
        StatsMgr::AggregatedStats agg;
        StatsMgr::OutputAllStats(oss, TEST_PID, agg);
        if (round % 2 == 1) {
            EXPECT_NE(oss.str().find("\"timeStamp\":\"\""), std::string::npos);
        } else {
            EXPECT_NE(oss.str().find("timeStamp"), std::string::npos);
        }
    }
    ock::ubs::Logger::Instance().logLevel = ock::ubs::LEVEL_INFO;
}

TEST_F(StatisticsStatsMgrTest, OutputStats_RecorderNull_NoOutput)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    std::ostringstream oss;
    mgr.OutputStats(TEST_FD_42, oss);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(StatisticsStatsMgrTest, OutputStats_WithRecorder_OutputsRows)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    mgr.UpdateTraceStats(StatsMgr::TX_PACKET_COUNT, 3);
    std::ostringstream oss;
    mgr.OutputStats(TEST_FD_42, oss);
    const std::string s = oss.str();
    EXPECT_NE(s.find("42"), std::string::npos);
    EXPECT_NE(s.find("sendPackets"), std::string::npos);
    EXPECT_NE(s.find("3"), std::string::npos);
}

// ==================== StatsMgr: CLI data ====================

TEST_F(StatisticsStatsMgrTest, GetSocketCLIData_RecorderNull_ZeroesData)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    for (int round = 0; round < 2; ++round) {
        CLISocketData data;
        data.sendPackets = 99;
        CLISocketData *target = (round == 1) ? nullptr : &data;
        mgr.GetSocketCLIData(target);
        if (round == 0) {
            EXPECT_EQ(data.sendPackets, 0u);
            EXPECT_EQ(data.recvPackets, 0u);
            EXPECT_EQ(data.sendBytes, 0u);
            EXPECT_EQ(data.recvBytes, 0u);
            EXPECT_EQ(data.errorPackets, 0u);
            EXPECT_EQ(data.lostPackets, 0u);
        }
    }
}

TEST_F(StatisticsStatsMgrTest, GetSocketCLIData_RecorderNullDataNull_Noop)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    for (int round = 0; round < 2; ++round) {
        CLISocketData data;
        data.sendPackets = 99;
        CLISocketData *target = (round == 1) ? &data : nullptr;
        EXPECT_NO_FATAL_FAILURE(mgr.GetSocketCLIData(target));
        if (round == 1) {
            EXPECT_EQ(data.sendPackets, 0u);
        }
    }
}

TEST_F(StatisticsStatsMgrTest, GetSocketCLIData_WithRecorder_FillsCounts)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
    }
    /* 单一调用点覆盖 || 短路三条路径：recorder空/数据空/全非空 */
    for (int round = 0; round < 3; ++round) {
        CLISocketData data;
        CLISocketData *target = (round == 1) ? nullptr : &data;
        mgr.GetSocketCLIData(target);
        if (round == 0) {
            EXPECT_EQ(data.sendPackets, 1u);
            EXPECT_EQ(data.recvPackets, 1u);
            EXPECT_EQ(data.sendBytes, 1u);
            EXPECT_EQ(data.recvBytes, 1u);
            EXPECT_EQ(data.errorPackets, 0u);
            EXPECT_EQ(data.lostPackets, 0u);
        }
        if (round == 2) {
            mgr.m_recorder_vec.reset();
        }
    }
}

TEST_F(StatisticsStatsMgrTest, GetSocketCLIData_WithRecorderDataNull_NoCrash)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    for (int round = 0; round < 2; ++round) {
        CLISocketData data;
        data.sendPackets = 99;
        CLISocketData *target = (round == 1) ? &data : nullptr;
        EXPECT_NO_FATAL_FAILURE(mgr.GetSocketCLIData(target));
        if (round == 1) {
            EXPECT_EQ(data.sendPackets, 0u);
        }
    }
}

// ==================== StatsMgr: switch / boundary 补充 ====================

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_AllTypesNullRecorder_AllSwitchCasesHit)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    StatsMgr mgr;
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        EXPECT_NO_FATAL_FAILURE(
            mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1));
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 1u);
    EXPECT_EQ(StatsMgr::GetActiveConnCount(), 1u);
    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_AllTypesRecorderEnabled_AllSwitchCasesHit)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 1u);
    EXPECT_EQ(StatsMgr::GetActiveConnCount(), 1u);
    EXPECT_EQ((*mgr.m_recorder_vec)[StatsMgr::RX_PACKET_COUNT].GetCnt(), 1u);
    EXPECT_EQ((*mgr.m_recorder_vec)[StatsMgr::TX_PACKET_COUNT].GetCnt(), 1u);
    EXPECT_EQ((*mgr.m_recorder_vec)[StatsMgr::RX_BYTE_COUNT].GetCnt(), 1u);
    EXPECT_EQ((*mgr.m_recorder_vec)[StatsMgr::TX_BYTE_COUNT].GetCnt(), 1u);
}

TEST_F(StatisticsStatsMgrTest, UpdateTraceStats_OutOfRangeValue_DefaultBranchNoop)
{
    StatsMgr mgr;
    /* 单一调用点遍历全部枚举值 + 越界值，default 分支被覆盖 */
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX) + 1; ++t) {
        EXPECT_NO_FATAL_FAILURE(mgr.UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1));
    }
    EXPECT_EQ(StatsMgr::GetConnCount(), 1u);
    EXPECT_EQ(StatsMgr::GetActiveConnCount(), 1u);
}

TEST_F(StatisticsStatsMgrTest, OutputAllStats_NonZeroCounters_OutputsCountValues)
{
    StatsMgr::mConnCount.store(5);
    StatsMgr::mActiveConnCount.store(6);
    StatsMgr::mReTxCount.store(7);
    g_fakeLocaltimeRCall = 0;
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&FakeLocaltimeRAlternating));
    for (int round = 0; round < 4; ++round) {
        std::ostringstream oss;
        StatsMgr::AggregatedStats agg;
        StatsMgr::OutputAllStats(oss, TEST_PID, agg);
        const std::string s = oss.str();
        EXPECT_NE(s.find("\"totalConnections\":5"), std::string::npos);
        EXPECT_NE(s.find("\"activeConnections\":6"), std::string::npos);
        EXPECT_NE(s.find("\"reTxCount\":7"), std::string::npos);
    }
}

TEST_F(StatisticsStatsMgrTest, FillEmptyForm_SecondCall_OnceGuardShortCircuits)
{
    std::ostringstream oss;
    Recorder::GetTitle(oss);
    Recorder::FillEmptyForm(oss);
    const size_t lenAfterFirst = oss.str().length();
    Recorder::FillEmptyForm(oss);
    EXPECT_EQ(oss.str().length(), lenAfterFirst);
}

TEST_F(StatisticsStatsMgrTest, OutputStats_RecorderAllZero_OutputsDashRows)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    StatsMgr mgr;
    for (int round = 0; round < 2; ++round) {
        std::ostringstream oss;
        mgr.OutputStats(TEST_FD_42, oss);
        const std::string s = oss.str();
        if (round == 0) {
            EXPECT_NE(s.find("totalConnections"), std::string::npos);
            EXPECT_NE(s.find("sendPackets"), std::string::npos);
            EXPECT_NE(s.find("receiveBytes"), std::string::npos);
        } else {
            EXPECT_TRUE(s.empty());
        }
        mgr.m_recorder_vec.reset();
    }
}

TEST_F(StatisticsStatsMgrTest, SubMConnCount_BoundaryOne_BecomesZero)
{
    StatsMgr::mConnCount.store(1);
    StatsMgr::SubMConnCount();
    EXPECT_EQ(StatsMgr::GetConnCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, SubMActiveConnCount_BoundaryOne_BecomesZero)
{
    StatsMgr::mActiveConnCount.store(1);
    StatsMgr::SubMActiveConnCount();
    EXPECT_EQ(StatsMgr::GetActiveConnCount(), 0u);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ProfEnabledSkipsStartStop_StoresRetryCount)
{
    GlobalSetting::UBS_PROF_ENABLE = true;
    MOCKER_CPP(::umq_stats_tp_perf_start).expects(exactly(0)).will(returnValue(0));
    SetFakePerfContent("retry_count: 7\n");
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).expects(exactly(0)).will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), TEST_RETRY_COUNT_7);
}

TEST_F(StatisticsStatsMgrTest, UpdateReTxCount_ProfEnabledInfoGetFail_LogsError)
{
    GlobalSetting::UBS_PROF_ENABLE = true;
    MOCKER_CPP(::umq_stats_tp_perf_start).expects(exactly(0)).will(returnValue(0));
    g_fakeInfoGetRet = -1;
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).expects(exactly(0)).will(returnValue(0));

    StatsMgr::UpdateReTxCount(TEST_TRANS_MODE_UB);

    EXPECT_EQ(StatsMgr::GetReTxCount(), 0u);
}

// ==================== PrintStatsMgr ====================

class PrintStatsMgrTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_PROF_ENABLE = false;
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        StatsMgr::mReTxCount.store(0);
        StatsMgr::mConnCount.store(0);
        StatsMgr::mActiveConnCount.store(0);
        g_fakePerfBufLen = 0;
        g_fakeInfoGetRet = 0;
        memset_s(g_fakePerfBuf, sizeof(g_fakePerfBuf), 0, sizeof(g_fakePerfBuf));
        RemoveTestDir();
        CreateTestDir();
        /* 单一 GetPrintStatsMgr 调用点: 该函数为 ALWAYS_INLINE, 每次调用都产生一份
         * 函数局部 static 的 guard + 构造异常分支插桩(记到头文件 47 行), 收敛为 fixture
         * 内一个调用点可大幅压低 print_stats_mgr.h 的线 47 分支噪音(ut-gen profiling 陷阱 25)。 */
        mgr_ = PrintStatsMgr::GetPrintStatsMgr();
    }

    void TearDown() override
    {
        PrintStatsMgr *mgr = mgr_;
        mgr->StopStatsCollection();
        mgr->StopExternalDrain();
        if (mgr->m_ext_fd_ >= 0) {
            ::close(mgr->m_ext_fd_);
            mgr->m_ext_fd_ = -1;
        }
        mgr->m_ext_lines_.clear();
        mgr->m_trace_active_ = false;
        mgr->m_ext_active_ = false;
        mgr->m_running = false;
        mgr->m_ext_written_ = 0;
        mgr->m_ext_max_bytes_ = 64ULL * 1024 * 1024;
        mgr->ubsocketTraceTime = ock::ubs::UBSOCKET_TRACE_TIME_DEFAULT;
        mgr->ubsocketPerFileThreshold = ock::ubs::UBSOCKET_TRACE_FILE_SIZE_DEFAULT;
        mgr->m_archive_count_ = 0;
        mgr->m_archive_idx_ = 0;
        mgr->ubsocketTraceFilePath = "/tmp/ubsocket/log";
        RemoveTestDir();
        GlobalMockObject::verify();
        errno = 0;
    }

    void MockUmqStats()
    {
        MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
        MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));
    }

    PrintStatsMgr *mgr_ = nullptr;
};

TEST_F(PrintStatsMgrTest, SetExternalSink_ValidPath_OpensAndWritesHeader)
{
    const std::string path = TEST_PRINT_STATS_DIR + "/txstat.log";
    PrintStatsMgr *mgr = mgr_;
    EXPECT_TRUE(mgr->SetExternalSink(path, 1));
    EXPECT_GE(mgr->m_ext_fd_, 0);
    EXPECT_NE(ReadFileContent(path).find("[TX-STAT] version=1"), std::string::npos);
}

TEST_F(PrintStatsMgrTest, SetExternalSink_InvalidPath_ReturnsFalse)
{
    const std::string dirPath = TEST_PRINT_STATS_DIR + "/adir";
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    PrintStatsMgr *mgr = mgr_;
    EXPECT_FALSE(mgr->SetExternalSink(dirPath, 1));
}

TEST_F(PrintStatsMgrTest, SetExternalSink_Twice_ReplacesExistingFd)
{
    const std::string path1 = TEST_PRINT_STATS_DIR + "/a.log";
    const std::string path2 = TEST_PRINT_STATS_DIR + "/b.log";
    PrintStatsMgr *mgr = mgr_;
    ASSERT_TRUE(mgr->SetExternalSink(path1, 1));
    ASSERT_TRUE(mgr->SetExternalSink(path2, 1));
    EXPECT_EQ(mgr->m_ext_path_, path2);
    mgr->SubmitExternalLine("hello\n", 6);
    mgr->FlushExternal();
    EXPECT_NE(ReadFileContent(path2).find("hello\n"), std::string::npos);
    EXPECT_EQ(ReadFileContent(path1).find("hello\n"), std::string::npos);
}

TEST_F(PrintStatsMgrTest, SubmitExternalLine_InvalidArgs_Noop)
{
    PrintStatsMgr *mgr = mgr_;
    EXPECT_NO_FATAL_FAILURE(mgr->SubmitExternalLine(nullptr, 10));
    EXPECT_NO_FATAL_FAILURE(mgr->SubmitExternalLine("x", 0));
    EXPECT_NO_FATAL_FAILURE(mgr->SubmitExternalLine("x", -1));
    EXPECT_TRUE(mgr->m_ext_lines_.empty());
}

TEST_F(PrintStatsMgrTest, SubmitExternalLine_FdNotReady_DropsLine)
{
    PrintStatsMgr *mgr = mgr_;
    EXPECT_NO_FATAL_FAILURE(mgr->SubmitExternalLine("hello\n", 6));
    EXPECT_TRUE(mgr->m_ext_lines_.empty());
}

TEST_F(PrintStatsMgrTest, FlushExternal_WithQueuedLines_WritesToFile)
{
    const std::string path = TEST_PRINT_STATS_DIR + "/txstat.log";
    PrintStatsMgr *mgr = mgr_;
    ASSERT_TRUE(mgr->SetExternalSink(path, 1));
    mgr->SubmitExternalLine("line1\n", 6);
    mgr->SubmitExternalLine("line2\n", 6);
    mgr->FlushExternal();
    const std::string content = ReadFileContent(path);
    EXPECT_NE(content.find("line1\n"), std::string::npos);
    EXPECT_NE(content.find("line2\n"), std::string::npos);
}

TEST_F(PrintStatsMgrTest, FlushExternal_EmptyQueue_Noop)
{
    const std::string path = TEST_PRINT_STATS_DIR + "/txstat.log";
    PrintStatsMgr *mgr = mgr_;
    ASSERT_TRUE(mgr->SetExternalSink(path, 1));
    EXPECT_NO_FATAL_FAILURE(mgr->FlushExternal());
    EXPECT_GE(mgr->m_ext_fd_, 0);
}

TEST_F(PrintStatsMgrTest, FlushExternal_WriteExceedsThreshold_RotatesFile)
{
    const std::string path = TEST_PRINT_STATS_DIR + "/txstat.log";
    PrintStatsMgr *mgr = mgr_;
    ASSERT_TRUE(mgr->SetExternalSink(path, 1));
    mgr->m_ext_max_bytes_ = TEST_EXT_MAX_BYTES_1;
    mgr->SubmitExternalLine("0123456789\n", 11);
    mgr->FlushExternal();
    EXPECT_TRUE(std::filesystem::exists(path + ".1"));
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_ThenStop_ThreadLifecycle)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 40, TEST_TRANS_MODE_UB);
    EXPECT_TRUE(mgr->m_trace_active_);
    EXPECT_NE(mgr->m_event_loop, nullptr);
    mgr->StopStatsCollection();
    EXPECT_FALSE(mgr->m_trace_active_);
    EXPECT_EQ(mgr->m_event_loop, nullptr);
}

TEST_F(PrintStatsMgrTest, ProcessStats_WritesKpiJson)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    g_fakeLocaltimeRCall = 0;
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&FakeLocaltimeRAlternating));
    /* 单一 ProcessStats 调用点，交替覆盖 OutputAllStats 的 localtime 成功/失败路径 */
    for (int round = 0; round < 2; ++round) {
        EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    }
    const std::string content = ReadFileContent(TEST_PRINT_STATS_DIR + "/ubsocket_kpi.json");
    EXPECT_NE(content.find("timeStamp"), std::string::npos);
    EXPECT_NE(content.find("\"pid\":\"1234\""), std::string::npos);
    EXPECT_NE(content.find("totalConnections"), std::string::npos);
}

TEST_F(PrintStatsMgrTest, ProcessStats_OpenJsonFail_LogsError)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR + "/nonexistent_subdir";
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
}

TEST_F(PrintStatsMgrTest, OutputJson_AboveThreshold_ArchivesFile)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    mgr->ProcessStats();
    const std::string archiveName =
        TEST_PRINT_STATS_DIR + "/ubsocket_kpi_" + std::to_string(TEST_PID) + TEST_LISTENER_ARCHIVE_SUFFIX;
    EXPECT_TRUE(std::filesystem::exists(archiveName));
}

TEST_F(PrintStatsMgrTest, ArchiveJson_FileNotExist_Noop)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->m_archive_count_ = 1;
    EXPECT_NO_FATAL_FAILURE(mgr->ArchiveJSON(TEST_PRINT_STATS_DIR, TEST_PID, "nonexistent.json"));
}

TEST_F(PrintStatsMgrTest, EnsureExternalDrain_AndStop_ThreadLifecycle)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->EnsureExternalDrain();
    EXPECT_NE(mgr->m_event_loop, nullptr);
    mgr->StopExternalDrain();
    EXPECT_EQ(mgr->m_event_loop, nullptr);
}

// ==================== Statistics Listener (statistics.h 纯逻辑子集) ====================

class StatisticsListenerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        g_mockCloseCount = 0;
        g_mockSetsockoptRet = 0;
        g_mockSetsockoptCall = 0;
        g_mockSetsockoptFailOnCall = -1;
        g_mockEpollCreateRet = TEST_EPOLL_FD;
        g_mockEventfdRet = TEST_WAKEUP_FD;
        g_mockEpollCtlRet = 0;
        g_mockEpollCtlCall = 0;
        g_mockEpollCtlFailOnCall = -1;
        g_mockEpollWaitRet = 0;
        g_mockEventFd = -1;
        g_mockEventMask = 0;
        g_mockWriteRet = static_cast<ssize_t>(sizeof(uint64_t));
        g_mockReadRet = static_cast<ssize_t>(sizeof(uint64_t));
        g_mockTransportPoolGetRet = 0;
        g_mockGetRouteListRet = 0;
        g_mockRouteNum = 0;
        g_mockProfCombineRet = 0;
        g_mockProfInitRet = 0;
        g_mockRecvCmdId = CLICommand::INVALID;
        g_mockRecvType = CLITypeParam::INVALID;
        g_mockCtrlDataSize = 0;
        g_mockRecvMValue = 0.0;
        g_mockRecvSocketDataFailCall = -1;
        g_mockRecvSocketDataCall = 0;
        g_mockSendSocketDataMode = 0;
        g_mockSendSocketDataCall = 0;
        g_mockRecvPathEnable = false;
        g_mockRecvPathPayload.clear();
        LibcApi::socket_ptr = MockSocketSuccess;
        LibcApi::bind_ptr = MockBindSuccess;
        LibcApi::listen_ptr = MockListenSuccess;
        LibcApi::close_ptr = MockCloseCount;
    }

    void TearDown() override
    {
        ProbeManager::GetInstance().Stop();
        GlobalSetting::UBS_PROBE_ENABLED = false;
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sockets_.clear();
        GlobalMockObject::verify();
        LibcApi::socket_ptr = nullptr;
        LibcApi::bind_ptr = nullptr;
        LibcApi::listen_ptr = nullptr;
        LibcApi::close_ptr = nullptr;
        LibcApi::accept_ptr = nullptr;
        LibcApi::setsockopt_ptr = nullptr;
        LibcApi::epoll_create_ptr = nullptr;
        LibcApi::epoll_ctl_ptr = nullptr;
        LibcApi::epoll_wait_ptr = nullptr;
        LibcApi::write_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        errno = 0;
    }

    UmqSocketPtr RegisterSocket(int fd, SocketCreateType createType)
    {
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(fd);
        umqSock->create_type_ = createType;
        ArraySet<Socket>::GetInstance().OverrideItem(fd, umqSock.Get());
        sockets_.push_back(umqSock);
        return umqSock;
    }

    /* 装配数据面条目，使 GetUmqTxOps()/GetUmqRxOps() 返回非空（握手前为空）。
     * GetAllTxStatData/GetAllRxStatData 会解引用这两个返回指针，未装配会导致空指针崩溃。
     * 必须在 GlobalSetting::UBS_MONITOR_ENABLE 设好之后再调用：计数器分配由
     * DataTxOps/DataRxOps 构造时的 UBS_MONITOR_ENABLE 决定。 */
    void WireDataPlane(const UmqSocketPtr &sock)
    {
        ASSERT_NE(sock->ReinitTxOps(), nullptr);
    }

    void MockListenerIo()
    {
        LibcApi::accept_ptr = MockAcceptSuccess;
        LibcApi::setsockopt_ptr = MockSetsockopt;
        MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    }

    /* GetAll*Data 依赖的 umq 数据面 API 全量 mock, 覆盖成功返回路径。 */
    void MockUmqDataApi()
    {
        MOCKER_CPP(::umq_stats_flow_control_get).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_qbuf_pool_get).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_info_get).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_io_get).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_perf_get).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));
        MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
        MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    }

    /* 单个 Process*Request 内部有两次 SendSocketData: 先 header 后 data。
     * mode=2 使第 2 次(data)失败, 覆盖 "Failed to send <Data>" 返回路径。 */
    void MockSendHeaderOkDataFail()
    {
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
        g_mockSendSocketDataMode = 2;
    }

    std::vector<UmqSocketPtr> sockets_;
};

TEST_F(StatisticsListenerTest, GetSockNum_FiltersByTypeAndCreateType)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    RegisterSocket(TEST_FD_43, SOCK_CREATE_TYPE_LISTEN);
    UmqSocketPtr tcp = RegisterSocket(TEST_FD_44, SOCK_CREATE_TYPE_CONNECT);
    tcp->type_ = SocketType::SOCK_TYPE_TCP;

    {
        Statistics::Listener listener;
        EXPECT_EQ(listener.GetSockNum(), 2u);
        EXPECT_EQ(listener.GetSockNum(true), 1u);
    }
    EXPECT_GT(g_mockCloseCount, 0);
}

TEST_F(StatisticsListenerTest, GetAllSocketData_FillsSocketId)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    RegisterSocket(TEST_FD_43, SOCK_CREATE_TYPE_CONNECT);

    Statistics::Listener listener;
    const uint32_t sockNum = listener.GetSockNum();
    CLISocketData data[2] = {};
    listener.GetAllSocketData(data, sockNum);
    EXPECT_EQ(sockNum, 2u);
    EXPECT_EQ(data[0].socketId, static_cast<uint64_t>(TEST_FD_42));
    EXPECT_EQ(data[1].socketId, static_cast<uint64_t>(TEST_FD_43));
}

TEST_F(StatisticsListenerTest, GetAllSocketData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllSocketData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, GetFirstUmqHandle_NoSockets_ReturnsInvalid)
{
    Statistics::Listener listener;
    EXPECT_EQ(listener.GetFirstUmqHandle(), static_cast<uint64_t>(UMQ_INVALID_HANDLE));
}

TEST_F(StatisticsListenerTest, GetFirstUmqHandle_WithSocket_ReturnsHandle)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
    MOCKER_CPP(::umq_destroy).stubs().will(returnValue(UMQ_SUCCESS));
    UmqSocketPtr umqSock = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    umqSock->umq_handle_ = TEST_UMQ_HANDLE;

    Statistics::Listener listener;
    EXPECT_EQ(listener.GetFirstUmqHandle(), TEST_UMQ_HANDLE);
}

TEST_F(StatisticsListenerTest, DealDelayOperation_InvalidType_SetsRetCode)
{
    Statistics::Listener listener;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader ctrlHeader;
    ctrlHeader.mType = CLITypeParam::INVALID;
    listener.DealDelayOperation(delayHeader, outStr, ctrlHeader);
    EXPECT_EQ(delayHeader.retCode, -1);
    EXPECT_TRUE(outStr.empty());
}

// ==================== Listener 构造失败分支 ====================

TEST_F(StatisticsListenerTest, Ctor_SocketFail_Throws)
{
    LibcApi::socket_ptr = MockSocketFail;
    EXPECT_THROW(Statistics::Listener listener, std::runtime_error);
}

TEST_F(StatisticsListenerTest, Ctor_BindFail_Throws)
{
    LibcApi::bind_ptr = MockBindFail;
    EXPECT_THROW(Statistics::Listener listener, std::runtime_error);
}

TEST_F(StatisticsListenerTest, Ctor_ListenFail_Throws)
{
    LibcApi::listen_ptr = MockListenFail;
    EXPECT_THROW(Statistics::Listener listener, std::runtime_error);
}

TEST_F(StatisticsListenerTest, GetFd_ReturnsUdsFd)
{
    Statistics::Listener listener;
    EXPECT_EQ(listener.GetFd(), TEST_FD_42);
}

// ==================== InternalEpollEnable ====================

TEST_F(StatisticsListenerTest, InternalEpollEnable_Success_Enables)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    LibcApi::epoll_ctl_ptr = MockEpollCtl;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));

    EXPECT_EQ(listener.InternalEpollEnable(), 0);
    EXPECT_TRUE(listener.m_internal_epoll_enable);
    EXPECT_EQ(listener.m_epoll_fd, TEST_EPOLL_FD);
    EXPECT_EQ(listener.m_wakeup_fd, TEST_WAKEUP_FD);
}

TEST_F(StatisticsListenerTest, InternalEpollEnable_EpollCreateFail_ReturnsMinusOne)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    g_mockEpollCreateRet = -1;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));

    EXPECT_EQ(listener.InternalEpollEnable(), -1);
    EXPECT_FALSE(listener.m_internal_epoll_enable);
}

TEST_F(StatisticsListenerTest, InternalEpollEnable_EventfdFail_ReturnsMinusOne)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    LibcApi::epoll_ctl_ptr = MockEpollCtl;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));
    g_mockEventfdRet = -1;

    EXPECT_EQ(listener.InternalEpollEnable(), -1);
    EXPECT_FALSE(listener.m_internal_epoll_enable);
    EXPECT_EQ(listener.m_epoll_fd, -1);
}

TEST_F(StatisticsListenerTest, InternalEpollEnable_EpollCtlWakeupFail_ReturnsMinusOne)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    LibcApi::epoll_ctl_ptr = MockEpollCtl;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));
    g_mockEpollCtlFailOnCall = 1;

    EXPECT_EQ(listener.InternalEpollEnable(), -1);
    EXPECT_FALSE(listener.m_internal_epoll_enable);
}

TEST_F(StatisticsListenerTest, InternalEpollEnable_EpollCtlUdsFail_ReturnsMinusOne)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    LibcApi::epoll_ctl_ptr = MockEpollCtl;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));
    g_mockEpollCtlFailOnCall = 2;

    EXPECT_EQ(listener.InternalEpollEnable(), -1);
    EXPECT_FALSE(listener.m_internal_epoll_enable);
}

// ==================== Poll / ProcessEpollEvents ====================

TEST_F(StatisticsListenerTest, ProcessEpollEvents_WakeupFd_ReturnsFalse)
{
    Statistics::Listener listener;
    LibcApi::epoll_create_ptr = MockEpollCreate;
    LibcApi::epoll_ctl_ptr = MockEpollCtl;
    LibcApi::read_ptr = MockRead;
    MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));
    ASSERT_EQ(listener.InternalEpollEnable(), 0);

    struct epoll_event events[1] = {};
    events[0].data.fd = TEST_WAKEUP_FD;
    EXPECT_FALSE(listener.ProcessEpollEvents(events, 1));
}

TEST_F(StatisticsListenerTest, ProcessEpollEvents_UdsFd_ProcessesAndReturnsTrue)
{
    Statistics::Listener listener;
    MockListenerIo();
    g_mockRecvCmdId = CLICommand::STAT;

    struct epoll_event events[1] = {};
    events[0].data.fd = TEST_FD_42;
    events[0].events = EPOLLIN;
    EXPECT_TRUE(listener.ProcessEpollEvents(events, 1));
    EXPECT_GT(g_mockSendSocketDataCall, 0);
}

TEST_F(StatisticsListenerTest, ProcessEpollEvents_OtherFd_ReturnsTrue)
{
    Statistics::Listener listener;
    struct epoll_event events[1] = {};
    events[0].data.fd = TEST_FD_43;
    EXPECT_TRUE(listener.ProcessEpollEvents(events, 1));
}

TEST_F(StatisticsListenerTest, Poll_EpollWaitFail_LogsError)
{
    Statistics::Listener listener;
    LibcApi::epoll_wait_ptr = MockEpollWait;
    g_mockEpollWaitRet = -1;
    EXPECT_NO_FATAL_FAILURE(listener.Poll());
}

TEST_F(StatisticsListenerTest, Poll_EpollWaitOk_ProcessesEvent)
{
    Statistics::Listener listener;
    LibcApi::epoll_wait_ptr = MockEpollWait;
    MockListenerIo();
    g_mockRecvCmdId = CLICommand::STAT;
    g_mockEpollWaitRet = 1;
    g_mockEventFd = TEST_FD_42;
    g_mockEventMask = EPOLLIN;
    EXPECT_NO_FATAL_FAILURE(listener.Poll());
    EXPECT_GT(g_mockSendSocketDataCall, 0);
}

// ==================== WakeupEpoll / AckWakeupEpoll ====================

TEST_F(StatisticsListenerTest, WakeupEpoll_Success_Writes)
{
    Statistics::Listener listener;
    LibcApi::write_ptr = MockWrite;
    EXPECT_NO_FATAL_FAILURE(listener.WakeupEpoll());
}

TEST_F(StatisticsListenerTest, WakeupEpoll_Fail_LogsError)
{
    Statistics::Listener listener;
    LibcApi::write_ptr = MockWrite;
    g_mockWriteRet = -1;
    EXPECT_NO_FATAL_FAILURE(listener.WakeupEpoll());
}

TEST_F(StatisticsListenerTest, AckWakeupEpoll_Success_Reads)
{
    Statistics::Listener listener;
    LibcApi::read_ptr = MockRead;
    EXPECT_NO_FATAL_FAILURE(listener.AckWakeupEpoll());
}

TEST_F(StatisticsListenerTest, AckWakeupEpoll_Fail_LogsError)
{
    Statistics::Listener listener;
    LibcApi::read_ptr = MockRead;
    g_mockReadRet = -1;
    EXPECT_NO_FATAL_FAILURE(listener.AckWakeupEpoll());
}

// ==================== Process() 主循环 ====================

TEST_F(StatisticsListenerTest, Process_ErrorEvent_ReturnsEarly)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLERR | EPOLLHUP));
}

TEST_F(StatisticsListenerTest, Process_AcceptFail_Returns)
{
    Statistics::Listener listener;
    LibcApi::accept_ptr = MockAcceptFail;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
}

TEST_F(StatisticsListenerTest, Process_SetSndTimeoutFail_Returns)
{
    Statistics::Listener listener;
    MockListenerIo();
    LibcApi::accept_ptr = MockAcceptSuccess;
    g_mockSetsockoptFailOnCall = 1;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
}

TEST_F(StatisticsListenerTest, Process_SetRcvTimeoutFail_Returns)
{
    Statistics::Listener listener;
    MockListenerIo();
    g_mockSetsockoptFailOnCall = 2;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
}

TEST_F(StatisticsListenerTest, Process_RecvHeaderFail_Returns)
{
    Statistics::Listener listener;
    MockListenerIo();
    g_mockRecvSocketDataFailCall = 1;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
}

TEST_F(StatisticsListenerTest, Process_AllCmdIds_Dispatches)
{
    Statistics::Listener listener;
    MockListenerIo();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
    MOCKER(&Profiling::Reset).stubs().will(invoke(&MockProfReset));
    MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));

    const CLICommand cmds[] = {
        CLICommand::STAT,       CLICommand::TOPO,         CLICommand::DELAY,   CLICommand::FLOW_CONTROL,
        CLICommand::QBUF_POOL,  CLICommand::UMQ_INFO,     CLICommand::IO,      CLICommand::UMQ,
        CLICommand::PROBE,      CLICommand::TX_STAT,      CLICommand::RX_STAT, CLICommand::INVALID,
    };
    for (CLICommand cmd : cmds) {
        g_mockRecvCmdId = cmd;
        EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    }
}

// ==================== Process*Request ====================

TEST_F(StatisticsListenerTest, ProcessStatRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::STAT;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(g_mockSendSocketDataCall, 2);
}

TEST_F(StatisticsListenerTest, ProcessStatRequest_SendHeaderFail_Returns)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 1;
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessStatRequest_SendDataFail_Returns)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 2;
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessFlowControlRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessQbufPoolRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessUmqInfoRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessIoRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessUmqRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessTxStatRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessRxStatRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessRequests_SendHeaderFail_AllReturnEarly)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 3;
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
}

TEST_F(StatisticsListenerTest, ProcessRequests_SendDataFail_AllReturnEarly)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
    g_mockSendSocketDataMode = 4;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_QUERY;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, GetAllData_SkipsTcpAndListen)
{
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(umq);
    RegisterSocket(TEST_FD_43, SOCK_CREATE_TYPE_LISTEN);
    UmqSocketPtr tcp = RegisterSocket(TEST_FD_44, SOCK_CREATE_TYPE_CONNECT);
    tcp->type_ = SocketType::SOCK_TYPE_TCP;
    MOCKER_CPP(::umq_stats_flow_control_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_qbuf_pool_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_info_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_io_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_perf_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLISocketData sockData[2];
    listener.GetAllSocketData(sockData, 2);
    CLIFlowControlData fcData[2];
    listener.GetAllFlowControlData(fcData, 2);
    CLIQbufPoolData qbufData[2];
    listener.GetAllQbufPoolData(qbufData, 2);
    CLIUmqInfoData umqInfoData[2];
    listener.GetAllUmqInfoData(umqInfoData, 2);
    CLIIoPacketData ioData[2];
    listener.GetAllIoPacketData(ioData, 2);
    CLIUmqPerfData perfData[2];
    listener.GetAllUmqPerfData(perfData, 2);
    CLITxStatData txData[2];
    listener.GetAllTxStatData(txData, 2);
    CLIRxStatData rxData[2];
    listener.GetAllRxStatData(rxData, 2);
    EXPECT_EQ(sockData[0].socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllQbufPoolData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllQbufPoolData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, GetAllUmqInfoData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllUmqInfoData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, GetAllIoPacketData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllIoPacketData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, GetAllUmqPerfData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllUmqPerfData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, ProcessProbeRequest_SendOk_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    CLIMessage msg;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_Query_CombineOk)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_QUERY;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_GT(g_mockSendSocketDataCall, 0);
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_Query_CombineFail)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
    g_mockProfCombineRet = UBS_ERROR;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_QUERY;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_Reset_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Reset).stubs().will(invoke(&MockProfReset));
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_RESET;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_EnableTrace_InitOk)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_ENABLE;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_TRUE(GlobalSetting::UBS_PROF_ENABLE);
    GlobalSetting::UBS_PROF_ENABLE = false;
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_EnableTrace_InitFail)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));
    g_mockProfInitRet = UBS_ERROR;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_ENABLE;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    GlobalSetting::UBS_PROF_ENABLE = false;
}

// ==================== ProcessTopoRequest / GetUmqPoolStats ====================

TEST_F(StatisticsListenerTest, ProcessTopoRequest_GetRouteFail_Returns)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    g_mockGetRouteListRet = -1;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
}

TEST_F(StatisticsListenerTest, ProcessTopoRequest_ZeroRoutes_Returns)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    g_mockGetRouteListRet = 0;
    g_mockRouteNum = 0;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
}

TEST_F(StatisticsListenerTest, ProcessTopoRequest_Success_SendsRouteList)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockGetRouteListRet = 0;
    g_mockRouteNum = 2;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
    EXPECT_GT(g_mockSendSocketDataCall, 0);
}

TEST_F(StatisticsListenerTest, GetUmqPoolStats_InvalidHandle_Returns)
{
    Statistics::Listener listener;
    CLIDataHeader header = {};
    listener.GetUmqPoolStats(header);
    EXPECT_EQ(header.poolTotalNum, 0u);
}

TEST_F(StatisticsListenerTest, GetUmqPoolStats_PoolGetFail_Returns)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_stats_transport_pool_get).stubs().will(invoke(&MockTransportPoolGet));
    UmqEidTable::Instance().Add(UmqSetting::UMQ_LOCAL_EID, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    g_mockTransportPoolGetRet = -1;
    CLIDataHeader header = {};
    listener.GetUmqPoolStats(header);
    EXPECT_EQ(header.poolTotalNum, 0u);
    UmqEidTable::Instance().Clean();
}

TEST_F(StatisticsListenerTest, GetUmqPoolStats_Success_FillsHeader)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_stats_transport_pool_get).stubs().will(invoke(&MockTransportPoolGet));
    UmqEidTable::Instance().Add(UmqSetting::UMQ_LOCAL_EID, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    g_mockTransportPoolGetRet = 0;
    CLIDataHeader header = {};
    listener.GetUmqPoolStats(header);
    EXPECT_EQ(header.poolTotalNum, 10u);
    EXPECT_EQ(header.poolAvailableNum, 5u);
    EXPECT_EQ(header.poolInUseNum, 5u);
    UmqEidTable::Instance().Clean();
}

// ==================== GetAll*Data ====================

TEST_F(StatisticsListenerTest, GetAllFlowControlData_FillsData)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    MOCKER_CPP(::umq_stats_flow_control_get).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLIFlowControlData data;
    listener.GetAllFlowControlData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllQbufPoolData_FillsData)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    MOCKER_CPP(::umq_stats_qbuf_pool_get).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLIQbufPoolData data;
    listener.GetAllQbufPoolData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllUmqInfoData_FillsData)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    MOCKER_CPP(::umq_info_get).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLIUmqInfoData data;
    listener.GetAllUmqInfoData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllIoPacketData_FillsData)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    MOCKER_CPP(::umq_stats_io_get).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLIIoPacketData data;
    listener.GetAllIoPacketData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllUmqPerfData_FillsData)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    MOCKER_CPP(::umq_stats_perf_get).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&FakeTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(0));
    Statistics::Listener listener;
    CLIUmqPerfData data;
    listener.GetAllUmqPerfData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllProbeData_Empty_ReturnsEmpty)
{
    Statistics::Listener listener;
    std::vector<CLIProbeData> out;
    listener.GetAllProbeData(out);
    EXPECT_TRUE(out.empty());
}

TEST_F(StatisticsListenerTest, GetAllTxStatData_CountersNonNull_Copies)
{
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(umq);
    Statistics::Listener listener;
    CLITxStatData data;
    listener.GetAllTxStatData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllTxStatData_CountersNull_Zeros)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(umq);
    Statistics::Listener listener;
    CLITxStatData data;
    listener.GetAllTxStatData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
    EXPECT_EQ(data.post_err[0], 0u);
}

TEST_F(StatisticsListenerTest, GetAllRxStatData_CountersNonNull_Copies)
{
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(umq);
    Statistics::Listener listener;
    CLIRxStatData data;
    listener.GetAllRxStatData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
}

TEST_F(StatisticsListenerTest, GetAllRxStatData_CountersNull_Zeros)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(umq);
    Statistics::Listener listener;
    CLIRxStatData data;
    listener.GetAllRxStatData(&data, 1);
    EXPECT_EQ(data.socketId, static_cast<uint64_t>(TEST_FD_42));
    EXPECT_EQ(data.poll_err[0], 0u);
}

// ==================== UmqSocket 生产路径 (umq_socket.o 拷贝) ====================

TEST_F(StatisticsListenerTest, UmqSocketOutputStats_StatsMgrPresent_OutputsRows)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    ASSERT_NE(umq->GetStatsMgr(), nullptr);
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        umq->GetStatsMgr()->UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
    }
    std::ostringstream oss;
    umq->OutputStats(oss);
    const std::string s = oss.str();
    EXPECT_NE(s.find(std::to_string(TEST_FD_42)), std::string::npos);
    EXPECT_NE(s.find("sendPackets"), std::string::npos);
}

TEST_F(StatisticsListenerTest, UmqSocketOutputStats_StatsMgrAbsent_Noop)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    EXPECT_EQ(umq->GetStatsMgr(), nullptr);
    std::ostringstream oss;
    EXPECT_NO_FATAL_FAILURE(umq->OutputStats(oss));
    EXPECT_TRUE(oss.str().empty());
    GlobalSetting::UBS_MONITOR_ENABLE = true;
}

TEST_F(StatisticsListenerTest, UmqSocketGetSocketCLIData_StatsMgrPresent_FillsCounts)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    ASSERT_NE(umq->GetStatsMgr(), nullptr);
    for (uint32_t t = 0; t <= static_cast<uint32_t>(StatsMgr::TRACE_STATE_TYPE_MAX); ++t) {
        umq->GetStatsMgr()->UpdateTraceStats(static_cast<StatsMgr::trace_stats_type>(t), 1);
    }
    CLISocketData data;
    umq->GetSocketCLIData(&data);
    EXPECT_EQ(data.sendPackets, 1u);
    EXPECT_EQ(data.recvPackets, 1u);
    EXPECT_EQ(data.errorPackets, 0u);
}

TEST_F(StatisticsListenerTest, UmqSocketGetSocketCLIData_StatsMgrAbsent_Noop)
{
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    UmqSocketPtr umq = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    EXPECT_EQ(umq->GetStatsMgr(), nullptr);
    CLISocketData data;
    data.sendPackets = 99;
    EXPECT_NO_FATAL_FAILURE(umq->GetSocketCLIData(&data));
    GlobalSetting::UBS_MONITOR_ENABLE = true;
}

// ==================== RecvCmd / SendCmd / ProcessStats ====================

TEST_F(StatisticsListenerTest, RecvCmd_DataSizeExceedsCache_ReturnsMinusOne)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    g_mockCtrlDataSize = 9000;
    Listener::CtrlHead ctrl;
    EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
}

TEST_F(StatisticsListenerTest, RecvCmd_DataSizeZero_ReturnsZero)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    g_mockCtrlDataSize = 0;
    Listener::CtrlHead ctrl;
    EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), 0);
}

TEST_F(StatisticsListenerTest, RecvCmd_DataRecvFail_ReturnsMinusOne)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    g_mockCtrlDataSize = 10;
    g_mockRecvSocketDataFailCall = 2;
    Listener::CtrlHead ctrl;
    EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
}

TEST_F(StatisticsListenerTest, RecvCmd_HeaderRecvFail_ReturnsMinusOne)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    g_mockRecvSocketDataFailCall = 1;
    Listener::CtrlHead ctrl;
    EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
}

TEST_F(StatisticsListenerTest, RecvCmd_Success_ReturnsZero)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    g_mockCtrlDataSize = 10;
    Listener::CtrlHead ctrl;
    EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), 0);
}

TEST_F(StatisticsListenerTest, SendCmd_DataSizeZero_ReturnsZero)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    Listener::CtrlHead ctrl;
    ctrl.m_data_size = 0;
    EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, nullptr), 0);
}

TEST_F(StatisticsListenerTest, SendCmd_Success_ReturnsZero)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    Listener::CtrlHead ctrl;
    ctrl.m_data_size = 10;
    EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, "0123456789"), 0);
}

TEST_F(StatisticsListenerTest, SendCmd_HeaderSendFail_ReturnsMinusOne)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 1;
    Listener::CtrlHead ctrl;
    ctrl.m_data_size = 10;
    EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, "0123456789"), -1);
}

TEST_F(StatisticsListenerTest, SendCmd_DataSendFail_ReturnsMinusOne)
{
    TestableListener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 2;
    Listener::CtrlHead ctrl;
    ctrl.m_data_size = 10;
    EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, "0123456789"), -1);
}

TEST_F(StatisticsListenerTest, ProcessStats_WithSocket_OutputsTitle)
{
    RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    TestableListener listener;
    EXPECT_NO_FATAL_FAILURE(listener.TestProcessStats());
    EXPECT_NE(listener.m_oss.str().find("fd"), std::string::npos);
}

// ==================== PrintStatsMgr 异常/事件循环分支覆盖 ====================


bool WaitForFile(const std::string &path)
{
    for (int i = 0; i < 20; ++i) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::filesystem::exists(path);
}

bool WaitForFileContent(const std::string &path, const std::string &needle)
{
    for (int i = 0; i < 20; ++i) {
        if (ReadFileContent(path).find(needle) != std::string::npos) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ReadFileContent(path).find(needle) != std::string::npos;
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_DiskLimitZero_ClampsBudget)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 0, TEST_TRANS_MODE_UB);
    EXPECT_EQ(mgr->ubsocketPerFileThreshold, 1u);
    EXPECT_EQ(mgr->m_archive_count_, 1u);
    mgr->StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_EmptyTracePath_UsesDefaultDir)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, "", 40, TEST_TRANS_MODE_UB);
    EXPECT_EQ(mgr->ubsocketTraceFilePath, "/tmp/ubsocket/log");
    mgr->StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_Twice_ReusesEventLoop)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 40, TEST_TRANS_MODE_UB);
    std::thread *first = mgr->m_event_loop;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 40, TEST_TRANS_MODE_UB);
    EXPECT_EQ(mgr->m_event_loop, first);
    mgr->StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, EventLoop_TraceActive_RunsProcessStats)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 40, TEST_TRANS_MODE_UB);
    const std::string kpi = TEST_PRINT_STATS_DIR + "/ubsocket_kpi.json";
    EXPECT_TRUE(WaitForFile(kpi));
    mgr->StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, EventLoop_ExternalDrainOnly_WritesLines)
{
    PrintStatsMgr *mgr = mgr_;
    const std::string path = TEST_PRINT_STATS_DIR + "/ext.log";
    ASSERT_TRUE(mgr->SetExternalSink(path, 1));
    mgr->EnsureExternalDrain();
    mgr->SubmitExternalLine("line1\n", 6);
    EXPECT_TRUE(WaitForFileContent(path, "line1\n"));
    mgr->StopExternalDrain();
}

TEST_F(PrintStatsMgrTest, StopStatsCollection_ExtActive_KeepsLoopRunning)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->EnsureExternalDrain();
    std::thread *t = mgr->m_event_loop;
    mgr->StopStatsCollection();
    EXPECT_EQ(mgr->m_event_loop, t);
    mgr->StopExternalDrain();
    EXPECT_EQ(mgr->m_event_loop, nullptr);
}

TEST_F(PrintStatsMgrTest, StopExternalDrain_TraceActive_KeepsLoopRunning)
{
    PrintStatsMgr *mgr = mgr_;
    mgr->StartStatsCollection(1, TEST_PRINT_STATS_DIR, 40, TEST_TRANS_MODE_UB);
    std::thread *t = mgr->m_event_loop;
    mgr->StopExternalDrain();
    EXPECT_EQ(mgr->m_event_loop, t);
    mgr->StopStatsCollection();
    EXPECT_EQ(mgr->m_event_loop, nullptr);
}

TEST_F(PrintStatsMgrTest, SetExternalSink_EmptyPath_ReturnsFalse)
{
    PrintStatsMgr *mgr = mgr_;
    EXPECT_FALSE(mgr->SetExternalSink("", 1));
    EXPECT_LT(mgr->m_ext_fd_, 0);
}

TEST_F(PrintStatsMgrTest, DrainExternalLines_FdInvalidWithQueuedLines_DropsBatch)
{
    PrintStatsMgr *mgr = mgr_;
    const std::string path = TEST_PRINT_STATS_DIR + "/ext.log";
    ASSERT_TRUE(mgr->SetExternalSink(path, 1));
    mgr->SubmitExternalLine("hello\n", 6);
    mgr->m_ext_fd_ = -1;
    mgr->FlushExternal();
    EXPECT_NO_FATAL_FAILURE(mgr->FlushExternal());
    EXPECT_TRUE(mgr->m_ext_lines_.empty());
}

TEST_F(PrintStatsMgrTest, ArchiveJSON_ArchiveCountZero_Returns)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 0;
    mgr->m_archive_idx_ = 0;
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    EXPECT_TRUE(std::filesystem::exists(TEST_PRINT_STATS_DIR + "/ubsocket_kpi.json"));
    const std::string archive =
        TEST_PRINT_STATS_DIR + "/ubsocket_kpi_" + std::to_string(TEST_PID) + ".1.json";
    EXPECT_FALSE(std::filesystem::exists(archive));
}

TEST_F(PrintStatsMgrTest, ArchiveJSON_RenameFail_LogsError)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    const std::string archive =
        TEST_PRINT_STATS_DIR + "/ubsocket_kpi_" + std::to_string(TEST_PID) + ".1.json";
    std::error_code ec;
    std::filesystem::create_directory(archive, ec);
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    EXPECT_TRUE(std::filesystem::is_directory(archive));
}

TEST_F(PrintStatsMgrTest, ArchiveJSON_ChmodFail_LogsError)
{
    MockUmqStats();
    MOCKER_CPP(::chmod).stubs().will(returnValue(-1));
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
}

// ==================== PrintStatsMgr 剩余分支: CreateDirectory 空路径 / log 宏使能·禁用 ====================

/* CreateDirectory 的空路径早退分支(297 行 `if (path.empty())` 的 true 路径)。
 * 既有用例均以非空路径调用,该分支未被执行。 */
TEST_F(PrintStatsMgrTest, CreateDirectory_EmptyPath_ReturnsEarly)
{
    PrintStatsMgr *mgr = mgr_;
    EXPECT_NO_FATAL_FAILURE(mgr->CreateDirectory(""));
}

/* ArchiveJSON 成功轮转后 UBS_VLOG_DEBUG 的“使能”分支(352 行)。默认 logLevel=INFO 时
 * DEBUG(0) >= INFO(1) 为假,使能分支未被执行;降到 LEVEL_DEBUG 后走 Logv 打印。 */
TEST_F(PrintStatsMgrTest, ArchiveJSON_DebugLogEnabled_LogsRotation)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = ock::ubs::LEVEL_DEBUG;
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    ock::ubs::Logger::Instance().logLevel = savedLevel;
    const std::string archive =
        TEST_PRINT_STATS_DIR + "/ubsocket_kpi_" + std::to_string(TEST_PID) + ".1.json";
    EXPECT_TRUE(std::filesystem::exists(archive));
}

/* ArchiveJSON rename 失败路径 UBS_VLOG_ERR 的“禁用”分支(343 行)。
 * 既有用例默认 logLevel=INFO(ERR 使能),禁用分支未被执行;提到 LEVEL_COUNT 后跳过 Logv。 */
TEST_F(PrintStatsMgrTest, ArchiveJSON_RenameFail_ErrLogDisabled_NoThrow)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    const std::string archive =
        TEST_PRINT_STATS_DIR + "/ubsocket_kpi_" + std::to_string(TEST_PID) + ".1.json";
    std::error_code ec;
    std::filesystem::create_directory(archive, ec);
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    ock::ubs::Logger::Instance().logLevel = savedLevel;
    EXPECT_TRUE(std::filesystem::is_directory(archive));
}

/* ArchiveJSON chmod 失败路径 UBS_VLOG_ERR 的“禁用”分支(348 行)。 */
TEST_F(PrintStatsMgrTest, ArchiveJSON_ChmodFail_ErrLogDisabled_NoThrow)
{
    MockUmqStats();
    MOCKER_CPP(::chmod).stubs().will(returnValue(-1));
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR;
    mgr->ubsocketPerFileThreshold = 0;
    mgr->m_archive_count_ = 1;
    mgr->m_archive_idx_ = 0;
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    ock::ubs::Logger::Instance().logLevel = savedLevel;
}

/* OutputJSON fopen 失败路径 UBS_VLOG_ERR 的“禁用”分支(374 行)。 */
TEST_F(PrintStatsMgrTest, OutputJson_OpenFail_ErrLogDisabled_NoThrow)
{
    MockUmqStats();
    PrintStatsMgr *mgr = mgr_;
    mgr->pidVal = TEST_PID;
    mgr->ubsocketTraceFilePath = TEST_PRINT_STATS_DIR + "/nonexistent_subdir";
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessStats());
    ock::ubs::Logger::Instance().logLevel = savedLevel;
}

// ==================== statistics.h 覆盖率补充: GetAll*Data 截断 / GetFirstUmqHandle ====================

/* 2 个合格 socket、请求 1 个, 第 2 个 socket 命中 `doneNum >= sockNum` 早退分支
 * (statistics.h 各 GetAll*Data 的 ForEach 回调头部 return)。 */
TEST_F(StatisticsListenerTest, GetAllData_RequestedSockNum_StopsAfterLimit)
{
    UmqSocketPtr a = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(a);
    UmqSocketPtr b = RegisterSocket(TEST_FD_43, SOCK_CREATE_TYPE_CONNECT);
    WireDataPlane(b);
    MockUmqDataApi();

    Statistics::Listener listener;
    CLISocketData sockData[1] = {};
    listener.GetAllSocketData(sockData, 1);
    EXPECT_EQ(sockData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIFlowControlData fcData[1] = {};
    listener.GetAllFlowControlData(fcData, 1);
    EXPECT_EQ(fcData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIQbufPoolData qbufData[1] = {};
    listener.GetAllQbufPoolData(qbufData, 1);
    EXPECT_EQ(qbufData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIUmqInfoData umqInfoData[1] = {};
    listener.GetAllUmqInfoData(umqInfoData, 1);
    EXPECT_EQ(umqInfoData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIIoPacketData ioData[1] = {};
    listener.GetAllIoPacketData(ioData, 1);
    EXPECT_EQ(ioData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIUmqPerfData perfData[1] = {};
    listener.GetAllUmqPerfData(perfData, 1);
    EXPECT_EQ(perfData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLITxStatData txData[1] = {};
    listener.GetAllTxStatData(txData, 1);
    EXPECT_EQ(txData[0].socketId, static_cast<uint64_t>(TEST_FD_42));

    CLIRxStatData rxData[1] = {};
    listener.GetAllRxStatData(rxData, 1);
    EXPECT_EQ(rxData[0].socketId, static_cast<uint64_t>(TEST_FD_42));
}

/* GetFirstUmqHandle ForEach 分支: TCP 跳过、handle 无效跳过、result 已定位后早退。 */
TEST_F(StatisticsListenerTest, GetFirstUmqHandle_MixedSockets_SkipsTcpAndInvalid)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
    MOCKER_CPP(::umq_destroy).stubs().will(returnValue(UMQ_SUCCESS));
    UmqSocketPtr tcp = RegisterSocket(TEST_FD_42, SOCK_CREATE_TYPE_CONNECT);
    tcp->type_ = SocketType::SOCK_TYPE_TCP;
    RegisterSocket(TEST_FD_43, SOCK_CREATE_TYPE_CONNECT);
    UmqSocketPtr first = RegisterSocket(TEST_FD_44, SOCK_CREATE_TYPE_CONNECT);
    first->umq_handle_ = TEST_UMQ_HANDLE;
    UmqSocketPtr second = RegisterSocket(TEST_FD_45, SOCK_CREATE_TYPE_CONNECT);
    second->umq_handle_ = TEST_UMQ_HANDLE + 1;

    Statistics::Listener listener;
    EXPECT_EQ(listener.GetFirstUmqHandle(), TEST_UMQ_HANDLE);
}

/* GetFirstUmqHandle 主 UMQ 路径的 UBS_VLOG_DEBUG "使能" 分支。 */
TEST_F(StatisticsListenerTest, GetFirstUmqHandle_MainUmq_DebugLogEnabled)
{
    MOCKER_CPP(::umq_stats_transport_pool_get).stubs().will(returnValue(0));
    UmqEidTable::Instance().Add(UmqSetting::UMQ_LOCAL_EID, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = ock::ubs::LEVEL_DEBUG;
    Statistics::Listener listener;
    EXPECT_EQ(listener.GetFirstUmqHandle(), TEST_UMQ_HANDLE);
    ock::ubs::Logger::Instance().logLevel = savedLevel;
    UmqEidTable::Instance().Clean();
}

// ==================== statistics.h 覆盖率补充: DealDelayOperation 全分支 ====================

TEST_F(StatisticsListenerTest, DealDelayOperation_Disable_SetsProfEnableFalse)
{
    Statistics::Listener listener;
    MOCKER(&Profiling::Uninit).stubs().will(returnValue(0));
    GlobalSetting::UBS_PROF_ENABLE = true;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_DISABLE;
    listener.DealDelayOperation(delayHeader, outStr, header);
    EXPECT_FALSE(GlobalSetting::UBS_PROF_ENABLE);
    EXPECT_EQ(delayHeader.retCode, 0);
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Interval_UpdatesInterval)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_INTERVAL;
    header.mValue = 3.0;
    listener.DealDelayOperation(delayHeader, outStr, header);
    EXPECT_EQ(delayHeader.retCode, 0);
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN, 3u);
    GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Interval_OutOfRange_Rejected)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 2;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_INTERVAL;
    /* 超上限：须拒绝且不修改配置，避免 DumpThread clamp 后静默回退默认值 */
    header.mValue = 123.0;
    listener.DealDelayOperation(delayHeader, outStr, header);
    EXPECT_EQ(delayHeader.retCode, -1);
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN, 2u);
    /* 低于下限 */
    delayHeader.retCode = 0;
    header.mValue = 0.0;
    listener.DealDelayOperation(delayHeader, outStr, header);
    EXPECT_EQ(delayHeader.retCode, -1);
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN, 2u);
    GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Path_NonEmptyUpdatesPath)
{
    Statistics::Listener listener;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    listener.DealDelayOperation(delayHeader, outStr, header, "/tmp/ubsocket/path_test");
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/path_test");
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Path_EmptyKeepsPath)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/keep";
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    listener.DealDelayOperation(delayHeader, outStr, header);
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/keep");
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

// ==================== DealDelayOperation: PROF_OP_MODE 全分支 ====================

TEST_F(StatisticsListenerTest, DealDelayOperation_Mode_InvalidMode_SetsRetCode)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_MODE = "fast";
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    listener.DealDelayOperation(delayHeader, outStr, header, "invalid");
    EXPECT_EQ(delayHeader.retCode, -1);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "fast");
    GlobalSetting::UBS_PROF_MODE = "fast";
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Mode_SameMode_Idempotent)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_MODE = "fast";
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    listener.DealDelayOperation(delayHeader, outStr, header, "fast");
    EXPECT_EQ(delayHeader.retCode, 0);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "fast");
    GlobalSetting::UBS_PROF_MODE = "fast";
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Mode_ProfDisabled_UpdatesModeOnly)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalSetting::UBS_PROF_ENABLE = false;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    listener.DealDelayOperation(delayHeader, outStr, header, "ext");
    EXPECT_EQ(delayHeader.retCode, 0);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "ext");
    EXPECT_FALSE(GlobalSetting::UBS_PROF_ENABLE);
    GlobalSetting::UBS_PROF_MODE = "fast";
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Mode_ProfEnabledInitOk_SwitchesMode)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalSetting::UBS_PROF_ENABLE = true;
    MOCKER(&Profiling::Uninit).stubs().will(returnValue(0));
    MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));
    g_mockProfInitRet = 0;
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    listener.DealDelayOperation(delayHeader, outStr, header, "ext");
    EXPECT_EQ(delayHeader.retCode, 0);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "ext");
    EXPECT_TRUE(GlobalSetting::UBS_PROF_ENABLE);
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalSetting::UBS_PROF_ENABLE = false;
}

TEST_F(StatisticsListenerTest, DealDelayOperation_Mode_ProfEnabledInitFail_DisablesProf)
{
    Statistics::Listener listener;
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalSetting::UBS_PROF_ENABLE = true;
    MOCKER(&Profiling::Uninit).stubs().will(returnValue(0));
    MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));
    g_mockProfInitRet = static_cast<int>(UBS_ERROR);
    CLIDelayHeader delayHeader;
    std::string outStr;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    listener.DealDelayOperation(delayHeader, outStr, header, "ext");
    EXPECT_EQ(delayHeader.retCode, -1);
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "ext");
    EXPECT_FALSE(GlobalSetting::UBS_PROF_ENABLE);
    GlobalSetting::UBS_PROF_MODE = "fast";
    g_mockProfInitRet = 0;
}

// ==================== statistics.h 覆盖率补充: ProcessDelayRequest 路径载荷 ====================

TEST_F(StatisticsListenerTest, ProcessDelayRequest_PathPayloadRecvOk_SetsDumpPath)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockRecvPathEnable = true;
    g_mockRecvPathPayload = "/tmp/ubsocket/new_path";
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    header.mDataSize = static_cast<uint32_t>(g_mockRecvPathPayload.size());
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/new_path");
    EXPECT_GT(g_mockSendSocketDataCall, 0);
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_PathPayloadRecvFail_KeepsPath)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/keep";
    g_mockRecvPathEnable = true;
    g_mockRecvPathPayload = "/tmp/ubsocket/new_path";
    g_mockRecvSocketDataFailCall = 1;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    header.mDataSize = static_cast<uint32_t>(g_mockRecvPathPayload.size());
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/keep");
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_PathZeroSize_SkipsRecv)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/keep";
    g_mockRecvPathEnable = true;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    header.mDataSize = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/keep");
    EXPECT_TRUE(g_mockRecvPathEnable);
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_PathTooLarge_SkipsRecv)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/keep";
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_PATH;
    header.mDataSize = static_cast<uint32_t>(PATH_MAX) + 1;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_DUMP_PATH, "/tmp/ubsocket/keep");
    GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
}

// ==================== ProcessDelayRequest: PROF_OP_MODE 载荷 ====================

TEST_F(StatisticsListenerTest, ProcessDelayRequest_ModePayloadRecvOk_SwitchesMode)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalSetting::UBS_PROF_ENABLE = false;
    g_mockRecvPathEnable = true;
    g_mockRecvPathPayload = "ext";
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    header.mDataSize = static_cast<uint32_t>(g_mockRecvPathPayload.size());
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "ext");
    EXPECT_GT(g_mockSendSocketDataCall, 0);
    GlobalSetting::UBS_PROF_MODE = "fast";
}

TEST_F(StatisticsListenerTest, ProcessDelayRequest_ModePayloadRecvFail_KeepsMode)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROF_MODE = "fast";
    g_mockRecvPathEnable = true;
    g_mockRecvPathPayload = "ext";
    g_mockRecvSocketDataFailCall = 1;
    CLIMessage msg;
    CLIControlHeader header;
    header.mType = CLITypeParam::PROF_OP_MODE;
    header.mDataSize = static_cast<uint32_t>(g_mockRecvPathPayload.size());
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_PROF_MODE, "fast");
    GlobalSetting::UBS_PROF_MODE = "fast";
}

// ==================== statistics.h 覆盖率补充: AllocateIfNeed 失败 ====================

// ==================== statistics.h 覆盖率补充: 数据面第二次发送失败 ====================

TEST_F(StatisticsListenerTest, ProcessRequests_DataSendFail_AllReturnEarly)
{
    Statistics::Listener listener;
    MockSendHeaderOkDataFail();
    MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
    CLIMessage msg;
    CLIControlHeader header;

    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
    g_mockSendSocketDataCall = 0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
}

TEST_F(StatisticsListenerTest, ProcessTopoRequest_SendDataFail_LogsError)
{
    Statistics::Listener listener;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockGetRouteListRet = 0;
    g_mockRouteNum = 2;
    g_mockSendSocketDataMode = 1;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
}

// ==================== statistics.h 覆盖率补充: Probe 动态启停 ====================

// ==================== SplitTrace CLI 动态参数调整 ====================

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_SetSampleRate_Valid_UpdatesSetting)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    header.mValue = 500.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE, 500u);
    EXPECT_EQ(g_mockSendSocketDataCall, 1);
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
}

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_SetSampleRate_OutOfRange_NoChange)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    header.mValue = 0.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE, 100u);
    header.Reset();
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    header.mValue = 1001.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE, 100u);
}

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_SetDrainInterval_Valid_UpdatesSetting)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
    header.mValue = 50.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS, 50u);
    EXPECT_EQ(g_mockSendSocketDataCall, 1);
    GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
}

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_SetDrainInterval_OutOfRange_NoChange)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
    header.mValue = 0.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS, 10u);
    header.Reset();
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
    header.mValue = 10001.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS, 10u);
}

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_Enable_ThenDisable_TogglesFlag)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_TRUE(GlobalSetting::UBS_SPLIT_TRACE_ENABLED);
    header.Reset();
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, false);
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_FALSE(GlobalSetting::UBS_SPLIT_TRACE_ENABLED);
    SplitTraceDrainThread::Instance().Stop();
    GlobalTracePool::Instance().DestroyPool();
}

TEST_F(StatisticsListenerTest, ProcessSplitTraceRequest_SendFail_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    g_mockSendSocketDataMode = 1;
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    CLIMessage msg;
    CLIControlHeader header;
    header.mCmdId = CLICommand::SPLIT_TRACE;
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    header.mValue = 200.0;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessSplitTraceRequest(TEST_FD_43, msg, header));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE, 200u);
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    g_mockSendSocketDataMode = 0;
}

TEST_F(StatisticsListenerTest, Process_SplitTraceSetSampleRate_FullDispatch_UpdatesSetting)
{
    Statistics::Listener listener;
    MockListenerIo();
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    g_mockRecvCmdId = CLICommand::SPLIT_TRACE;
    g_mockRecvType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    g_mockRecvMValue = 1000.0;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE, 1000u);
    GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = 100;
    g_mockRecvMValue = 0.0;
}

TEST_F(StatisticsListenerTest, Process_SplitTraceSetDrainInterval_FullDispatch_UpdatesSetting)
{
    Statistics::Listener listener;
    MockListenerIo();
    GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
    g_mockRecvCmdId = CLICommand::SPLIT_TRACE;
    g_mockRecvType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
    g_mockRecvMValue = 100.0;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    EXPECT_EQ(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS, 100u);
    GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
    g_mockRecvMValue = 0.0;
}

// ==================== statistics.h 覆盖率补充: Probe 动态启停 ====================

TEST_F(StatisticsListenerTest, Process_ProbeEnable_StartsProbeManager)
{
    Statistics::Listener listener;
    MockListenerIo();
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    g_mockRecvCmdId = CLICommand::PROBE;
    g_mockRecvType = CLITypeParam::PROBE_OP_ENABLE;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    EXPECT_TRUE(GlobalSetting::UBS_PROBE_ENABLED);
}

TEST_F(StatisticsListenerTest, Process_ProbeDisable_StopsProbeManager)
{
    Statistics::Listener listener;
    MockListenerIo();
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    g_mockRecvCmdId = CLICommand::PROBE;
    g_mockRecvType = CLITypeParam::PROBE_OP_ENABLE;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    EXPECT_TRUE(GlobalSetting::UBS_PROBE_ENABLED);
    g_mockRecvType = CLITypeParam::PROBE_OP_DISABLE;
    EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
    EXPECT_FALSE(GlobalSetting::UBS_PROBE_ENABLED);
}

TEST_F(StatisticsListenerTest, ProcessProbeControl_EnableSendFail_LogsError)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    g_mockSendSocketDataMode = 1;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeControl(TEST_FD_43, header, true));
    EXPECT_TRUE(GlobalSetting::UBS_PROBE_ENABLED);
}

TEST_F(StatisticsListenerTest, ProcessProbeControl_DisableNotRunning_NoThrow)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    GlobalSetting::UBS_PROBE_ENABLED = true;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeControl(TEST_FD_43, header, false));
    EXPECT_FALSE(GlobalSetting::UBS_PROBE_ENABLED);
}

TEST_F(StatisticsListenerTest, ProcessProbeControl_DebugLogEnabled_BothBranches)
{
    Statistics::Listener listener;
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = ock::ubs::LEVEL_DEBUG;
    CLIControlHeader header;
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeControl(TEST_FD_43, header, true));
    EXPECT_TRUE(GlobalSetting::UBS_PROBE_ENABLED);
    EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeControl(TEST_FD_43, header, false));
    EXPECT_FALSE(GlobalSetting::UBS_PROBE_ENABLED);
    ock::ubs::Logger::Instance().logLevel = savedLevel;
}

// ==================== statistics.h 覆盖率补充: GetAllTx/RxStatData 空数据 / 错误路径日志禁用 ====================

TEST_F(StatisticsListenerTest, GetAllTxStatData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllTxStatData(nullptr, 1));
}

TEST_F(StatisticsListenerTest, GetAllRxStatData_NullData_Noop)
{
    Statistics::Listener listener;
    EXPECT_NO_FATAL_FAILURE(listener.GetAllRxStatData(nullptr, 1));
}

/* 全部错误路径在日志禁用(logLevel=LEVEL_COUNT)下执行, 覆盖 UBS_VLOG_* 宏
 * "禁用"分支(statistics.h 各错误返回点)。单用例内收敛所有 LibcApi 替换与
 * RecvCmd/SendCmd mock, 避免逐个 error path 重复建 Listener。 */
TEST_F(StatisticsListenerTest, ErrorPaths_LogDisabled_NoCrash)
{
    const ock::ubs::LogLevel savedLevel = ock::ubs::Logger::Instance().logLevel;
    ock::ubs::Logger::Instance().logLevel = static_cast<ock::ubs::LogLevel>(ock::ubs::LEVEL_COUNT);

    /* InternalEpollEnable 四类失败 */
    {
        Statistics::Listener listener;
        LibcApi::epoll_create_ptr = MockEpollCreate;
        LibcApi::epoll_ctl_ptr = MockEpollCtl;
        MOCKER_CPP(::eventfd).stubs().will(invoke(&MockEventfd));
        g_mockEpollCreateRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.InternalEpollEnable());
        g_mockEpollCreateRet = TEST_EPOLL_FD;
        g_mockEventfdRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.InternalEpollEnable());
        g_mockEventfdRet = TEST_WAKEUP_FD;
        g_mockEpollCtlFailOnCall = 1;
        EXPECT_NO_FATAL_FAILURE(listener.InternalEpollEnable());
        g_mockEpollCtlFailOnCall = 2;
        EXPECT_NO_FATAL_FAILURE(listener.InternalEpollEnable());
        g_mockEpollCtlFailOnCall = -1;
    }

    /* Poll epoll_wait 失败 + Process 各类失败 + Wakeup/Ack 失败 */
    {
        Statistics::Listener listener;
        LibcApi::epoll_wait_ptr = MockEpollWait;
        g_mockEpollWaitRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.Poll());
        g_mockEpollWaitRet = 0;

        LibcApi::accept_ptr = MockAcceptFail;
        EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));

        LibcApi::accept_ptr = MockAcceptSuccess;
        LibcApi::setsockopt_ptr = MockSetsockopt;
        MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
        g_mockSetsockoptCall = 0;
        g_mockSetsockoptFailOnCall = 1;
        EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
        g_mockSetsockoptCall = 0;
        g_mockSetsockoptFailOnCall = 2;
        EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
        g_mockSetsockoptFailOnCall = -1;
        g_mockRecvSocketDataCall = 0;
        g_mockRecvSocketDataFailCall = 1;
        EXPECT_NO_FATAL_FAILURE(listener.Process(EPOLLIN));
        g_mockRecvSocketDataFailCall = -1;

        LibcApi::write_ptr = MockWrite;
        g_mockWriteRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.WakeupEpoll());
        g_mockWriteRet = static_cast<ssize_t>(sizeof(uint64_t));
        LibcApi::read_ptr = MockRead;
        g_mockReadRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.AckWakeupEpoll());
        g_mockReadRet = static_cast<ssize_t>(sizeof(uint64_t));
    }

    /* RecvCmd / SendCmd 各失败分支 */
    {
        TestableListener listener;
        MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&MockRecvSocketData));
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
        Listener::CtrlHead ctrl;
        g_mockRecvSocketDataCall = 0;
        g_mockSendSocketDataCall = 0;

        g_mockRecvSocketDataFailCall = 1;
        EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
        g_mockRecvSocketDataFailCall = -1;
        g_mockRecvSocketDataCall = 0;

        g_mockCtrlDataSize = 9000;
        EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
        g_mockRecvSocketDataCall = 0;

        g_mockCtrlDataSize = 10;
        g_mockRecvSocketDataFailCall = 2;
        EXPECT_EQ(listener.TestRecvCmd(TEST_FD_43, ctrl), -1);
        g_mockRecvSocketDataFailCall = -1;
        g_mockRecvSocketDataCall = 0;

        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 1;
        EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, "0123456789"), -1);
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 2;
        EXPECT_EQ(listener.TestSendCmd(TEST_FD_43, ctrl, "0123456789"), -1);
        g_mockSendSocketDataMode = 0;
        g_mockSendSocketDataCall = 0;
    }

    /* ProcessStatRequest 两次发送失败 */
    {
        Statistics::Listener listener;
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
        CLIMessage msg;
        CLIControlHeader header;
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 1;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 2;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessStatRequest(TEST_FD_43, msg, header));
        g_mockSendSocketDataMode = 0;
    }

    /* 各 Process*Request 发送失败 + 数据收集错误路径 */
    {
        Statistics::Listener listener;
        MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&MockSendSocketData));
        MOCKER(&Profiling::Combine).stubs().will(invoke(&MockProfCombine));
        MOCKER(&Profiling::Init).stubs().will(invoke(&MockProfInit));
        MOCKER(&Profiling::Reset).stubs().will(invoke(&MockProfReset));
        MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
        MOCKER_CPP(::umq_stats_transport_pool_get).stubs().will(invoke(&MockTransportPoolGet));
        CLIMessage msg;
        CLIControlHeader header;

        /* DealDelayOperation: combine 失败 / init 失败 / 非法 op */
        header.mType = CLITypeParam::PROF_OP_QUERY;
        g_mockProfCombineRet = UBS_ERROR;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
        g_mockProfCombineRet = 0;
        g_mockProfInitRet = UBS_ERROR;
        header.mType = CLITypeParam::PROF_OP_ENABLE;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
        g_mockProfInitRet = 0;
        header.mType = CLITypeParam::INVALID;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));

        /* Delay 两次发送失败 */
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 1;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 2;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessDelayRequest(TEST_FD_43, msg, header));
        g_mockSendSocketDataMode = 0;

        /* 其余 Request 两次发送失败 */
        const auto sendFailBoth = [&]() {
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessFlowControlRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessQbufPoolRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqInfoRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessIoRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessUmqRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessTxStatRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessRxStatRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 1;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataCall = 0;
            g_mockSendSocketDataMode = 2;
            EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeRequest(TEST_FD_43, msg, header));
            g_mockSendSocketDataMode = 0;
        };
        sendFailBoth();

        /* ProcessProbeControl 发送失败 */
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 1;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessProbeControl(TEST_FD_43, header, false));
        g_mockSendSocketDataMode = 0;

        /* ProcessTopoRequest 三类失败 */
        g_mockGetRouteListRet = -1;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
        g_mockGetRouteListRet = 0;
        g_mockRouteNum = 0;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
        g_mockRouteNum = 2;
        g_mockSendSocketDataCall = 0;
        g_mockSendSocketDataMode = 1;
        EXPECT_NO_FATAL_FAILURE(listener.ProcessTopoRequest(TEST_FD_43, header));
        g_mockSendSocketDataMode = 0;

        /* GetUmqPoolStats pool get 失败 */
        UmqEidTable::Instance().Add(UmqSetting::UMQ_LOCAL_EID, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
        g_mockTransportPoolGetRet = -1;
        CLIDataHeader cliHeader{};
        EXPECT_NO_FATAL_FAILURE(listener.GetUmqPoolStats(cliHeader));
        g_mockTransportPoolGetRet = 0;
        UmqEidTable::Instance().Clean();
    }

    ock::ubs::Logger::Instance().logLevel = savedLevel;
}