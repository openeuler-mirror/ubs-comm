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

#include "umq_tx_helper.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_port_cooldown.h"
#include "core/ubsocket_bigdata.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_socket.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/tx_stat_defs.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const int TEST_FD = 42;
static const int TEST_FD_2 = 43;
static const uint64_t TEST_HANDLE = 12345;
static const uint32_t TEST_TOTAL_SIZE = 100;
static const uint32_t TEST_DATA_SIZE = 100;
static const uint32_t TEST_DATA_SIZE_SMALL = 40; /* 拆分偏差基准(40/2=20),非 TEST_DATA_SIZE 之半 */
static const uint32_t TEST_SPAN = 5;
static const uint32_t TEST_SPAN_SMALL = 3;
static const uint32_t TEST_FREED_JETTYS = 3;
static const uint64_t TEST_STATUS_OUT_OF_RANGE = 100;    /* 枚举空洞(99 与 128 之间),表外值 */
static const uint64_t TEST_STATUS_BELOW_FC_UPDATE = 191; /* FC_UPDATE(192) 相邻值 192-1 */

/* ---- ::umq_poll mock 状态 ---- */
static int g_pollNum = 0;
static umq_buf_t *g_cqeBufs[POLL_BATCH_MAX] = {};
static uint64_t g_polledHandle = 0;

static int MockUmqPoll(uint64_t umqh, umq_io_option_t *option, umq_buf_t **buf, uint32_t max_buf_count)
{
    g_polledHandle = umqh;
    const uint32_t count = static_cast<uint32_t>(g_pollNum);
    const uint32_t n = count < max_buf_count ? count : max_buf_count;
    for (uint32_t i = 0; i < n; ++i) {
        buf[i] = g_cqeBufs[i];
    }
    return static_cast<int>(n);
}

/* ---- ::umq_buf_free mock 状态 ---- */
static int g_bufFreeCount = 0;
static umq_buf_t *g_freedBuf = nullptr;

static void MockUmqBufFree(umq_buf_t *qbuf)
{
    g_bufFreeCount++;
    g_freedBuf = qbuf;
}

/* ---- ::umq_data_to_head mock 状态 ----
 * 被测 DataToBlock(data) 忽略入参,Block 取自返回 qbuf 的 buf_data 字段。
 * 多 qbuf 链场景按 data 入参分发,每个 qbuf 对应自己的 Block。 */
static umq_buf_t *g_dataToHeadReturn = nullptr;
static std::unordered_map<void *, umq_buf_t *> g_dataToHeadByData;

static umq_buf_t *MockUmqDataToHead(void *data)
{
    auto it = g_dataToHeadByData.find(data);
    if (it != g_dataToHeadByData.end()) {
        return it->second;
    }
    return g_dataToHeadReturn;
}

/* ---- UbsBigdata::HandleTxCompletion mock 状态 ---- */
static bool g_bigdataReturn = false;
static uint32_t g_bigdataSpan = 0;
static int g_bigdataProgressFd = -1;

static bool MockBigdataHandleTxCompletion(ock::ubs::Socket *sock, umq_buf_t *qbuf, uint32_t *completion_span,
                                          int *deferred_progress_fd)
{
    if (completion_span != nullptr) {
        *completion_span = g_bigdataSpan;
    }
    if (deferred_progress_fd != nullptr) {
        *deferred_progress_fd = g_bigdataProgressFd;
    }
    return g_bigdataReturn;
}

/* ---- LibcApi::shutdown_ptr 替换 ---- */
static int g_shutdownCallCount = 0;
static int g_shutdownFd = -1;

static int MockShutdown(int fd, int how)
{
    g_shutdownCallCount++;
    g_shutdownFd = fd;
    return 0;
}
} // namespace

class UmqTxHelperTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED = true;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        UmqSetting::UMQ_TP_TYPE = POOL;
        ResetMockState();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        LibcApi::shutdown_ptr = nullptr;
        errno = 0;
    }

    void ResetMockState()
    {
        g_pollNum = 0;
        std::memset(g_cqeBufs, 0, sizeof(g_cqeBufs));
        g_polledHandle = 0;
        g_bufFreeCount = 0;
        g_freedBuf = nullptr;
        g_dataToHeadReturn = nullptr;
        g_dataToHeadByData.clear();
        g_bigdataReturn = false;
        g_bigdataSpan = 0;
        g_bigdataProgressFd = -1;
        g_shutdownCallCount = 0;
        g_shutdownFd = -1;
        std::memset(&m_qbuf, 0, sizeof(m_qbuf));
        std::memset(&m_pro, 0, sizeof(m_pro));
        std::memcpy(m_qbuf.qbuf_ext, &m_pro, sizeof(m_pro));
        m_qbuf.qbuf_next = nullptr;
    }

    /* 基础 qbuf/pro 对（status=0、无链、无 data）——测试按需覆盖字段。 */
    umq_buf_t *MakeBaseQbuf()
    {
        return &m_qbuf;
    }

    umq_buf_pro_t *MakeBasePro()
    {
        return reinterpret_cast<umq_buf_pro_t *>(m_qbuf.qbuf_ext);
    }

    /* 注册 sock 到 ArraySet（ref 计数由 ArraySet 持有）。 */
    void RegisterSocket(const UmqSocketPtr &sock)
    {
        ArraySet<Socket>::GetInstance().OverrideItem(sock->Fd(), sock.Get());
    }

    /* 装配数据面条目 + 壳 ops 指针（-fno-access-control），使 GetTxOps() 返回非空。 */
    void WireTxOps(const UmqSocketPtr &sock)
    {
        sock->ReinitTxOps();
        auto *entry = DataPlaneTable::Instance().SlotFor(sock->Fd());
        sock->GetTx()->tx_ops_ = &entry->tx;
    }

    umq_buf_t m_qbuf;
    umq_buf_pro_t m_pro;
};

/* ==================== 数据面条目归属保护（fork#24 现场 core 回归） ==================== */

/* 背景：fd 复用窗口下，新 socket 会在同一 fd 的槽位上调 ReinitTxOps，而旧
 * socket 可能仍活着并持有该条目（其 DataTx/DataRx 壳指向此处）。原实现只判
 * `owner != nullptr` 就地析构+重建，等于释放了旧 socket 的 TxStatCounters 并
 * 让它的壳悬空——现场表现为 free() 处 glibc abort，或随后 SIGSEGV。
 * 修复后：槽位归属他人时拒绝重建（返回 nullptr，上层按资源不足处理）。 */
TEST_F(UmqTxHelperTest, ReinitTxOps_SlotOwnedByOtherSocket_RefusesInsteadOfDestroying)
{
    UmqSocketPtr first = MakeRef<UmqSocket>(TEST_FD);
    ASSERT_NE(first->ReinitTxOps(), nullptr);
    auto *entry = DataPlaneTable::Instance().SlotFor(TEST_FD);
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->owner, first.Get());

    /* 同一 fd 上的第二个 socket（fd 复用形态）：必须拒绝，且不得改动条目 */
    UmqSocketPtr second = MakeRef<UmqSocket>(TEST_FD);
    EXPECT_EQ(second->ReinitTxOps(), nullptr);
    EXPECT_EQ(entry->owner, first.Get()); /* 归属未被抢走 */

    /* 第一个 socket 自己重协商仍应成功（owner == this 的原语义保持） */
    EXPECT_NE(first->ReinitTxOps(), nullptr);
    EXPECT_EQ(entry->owner, first.Get());
}

/* ==================== PollUmqTx: 外层 poll 返回 <= 0 ==================== */

TEST_F(UmqTxHelperTest, PollUmqTx_PollNumZero_ReturnsZero)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_PollNumNegativeNotSilent_ReturnsPollNum)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_EBUSY));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, -UMQ_ERR_EBUSY);
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_PollSilentErrEmlink_LogsDebugAndReturnsPollNum)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    args.silent_poll_err = true;
    errno = 0;

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_EMLINK));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, -UMQ_ERR_EMLINK);
    EXPECT_EQ(errno, EMLINK); /* Convert(WRITEV) 后 errno == EMLINK → 只打 DEBUG */
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_PollSilentErrOther_LogsErrorAndReturnsPollNum)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    args.silent_poll_err = true;
    errno = 0;

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_ENOBUFS));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, -UMQ_ERR_ENOBUFS);
    EXPECT_EQ(errno, ENOBUFS); /* 非 EMLINK → 打 ERR 日志 */
    GlobalMockObject::verify();
}

/* ==================== PollUmqTx: CQE 分发 ==================== */

TEST_F(UmqTxHelperTest, PollUmqTx_CqeNull_LogsDebugSetsNormalErrorAndContinues)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    g_pollNum = 1;
    g_cqeBufs[0] = nullptr;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(err_code, ops_error_code::NORMAL_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeInvalidUserCtx_LogsAndContinues)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf(); /* status=0, user_ctx=0 → 无效 CQE */

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(err_code, ops_error_code::NORMAL_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeErrorStatus_InvokesCallbackAndIncrementsWrCnt)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    int callbackCount = 0;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [&callbackCount](umq_buf_t *qbuf) {
        callbackCount++;
        (void)qbuf;
    });
    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(g_bufFreeCount, 1); /* HandleTxCqeError → ProcessErrorTxCqe 释放 */
    EXPECT_EQ(ret, 1);            /* HandleTxCqeError 累加 wr_cnt */
    EXPECT_EQ(err_code, ops_error_code::NORMAL_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeProbe_FreesProbeAndContinues)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe); /* 探测包同样要过 user_ctx 判定 */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(g_bufFreeCount, 1); /* HandleProbePacket 直接释放 */
    EXPECT_EQ(ret, 0);            /* 探测包不累加 wr_cnt */
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeRnrBackpressureEnabled_NotifiesAndKeepsBuf)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC;
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(exactly(1));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(g_bufFreeCount, 0); /* RNR 软反压不释放 buf（bonding 重发 double free 防护） */
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(sock->IsRnrBlocked(), true);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeRnrBackpressureDisabled_GoesErrorPath)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED = false;
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    int callbackCount = 0;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(never()); /* 反压关闭 → 不进 HandleRnrNotify */

    int ret = UmqTxHelper::PollUmqTx(args, [&callbackCount](umq_buf_t *qbuf) {
        callbackCount++;
        (void)qbuf;
    });
    EXPECT_EQ(callbackCount, 1);  /* status != 0 → 错误路径 */
    EXPECT_EQ(g_bufFreeCount, 1); /* ProcessErrorTxCqe 释放 */
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(sock->IsRnrBlocked(), false);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeRnrAdjacentAckTimeout_GoesErrorPath)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_ACK_TIMEOUT_ERR; /* RNR(10) 相邻值 9 + flag on → 非 RNR 路径 */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1); /* status != 0 → HandleTxCqeError 累加 */
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(err_code, ops_error_code::NORMAL_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_CqeRnrAdjacentWrFlush_GoesErrorPath)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_WR_FLUSH_ERR; /* RNR(10) 相邻值 11 + flag on → 非 RNR 路径 */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1); /* status != 0 → HandleTxCqeError 累加 */
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(err_code, ops_error_code::NORMAL_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_BigdataCompletion_AddsSpanAndSkips)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    g_bigdataReturn = true;
    g_bigdataSpan = TEST_SPAN;
    g_bigdataProgressFd = -1;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(invoke(&MockBigdataHandleTxCompletion));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, static_cast<int>(TEST_SPAN));
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_BigdataCompletionWithProgressFd_SocketTxOpsNullSkips)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD_2);
    RegisterSocket(sock); /* 已注册但无数据面条目 → GetTxOps() == nullptr */
    g_pollNum = 1;
    g_cqeBufs[0] = MakeBaseQbuf();
    g_bigdataReturn = true;
    g_bigdataSpan = TEST_SPAN_SMALL;
    g_bigdataProgressFd = TEST_FD_2;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(invoke(&MockBigdataHandleTxCompletion));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, static_cast<int>(TEST_SPAN_SMALL));
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_NormalCqe_UpdatesTxQueueAvailAndNotifies)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe); /* 单 WR 单 qbuf 链 */
    MakeBasePro()->umq_ctx = TEST_FD;                          /* 未传 args.sock → fd 取自 umq_ctx */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    auto *tx_ops = sock->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(std::memory_order_relaxed),
              static_cast<uint16_t>(GlobalSetting::UBS_TX_DEPTH + 1));
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_RnrRecover_UnblocksSocket)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    sock->SetRnrBlocked(true); /* 预置反压,本批成功 CQE 且无 RNR 通知 → 解除 */
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe);
    MakeBasePro()->umq_ctx = TEST_FD; /* 未传 args.sock → fd 取自 umq_ctx */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    MOCKER_CPP(&UmqSocket::OnRnrRecover).expects(exactly(1));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(sock->IsRnrBlocked(), false);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_RnrNotifyWithNormalCqe_SkipsRecover)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    umq_buf_t rnrCqe = {};
    umq_buf_pro_t rnrPro = {};
    std::memset(&rnrCqe, 0, sizeof(rnrCqe));
    std::memset(&rnrPro, 0, sizeof(rnrPro));
    std::memcpy(rnrCqe.qbuf_ext, &rnrPro, sizeof(rnrPro));
    rnrCqe.status = UMQ_BUF_RNR_RETRY_CNT_EXC;
    reinterpret_cast<umq_buf_pro_t *>(rnrCqe.qbuf_ext)->umq_ctx = TEST_FD;
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe);
    MakeBasePro()->umq_ctx = TEST_FD; /* 未传 args.sock → fd 取自 umq_ctx */
    g_pollNum = 2;
    g_cqeBufs[0] = &rnrCqe;
    g_cqeBufs[1] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(exactly(1));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(sock->IsRnrBlocked(), true); /* 同批有 RNR 通知 → 不解除反压 */
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_SocketRemoved_LogsAndContinues)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe);
    MakeBasePro()->umq_ctx = TEST_FD; /* sock == nullptr → fd 取自 umq_ctx;未注册 → GetItem 空 */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(err_code, ops_error_code::OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTx_FreedJettys_WakesWaitQueueAndResetsOption)
{
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    poll_option.tp_handle_free_num = TEST_FREED_JETTYS;
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe); /* 需走完分发循环才到 freed_jettys 处理 */
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(poll_option.tp_handle_free_num, 0u); /* std::exchange 清空(poll_option 是引用) */
    /* WakeUp 走真实 Instance()(空队列,返回 0,无副作用) */
    GlobalMockObject::verify();
}

/* ==================== ProcessTxCqe ==================== */

TEST_F(UmqTxHelperTest, ProcessTxCqe_SingleWrNormalBlock_DecrefsAndFrees)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage)
        Block(reinterpret_cast<char *>(&m_qbuf), SIZE_4K, 2); /* nshared=2,DecRef 不触发 dealloc */
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(block); /* DataToBlock 从返回 qbuf 的 buf_data 提取 Block */
    g_dataToHeadReturn = cqe;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(cqe, cqe, nullptr, true);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 1); /* DecRef 生效 */
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(g_freedBuf, cqe);
    block->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessTxCqe_CoalescedSmall_SkipsDecRef)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(reinterpret_cast<char *>(&m_qbuf), SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->is_coalesced_small = 1;
    cqe->buf_data = reinterpret_cast<char *>(&m_qbuf);
    g_dataToHeadReturn = cqe;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(cqe, cqe, nullptr, true);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 2); /* coalesced → 无 DecRef */
    EXPECT_EQ(g_bufFreeCount, 1);
    block->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessTxCqe_MultiQbufSingleWr_DecrefsAll)
{
    alignas(alignof(Block)) char storage1[sizeof(Block)] = {};
    alignas(alignof(Block)) char storage2[sizeof(Block)] = {};
    Block *block1 = new (storage1) Block(nullptr, SIZE_4K, 2);
    Block *block2 = new (storage2) Block(nullptr, SIZE_4K, 2);
    umq_buf_t qbuf1 = {};
    umq_buf_t qbuf2 = {};
    umq_buf_t head1 = {};
    umq_buf_t head2 = {};
    std::memset(&qbuf1, 0, sizeof(qbuf1));
    std::memset(&qbuf2, 0, sizeof(qbuf2));
    std::memset(&head1, 0, sizeof(head1));
    std::memset(&head2, 0, sizeof(head2));
    qbuf1.total_data_size = 2 * TEST_DATA_SIZE; /* 单个 WR 两个 qbuf: 110 + 90 */
    qbuf1.data_size = TEST_TOTAL_SIZE + TEST_DATA_SIZE_SMALL / 2 - 10;
    qbuf1.buf_data = reinterpret_cast<char *>(&qbuf1);
    qbuf1.qbuf_next = &qbuf2;
    qbuf2.data_size = TEST_DATA_SIZE - TEST_DATA_SIZE_SMALL / 2 + 10;
    qbuf2.buf_data = reinterpret_cast<char *>(&qbuf2);
    head1.buf_data = reinterpret_cast<char *>(block1); /* 每个 qbuf 映射到自己的 Block */
    head2.buf_data = reinterpret_cast<char *>(block2);
    g_dataToHeadByData[&qbuf1] = &head1;
    g_dataToHeadByData[&qbuf2] = &head2;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(&qbuf1, &qbuf2, nullptr, true);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(block1->nshared.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(block2->nshared.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(g_freedBuf, &qbuf1);
    block1->~Block();
    block2->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessTxCqe_MultiWrChain_ReturnsWrCount)
{
    alignas(alignof(Block)) char storage1[sizeof(Block)] = {};
    alignas(alignof(Block)) char storage2[sizeof(Block)] = {};
    Block *block1 = new (storage1) Block(nullptr, SIZE_4K, 2);
    Block *block2 = new (storage2) Block(nullptr, SIZE_4K, 2);
    umq_buf_t wr1 = {};
    umq_buf_t wr2 = {};
    umq_buf_t head1 = {};
    umq_buf_t head2 = {};
    std::memset(&wr1, 0, sizeof(wr1));
    std::memset(&wr2, 0, sizeof(wr2));
    std::memset(&head1, 0, sizeof(head1));
    std::memset(&head2, 0, sizeof(head2));
    wr1.total_data_size = TEST_DATA_SIZE;
    wr1.data_size = TEST_DATA_SIZE;
    wr1.buf_data = reinterpret_cast<char *>(&wr1);
    wr1.qbuf_next = &wr2;
    wr2.total_data_size = TEST_DATA_SIZE_SMALL;
    wr2.data_size = TEST_DATA_SIZE_SMALL;
    wr2.buf_data = reinterpret_cast<char *>(&wr2);
    head1.buf_data = reinterpret_cast<char *>(block1);
    head2.buf_data = reinterpret_cast<char *>(block2);
    g_dataToHeadByData[&wr1] = &head1;
    g_dataToHeadByData[&wr2] = &head2;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(&wr1, &wr2, nullptr, true);
    EXPECT_EQ(ret, 2); /* 两个 WR 各计 1 */
    EXPECT_EQ(block1->nshared.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(block2->nshared.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(g_freedBuf, &wr1);
    block1->~Block();
    block2->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessTxCqe_DataToBlockNull_LogsErrorAndFrees)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(&m_qbuf);
    g_dataToHeadReturn = nullptr; /* ::umq_data_to_head 返回空 → 定位不到 Block */

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(cqe, cqe, nullptr, true);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(g_bufFreeCount, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessTxCqe_ReadOpLeftSizeZero_NoDecRefAndFrees)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(nullptr, SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->total_data_size = 0; /* read OP: left_size 初始为 0,while 循环不执行 */
    cqe->data_size = 0;
    cqe->buf_data = reinterpret_cast<char *>(block);

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::ProcessTxCqe(cqe, cqe, nullptr, true);
    EXPECT_EQ(ret, 1); /* 仍计一个 WR */
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 2); /* 无 DecRef */
    EXPECT_EQ(g_freedBuf, cqe);
    block->~Block();
    GlobalMockObject::verify();
}

/* ==================== HandleTxCqeError ==================== */

TEST_F(UmqTxHelperTest, HandleTxCqeError_ProbePacket_ReturnsWithoutIncrement)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_SUCCESS;
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;
    int wr_cnt = 5;

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::HandleTxCqeError(cqe, wr_cnt, nullptr);
    EXPECT_EQ(g_bufFreeCount, 1); /* HandleProbePacket 释放 */
    EXPECT_EQ(wr_cnt, 5);         /* 探测包不累加 */
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleTxCqeError_NormalError_IncrementsAndFrees)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = nullptr; /* DataToBlock → 返回 qbuf 的 buf_data==nullptr → 定位失败日志路径 */
    int wr_cnt = 5;
    g_dataToHeadReturn = cqe;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::HandleTxCqeError(cqe, wr_cnt, nullptr);
    EXPECT_EQ(wr_cnt, 6); /* 错误 CQE 累加 1 */
    EXPECT_EQ(g_bufFreeCount, 1);
    GlobalMockObject::verify();
}

/* ==================== HandleProbePacket ==================== */

TEST_F(UmqTxHelperTest, HandleProbePacket_MatchAll_FreesAndReturnsTrue)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    bool ret = UmqTxHelper::HandleProbePacket(cqe);
    EXPECT_TRUE(ret);
    EXPECT_EQ(g_bufFreeCount, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleProbePacket_OpcodeMatchUserDataMismatch_ReturnsFalse)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->opcode = UMQ_OPC_SEND_IMM;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID - 1;

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    bool ret = UmqTxHelper::HandleProbePacket(cqe);
    EXPECT_FALSE(ret);
    EXPECT_EQ(g_bufFreeCount, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleProbePacket_UserDataMatchOpcodeMismatch_ReturnsFalse)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    MakeBasePro()->opcode = UMQ_OPC_READ;
    MakeBasePro()->imm.user_data = UmqSetting::UMQ_PROBE_USER_DATA_ID;

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    bool ret = UmqTxHelper::HandleProbePacket(cqe);
    EXPECT_FALSE(ret);
    EXPECT_EQ(g_bufFreeCount, 0);
    GlobalMockObject::verify();
}

/* ==================== LogTxCqeErrorMsg: status 桶 + 日志分支 ==================== */

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_SuccessStatus_ReturnsEarly)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_SUCCESS;
    errno = EAGAIN;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RnrStatus_LogsRnrBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_AckTimeout_LogsAckTimeoutBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_ACK_TIMEOUT_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_FcErr_LogsFcBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_FcErrFatal_LogsFatal)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR_FATAL;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RemRespLen_LogsRemoteBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_REM_RESP_LEN_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_LocLen_LogsLocalBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_LOC_LEN_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_LocOperationErr_LogsLocalOpErr)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_LOC_OPERATION_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_LocAccessErr_LogsLocalAccessErr)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_LOC_ACCESS_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RemUnsupportedReqErr_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_REM_UNSUPPORTED_REQ_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RemOperationErr_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_REM_OPERATION_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RemAccessAbortErr_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_REM_ACCESS_ABORT_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_UnsupportedOpcode_LogsOtherBucket)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RnrFatalStatus_OnRnrTimeoutCalled)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD;
    errno = 0;

    MOCKER_CPP(&UmqSocket::OnRnrTimeout).expects(exactly(1));

    UmqTxHelper::LogTxCqeErrorMsg(cqe, sock.Get());
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_RnrFatalSocketRemoved_SkipsOnRnrTimeout)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD; /* 未注册到 ArraySet */
    errno = 0;

    MOCKER_CPP(&UmqSocket::OnRnrTimeout).expects(never());

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_WrFlushErr_NoLog)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_WR_FLUSH_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_WrSuspendDone_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_WR_SUSPEND_DONE;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_WrFlushErrDone_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_WR_FLUSH_ERR_DONE;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_WrUnhandled_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_WR_UNHANDLED;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_DataPoison_Logs)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_LOC_DATA_POISON;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_UnknownStatus_LogsUnreachable)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = TEST_STATUS_OUT_OF_RANGE; /* 表外值 → OTHER 桶 + default 日志 */
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, LogTxCqeErrorMsg_TxOpsWired_IncrementsRnrCounter)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    errno = 0;

    UmqTxHelper::LogTxCqeErrorMsg(cqe, sock.Get());
    auto *tx_ops = sock->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    auto *counters = tx_ops->GetTxStatCounters();
    ASSERT_NE(counters, nullptr); /* UBS_MONITOR_ENABLE 默认 on */
    EXPECT_EQ(counters->cqe_err[txstat::CQE_ERR_RNR], 1u);
    GlobalMockObject::verify();
}

/* ==================== ProcessErrorTxCqe ==================== */

TEST_F(UmqTxHelperTest, ProcessErrorTxCqe_FcUpdateStatus_DirectFreeNoChain)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(nullptr, SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_UPDATE; /* 192: status >= FC_UPDATE → 直接 free */
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(&m_qbuf);

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    MOCKER_CPP(::umq_data_to_head).expects(never()); /* 不走链,不定位 Block */

    UmqTxHelper::ProcessErrorTxCqe(cqe);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 2);
    block->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessErrorTxCqe_FcErrStatus_DirectFree)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR; /* 193: 相邻值(>= 192)同样直接 free */

    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::ProcessErrorTxCqe(cqe);
    EXPECT_EQ(g_bufFreeCount, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessErrorTxCqe_BelowFcUpdateStatus_WalksChainAndFrees)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(nullptr, SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = TEST_STATUS_BELOW_FC_UPDATE; /* 相邻值(192-1): 低于 FC_UPDATE → 走链 DecRef */
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(block); /* DataToBlock 从返回 qbuf 的 buf_data 提取 Block */
    g_dataToHeadReturn = cqe;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::ProcessErrorTxCqe(cqe);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 1);
    block->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessErrorTxCqe_CoalescedSmall_SkipsDecRef)
{
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(nullptr, SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->is_coalesced_small = 1;
    cqe->buf_data = reinterpret_cast<char *>(&m_qbuf);

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::ProcessErrorTxCqe(cqe);
    EXPECT_EQ(g_bufFreeCount, 1);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 2);
    block->~Block();
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, ProcessErrorTxCqe_DataToBlockNull_LogsAndFrees)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(&m_qbuf);
    g_dataToHeadReturn = nullptr;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    UmqTxHelper::ProcessErrorTxCqe(cqe);
    EXPECT_EQ(g_bufFreeCount, 1);
    GlobalMockObject::verify();
}

/* ==================== HandleRnrNotify ==================== */

TEST_F(UmqTxHelperTest, HandleRnrNotify_FirstEntry_EntersAndMarksBlocked)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    std::unordered_set<int> rnr_fds;

    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(exactly(1));

    UmqTxHelper::HandleRnrNotify(sock.Get(), cqe, rnr_fds);
    EXPECT_TRUE(sock->IsRnrBlocked());
    EXPECT_EQ(rnr_fds.count(TEST_FD), 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleRnrNotify_RepeatedEntry_DebugOnly)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    sock->SetRnrBlocked(true);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    std::unordered_set<int> rnr_fds;

    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(never());

    UmqTxHelper::HandleRnrNotify(sock.Get(), cqe, rnr_fds);
    EXPECT_TRUE(sock->IsRnrBlocked());
    EXPECT_EQ(rnr_fds.count(TEST_FD), 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleRnrNotify_NullSockWithBuf_LooksUpArraySet)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD;
    std::unordered_set<int> rnr_fds;

    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(exactly(1));

    UmqTxHelper::HandleRnrNotify(nullptr, cqe, rnr_fds);
    EXPECT_EQ(rnr_fds.count(TEST_FD), 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleRnrNotify_NullSockSocketMissing_Noop)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_RNR_RETRY_CNT_EXC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD; /* 未注册 */
    std::unordered_set<int> rnr_fds;

    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(never());

    UmqTxHelper::HandleRnrNotify(nullptr, cqe, rnr_fds);
    EXPECT_TRUE(rnr_fds.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, HandleRnrNotify_NullSockNullBuf_Noop)
{
    std::unordered_set<int> rnr_fds;

    MOCKER_CPP(&UmqSocket::OnRnrEnter).expects(never());

    UmqTxHelper::HandleRnrNotify(nullptr, nullptr, rnr_fds);
    EXPECT_TRUE(rnr_fds.empty());
    GlobalMockObject::verify();
}

/* ==================== PollUmqTxForFcReturn ==================== */

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_PoolPollNumZero_ReturnsOk)
{
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_SingleTpPollNumZero_ReturnsOk)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_PoolEmlinkSilent_LogsDebugReturnsOk)
{
    errno = 0;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_EMLINK));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK); /* POOL 静默: EMLINK 只打 DEBUG,不报错 */
    EXPECT_EQ(errno, EMLINK);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_PoolOtherError_LogsErrorReturnsError)
{
    errno = 0;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_ENOBUFS));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, ENOBUFS);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_SingleTpPollError_ReturnsOk)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    errno = 0;
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_ENOBUFS));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK); /* 非 POOL 非静默: 静默条件不成立,直接 UBS_OK */
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_CqeErrorSocketMissing_LogsDebugReturnsOk)
{
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD; /* 未注册到 ArraySet → lambda 内 GetItem 空 */
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_bufFreeCount, 1); /* ProcessErrorTxCqe 直接 free */
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_CqeErrorClosPorts_ShutsDownAndMarksCooldown)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    sock->SetTopoType(UMQ_TOPO_TYPE_CLOS);
    umq_port_id_t ports[2] = {};
    ports[0].bs.chip_id = 1;
    ports[0].bs.die_id = 2;
    ports[0].bs.port_idx = 3;
    ports[1].bs.chip_id = 4;
    ports[1].bs.die_id = 5;
    ports[1].bs.port_idx = 6;
    auto *cold = sock->GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->used_ports.reset(new umq_port_id_t[2]);
    std::memcpy(cold->used_ports.get(), ports, sizeof(ports));
    cold->used_ports_num = 2;

    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    LibcApi::shutdown_ptr = MockShutdown;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_shutdownCallCount, 1);
    EXPECT_EQ(g_shutdownFd, TEST_FD);
    EXPECT_EQ(sock->State(), SOCK_STAT_CLOSE);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(ports[0]));
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(ports[1]));
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_CqeErrorNonClos_ShutsDownOnly)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock); /* 默认 topo FULLMESH_1D → 非 CLOS → 不标记 port */
    umq_port_id_t port = {};
    port.bs.chip_id = 7;
    port.bs.die_id = 8;
    port.bs.port_idx = 9;
    auto *cold = sock->GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->used_ports.reset(new umq_port_id_t[1]);
    std::memcpy(cold->used_ports.get(), &port, sizeof(port));
    cold->used_ports_num = 1;

    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_FAKE_BUF_FC_ERR;
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    LibcApi::shutdown_ptr = MockShutdown;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_shutdownCallCount, 1);
    EXPECT_EQ(sock->State(), SOCK_STAT_CLOSE);
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(port));
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperTest, PollUmqTxForFcReturn_CqeErrorClosNonCooldownStatus_NoCooldown)
{
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    sock->SetTopoType(UMQ_TOPO_TYPE_CLOS);
    umq_port_id_t port = {};
    port.bs.chip_id = 10;
    port.bs.die_id = 11;
    port.bs.port_idx = 12;
    auto *cold = sock->GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->used_ports.reset(new umq_port_id_t[1]);
    std::memcpy(cold->used_ports.get(), &port, sizeof(port));
    cold->used_ports_num = 1;

    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR; /* 不在冷却集合 {2,4,9,193,194} */
    MakeBasePro()->umq_ctx = TEST_FD;
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    LibcApi::shutdown_ptr = MockShutdown;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTxForFcReturn(TEST_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_shutdownCallCount, 1); /* 断链仍执行 */
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(port));
    GlobalMockObject::verify();
}

/* ==================== trace 路径 ==================== */

TEST_F(UmqTxHelperTest, PollUmqTx_TraceEnabled_AddsWriteTrace)
{
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = true;
    ops_error_code err_code = ops_error_code::OK;
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs args(TEST_HANDLE, poll_option, err_code);
    UmqSocketPtr sock = MakeRef<UmqSocket>(TEST_FD);
    RegisterSocket(sock);
    WireTxOps(sock);
    alignas(alignof(Block)) char storage[sizeof(Block)] = {};
    Block *block = new (storage) Block(nullptr, SIZE_4K, 2);
    umq_buf_t *cqe = MakeBaseQbuf();
    cqe->total_data_size = TEST_TOTAL_SIZE;
    cqe->data_size = TEST_DATA_SIZE;
    cqe->buf_data = reinterpret_cast<char *>(block);
    MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe);
    args.sock = sock.Get();
    g_pollNum = 1;
    g_cqeBufs[0] = cqe;
    g_dataToHeadReturn = cqe;

    MOCKER_CPP(::umq_poll).stubs().will(invoke(&MockUmqPoll));
    MOCKER(&UbsBigdata::HandleTxCompletion).stubs().will(returnValue(false));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));

    int ret = UmqTxHelper::PollUmqTx(args, [](umq_buf_t *qbuf) { (void)qbuf; });
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(g_polledHandle, TEST_HANDLE);
    EXPECT_EQ(block->nshared.load(std::memory_order_relaxed), 1);
    block->~Block();
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
    GlobalMockObject::verify();
}
