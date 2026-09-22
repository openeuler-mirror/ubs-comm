/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "umq_tp_tx_epoll_runner_ops.h"

#include <sys/epoll.h>
#include <cstring>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_errno.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_lock.h"
#include "ubsocket_port_cooldown.h"
#include "ubsocket_set.h"
#include "umq_socket.h"
#include "umq_tx_helper.h"
#include "umq_transport_pool.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
static const int TEST_FD = 42;
static const int TEST_EPOLL_FD = 7;
static const uint64_t TEST_UMQ_HANDLE = 0x1234;
static const uint32_t TEST_TP_IDX = 3;

/* ---- state shared with the _ptr fake implementations ---- */
static int g_read_fd = -1;
static int g_close_cnt = 0;
static int g_shutdown_cnt = 0;
static int g_cq_event_ret = 0;
static int g_rearm_ret = 0;
static uint64_t g_rearm_handle = 0;

static ssize_t FakeRead(int fd, void *buf, size_t nbytes)
{
    g_read_fd = fd;
    memset(buf, 0, nbytes);
    return static_cast<ssize_t>(nbytes);
}

static int FakeClose(int fd)
{
    (void)fd;
    g_close_cnt++;
    return 0;
}

static int FakeShutdown(int fd, int how)
{
    (void)fd;
    (void)how;
    g_shutdown_cnt++;
    return 0;
}

static int FakeGetCqEvent(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    return g_cq_event_ret;
}

static int FakeRearmInterrupt(uint64_t umqh, bool solicated, umq_interrupt_option_t *option)
{
    (void)solicated;
    (void)option;
    g_rearm_handle = umqh;
    return g_rearm_ret;
}

/* ---- mockcpp invoke targets (signatures must match the mocked functions) ---- */

/* Empty TX poll: the do-while loop exits after the mandatory first call. */
static int FakePollInternalEmpty(UmqTxHelper::PollArgs &args, UmqTxHelper::ICallback &error_cb)
{
    (void)args;
    (void)error_cb;
    return 0;
}

/* Error-CQE poll: drives the error callback lambda of ProcessOneEvent. */
static umq_buf_t g_qbuf{};

static int FakePollInternalErrorCqe(UmqTxHelper::PollArgs &args, UmqTxHelper::ICallback &error_cb)
{
    (void)args;
    error_cb.invoke(&g_qbuf);
    return 0;
}
} // namespace

class UmqTpTxEpollRunnerOpsTest : public ::testing::Test {
protected:
    using TxEpollEvent = UmqTpTxEpollRunnerOps::TxEpollEvent;
    using TpTxExtContext = UmqTpTxEpollRunnerOps::TpTxExtContext;

    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_PORT_COOLDOWN_SEC = 60;
        ArraySet<Socket>::GetInstance().Init();

        g_read_fd = -1;
        g_close_cnt = 0;
        g_shutdown_cnt = 0;
        g_cq_event_ret = 0;
        g_rearm_ret = 0;
        g_rearm_handle = 0;
        memset(&g_qbuf, 0, sizeof(g_qbuf));

        /* LibcApi dispatches through _ptr which is null before Load(), every
         * reachable call site needs a fake installed here. UmqApi is the
         * direct-symbol backend in UT builds: hook the global C symbols. */
        LibcApi::read_ptr = FakeRead;
        LibcApi::close_ptr = FakeClose;
        LibcApi::shutdown_ptr = FakeShutdown;
        MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(FakeGetCqEvent));
        MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(FakeRearmInterrupt));
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        LibcApi::read_ptr = nullptr;
        LibcApi::close_ptr = nullptr;
        LibcApi::shutdown_ptr = nullptr;
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }

    /* The runner encodes the TxEpollEvent pointer directly into data.u64. */
    static struct epoll_event MakeEvent(const TxEpollEvent &tx_event)
    {
        struct epoll_event event{};
        event.events = EPOLLIN;
        event.data.u64 = reinterpret_cast<uint64_t>(&tx_event);
        return event;
    }

    /* Heap-allocated event, ownership transfers to the runner (freed by ~UmqTpTxEpollRunnerOps). */
    static TxEpollEvent *NewEvent(uint64_t type, uint64_t umq_handle = TEST_UMQ_HANDLE, int timer_fd = -1)
    {
        auto *event = new TxEpollEvent{};
        event->type = type;
        event->umq_handle = umq_handle;
        event->tp_idx = TEST_TP_IDX;
        event->timer_fd = timer_fd;
        return event;
    }

    /* Craft the error CQE consumed by the TX error callbacks: qbuf_ext carries
     * a umq_buf_pro_t whose umq_ctx is the socket fd, status drives the
     * port-cooldown decision. */
    static void SetErrorCqe(uint64_t status, int socket_fd)
    {
        memset(&g_qbuf, 0, sizeof(g_qbuf));
        umq_buf_pro_t pro{};
        pro.umq_ctx = static_cast<uint64_t>(socket_fd);
        memcpy(g_qbuf.qbuf_ext, &pro, sizeof(pro));
        g_qbuf.status = status;
    }

    /* Real UmqSocket registered in ArraySet; ReleaseAll() in TearDown deletes it.
     * used_ports now lives in the lazy cold-side struct (upstream hot/cold split). */
    static UmqSocket *RegisterUmqSocket(int fd, umq_topo_type_t topo, uint32_t chip_id, uint32_t ports_num)
    {
        auto *sock = new UmqSocket(fd);
        sock->SetTopoType(topo);
        auto ports = std::make_unique<umq_port_id_t[]>(ports_num);
        for (uint32_t i = 0; i < ports_num; ++i) {
            ports[i].bs.chip_id = static_cast<uint8_t>(chip_id + i);
        }
        UmqSocketCold *cold = sock->GetOrCreateCold();
        cold->used_ports = std::move(ports);
        cold->used_ports_num = ports_num;
        ArraySet<Socket>::GetInstance().OverrideItem(fd, sock);
        return sock;
    }
};

// ==================== ProcessOneEvent: TP_TX_TIMER ====================

// Timer tick with an empty TX poll: timer fd is drained once and the runner
// keeps going with UBS_OK.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTimerPollEmpty_ReturnsOk)
{
    UmqTpTxEpollRunnerOps ops;

    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalEmpty));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER, TEST_UMQ_HANDLE, 55);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_read_fd, 55);
    delete tx_event;
}

// Error CQE whose socket is already gone: the callback must bail out before
// touching shutdown/state.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTimerErrorCqeSocketAbsent_SkipsShutdown)
{
    UmqTpTxEpollRunnerOps ops;

    SetErrorCqe(UMQ_BUF_LOC_LEN_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 0);
    delete tx_event;
}

// CLOS topo + fatal CQE status: every used port must enter cooldown and the
// socket must be shut down and marked closed.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTimerErrorCqeClosFatal_MarksUsedPortsCooldown)
{
    UmqTpTxEpollRunnerOps ops;
    auto *sock = RegisterUmqSocket(TEST_FD, UMQ_TOPO_TYPE_CLOS, 10, 2);

    SetErrorCqe(UMQ_BUF_LOC_LEN_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 1);
    EXPECT_EQ(sock->State(), SOCK_STAT_CLOSE);
    auto [ports, ports_num] = sock->GetUsedPorts();
    ASSERT_NE(ports, nullptr);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(ports[0]));
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(ports[1]));
    delete tx_event;
}

// CLOS topo but a non-fatal status: ports stay out of cooldown.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTimerErrorCqeClosNonFatalStatus_SkipsCooldown)
{
    UmqTpTxEpollRunnerOps ops;
    auto *sock = RegisterUmqSocket(TEST_FD, UMQ_TOPO_TYPE_CLOS, 20, 1);

    SetErrorCqe(UMQ_BUF_RNR_RETRY_CNT_EXC_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 1);
    auto [ports, ports_num] = sock->GetUsedPorts();
    EXPECT_NE(ports, nullptr);
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(ports[0]));
    delete tx_event;
}

// Non-CLOS topo: the port-cooldown block is skipped even for a fatal status.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTimerErrorCqeFullmesh_SkipsCooldown)
{
    UmqTpTxEpollRunnerOps ops;
    auto *sock = RegisterUmqSocket(TEST_FD, UMQ_TOPO_TYPE_FULLMESH_1D, 30, 1);

    SetErrorCqe(UMQ_BUF_ACK_TIMEOUT_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 1);
    auto [ports, ports_num] = sock->GetUsedPorts();
    EXPECT_NE(ports, nullptr);
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(ports[0]));
    delete tx_event;
}

// ==================== ProcessOneEvent: TP_TX ====================

// umq_get_cq_event failure: the raw negative count is propagated to the caller.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTxGetCqEventFail_PropagatesRet)
{
    UmqTpTxEpollRunnerOps ops;
    g_cq_event_ret = -1;

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    delete tx_event;
}

// Successful CQE drain but rearm failure: the event ends with UBS_ERROR.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTxRearmFail_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;
    g_rearm_ret = -1;

    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalEmpty));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_ERROR);
    delete tx_event;
}

// Happy path: CQE event drained, poll empty, rearm ok.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTxAllSuccess_ReturnsOk)
{
    UmqTpTxEpollRunnerOps ops;

    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalEmpty));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    delete tx_event;
}

// Error CQE for a live socket: socket is shut down, marked closed and the TP
// is rebuilt (the RebuildTp args are the event's handle/tp_idx by
// construction, see the source lambda capture).
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTxErrorCqe_ClosesSocketAndRebuildsTp)
{
    UmqTpTxEpollRunnerOps ops;
    auto *sock = RegisterUmqSocket(TEST_FD, UMQ_TOPO_TYPE_FULLMESH_1D, 40, 1);

    SetErrorCqe(UMQ_FAKE_BUF_FC_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));
    MOCKER_CPP(&UmqTransportPool::RebuildTp).expects(exactly(1)).will(returnValue(static_cast<int>(UBS_OK)));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 1);
    EXPECT_EQ(sock->State(), SOCK_STAT_CLOSE);
    delete tx_event;
}

// Error CQE for a gone socket: no shutdown, but the TP is still rebuilt.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_TxTxErrorCqeSocketAbsent_StillRebuildsTp)
{
    UmqTpTxEpollRunnerOps ops;

    SetErrorCqe(UMQ_BUF_LOC_ACCESS_ERR, TEST_FD);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).expects(exactly(1)).will(invoke(FakePollInternalErrorCqe));
    MOCKER_CPP(&UmqTransportPool::RebuildTp).expects(exactly(1)).will(returnValue(static_cast<int>(UBS_OK)));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    EXPECT_EQ(g_shutdown_cnt, 0);
    delete tx_event;
}

// ==================== ProcessOneEvent: FC_TX ====================

// A deregistered event (handle cleared by RemoveSocketEventData) is skipped.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_FcTxInvalidHandle_ReturnsOkWithoutPoll)
{
    UmqTpTxEpollRunnerOps ops;

    MOCKER_CPP(&UmqTxHelper::PollUmqTxForFcReturn).expects(never()).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_FC_TX, UMQ_INVALID_HANDLE);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
    delete tx_event;
}

// Valid handle: the FC poll result is returned as-is.
TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_FcTxValidHandle_ReturnsPollRet)
{
    UmqTpTxEpollRunnerOps ops;

    MOCKER_CPP(&UmqTxHelper::PollUmqTxForFcReturn).expects(exactly(1)).will(returnValue(7));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_FC_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), 7);
    delete tx_event;
}

// ==================== ProcessOneEvent: unknown type ====================

TEST_F(UmqTpTxEpollRunnerOpsTest, ProcessOneEvent_UnknownType_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_EVENT);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_ERROR);
    delete tx_event;
}

// ==================== AddEventToRunner ====================

// Only TpTxExtContext is accepted; a plain ExtContext must be rejected before
// any epoll_ctl call.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_WrongCtxType_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;
    EpollRunnerOps::ExtContext base_ctx;

    MOCKER_CPP(::epoll_ctl).expects(never()).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &base_ctx), UBS_ERROR);
    EXPECT_FALSE(ops.IsSocketEventDataExist(TEST_FD));
    delete tx_event;
}

// epoll_ctl(ADD) failure: nothing is stored.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_EpollCtlAddFail_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(-1));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_ERROR);
    EXPECT_FALSE(ops.IsSocketEventDataExist(TEST_FD));
    delete tx_event;
}

// TP TX happy path: event stored, TX interrupt armed for the context handle.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_TxTxSuccess_StoresEventAndRearms)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};
    ctx.umq_handle = TEST_UMQ_HANDLE;
    ctx.tp_idx = TEST_TP_IDX;

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_OK);
    EXPECT_TRUE(ops.IsSocketEventDataExist(TEST_FD));
    EXPECT_EQ(g_rearm_handle, TEST_UMQ_HANDLE);
    // stored event is freed by the runner destructor, tx_event stays owned there
}

// Duplicate registration: insert fails and the epoll entry is rolled back
// with an extra EPOLL_CTL_DEL.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_TxTxInsertFail_RollsBackWithDel)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(2)).will(returnValue(0));
    ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_TP_TX));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_ERROR);
    delete tx_event;
}

// Rearm failure after a successful insert: UBS_ERROR (event stays stored for
// the destructor).
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_TxTxRearmFail_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};
    g_rearm_ret = -1;

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_ERROR);
    EXPECT_TRUE(ops.IsSocketEventDataExist(TEST_FD));
}

// Timer registration needs no interrupt rearm.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_TimerSuccess_StoresWithoutRearm)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_OK);
    EXPECT_TRUE(ops.IsSocketEventDataExist(TEST_FD));
    EXPECT_EQ(g_rearm_handle, 0);
}

TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_TimerInsertFail_RollsBackWithDel)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(2)).will(returnValue(0));
    ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_ERROR);
    delete tx_event;
}

TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_FcTxSuccess_StoresWithoutRearm)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_FC_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_OK);
    EXPECT_TRUE(ops.IsSocketEventDataExist(TEST_FD));
    EXPECT_EQ(g_rearm_handle, 0);
}

TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_FcTxInsertFail_RollsBackWithDel)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(2)).will(returnValue(0));
    ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_FC_TX));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_FC_TX);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_ERROR);
    delete tx_event;
}

// Event types this runner does not own: accepted with UBS_OK but neither
// stored nor armed.
TEST_F(UmqTpTxEpollRunnerOpsTest, AddEventToRunner_UnhandledType_NoStoreNoRearm)
{
    UmqTpTxEpollRunnerOps ops;
    TpTxExtContext ctx{};

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto *tx_event = NewEvent(RUNNER_EVENT_TYPE_TP_EVENT);
    auto event = MakeEvent(*tx_event);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_FD, &event, &ctx), UBS_OK);
    EXPECT_FALSE(ops.IsSocketEventDataExist(TEST_FD));
    EXPECT_EQ(g_rearm_handle, 0);
    delete tx_event;
}

// ==================== DelEpollEvent ====================

// epoll_ctl(DEL) failure still removes the bookkeeping entry, then reports UBS_ERROR.
TEST_F(UmqTpTxEpollRunnerOpsTest, DelEpollEvent_EpollCtlDelFail_ReturnsError)
{
    UmqTpTxEpollRunnerOps ops;
    ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_TP_TX));

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(-1));

    EXPECT_EQ(ops.DelEpollEvent(TEST_EPOLL_FD, TEST_FD), UBS_ERROR);
    EXPECT_FALSE(ops.IsSocketEventDataExist(TEST_FD));
}

// Happy path: entry removed and UBS_OK returned.
TEST_F(UmqTpTxEpollRunnerOpsTest, DelEpollEvent_Success_RemovesStoredEvent)
{
    UmqTpTxEpollRunnerOps ops;
    ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_TP_TX));

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    EXPECT_EQ(ops.DelEpollEvent(TEST_EPOLL_FD, TEST_FD), UBS_OK);
    EXPECT_FALSE(ops.IsSocketEventDataExist(TEST_FD));
}

// ==================== ~UmqTpTxEpollRunnerOps ====================

// Live and removed TIMER events both have their timer fd closed on teardown.
TEST_F(UmqTpTxEpollRunnerOpsTest, Destructor_TimerEvents_CloseTimerFds)
{
    {
        UmqTpTxEpollRunnerOps ops;
        ops.InsertSocketEventData(TEST_FD, NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER, TEST_UMQ_HANDLE, 99));
        ops.InsertSocketEventData(TEST_FD + 1, NewEvent(RUNNER_EVENT_TYPE_TP_TX_TIMER, TEST_UMQ_HANDLE, 98));
        ops.RemoveSocketEventData(TEST_FD + 1); // moves into removed_events_
    }
    EXPECT_EQ(g_close_cnt, 2);
}

// Null and non-TIMER entries are skipped without any close, and null entries
// in removed_events_ (defensive) do not crash the teardown.
TEST_F(UmqTpTxEpollRunnerOpsTest, Destructor_NullAndNonTimerEntries_AreJustDeleted)
{
    {
        UmqTpTxEpollRunnerOps ops;
        ops.InsertSocketEventData(TEST_FD, nullptr);
        ops.InsertSocketEventData(TEST_FD + 1, NewEvent(RUNNER_EVENT_TYPE_TP_TX));
        ops.RemoveSocketEventData(TEST_FD); // null entry cannot move, defensive path only
        ops.removed_events_.push_back(nullptr); /* private: -fno-access-control */
    }
    EXPECT_EQ(g_close_cnt, 0);
}
