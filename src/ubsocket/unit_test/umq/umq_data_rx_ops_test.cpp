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

#include "umq_data_rx_ops.h"

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_socket.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/probe/probe_manager.h"
#include "profiling/statistics/rx_stat_defs.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {

static const int TEST_FD = 42;
static const uint64_t TEST_HANDLE = 12345;
static const uint32_t TEST_DATA_SIZE = 40;       /* head 节点 data_size */
static const uint32_t TEST_DATA_SIZE_MID = 60;   /* 链中间节点 data_size */
static const uint32_t TEST_DATA_SIZE_TAIL = 100; /* 链尾节点 data_size */
static const uint32_t TEST_TOTAL_SIZE = 100;     /* 链 total_data_size */
static const uint32_t TEST_POLL_NUM = 2;
static const uint32_t TEST_RX_DEPTH = 2048;       /* GlobalSetting::UBS_RX_DEPTH 默认值 */
static const uint32_t TEST_WINDOW_FULL = 2017;    /* 减 poll 后 diff==32 == 阈值: 不触发 refill */
static const uint32_t TEST_WINDOW_TRIGGER = 2016; /* 减 poll 后 diff==33 > 阈值: 触发 refill */
static const uint32_t TEST_ACK_PRE_BELOW = 30;    /* GET_PER_ACK(32) 相邻值 31: 加 1 后 31 < 32 不 ack */
static const uint32_t TEST_ACK_PRE_AT = 31;       /* GET_PER_ACK(32) 相邻值 32: 加 1 后 32 >= 32 触发 ack */

/* ---- ::umq_poll mock 状态 ---- */
static int g_pollNum = 0;
static umq_buf_t *g_cqeBufs[POLL_BATCH_MAX] = {};

/* ---- ::umq_post mock 状态 ---- */
static int g_postRet = 0;
static umq_buf_t *g_postBadQbuf = nullptr;

/* ---- ::umq_buf_alloc mock 状态 ---- */
static umq_buf_t *g_allocBuf = nullptr;

/* ---- ::umq_buf_free mock 状态 ---- */
static int g_bufFreeCount = 0;
static umq_buf_t *g_freedBuf = nullptr;

/* ---- ::umq_get_cq_event / ::umq_ack_interrupt mock 状态 ---- */
static int g_getCqEventRet = 0;
static int g_ackInterruptCount = 0;
static uint32_t g_ackNevents = 0;

/* ---- ::umq_rearm_interrupt mock 状态 ---- */
static int g_rearmRet = 0;

/* ---- ::umq_data_to_head mock 状态 ---- */
static umq_buf_t *g_dataToHeadReturn = nullptr;

/* ---- UmqSocket::GetAndPopQbuf mock 状态 ---- */
static int g_popNum = 0;

/* ---- LibcApi::shutdown_ptr 替换状态 ---- */
static int g_shutdownCallCount = 0;
static int g_shutdownFd = -1;

static int MockUmqPoll(uint64_t umqh, umq_io_option_t *option, umq_buf_t **buf, uint32_t max_buf_count)
{
    (void)umqh;
    (void)option;
    const uint32_t count = static_cast<uint32_t>(g_pollNum);
    const uint32_t n = count < max_buf_count ? count : max_buf_count;
    for (uint32_t i = 0; i < n; ++i) {
        buf[i] = g_cqeBufs[i];
    }
    return static_cast<int>(n);
}

static int MockUmqPost(uint64_t umqh, umq_buf_t *qbuf, umq_io_option_t *option, umq_buf_t **bad_qbuf)
{
    (void)umqh;
    (void)qbuf;
    (void)option;
    if (bad_qbuf != nullptr) {
        *bad_qbuf = g_postBadQbuf;
    }
    return g_postRet;
}

static umq_buf_t *MockUmqBufAlloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                                  umq_alloc_option_t *option)
{
    (void)request_size;
    (void)request_qbuf_num;
    (void)umqh;
    (void)option;
    return g_allocBuf;
}

static void MockUmqBufFree(umq_buf_t *qbuf)
{
    g_bufFreeCount++;
    g_freedBuf = qbuf;
}

static int MockUmqGetCqEvent(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    return g_getCqEventRet;
}

static void MockUmqAckInterrupt(uint64_t umqh, uint32_t nevents, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    g_ackInterruptCount++;
    g_ackNevents = nevents;
}

static int MockUmqRearmInterrupt(uint64_t umqh, bool solicited, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)solicited;
    (void)option;
    return g_rearmRet;
}

static umq_buf_t *MockUmqDataToHead(void *data)
{
    (void)data;
    return g_dataToHeadReturn;
}

static int MockGetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size)
{
    const uint32_t count = static_cast<uint32_t>(g_popNum);
    const uint32_t n = count < max_buf_size ? count : max_buf_size;
    for (uint32_t i = 0; i < n; ++i) {
        buf[i] = g_cqeBufs[i];
    }
    return g_popNum;
}

static int MockShutdown(int fd, int how)
{
    (void)how;
    g_shutdownCallCount++;
    g_shutdownFd = fd;
    return 0;
}

/* Block 通过 placement new 构造于 buf_data - sizeof(Block) 处, 故数据区需预留 Block 头空间 */
static char *AllocBufData(uint32_t data_size)
{
    auto *raw = new char[sizeof(Block) + data_size];
    return raw + sizeof(Block);
}

static void FreeBufData(char *buf_data)
{
    delete[](buf_data - sizeof(Block));
}

class UmqDataRxOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_MONITOR_ENABLE = true; /* 计数器可观测: cqe_err/poll_err */
        GlobalSetting::UBS_RX_DEPTH = TEST_RX_DEPTH;
        UmqSetting::UMQ_TP_TYPE = POOL;
        ResetMockState();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        Statistics::ProbeManager::GetInstance().Stop();
        LibcApi::shutdown_ptr = nullptr;
        UmqSetting::UMQ_TP_TYPE = POOL;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = true; /* 恢复默认 */
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_RX_DEPTH = TEST_RX_DEPTH;
        errno = 0;
    }

    void ResetMockState()
    {
        g_pollNum = 0;
        std::memset(g_cqeBufs, 0, sizeof(g_cqeBufs));
        g_postRet = 0;
        g_postBadQbuf = nullptr;
        g_allocBuf = nullptr;
        g_bufFreeCount = 0;
        g_freedBuf = nullptr;
        g_getCqEventRet = 0;
        g_ackInterruptCount = 0;
        g_ackNevents = 0;
        g_rearmRet = 0;
        g_dataToHeadReturn = nullptr;
        g_popNum = 0;
        g_shutdownCallCount = 0;
        g_shutdownFd = -1;
        std::memset(&m_qbuf, 0, sizeof(m_qbuf));
        std::memset(&m_pro, 0, sizeof(m_pro));
        std::memcpy(m_qbuf.qbuf_ext, &m_pro, sizeof(m_pro));
        m_qbuf.qbuf_next = nullptr;
        std::memset(&m_qbuf2, 0, sizeof(m_qbuf2));
        std::memset(&m_pro2, 0, sizeof(m_pro2));
        std::memcpy(m_qbuf2.qbuf_ext, &m_pro2, sizeof(m_pro2));
        m_qbuf2.qbuf_next = nullptr;
    }

    /* 基础 qbuf/pro 对: status=0、无链、无 data, 用例按需覆盖字段 */
    umq_buf_t *MakeBaseQbuf()
    {
        return &m_qbuf;
    }

    umq_buf_pro_t *MakeBasePro()
    {
        return reinterpret_cast<umq_buf_pro_t *>(m_qbuf.qbuf_ext);
    }

    DataRxOps MakeOps()
    {
        return DataRxOps(TEST_FD, TEST_HANDLE);
    }

    /* UBS_ENABLE_SHARE_JFR 关闭时 (SetUp 默认), PollRx 需 UmqSocket 用于数组访问; 测试构造真实对象 */
    SocketPtr MakeSock()
    {
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        return RefConvert<UmqSocket, Socket>(umqSock);
    }

    umq_buf_t m_qbuf;
    umq_buf_pro_t m_pro;
    umq_buf_t m_qbuf2;
    umq_buf_pro_t m_pro2;
};

/* ================= DataToBlock ================= */

TEST_F(UmqDataRxOpsTest, DataToBlock_DataToHeadReturnsNull_ReturnsNull)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(static_cast<umq_buf_t *>(nullptr)));

    Block *block = ops.DataToBlock(reinterpret_cast<void *>(TEST_FD));
    EXPECT_EQ(block, nullptr);
}

TEST_F(UmqDataRxOpsTest, DataToBlock_BufDataNull_ReturnsNull)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(MakeBaseQbuf()));

    Block *block = ops.DataToBlock(reinterpret_cast<void *>(TEST_FD));
    EXPECT_EQ(block, nullptr);
}

TEST_F(UmqDataRxOpsTest, DataToBlock_ValidQbuf_ReturnsBlock)
{
    DataRxOps ops = MakeOps();
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(MakeBaseQbuf()));

    /* DataToBlock 仅 reinterpret_cast, 不构造 Block, 故只断言非空与指向 buf_data 所在内存 */
    Block *block = ops.DataToBlock(reinterpret_cast<void *>(TEST_FD));
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(reinterpret_cast<char *>(block), buf_data);
    FreeBufData(buf_data);
}

/* ================= GetQbuf ================= */

TEST_F(UmqDataRxOpsTest, GetQbuf_ShareJfrPopSuccess_ReturnsPollNum)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    DataRxOps ops = MakeOps();
    g_popNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    MOCKER_CPP(&UmqSocket::GetAndPopQbuf).stubs().will(invoke(&MockGetAndPopQbuf));

    SocketPtr sock = MakeSock();
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    int ret = ops.GetQbuf(sock, buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(buf[0], MakeBaseQbuf());
}

TEST_F(UmqDataRxOpsTest, GetQbuf_ShareJfrPopFail_ReturnsMinusOne)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    DataRxOps ops = MakeOps();
    g_popNum = -1;
    MOCKER_CPP(&UmqSocket::GetAndPopQbuf).stubs().will(invoke(&MockGetAndPopQbuf));

    SocketPtr sock = MakeSock();
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    int ret = ops.GetQbuf(sock, buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    ASSERT_NE(ops.GetRxStatCounters(), nullptr);
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_QBUF_POP_FAIL], 1);
}

/* ================= UmqPollAndRefillRx ================= */

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_PollFailMapsErrno_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_ENOMEM));
    errno = EIO;
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOMEM); /* override: UMQ_FAIL(-1) 变体 + ENOMEM -> ENOMEM */
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_POLL_FAIL], 1);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_PollZeroAvailZero_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    errno = EINVAL;
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL); /* 无 API 失败, errno 不被改动 */
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_PollZeroAvailNonzero_ReturnsZero)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_buf_alloc).expects(never());
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, 0);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_RefillDiffAtThreshold_SkipsRefill)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_WINDOW_FULL; /* 2017: 减 1 后 diff=32 == 阈值, 不触发 */
    g_pollNum = 1;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_alloc).expects(never());
    MOCKER_CPP(::umq_post).expects(never());
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(ops.rx_queue_avail_num_, TEST_WINDOW_FULL - 1);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_RefillDiffAboveThreshold_AllocFailNonFatal)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_WINDOW_TRIGGER; /* 2016: 减 1 后 diff=33 > 阈值, 触发 */
    g_pollNum = 1;
    g_allocBuf = nullptr; /* alloc 失败不致命 */
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(::umq_post).expects(never());
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(ops.rx_queue_avail_num_, TEST_WINDOW_TRIGGER - 1);
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_REFILL_ALLOC_FAIL], 1);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_RefillPostSuccess_RefillsWindow)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_WINDOW_TRIGGER;
    g_pollNum = TEST_POLL_NUM;
    g_allocBuf = MakeBaseQbuf();
    g_postRet = UMQ_SUCCESS;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, static_cast<int>(TEST_POLL_NUM));
    EXPECT_EQ(ops.rx_queue_avail_num_, TEST_WINDOW_TRIGGER - TEST_POLL_NUM + TX_REFILL_THRESHOLD);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_RefillPostPartialFail_ReturnsPollNum)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_WINDOW_TRIGGER;
    g_pollNum = TEST_POLL_NUM;
    g_allocBuf = MakeBaseQbuf();
    g_postRet = -UMQ_ERR_EAGAIN;
    g_postBadQbuf = &m_qbuf2; /* bad 为 head 下一节点: wr_cnt=1 */
    m_qbuf.qbuf_next = &m_qbuf2;
    m_qbuf.total_data_size = TEST_DATA_SIZE; /* 链前段(仅 head)data 和 */
    m_qbuf.data_size = TEST_DATA_SIZE;
    m_qbuf2.data_size = TEST_DATA_SIZE_TAIL;
    m_qbuf2.qbuf_next = nullptr;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, static_cast<int>(TEST_POLL_NUM));
    EXPECT_EQ(ops.rx_queue_avail_num_, TEST_WINDOW_TRIGGER - TEST_POLL_NUM + 1);
    EXPECT_EQ(m_qbuf.qbuf_next, nullptr); /* HandleBadQBuf 在 bad 前断开链 */
    EXPECT_EQ(g_freedBuf, &m_qbuf2);
}

TEST_F(UmqDataRxOpsTest, UmqPollAndRefillRx_RefillPostTotalFailMapsErrno_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_POLL_NUM; /* 减 2 后 avail==0, 依赖 refill 补回 */
    g_pollNum = TEST_POLL_NUM;
    g_allocBuf = MakeBaseQbuf();
    g_postRet = -UMQ_ERR_ENOMEM;
    g_postBadQbuf = &m_qbuf; /* bad == head: wr_cnt=0, 补回后 avail 仍为 0 */
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    errno = EIO;
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    int ret = ops.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOMEM);
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_REFILL_POST_FAIL], 1);
    EXPECT_EQ(g_freedBuf, &m_qbuf);
}

/* ================= HandleBadQBuf ================= */

TEST_F(UmqDataRxOpsTest, HandleBadQBuf_BadQbufIsHead_ReturnsZero)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    uint32_t cnt = ops.HandleBadQBuf(&m_qbuf, &m_qbuf);
    EXPECT_EQ(cnt, 0);
    EXPECT_EQ(g_freedBuf, &m_qbuf);
}

TEST_F(UmqDataRxOpsTest, HandleBadQBuf_BadQbufTail_ReturnsOne)
{
    DataRxOps ops = MakeOps();
    m_qbuf.qbuf_next = &m_qbuf2;
    m_qbuf.total_data_size = TEST_DATA_SIZE; /* 链前段(仅 head)data 和 */
    m_qbuf.data_size = TEST_DATA_SIZE;
    m_qbuf2.data_size = TEST_DATA_SIZE_TAIL;
    m_qbuf2.qbuf_next = nullptr;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    uint32_t cnt = ops.HandleBadQBuf(&m_qbuf, &m_qbuf2);
    EXPECT_EQ(cnt, 1);
    EXPECT_EQ(m_qbuf.qbuf_next, nullptr); /* 链在 bad 前断开 */
    EXPECT_EQ(g_freedBuf, &m_qbuf2);
}

TEST_F(UmqDataRxOpsTest, HandleBadQBuf_BadQbufMidChain_ReturnsOne)
{
    DataRxOps ops = MakeOps();
    /* head(40) -> mid(60) -> bad(100), total=100 与 head 以下各节点 data 和一致 */
    umq_buf_t head = {};
    umq_buf_t mid = {};
    umq_buf_t bad = {};
    head.qbuf_next = &mid;
    head.total_data_size = TEST_TOTAL_SIZE;
    head.data_size = TEST_DATA_SIZE;
    mid.qbuf_next = &bad;
    mid.data_size = TEST_DATA_SIZE_MID;
    bad.qbuf_next = nullptr;
    bad.data_size = TEST_DATA_SIZE_TAIL;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    uint32_t cnt = ops.HandleBadQBuf(&head, &bad);
    EXPECT_EQ(cnt, 1);
    EXPECT_EQ(mid.qbuf_next, nullptr); /* 链在 bad 前断开 */
    EXPECT_EQ(g_freedBuf, &bad);
}

/* ================= GetAndAckEvent ================= */

TEST_F(UmqDataRxOpsTest, GetAndAckEvent_NoEvents_ReturnsZero)
{
    DataRxOps ops = MakeOps();
    g_getCqEventRet = 0;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).expects(never());

    int ret = ops.GetAndAckEvent();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.ack_event_num_, 0);
}

TEST_F(UmqDataRxOpsTest, GetAndAckEvent_GetEventFailMapsErrno_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    g_getCqEventRet = -UMQ_ERR_EAGAIN;
    errno = EIO;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));

    int ret = ops.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
}

TEST_F(UmqDataRxOpsTest, GetAndAckEvent_EventsBelowBatch_AccumulatesWithoutAck)
{
    DataRxOps ops = MakeOps();
    ops.ack_event_num_ = TEST_ACK_PRE_BELOW; /* 30: 加 1 后 31 < GET_PER_ACK(32) */
    g_getCqEventRet = 1;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).expects(never());

    int ret = ops.GetAndAckEvent();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.ack_event_num_, TEST_ACK_PRE_BELOW + 1);
}

TEST_F(UmqDataRxOpsTest, GetAndAckEvent_EventsReachBatch_AcksAndResets)
{
    DataRxOps ops = MakeOps();
    ops.ack_event_num_ = TEST_ACK_PRE_AT; /* 31: 加 1 后 32 >= GET_PER_ACK(32) */
    g_getCqEventRet = 1;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));

    int ret = ops.GetAndAckEvent();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(g_ackInterruptCount, 1);
    EXPECT_EQ(g_ackNevents, GET_PER_ACK);
    EXPECT_EQ(ops.ack_event_num_, 0);
}

TEST_F(UmqDataRxOpsTest, GetAndAckEvent_EventsExceedBatch_AcksWithTotal)
{
    DataRxOps ops = MakeOps();
    g_getCqEventRet = static_cast<int>(GET_PER_ACK); /* 32: 单次即达批量 */
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));

    int ret = ops.GetAndAckEvent();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(g_ackNevents, GET_PER_ACK);
    EXPECT_EQ(ops.ack_event_num_, 0);
}

/* ================= HandleErrorRxCqe ================= */

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_SuccessStatus_ReturnsWithoutShutdown)
{
    DataRxOps ops = MakeOps();
    m_qbuf.status = UMQ_BUF_SUCCESS;
    LibcApi::shutdown_ptr = MockShutdown;

    ops.HandleErrorRxCqe(&m_qbuf);
    EXPECT_EQ(g_shutdownCallCount, 0);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 0);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_FcErrBucket_CountsFcAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    const uint64_t statuses[] = {UMQ_FAKE_BUF_FC_ERR, UMQ_FAKE_BUF_FC_ERR_FATAL};
    for (uint64_t st : statuses) {
        m_qbuf.status = st;
        ops.HandleErrorRxCqe(&m_qbuf);
    }
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_FC], 2);
    EXPECT_EQ(g_shutdownCallCount, 2);
    EXPECT_EQ(g_shutdownFd, TEST_FD);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_LocalBucket_CountsLocalAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    const uint64_t statuses[] = {UMQ_BUF_LOC_LEN_ERR, UMQ_BUF_LOC_OPERATION_ERR, UMQ_BUF_LOC_ACCESS_ERR};
    for (uint64_t st : statuses) {
        m_qbuf.status = st;
        ops.HandleErrorRxCqe(&m_qbuf);
    }
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_LOCAL], 3);
    EXPECT_EQ(g_shutdownCallCount, 3);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_RemoteBucket_CountsRemoteAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    const uint64_t statuses[] = {UMQ_BUF_REM_RESP_LEN_ERR, UMQ_BUF_REM_UNSUPPORTED_REQ_ERR, UMQ_BUF_REM_OPERATION_ERR,
                                 UMQ_BUF_REM_ACCESS_ABORT_ERR};
    for (uint64_t st : statuses) {
        m_qbuf.status = st;
        ops.HandleErrorRxCqe(&m_qbuf);
    }
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_REMOTE], 4);
    EXPECT_EQ(g_shutdownCallCount, 4);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_AckTimeoutBucket_CountsAckTimeoutAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    m_qbuf.status = UMQ_BUF_ACK_TIMEOUT_ERR;
    ops.HandleErrorRxCqe(&m_qbuf);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_ACK_TIMEOUT], 1);
    EXPECT_EQ(g_shutdownCallCount, 1);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_RnrBucket_CountsRnrAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    m_qbuf.status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    ops.HandleErrorRxCqe(&m_qbuf);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_RNR], 1);
    EXPECT_EQ(g_shutdownCallCount, 1);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_OtherCaseBucket_CountsOtherAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    const uint64_t statuses[] = {UMQ_BUF_UNSUPPORTED_OPCODE_ERR, UMQ_BUF_WR_FLUSH_ERR, UMQ_BUF_WR_SUSPEND_DONE,
                                 UMQ_BUF_WR_FLUSH_ERR_DONE,      UMQ_BUF_WR_UNHANDLED, UMQ_BUF_LOC_DATA_POISON,
                                 UMQ_BUF_REM_DATA_POISON};
    for (uint64_t st : statuses) {
        m_qbuf.status = st;
        ops.HandleErrorRxCqe(&m_qbuf);
    }
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 7);
    EXPECT_EQ(g_shutdownCallCount, 7);
}

TEST_F(UmqDataRxOpsTest, HandleErrorRxCqe_DefaultBucket_CountsOtherAndShutdowns)
{
    DataRxOps ops = MakeOps();
    LibcApi::shutdown_ptr = MockShutdown;
    /* 129/130/192 为显式 case 但不改 bucket (初始 OTHER), 与 default 组同桶 */
    const uint64_t statuses[] = {UMQ_BUF_RNR_RETRY_CNT_EXC, UMQ_BUF_FLOW_CONTROL_UPDATE, UMQ_MEMPOOL_UPDATE_SUCCESS,
                                 UMQ_MEMPOOL_UPDATE_FAILED, UMQ_IMPORT_TSEG_SUCCESS,     UMQ_FAKE_BUF_FC_UPDATE,
                                 UMQ_FAKE_BUF_FC_MSG,       UMQ_FAKE_BUF_FC_EMLINK,      UMQ_FAKE_BUF_MAX};
    for (uint64_t st : statuses) {
        m_qbuf.status = st;
        ops.HandleErrorRxCqe(&m_qbuf);
    }
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 9);
    EXPECT_EQ(g_shutdownCallCount, 9);
}

/* ================= RearmRxInterrupt ================= */

TEST_F(UmqDataRxOpsTest, RearmRxInterrupt_TpTypePool_ReturnsOk)
{
    DataRxOps ops = MakeOps();
    UmqSetting::UMQ_TP_TYPE = POOL;
    MOCKER_CPP(::umq_rearm_interrupt).expects(never());

    int ret = ops.RearmRxInterrupt();
    EXPECT_EQ(ret, static_cast<int>(UBS_OK));
}

TEST_F(UmqDataRxOpsTest, RearmRxInterrupt_RearmSuccess_ReturnsZero)
{
    DataRxOps ops = MakeOps();
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    g_rearmRet = 0;
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));

    int ret = ops.RearmRxInterrupt();
    EXPECT_EQ(ret, 0);
}

TEST_F(UmqDataRxOpsTest, RearmRxInterrupt_FailUmqFailSavedEinval_MapsEinval)
{
    DataRxOps ops = MakeOps();
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    g_rearmRet = UMQ_FAIL;
    errno = EINVAL;
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));

    int ret = ops.RearmRxInterrupt();
    EXPECT_EQ(ret, UMQ_FAIL);
    EXPECT_EQ(errno, EINVAL); /* override: UMQ_FAIL(-1) + EINVAL -> EINVAL */
}

TEST_F(UmqDataRxOpsTest, RearmRxInterrupt_FailEnodevSavedEio_MapsEio)
{
    DataRxOps ops = MakeOps();
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    g_rearmRet = -UMQ_ERR_ENODEV;
    errno = EIO;
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));

    int ret = ops.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_ENODEV);
    EXPECT_EQ(errno, EIO); /* override: UMQ_ERR_ENODEV 变体 + EIO -> EIO */
}

/* ================= PollSubUmqRx ================= */

TEST_F(UmqDataRxOpsTest, PollSubUmqRx_PollPositive_ReturnsTrue)
{
    DataRxOps ops = MakeOps();
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    bool ok = ops.PollSubUmqRx(buf, 0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(buf[0], MakeBaseQbuf());
}

TEST_F(UmqDataRxOpsTest, PollSubUmqRx_PollZero_ReturnsFalse)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    bool ok = ops.PollSubUmqRx(buf, 0);
    EXPECT_FALSE(ok);
}

TEST_F(UmqDataRxOpsTest, PollSubUmqRx_PollNegative_ReturnsFalse)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-1));
    umq_buf_t *buf[POLL_BATCH_MAX] = {};

    bool ok = ops.PollSubUmqRx(buf, 0);
    EXPECT_FALSE(ok);
}

/* ================= FlushRx ================= */

TEST_F(UmqDataRxOpsTest, FlushRx_NoPendingAvail_ReturnsEarly)
{
    DataRxOps ops = MakeOps();
    MOCKER_CPP(::umq_poll).expects(never());
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);

    ops.FlushRx(umqSock.Get(), 0);
}

TEST_F(UmqDataRxOpsTest, FlushRx_PollFail_BreaksWithLeak)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = 1;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-1));
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);

    ops.FlushRx(umqSock.Get(), 0);
    EXPECT_EQ(ops.rx_queue_avail_num_, 1); /* 泄漏 1, 仅日志 */
}

TEST_F(UmqDataRxOpsTest, FlushRx_DrainAllPending_FreesAll)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = 1;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);

    ops.FlushRx(umqSock.Get(), 0);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(ops.rx_queue_avail_num_, 0);
}

TEST_F(UmqDataRxOpsTest, FlushRx_FcUpdateBuf_NotifiesReadableAndFrees)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = 1;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_FAKE_BUF_FC_UPDATE;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);

    ops.FlushRx(umqSock.Get(), 0);
    EXPECT_EQ(g_bufFreeCount, 1); /* 未注册 epoll 时 NotifyReadable 返回 -1, 仅日志 */
    EXPECT_EQ(ops.rx_queue_avail_num_, 0);
}

TEST_F(UmqDataRxOpsTest, FlushRx_TimeoutExceeded_BreaksWithLeak)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = 1;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);

    ops.FlushRx(umqSock.Get(), 0);         /* timeout_ms=0: 第二轮 IsTimeout 必然超时 */
    EXPECT_EQ(ops.rx_queue_avail_num_, 1); /* 泄漏 1, 仅日志 */
}

/* ================= PollRx ================= */

TEST_F(UmqDataRxOpsTest, PollRx_PollDisabled_SkipsPolling)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = false;
    MOCKER_CPP(::umq_poll).expects(never());
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
}

TEST_F(UmqDataRxOpsTest, PollRx_ShareJfrPath_PopsFromSocket)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_popNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    m_qbuf.data_size = TEST_DATA_SIZE;
    MOCKER_CPP(&UmqSocket::GetAndPopQbuf).stubs().will(invoke(&MockGetAndPopQbuf));
    MOCKER_CPP(::umq_poll).expects(never());
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), TEST_DATA_SIZE);
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    FreeBufData(buf_data);
}

TEST_F(UmqDataRxOpsTest, PollRx_HappyPathNormalBuf_CachesData)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    m_qbuf.data_size = TEST_DATA_SIZE;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).expects(never());
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), TEST_DATA_SIZE);
    EXPECT_EQ(ops.poll_, true);
    FreeBufData(buf_data);
}

TEST_F(UmqDataRxOpsTest, PollRx_TraceEnabledWithoutStatsMgr_NoTraceUpdate)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    m_qbuf.data_size = TEST_DATA_SIZE;
    GlobalSetting::UBS_MONITOR_ENABLE = true; /* 无 ext -> GetStatsMgr 为 nullptr, 内层不进入 */
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), TEST_DATA_SIZE);
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    FreeBufData(buf_data);
}

TEST_F(UmqDataRxOpsTest, PollRx_GetAndAckEventFail_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    ops.get_and_ack_event_ = true;
    ops.poll_ = false;
    g_getCqEventRet = UMQ_FAIL;
    errno = EINVAL;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_poll).expects(never());
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_POLL_GET_EVENT_FAIL], 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_GetAndAckEventOk_ResetsFlag)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.get_and_ack_event_ = true;
    g_getCqEventRet = 1; /* 低于批量: 不 ack */
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    m_qbuf.data_size = TEST_DATA_SIZE;
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.get_and_ack_event_, false);
    EXPECT_EQ(ops.ack_event_num_, 1);
    FreeBufData(buf_data);
}

TEST_F(UmqDataRxOpsTest, PollRx_GetQbufFailMapsErrno_ReturnsMinusOne)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    errno = EIO;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_EBUSY));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBUSY);
    EXPECT_EQ(ops.GetRxStatCounters()->poll_err[rxstat::RX_POLL_FAIL], 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_PollNumZero_DisablesPollFlag)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.poll_, false);
}

TEST_F(UmqDataRxOpsTest, PollRx_ProbePacket_FreesWithoutCache)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(g_freedBuf, MakeBaseQbuf());
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), 0);
}

TEST_F(UmqDataRxOpsTest, PollRx_ProbePacketWithChainNext_WarnsAndFrees)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.qbuf_next = &m_qbuf2; /* 探测包链非空: 告警分支 */
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), 0);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusFcUpdate_CountsFcWithoutShutdown)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_FAKE_BUF_FC_UPDATE; /* 192: 流控回复, 不触发错误处理 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_FC], 1);
    EXPECT_EQ(g_shutdownCallCount, 0);
    EXPECT_EQ(g_bufFreeCount, 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusFcErr_ClosesSocketAndShutdowns)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_FAKE_BUF_FC_ERR; /* 194 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.flow_control_failed_, true);
    EXPECT_EQ(umqSock->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(g_shutdownCallCount, 1);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_FC], 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusFcErrFatal_ClosesSocketAndShutdowns)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_FAKE_BUF_FC_ERR_FATAL; /* 196 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.flow_control_failed_, true);
    EXPECT_EQ(umqSock->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(g_shutdownCallCount, 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusAboveFcUpdateUnknown_CountsOtherWithoutShutdown)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_FAKE_BUF_FC_MSG; /* 193: >= 192 且非 192/194/196 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 1);
    EXPECT_EQ(ops.flow_control_failed_, false);
    EXPECT_EQ(g_shutdownCallCount, 0);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusBelowFcUpdate_ClosesSocketAndShutdowns)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = static_cast<uint64_t>(UMQ_FAKE_BUF_FC_UPDATE) - 1; /* 191: FC_UPDATE 相邻值 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.flow_control_failed_, false); /* 仅 FC_ERR/FATAL 置位 */
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 1);
    EXPECT_EQ(g_shutdownCallCount, 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_StatusUnsupportedOpcode_ClosesSocketAndShutdowns)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    m_qbuf.status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR; /* 1: <192 且非 0 */
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.GetRxStatCounters()->cqe_err[rxstat::RXCQE_OTHER], 1);
    EXPECT_EQ(g_shutdownCallCount, 1);
}

TEST_F(UmqDataRxOpsTest, PollRx_MultipleBufsMixed_HandlesBoth)
{
    DataRxOps ops = MakeOps();
    ops.rx_queue_avail_num_ = TEST_RX_DEPTH;
    ops.poll_ = true;
    g_pollNum = 2;
    g_cqeBufs[0] = MakeBaseQbuf();
    g_cqeBufs[1] = &m_qbuf2;
    char *buf_data = AllocBufData(TEST_DATA_SIZE);
    m_qbuf.buf_data = buf_data;
    m_qbuf.data_size = TEST_DATA_SIZE;
    m_qbuf2.status = UMQ_FAKE_BUF_FC_ERR;
    LibcApi::shutdown_ptr = MockShutdown;
    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    SocketPtr sock = MakeSock();
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

    int ret = ops.PollRx(sock);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ops.block_cache_.GetCacheLen(), TEST_DATA_SIZE);
    EXPECT_EQ(ops.flow_control_failed_, true);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(g_shutdownCallCount, 1);
    EXPECT_EQ(umqSock->State(), SOCK_STAT_CLOSE);
    FreeBufData(buf_data);
}
} // namespace
