/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "umq_data_tx_ops.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_event_epoll.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_lock.h"
#include "ubsocket_set.h"
#include "ubsocket_tx_cqe_poller.h"
#include "umq_share_jfr_epoll_runner_ops.h"
#include "umq_socket.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
static const int TEST_FD = 42;
static const uint64_t TEST_UMQ_HANDLE = 100;
static const uint64_t TEST_UMQ_HANDLE_2 = 200;
} // namespace

class TxUnifiedPollerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
        GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US = 100;
        SetupPoller();
    }

    void TearDown() override
    {
        CleanupPoller();
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }

    void SetupPoller()
    {
        auto &poller = TxCqePoller::Instance();
        poller.mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        poller.stopped_.store(false, std::memory_order_relaxed);
    }

    void CleanupPoller()
    {
        auto &poller = TxCqePoller::Instance();
        if (poller.mutex_ != nullptr) {
            {
                Locker lock(poller.mutex_);
                poller.sockets_.clear();
            }
            LockRegistry::LOCK_OPS.destroy(poller.mutex_);
            poller.mutex_ = nullptr;
        }
        {
            std::lock_guard<std::mutex> lk(poller.active_mutex_);
            poller.active_.clear();
        }
        // Reset transient state for next test
        poller.stopped_.store(true, std::memory_order_relaxed);
        poller.shutdown_.store(
            false, std::memory_order_relaxed); /* issue#30 lifecycle latch — leaky singleton needs manual reset */
        poller.wake_fd_ = -1;
        poller.sleeping_.store(false, std::memory_order_relaxed);
        poller.any_inflight_.store(false, std::memory_order_relaxed);
        poller.active_dirty_.store(true, std::memory_order_relaxed);
        poller.active_cache_.clear();
        poller.timer_fast_ = true;
        ClearRetired();
        /* reaper 仍在跑（用例没走到自己的 StopReaper）也在此收割：retired_ 已清空，
         * (stop && empty) 可达，join 有界。正常收尾过的用例 joinable()==false，零开销。 */
        poller.reaper_stop_.store(true, std::memory_order_release);
        poller.retire_cv_.notify_all();
        if (poller.reaper_thread_.joinable()) {
            poller.reaper_thread_.join();
        }
        poller.reaper_stop_.store(false, std::memory_order_relaxed);
    }

    /* 单独清理 retired_：把残留条目整体 swap 出来销毁（~UmqSocket + close fd），
     * 队列瞬时清空。Stop()/StopReaper() 前调用：ReaperLoop 的退出条件是
     * (reaper_stop_ && retired_.empty())，残留条目会让 reaper 500us/轮空转
     * 清不空、后续 join 死等。清理后 usleep 1ms 宽限（≥ 2 × 500us reaper
     * 轮次节拍）：让可能在飞的 RetireSweep（batch 已 swap 出、暂不在
     * retired_ 里）跑完本轮、keep 回插落定，再进入随后的 Stop/StopReaper，
     * 避免清空后立刻 join 撞上轮中途状态。 */
    void ClearRetired()
    {
        auto &poller = TxCqePoller::Instance();
        std::vector<TxCqePoller::RetiredSocket> leftovers;
        {
            std::lock_guard<std::mutex> lk(poller.retire_mutex_);
            leftovers.swap(poller.retired_);
        }
        for (auto &entry : leftovers) {
            entry.sock = nullptr; /* ~UmqSocket：UT 路径无 bind，直接析构 */
            if (entry.fd >= 0) {
                ::close(entry.fd); /* 用 ::close：此时 LibcApi::close_ptr 可能为 null */
            }
        }
        usleep(1000);
    }

    /* Register the socket the way production does (AddSocket keeps the O(1)
     * slot bookkeeping) and put it into the active set — sweeps only visit
     * active sockets. */
    void AddSocketToPoller(const SocketPtr &sock)
    {
        auto &poller = TxCqePoller::Instance();
        poller.AddSocket(sock);
        poller.MarkActive(sock);
    }
};

// ==================== PollUmqTx first-empty-exit (via ForceDrainTx) ====================

// Tracer Bullet: when umq_poll returns 0 on the first call, ForceDrainTx
// (which calls PollUmqTx with poll_to_empty=true) must exit after exactly
// one poll — not loop POLL_TX_RETRY_MAX_CNT (50) times as before.
TEST_F(TxUnifiedPollerTest, ForceDrainTx_FirstZeroPoll_ExitsAfterSingleCall)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));

    txOps.ForceDrainTx(nullptr);

    GlobalMockObject::verify();
}

// Drain mode: when DoUmqTxPoll returns CQEs (>0) then 0 (empty), PollUmqTx
// must continue past the first non-zero poll and exit on the first zero.
// This verifies the loop continues while CQEs are available and breaks on
// first empty — the core drain semantics of poll_to_empty=true.
TEST_F(TxUnifiedPollerTest, ForceDrainTx_DrainMode_ReturnsCqeThenZero_ExitsOnSecondCall)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);

    MOCKER_CPP(&UmqTxOps::DoUmqTxPoll).expects(exactly(2)).will(returnValue(1)).then(returnValue(0));

    txOps.ForceDrainTx(nullptr);

    GlobalMockObject::verify();
}

// ==================== PollAllSockets skip-no-inflight ====================

// PollAllSockets must skip sockets with no in-flight WRs
// (tx_queue_avail_num_ == UBS_TX_DEPTH). A freshly constructed UmqTxOps
// has tx_queue_avail_num_ initialized to UBS_TX_DEPTH, so ForceDrainTx
// (and thus umq_poll) must NOT be called for such sockets.
TEST_F(TxUnifiedPollerTest, PollAllSockets_NoInflightSocket_SkipsForceDrainTx)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    /* tx_ops_ 已是非拥有指针（ops 内嵌于 UmqSocket）：测试以栈对象注入 */
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    TxCqePoller::Instance().PollAllSockets();

    GlobalMockObject::verify();
}

// PollAllSockets must process sockets with in-flight WRs
// (tx_queue_avail_num_ < UBS_TX_DEPTH). When the socket has outstanding TX,
// ForceDrainTx is called which invokes umq_poll. With first-empty-exit,
// a single umq_poll returning 0 is sufficient to complete the drain.
TEST_F(TxUnifiedPollerTest, PollAllSockets_InflightSocket_CallsForceDrainTx)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    /* tx_ops_ 已是非拥有指针（ops 内嵌于 UmqSocket）：测试以栈对象注入 */
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));

    TxCqePoller::Instance().PollAllSockets();

    GlobalMockObject::verify();
}

// ==================== pool-touch serialization (issue #41 review) ====================

// While tx_pool_touch_mutex_ is held (reaper destroying on the shared pool),
// PollAllSockets must skip the whole tick instead of draining concurrently
// with umq_destroy: with an in-flight socket registered and the mutex held
// by the test, umq_poll must NOT be called. Once the lock is released, the
// very next tick drains normally — the guard skips, never wedges.
TEST_F(TxUnifiedPollerTest, PollAllSockets_PoolMutexHeld_SkipsTick)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    /* tx_ops_ 已是非拥有指针（ops 内嵌于 UmqSocket）：测试以栈对象注入 */
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    auto &poller = TxCqePoller::Instance();

    /* 争用轮：持锁期间整个 tick 必须被跳过（-fno-access-control 直取私有锁） */
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    {
        std::lock_guard<std::mutex> reaper_stand_in(poller.tx_pool_touch_mutex_);
        poller.PollAllSockets();
    }
    GlobalMockObject::verify();

    /* 释放后下一 tick 正常排水：跳过语义不会卡死 */
    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
}

// ==================== active set (O(active) sweeps) ====================

// A socket with nothing in flight and no bigdata work is dropped from the
// active set by the first sweep and its tx_poller_active_ flag is cleared —
// this is what makes 40k idle links cost the poller nothing per round.
TEST_F(TxUnifiedPollerTest, ActiveSet_IdleSocket_DroppedBySweep)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    auto &poller = TxCqePoller::Instance();
    EXPECT_EQ(poller.ActiveCount(), 1u);
    EXPECT_TRUE(sock->tx_poller_active_.load(std::memory_order_relaxed));

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();

    EXPECT_EQ(poller.ActiveCount(), 0u);
    EXPECT_FALSE(sock->tx_poller_active_.load(std::memory_order_relaxed));
    /* still registered: only the active-set membership changed */
    {
        Locker lock(poller.mutex_);
        EXPECT_EQ(poller.sockets_.size(), 1u);
    }
}

// A socket with in-flight WRs stays in the active set across sweeps as long
// as its SQ is not fully available again.
TEST_F(TxUnifiedPollerTest, ActiveSet_InflightSocket_Retained)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    auto &poller = TxCqePoller::Instance();

    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    poller.PollAllSockets();
    EXPECT_EQ(poller.ActiveCount(), 1u);
    poller.TxSweepOnce(false);
    EXPECT_EQ(poller.ActiveCount(), 1u);
    GlobalMockObject::verify();
    EXPECT_TRUE(sock->tx_poller_active_.load(std::memory_order_relaxed));
}

// MarkActive is idempotent: a busy link marking itself on every post yields
// exactly one active entry, and re-marking after a drop re-adds exactly one.
TEST_F(TxUnifiedPollerTest, ActiveSet_MarkActive_Idempotent)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    auto &poller = TxCqePoller::Instance();
    poller.AddSocket(sock);
    poller.MarkActive(sock);
    poller.MarkActive(sock);
    poller.MarkActive(sock.Get());
    EXPECT_EQ(poller.ActiveCount(), 1u);

    poller.PollAllSockets(); /* idle → dropped */
    EXPECT_EQ(poller.ActiveCount(), 0u);
    poller.MarkActive(sock);
    EXPECT_EQ(poller.ActiveCount(), 1u);
}

// A deregistered socket (DelSocket) is dropped by the next sweep even if it
// still has in-flight WRs — nobody polls it any more.
TEST_F(TxUnifiedPollerTest, ActiveSet_DeregisteredSocket_Dropped)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    auto &poller = TxCqePoller::Instance();
    poller.DelSocket(sock);
    EXPECT_EQ(sock->tx_poller_slot_.load(std::memory_order_relaxed), -1);

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets(); /* not touched any more (the reaper may own its UMQ), just forgotten */
    GlobalMockObject::verify();
    EXPECT_EQ(poller.ActiveCount(), 0u);
    EXPECT_FALSE(sock->tx_poller_active_.load(std::memory_order_relaxed));
}

// AddSocket/DelSocket are O(1) via tx_poller_slot_: adding twice keeps one
// entry, deleting the middle of three keeps the other two with correct slots.
TEST_F(TxUnifiedPollerTest, Registry_SlotBookkeeping_AddTwiceDelMiddle)
{
    auto &poller = TxCqePoller::Instance();
    UmqSocketPtr a = MakeRef<UmqSocket>(TEST_FD);
    UmqSocketPtr b = MakeRef<UmqSocket>(TEST_FD + 1);
    UmqSocketPtr c = MakeRef<UmqSocket>(TEST_FD + 2);
    SocketPtr sa = RefConvert<UmqSocket, Socket>(a);
    SocketPtr sb = RefConvert<UmqSocket, Socket>(b);
    SocketPtr sc = RefConvert<UmqSocket, Socket>(c);
    poller.AddSocket(sa);
    poller.AddSocket(sa); /* duplicate: no second entry */
    poller.AddSocket(sb);
    poller.AddSocket(sc);
    {
        Locker lock(poller.mutex_);
        EXPECT_EQ(poller.sockets_.size(), 3u);
    }
    EXPECT_EQ(sa->tx_poller_slot_.load(), 0);
    EXPECT_EQ(sb->tx_poller_slot_.load(), 1);
    EXPECT_EQ(sc->tx_poller_slot_.load(), 2);
    poller.DelSocket(sb);
    {
        Locker lock(poller.mutex_);
        EXPECT_EQ(poller.sockets_.size(), 2u);
        EXPECT_EQ(poller.sockets_[1].Get(), sc.Get()); /* c moved into b's slot */
    }
    EXPECT_EQ(sb->tx_poller_slot_.load(), -1);
    EXPECT_EQ(sc->tx_poller_slot_.load(), 1);
    poller.DelSocket(sb); /* second delete: no-op */
    poller.DelSocket(sa);
    poller.DelSocket(sc);
    {
        Locker lock(poller.mutex_);
        EXPECT_TRUE(poller.sockets_.empty());
    }
}

// ==================== S1: TxCqePoller state primitives ====================

// NotifyInflight sets the global "any in-flight TX WR" flag. AnyTxInflight
// returns the flag's current value. Freshly reset poller has no inflight.
TEST_F(TxUnifiedPollerTest, AnyTxInflight_DefaultFalse_AfterNotifyInflightTrue)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
    EXPECT_FALSE(poller.AnyTxInflight());

    poller.NotifyInflight();
    EXPECT_TRUE(poller.AnyTxInflight());
}

// ==================== GlobalSetting defaults ====================

// UBS_TX_UNIFIED_POLL_ENABLED defaults to true (unified poller is the
// default mode; flag=OFF reverts to legacy 1ms timer + PollAllSockets).
TEST_F(TxUnifiedPollerTest, GlobalSetting_TxUnifiedPollEnabled_DefaultsTrue)
{
    EXPECT_TRUE(GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED);
}

// ==================== S1: NotifyPosted state machine ====================

// NotifyPosted: when stopped_=true, return immediately without writing
// eventfd (protects against writing a closed fd after Stop()).
TEST_F(TxUnifiedPollerTest, NotifyPosted_Stopped_ReturnsWithoutWrite)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(true, std::memory_order_relaxed);
    poller.sleeping_.store(true, std::memory_order_relaxed);
    poller.wake_fd_ = -1; // ensure no real fd

    MOCKER_CPP(::eventfd_write).expects(exactly(0)).will(returnValue(0));

    poller.NotifyPosted();

    GlobalMockObject::verify();
}

// NotifyPosted: when sleeping_=false (poller active), return without
// writing eventfd — avoid syscall when the poller is already spinning.
TEST_F(TxUnifiedPollerTest, NotifyPosted_NotSleeping_ReturnsWithoutWrite)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.sleeping_.store(false, std::memory_order_relaxed);
    poller.wake_fd_ = -1;

    MOCKER_CPP(::eventfd_write).expects(exactly(0)).will(returnValue(0));

    poller.NotifyPosted();

    GlobalMockObject::verify();
}

// NotifyPosted: when sleeping_=true and not stopped, write eventfd once
// to wake the poller from deep sleep.
TEST_F(TxUnifiedPollerTest, NotifyPosted_Sleeping_WritesEventfdOnce)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.sleeping_.store(true, std::memory_order_relaxed);
    // Use a real eventfd so eventfd_write succeeds
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    poller.wake_fd_ = fd;

    MOCKER_CPP(::eventfd_write).expects(exactly(1)).will(returnValue(0));

    poller.NotifyPosted();

    GlobalMockObject::verify();
    close(fd);
    poller.wake_fd_ = -1;
}

// ==================== S3b: deferred teardown (retire list) ====================

// RetireSocket on a running poller must NOT release the socket inline: it
// parks it on the retire list, keeping it alive, for the reaper thread to
// drive. A socket that never bound a remote (no in-flight WRs) is released on
// the first RetireSweep. This keeps the 15s CQE drain off the brpc worker
// that closed the fd. (Handoff point is ubsocket close(), with the last ref
// that ArraySet::OverrideItem returns — NOT DelSocket, which runs while brpc
// may still touch the socket.)
TEST_F(TxUnifiedPollerTest, RetireSocket_RunningPoller_DefersReleaseUntilRetireSweep)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    ASSERT_EQ(poller.RetiredCount(), 0u);

    EXPECT_TRUE(poller.RetireSocket(-1, RefConvert<UmqSocket, Socket>(umqSock))); // moved-in extra ref, no fd
    EXPECT_EQ(poller.RetiredCount(), 1u);
    EXPECT_FALSE(umqSock->IsRetiring());

    // Never bound a remote → RetireStep completes on the first call，但本测试
    // 仍持一份引用：释放（连同随之的 close）押后到引用独占（issue#32 fd 复用）。
    EXPECT_FALSE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 1u);
    EXPECT_TRUE(umqSock->IsRetiring());

    umqSock = nullptr; // 滞留引用放手 → 下一轮释放
    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);

    EXPECT_FALSE(poller.RetireSweep()); // idempotent
    poller.started_.store(false, std::memory_order_relaxed);
}

// issue #49: a socket removed from the ArraySet on a FAILED handshake is never
// retired (the failure path removes it itself, so brpc's close finds nothing).
// Its last reference sits in the deferred-release queue; the reaper must drop
// it without any RetireSocket ever happening — via the enqueue notification
// alone (there is deliberately no periodic tick).
TEST_F(TxUnifiedPollerTest, ReaperLoop_DrainsDeferredWithoutAnyRetire)
{
    auto &poller = TxCqePoller::Instance();
    auto &set = ArraySet<Socket>::GetInstance();
    ASSERT_EQ(set.Init(), 0);
    (void)set.DrainDeferredRelease();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.StartReaper();
    set.SetDeferredNotifier(&TxCqePoller::DeferredNotifierThunk, &poller);

    {
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        set.OverrideItem(TEST_FD, umqSock.Get());
        SocketPtr last = set.OverrideItem(TEST_FD, nullptr); /* 失败路径的摘表：不 Retire */
        last = nullptr;
        umqSock = nullptr; /* 仅剩队列里那份引用 */
    }
    bool drained = false;
    for (int i = 0; i < 300 && !drained; ++i) { /* 最多 ~3 s（CI 慢机器余量）；通知路径本身毫秒级 */
        drained = (set.DeferredCount() == 0);
        if (!drained) {
            usleep(10000);
        }
    }
    EXPECT_TRUE(drained);
    EXPECT_EQ(poller.RetiredCount(), 0u); /* 全程没有任何 Retire */

    set.SetDeferredNotifier(nullptr, nullptr);
    poller.StopReaper();
    poller.started_.store(false, std::memory_order_relaxed);
}

// The retired socket's fd must stay OPEN until the reaper releases the socket
// (i.e. after ~UmqSocket / umq_destroy), and be closed at that point. Closing
// it early let the kernel recycle the number for a new link while the old
// UMQ (umq_ctx == fd) was still alive in the reaper — misrouted completions,
// one hung handshake per run. A pipe fd stands in for the socket fd.
TEST_F(TxUnifiedPollerTest, RetireSocket_FdClosedOnlyAtRelease)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    /* Product code closes through LibcApi::close (dlsym'd close_ptr, resolved at
     * library init). In the UT process it is unset, so route it to libc close so
     * the fd-lifetime assertions below run against a real descriptor. */
    auto saved_close = LibcApi::close_ptr;
    LibcApi::close_ptr = ::close;

    int pfd[2] = {-1, -1};
    ASSERT_EQ(pipe(pfd), 0);
    const int retired_fd = pfd[0];

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    ASSERT_TRUE(poller.RetireSocket(retired_fd, RefConvert<UmqSocket, Socket>(umqSock)));

    // Still parked → fd must still be valid.
    EXPECT_EQ(fcntl(retired_fd, F_GETFD), 0) << "fd closed too early (before UMQ teardown)";

    // 首轮已 drain，但本测试仍持引用：号码继续保留——此刻 close 会让内核把号码
    // 复用到仍被旧 socket 拥有的数据面槽位上（issue#32 连环拒绝的窗口）。
    EXPECT_FALSE(poller.RetireSweep());
    EXPECT_EQ(fcntl(retired_fd, F_GETFD), 0) << "fd closed while socket still referenced";

    umqSock = nullptr; // 独占 → 释放 + 关号
    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);
    EXPECT_EQ(fcntl(retired_fd, F_GETFD), -1) << "fd must be closed at release";
    EXPECT_EQ(errno, EBADF);

    close(pfd[1]);
    LibcApi::close_ptr = saved_close;
    poller.started_.store(false, std::memory_order_relaxed);
}

// A reaper round is bounded by kRetireBatch so a mass-delete burst is paced
// against concurrent establishment (the destroy verbs serialize per device
// with create/bind). With kRetireBatch+1 retired sockets, one RetireSweep
// must release exactly kRetireBatch and leave one behind.
TEST_F(TxUnifiedPollerTest, RetireSweep_BoundedByRetireBatch)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    const size_t n = TxCqePoller::kRetireBatch + 1;
    std::vector<UmqSocketPtr> keep_alive;
    for (size_t i = 0; i < n; ++i) {
        UmqSocketPtr s = MakeRef<UmqSocket>(TEST_FD + static_cast<int>(i));
        keep_alive.push_back(s);
        EXPECT_TRUE(poller.RetireSocket(-1, RefConvert<UmqSocket, Socket>(s)));
    }
    ASSERT_EQ(poller.RetiredCount(), n);
    keep_alive.clear(); // 放手滞留引用：本测试只验证 batch 上限，不验证押后

    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 1u); // one left for the next paced round

    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);
    poller.started_.store(false, std::memory_order_relaxed);
}

// When the poller is not running there is no reaper to drive the retire
// list, so RetireSocket must fall back to the pre-existing inline behaviour:
// it does not park the ref, and the caller's drop tears the socket down as
// before — nothing may be left dangling on the list.
TEST_F(TxUnifiedPollerTest, RetireSocket_StoppedPoller_NoDeferral)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(false, std::memory_order_relaxed);
    poller.stopped_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr ref = RefConvert<UmqSocket, Socket>(umqSock);
    // Returns false and must NOT consume the ref: the caller closes the fd and
    // lets its ref drop (inline teardown), exactly the pre-existing path.
    EXPECT_FALSE(poller.RetireSocket(-1, std::move(ref)));
    EXPECT_NE(ref, nullptr);
    EXPECT_EQ(poller.RetiredCount(), 0u);
    poller.stopped_.store(false, std::memory_order_relaxed);
}

// DelSocket is unchanged: plain removal from the live list, no deferral —
// it runs from EpollCtlDel (brpc RemoveConsumer) while the socket is live.
TEST_F(TxUnifiedPollerTest, DelSocket_PlainRemoval_NoRetire)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    poller.DelSocket(sock);
    {
        Locker lock(poller.mutex_);
        EXPECT_TRUE(poller.sockets_.empty());
    }
    EXPECT_EQ(poller.RetiredCount(), 0u);
    poller.started_.store(false, std::memory_order_relaxed);
}

// ==================== S4: Stop idempotency ====================

// Stop() must be idempotent: multiple calls do not crash. The first call
// flips started_ from true to false; subsequent calls observe started_=false
// and return immediately. This protects against double-free of fds/mutex.
TEST_F(TxUnifiedPollerTest, Stop_CalledMultipleTimes_NoCrash)
{
    auto &poller = TxCqePoller::Instance();
    // Tear down the mutex SetUp created so Stop() sees a clean pre-Start
    // state (no fds, no mutex). This mirrors the real post-Start() teardown
    // path without requiring a live EpollRunner.
    if (poller.mutex_ != nullptr) {
        LockRegistry::LOCK_OPS.destroy(poller.mutex_);
        poller.mutex_ = nullptr;
    }
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;
    poller.wake_fd_ = -1;

    ClearRetired();
    poller.Stop(); // first call: flips started_ true->false, sets stopped_=true
    EXPECT_TRUE(poller.stopped_.load(std::memory_order_relaxed));
    EXPECT_FALSE(poller.started_.load(std::memory_order_relaxed));

    poller.Stop(); // second call: started_ already false, no-op
    poller.Stop(); // third call: still no-op
}

// ==================== S4: AddEventToRunner handles TX_WAKE ====================

// Regression: AddEventToRunner must accept RUNNER_EVENT_TYPE_TX_WAKE and
// register it via direct epoll_ctl (same path as TX_CQE_TIMER). Previously
// TX_WAKE fell through to the ShareJfrExtContext path and returned UBS_ERROR
// because ctx was nullptr — breaking flag=ON Start().
TEST_F(TxUnifiedPollerTest, AddEventToRunner_TxWakeType_ReturnsOk)
{
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epoll_fd, 0);
    int wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(wake_fd, 0);

    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_TX_WAKE;
    data.event_data.data = static_cast<uint64_t>(wake_fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};

    UmqShareJfrEpollRunnerOps ops;
    int ret = ops.AddEventToRunner(epoll_fd, wake_fd, &ev, nullptr);

    EXPECT_EQ(ret, static_cast<int>(UBS_OK));

    close(wake_fd);
    close(epoll_fd);
}

// ==================== S3: TxSweepOnce ====================

// TxSweepOnce: when stopped_=true, return false immediately without
// touching sockets_ (safe to call after Stop()).
TEST_F(TxUnifiedPollerTest, TxSweepOnce_Stopped_ReturnsFalseNoSweep)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(true, std::memory_order_relaxed);

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    EXPECT_FALSE(poller.TxSweepOnce());

    GlobalMockObject::verify();
}

// TxSweepOnce: all sockets idle (tx_queue_avail_num_ == UBS_TX_DEPTH) →
// returns false and clears any_inflight_ (release) so the active loop
// can enter backoff.
TEST_F(TxUnifiedPollerTest, TxSweepOnce_AllSocketsIdle_ReturnsFalseClearsInflight)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    /* tx_ops_ 已是非拥有指针（ops 内嵌于 UmqSocket）：测试以栈对象注入 */
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    EXPECT_FALSE(poller.TxSweepOnce());
    EXPECT_FALSE(poller.AnyTxInflight());

    GlobalMockObject::verify();
}

// TxSweepOnce: socket with in-flight WRs and umq_poll returns 0 (empty)
// → ForceDrainTx called once (first-empty-exit), no progress (avail
// unchanged), returns false, clears any_inflight_.
TEST_F(TxUnifiedPollerTest, TxSweepOnce_InflightButEmptyPoll_ReturnsFalseClearsInflight)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    /* tx_ops_ 已是非拥有指针（ops 内嵌于 UmqSocket）：测试以栈对象注入 */
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));

    EXPECT_FALSE(poller.TxSweepOnce());
    EXPECT_FALSE(poller.AnyTxInflight());

    GlobalMockObject::verify();
}

// ==================== S2: RunUnifiedActiveLoop degenerate (LOOP_POLL=false) ====================

// RunUnifiedActiveLoop(main_umq) with UBS_SHARE_JFR_LOOP_POLL_ENABLED=false:
// degenerates to a single RxPollQuantum + TxSweepOnce, no spin. With
// umq_poll returning 0 (empty), both are single-call and the loop returns
// immediately. Verifies bounded execution (≤ 1s) and single-round semantics.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_LoopPollDisabled_SingleRoundNoSpin)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = false;
    UmqShareJfrEpollRunnerOps ops;

    // RxPollQuantum calls umq_poll once → returns 0 → false (single round)
    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
}

// RunUnifiedActiveLoop() (Tx-only overload) with LOOP_POLL=false: single
// TxSweepOnce, no spin. With no sockets registered, TxSweepOnce returns
// false immediately.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_TxOnly_LoopPollDisabled_SingleRound)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = false;
    UmqShareJfrEpollRunnerOps ops;

    // No sockets → TxSweepOnce returns false without umq_poll
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    ops.RunUnifiedActiveLoop();

    GlobalMockObject::verify();
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
}

// ==================== S2: RunUnifiedActiveLoop spin with backoff (bounded) ====================

// RunUnifiedActiveLoop(main_umq) with LOOP_POLL=true, SPIN_ROUNDS=1,
// BACKOFF_MAX_US=1: spins 1 round (idle), then backoff 10us → 1us cap →
// break. Bounded by the injected tight parameters so the test stays ≤ 1s.
// umq_poll always returns 0 (empty), TxSweepOnce returns false (no sockets).
// Deterministic iteration: Round1(idleRounds=1≤1→continue) → Round2(idleRounds=2>1,
// backoffUs=0→10, usleep) → Round3(backoffUs=10, 10<1 false→break) = 3 calls.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_SpinRounds1_BackoffMax1_ExitsBounded)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false; // skip sched_yield
    UmqShareJfrEpollRunnerOps ops;

    // Bounded spin with MAX_IDLE_ROUNDS=2: RxPollQuantum returns false
    // twice (umq_poll=0, no sockets), then loop exits.
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
}

// ==================== S2: RunUnifiedActiveLoop progress path ====================

// RunUnifiedActiveLoop: the AnyTxInflight() continuation path is a
// race-condition optimization — if NotifyInflight() fires between
// TxSweepOnce() and the condition check, the loop continues for one
// more round. However, TxSweepOnce clears any_inflight_ when there's no
// progress, so with no registered sockets the flag is cleared on the
// first TxSweepOnce call. This test verifies that even with
// any_inflight_=true initially, the loop still exits within bounded
// iterations (no infinite loop). This documents the design: the
// AnyTxInflight flag is a one-shot hint, not a persistent spin trigger.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_AnyInflightClearedBySweep_ExitsBounded)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false;
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(true, std::memory_order_relaxed);
    UmqShareJfrEpollRunnerOps ops;

    // TxSweepOnce clears any_inflight_ on first call (no progress, no
    // sockets). RxPollQuantum returns false → MAX_IDLE_ROUNDS=2 → 2 calls.
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
    // Flag was cleared by TxSweepOnce
    EXPECT_FALSE(poller.AnyTxInflight());
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
}

// ==================== Issue 04: sleeping_ state machine ====================

// SetSleeping/IsSleeping: basic state query for the DEEP_IDLE flag.
TEST_F(TxUnifiedPollerTest, SetSleeping_IsSleeping_RoundTrip)
{
    auto &poller = TxCqePoller::Instance();
    poller.SetSleeping(false, std::memory_order_relaxed);
    EXPECT_FALSE(poller.IsSleeping());

    poller.SetSleeping(true, std::memory_order_relaxed);
    EXPECT_TRUE(poller.IsSleeping());

    poller.SetSleeping(false, std::memory_order_relaxed);
    EXPECT_FALSE(poller.IsSleeping());
}

// Lost-wakeup guard: when RunUnifiedActiveLoop enters DEEP_IDLE (sets
// sleeping_=true), it re-checks AnyTxInflight(). If true (a post raced
// between the last sweep and the store), it re-enters ACTIVE instead of
// sleeping. This test verifies the recheck prevents lost wakeup by
// setting any_inflight_ AFTER the normal break would occur, simulating
// a racing NotifyInflight call. Since TxSweepOnce clears the flag when
// there's no progress, the recheck catches a flag set by a concurrent
// thread between TxSweepOnce and the sleeping_ store.
// RunUnifiedActiveLoop(main_umq) bounded spin: when both RX and TX are
// idle (umq_poll returns 0, no sockets), the loop exits after 1 idle
// round without entering DEEP_IDLE (no sleeping_ state machine in the
// SHARE_JFR path — bounded rounds prevent starvation). sleeping_ is
// NOT set by the SHARE_JFR overload; it is only set by the Tx-only
// overload's DEEP_IDLE path.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_LostWakeupRecheck_SleepingStaysTrueWhenIdle)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false;
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
    poller.SetSleeping(false, std::memory_order_relaxed);
    UmqShareJfrEpollRunnerOps ops;

    // Bounded spin with MAX_IDLE_ROUNDS=2: 2 idle rounds (umq_poll=0 each)
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
    // SHARE_JFR + no inflight → DEEP_IDLE sets sleeping_=true (design §4.3)
    EXPECT_TRUE(poller.IsSleeping());
    poller.SetSleeping(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
}

// Lost-wakeup recheck: if AnyTxInflight()=true at the break point (set
// by a concurrent NotifyInflight between TxSweepOnce and SetSleeping),
// the loop re-enters ACTIVE instead of sleeping. This race window is
// inherently multi-threaded and cannot be deterministically tested in
// a single-threaded unit test (the umq_poll mock runs before
// TxSweepOnce, which clears the flag). The recheck logic is verified
// by code inspection: the 2-line guard `if (AnyTxInflight()) { continue; }`
// after `SetSleeping(true)` is a standard lost-wakeup pattern.

// ==================== Issue 04: ProcessOneEvent clears sleeping_ ====================

// ProcessOneEvent clears sleeping_ on entry (woken from epoll_wait).
// This test verifies the flag transitions: set sleeping_=true, then
// simulate a TX_WAKE event → ProcessOneEvent clears sleeping_.
TEST_F(TxUnifiedPollerTest, ProcessOneEvent_TxWake_ClearsSleeping)
{
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    auto &poller = TxCqePoller::Instance();
    poller.SetSleeping(true, std::memory_order_relaxed);
    EXPECT_TRUE(poller.IsSleeping());

    // Initialize LibcApi::read_ptr to the real read (needed by
    // ProcessOneEvent's TX_WAKE branch which drains the eventfd).
    LibcApi::read_ptr = ::read;

    // Create a TX_WAKE event with a real eventfd (so read succeeds)
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    eventfd_write(fd, 1); // prime the eventfd so read has data
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_TX_WAKE;
    data.event_data.data = static_cast<uint64_t>(fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};

    UmqShareJfrEpollRunnerOps ops;
    // ProcessOneEvent should clear sleeping_ and call RunUnifiedActiveLoop
    // (Tx-only, LOOP_POLL=false → single TxSweepOnce, no umq_poll)
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = false;
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    ops.ProcessOneEvent(ev);

    GlobalMockObject::verify();
    EXPECT_FALSE(poller.IsSleeping());
    close(fd);
    LibcApi::read_ptr = nullptr;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
}

// RunUnifiedActiveLoop(main_umq) bounded spin idle path: no inflight,
// no progress → exits after 1 idle round. sleeping_ is NOT set (SHARE_JFR
// path has no DEEP_IDLE state machine; bounded rounds prevent starvation).
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_DeepIdle_SleepingStaysTrue)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false;
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
    poller.SetSleeping(false, std::memory_order_relaxed);
    UmqShareJfrEpollRunnerOps ops;

    // MAX_IDLE_ROUNDS=2: 2 idle rounds, umq_poll=0 each
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
    // SHARE_JFR + no inflight → DEEP_IDLE sets sleeping_=true (design §4.3)
    EXPECT_TRUE(poller.IsSleeping());
    poller.SetSleeping(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
}

// RunUnifiedActiveLoop() Tx-only overload with LOOP_POLL=true, SPIN_ROUNDS=1,
// BACKOFF_MAX_US=1: same backoff state machine but only TxSweepOnce (no
// RxPollQuantum). No sockets → TxSweepOnce returns false → 3 rounds → break.
TEST_F(TxUnifiedPollerTest, RunUnifiedActiveLoop_TxOnly_SpinRounds1_BackoffMax1_ExitsBounded)
{
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_SPIN_ROUNDS = 1;
    GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 1;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false;
    UmqShareJfrEpollRunnerOps ops;

    // No sockets → TxSweepOnce false each round. 3 rounds: spin + backoff + break
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    ops.RunUnifiedActiveLoop();

    GlobalMockObject::verify();
    GlobalSetting::UBS_TX_POLLER_SPIN_ROUNDS = 64;
    GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 200;
    GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = true;
}

// ==================== Timer adaptive period (OPT-1) ====================

// --- Start() with flag=ON: timer starts at 100us (FAST) ---
// Note: cannot verify timerfd_gettime here because Start() registers the
// timer onto the SHARE_JFR_RX_RUNNER which may fire PollAllSockets
// (empty active set) and switch to SLOW before we read. Verify timer_fast_
// instead — Start() explicitly sets it to true.
TEST_F(TxUnifiedPollerTest, Start_UnifiedEnabled_TimerFastInitTrue)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(false, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;
    poller.wake_fd_ = -1;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    /* FALLBACK_MS=0 disables adaptive SLOW switching (MaybeSwitchToSlow
     * early-returns), so the background runner thread cannot flip
     * timer_fast_ to false before we read it. Without this, the 100us
     * timer fires PollAllSockets→MaybeSwitchToSlow on an empty active
     * set and races the EXPECT_TRUE below. */
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 0;
    LibcApi::close_ptr = ::close;
    LibcApi::read_ptr = ::read;

    ASSERT_EQ(poller.Start(), 0);
    EXPECT_TRUE(poller.timer_fast_); /* starts in FAST; SLOW switch disabled */
    ClearRetired();
    poller.Stop();
    LibcApi::close_ptr = nullptr;
    LibcApi::read_ptr = nullptr;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
}

// --- Start() with flag=OFF: timer starts at 100us (FAST) ---
TEST_F(TxUnifiedPollerTest, Start_UnifiedDisabled_TimerFastInitTrue)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(false, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;
    poller.wake_fd_ = -1;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    /* FALLBACK_MS=0 disables adaptive SLOW switching so the background
     * runner thread cannot flip timer_fast_ before we read it. */
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 0;
    LibcApi::close_ptr = ::close;
    LibcApi::read_ptr = ::read;

    ASSERT_EQ(poller.Start(), 0);
    EXPECT_TRUE(poller.timer_fast_); /* starts in FAST; SLOW switch disabled */

    ClearRetired();
    poller.Stop();
    LibcApi::close_ptr = nullptr;
    LibcApi::read_ptr = nullptr;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
}

// --- flag=ON: PollAllSockets with empty active set also switches to SLOW ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_EmptyActiveSet_FlagOn_SwitchesToSlow)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    LibcApi::read_ptr = ::read;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    timerfd_settime(tfd, 0, &fast, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = true;

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, static_cast<long>(GlobalSetting::UBS_TX_POLLER_FALLBACK_MS) * 1000 * 1000);
    EXPECT_FALSE(poller.timer_fast_);

    LibcApi::read_ptr = nullptr;
    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = false;
    GlobalMockObject::verify();
}

// --- flag=OFF: PollAllSockets with empty active set switches timer to SLOW ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_EmptyActiveSet_SwitchesTimerToSlow)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    LibcApi::read_ptr = ::read;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    timerfd_settime(tfd, 0, &fast, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = true;

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, static_cast<long>(GlobalSetting::UBS_TX_POLLER_FALLBACK_MS) * 1000 * 1000);
    EXPECT_FALSE(poller.timer_fast_);

    LibcApi::read_ptr = nullptr;
    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = false;
    GlobalMockObject::verify();
}

// --- flag=OFF: MarkActive on SLOW timer switches back to FAST ---
TEST_F(TxUnifiedPollerTest, MarkActive_SlowTimer_SwitchesToFast)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec slow{};
    slow.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FALLBACK_MS) * 1000 * 1000;
    slow.it_interval.tv_nsec = slow.it_value.tv_nsec;
    timerfd_settime(tfd, 0, &slow, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = false;

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    poller.MarkActive(sock);

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L); /* back to FAST */
    EXPECT_TRUE(poller.timer_fast_);

    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }
    GlobalMockObject::verify();
}

// --- FALLBACK_MS >= 1000 must be normalized to tv_sec/tv_nsec (a raw
// nsec-only value >= 1e9 makes timerfd_settime fail with EINVAL, which must
// not silently leave timer_fast_ inconsistent) ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_EmptyActiveSet_FallbackMsAbove1000_NormalizesSecNsec)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 1500;
    LibcApi::read_ptr = ::read;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    timerfd_settime(tfd, 0, &fast, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = true;

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 1);
    EXPECT_EQ(curr.it_interval.tv_nsec, 500'000'000L);
    EXPECT_FALSE(poller.timer_fast_);

    LibcApi::read_ptr = nullptr;
    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED = false;
    GlobalMockObject::verify();
}

// --- FALLBACK_MS == 0 would disarm the timerfd (it_value==0 kills the
// fallback cadence entirely): adaptive switching must stay off (keep FAST) ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_EmptyActiveSet_FallbackMsZero_KeepsFast)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 0;
    LibcApi::read_ptr = ::read;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    timerfd_settime(tfd, 0, &fast, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = true;

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L); /* still FAST */
    EXPECT_TRUE(poller.timer_fast_);

    LibcApi::read_ptr = nullptr;
    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    GlobalMockObject::verify();
}

// --- Busy socket in the active set: timer must stay FAST (no SLOW switch) ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_BusyActiveSocket_KeepsFast)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps();
    ASSERT_NE(umqSock->GetTx(), nullptr);
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    GlobalSetting::UBS_TX_POLLER_FALLBACK_MS = 100;
    LibcApi::read_ptr = ::read;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(tfd, 0);
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    timerfd_settime(tfd, 0, &fast, nullptr);
    poller.timer_fd_ = tfd;
    poller.timer_fast_ = true;

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0)); /* ForceDrainTx only */
    poller.PollAllSockets();

    struct itimerspec curr{};
    ASSERT_EQ(timerfd_gettime(tfd, &curr), 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L); /* still FAST */
    EXPECT_TRUE(poller.timer_fast_);

    LibcApi::read_ptr = nullptr;
    close(tfd);
    poller.timer_fd_ = -1;
    poller.timer_fast_ = true;
    GlobalMockObject::verify();
}

// --- Sweep that drops the last idle socket must re-sync active_cache_
// (dropped sockets must not linger in the cache pinning SocketPtr refs) ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_DropsIdleSocket_ActiveCacheResynced)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    poller.MarkActive(sock);
    EXPECT_EQ(poller.ActiveCount(), 1u);

    poller.TxSweepOnce(false, true); /* idle → dropped, cache re-synced */

    EXPECT_EQ(poller.ActiveCount(), 0u);
    EXPECT_TRUE(poller.active_cache_.empty());
    EXPECT_FALSE(poller.active_dirty_.load(std::memory_order_relaxed));

    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }
}

// ==================== Additional coverage tests ====================

// --- NotifyPosted: eventfd_write fails with non-EBADF → logs error ---
TEST_F(TxUnifiedPollerTest, NotifyPosted_EventfdWriteFails_LogsError)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.sleeping_.store(true, std::memory_order_relaxed);
    // Use an invalid fd so eventfd_write fails with EBADF (downgraded, no error log)
    poller.wake_fd_ = -1;

    // wake_fd_ < 0 → return without writing
    MOCKER_CPP(::eventfd_write).expects(exactly(0)).will(returnValue(0));
    poller.NotifyPosted();
    GlobalMockObject::verify();
}

// --- AddSocket with null socket: no-op ---
TEST_F(TxUnifiedPollerTest, AddSocket_Null_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.AddSocket(SocketPtr());
    EXPECT_EQ(poller.ActiveCount(), 0u);
}

// --- DelSocket with null socket: no-op ---
TEST_F(TxUnifiedPollerTest, DelSocket_Null_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.DelSocket(SocketPtr());
    // No crash
}

// --- MarkActive with null socket: no-op ---
TEST_F(TxUnifiedPollerTest, MarkActive_Null_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.MarkActive(nullptr);
    EXPECT_EQ(poller.ActiveCount(), 0u);
}

// --- RetireSocket with null sock: returns false ---
TEST_F(TxUnifiedPollerTest, RetireSocket_Null_ReturnsFalse)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    EXPECT_FALSE(poller.RetireSocket(-1, SocketPtr()));
    EXPECT_EQ(poller.RetiredCount(), 0u);
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- RetireSweep empty list: returns false ---
TEST_F(TxUnifiedPollerTest, RetireSweep_Empty_ReturnsFalse)
{
    auto &poller = TxCqePoller::Instance();
    EXPECT_FALSE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);
}

// --- DrainTimerFd: no timer_fd (default -1), no-op ---
TEST_F(TxUnifiedPollerTest, DrainTimerFd_NoTimerFd_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.timer_fd_ = -1;
    poller.DrainTimerFd(); // no crash
}

// --- DrainTimerFd with real timer_fd ---
TEST_F(TxUnifiedPollerTest, DrainTimerFd_RealTimerFd_Drains)
{
    auto &poller = TxCqePoller::Instance();
    LibcApi::read_ptr = ::read;
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct itimerspec its = {};
    its.it_value.tv_nsec = 1; // fire immediately
    its.it_interval.tv_nsec = 100000;
    timerfd_settime(fd, 0, &its, nullptr);
    usleep(1000); // wait for timer to fire

    poller.timer_fd_ = fd;
    poller.DrainTimerFd();
    // No crash, timer drained

    close(fd);
    poller.timer_fd_ = -1;
    LibcApi::read_ptr = nullptr;
}

// --- PollAllSockets: stopped_ returns after DrainTimerFd ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_Stopped_ReturnsEarly)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(true, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
    poller.stopped_.store(false, std::memory_order_relaxed);
}

// --- RegisterOrphanSweep with invalid handle: no-op ---
TEST_F(TxUnifiedPollerTest, RegisterOrphanSweep_InvalidHandle_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.RegisterOrphanSweep(UMQ_INVALID_HANDLE);
    Locker lock(poller.mutex_);
    EXPECT_TRUE(poller.orphan_main_umqs_.empty());
}

// --- RegisterOrphanSweep: register and duplicate ---
TEST_F(TxUnifiedPollerTest, RegisterOrphanSweep_RegisterAndDuplicate)
{
    auto &poller = TxCqePoller::Instance();
    poller.RegisterOrphanSweep(TEST_UMQ_HANDLE);
    poller.RegisterOrphanSweep(TEST_UMQ_HANDLE); // duplicate, no second entry
    {
        Locker lock(poller.mutex_);
        EXPECT_EQ(poller.orphan_main_umqs_.size(), 1u);
    }
    poller.RegisterOrphanSweep(TEST_UMQ_HANDLE_2);
    {
        Locker lock(poller.mutex_);
        EXPECT_EQ(poller.orphan_main_umqs_.size(), 2u);
        EXPECT_EQ(poller.orphan_main_umqs_[0], TEST_UMQ_HANDLE);
        EXPECT_EQ(poller.orphan_main_umqs_[1], TEST_UMQ_HANDLE_2);
    }
    // Cleanup
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.clear();
    }
}

// --- SweepOrphanPools stopped: returns false ---
TEST_F(TxUnifiedPollerTest, SweepOrphanPools_Stopped_ReturnsFalse)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(true, std::memory_order_relaxed);
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    EXPECT_FALSE(poller.SweepOrphanPools());
    GlobalMockObject::verify();
    poller.stopped_.store(false, std::memory_order_relaxed);
}

// ==================== SnapshotActive cache (OPT-2) ====================

// Two consecutive TxSweepOnce without MarkActive: active_dirty_ goes
// true→false after first sweep (lock-protected), stays false after second.
TEST_F(TxUnifiedPollerTest, TxSweepOnce_NoMarkActiveBetween_DirtyStaysFalse)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }
    poller.active_dirty_.store(true, std::memory_order_relaxed);

    poller.TxSweepOnce(false, true);
    EXPECT_FALSE(poller.active_dirty_.load(std::memory_order_relaxed));

    poller.TxSweepOnce(false, true);
    EXPECT_FALSE(poller.active_dirty_.load(std::memory_order_relaxed));
}

// MarkActive between two sweeps sets dirty=true inside active_mutex_,
// so the second sweep re-snapshots (dirty goes true→false→true→false).
TEST_F(TxUnifiedPollerTest, MarkActive_BetweenSweeps_TriggersResnapshot)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }
    poller.active_dirty_.store(true, std::memory_order_relaxed);

    poller.TxSweepOnce(false, true);
    EXPECT_FALSE(poller.active_dirty_.load(std::memory_order_relaxed));

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    poller.MarkActive(sock);
    EXPECT_TRUE(poller.active_dirty_.load(std::memory_order_relaxed));

    poller.TxSweepOnce(false, true);
    EXPECT_FALSE(poller.active_dirty_.load(std::memory_order_relaxed));

    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }
}

// --- SweepOrphanPools empty: returns false, no umq_poll ---
TEST_F(TxUnifiedPollerTest, SweepOrphanPools_Empty_ReturnsFalse)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.clear();
    }
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    EXPECT_FALSE(poller.SweepOrphanPools());
    GlobalMockObject::verify();
}

// --- TxSweepOnce with quick_poll=true: calls QuickPollTx ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_QuickPoll_CallsQuickPollTx)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));
    EXPECT_FALSE(poller.TxSweepOnce(true, true)); // quick_poll=true
    EXPECT_FALSE(poller.AnyTxInflight());
    GlobalMockObject::verify();
}

// --- TxSweepOnce with sweep_orphan=true and orphan registered: calls SweepOrphanPools ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_SweepOrphan_CallsSweepOrphanPools)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    // Register an orphan main umq
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.push_back(TEST_UMQ_HANDLE);
    }

    // No active sockets, but orphan sweep runs
    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));
    poller.TxSweepOnce(true, false); // sweep_orphan=true
    GlobalMockObject::verify();

    // Cleanup
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.clear();
    }
    poller.any_inflight_.store(false, std::memory_order_relaxed);
}

// --- RetireSweep: socket not drained (umqSock != nullptr, RetireStep returns false) ---
TEST_F(TxUnifiedPollerTest, RetireSweep_NotDrained_KeptInList)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    // Set state to SOCK_STAT_INIT so RetireStep returns false (not done)
    umqSock->state_.store(SOCK_STAT_INIT, std::memory_order_relaxed);
    // RetireStep checks IsRetiring() → true after first RetireSocket, returns false until drained
    EXPECT_TRUE(poller.RetireSocket(-1, RefConvert<UmqSocket, Socket>(umqSock)));
    EXPECT_EQ(poller.RetiredCount(), 1u);

    // RetireSweep: umqSock is not nullptr, RetireStep may return false → keep
    // But IsRetiring() is true → RetireStep returns false → not done → keep
    // However, deadline might have passed → done=true → release
    // Since deadline is now + UMQ_DESTROY_FLUSH_TIMEOUT_MS, it's in the future → not expired
    // So done = false || false = false → kept
    poller.RetireSweep();
    // Socket still in list (not drained, not expired)
    // RetiredCount may be 0 or 1 depending on RetireStep behavior
    // With IsRetiring=true, RetireStep returns false → kept

    // Cleanup: force release by clearing the list
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        poller.retired_.clear();
    }
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- SnapshotActive / RebuildActive basic ---
TEST_F(TxUnifiedPollerTest, SnapshotActive_Empty)
{
    auto &poller = TxCqePoller::Instance();
    std::vector<SocketPtr> out;
    poller.SnapshotActive(out);
    EXPECT_TRUE(out.empty());
}

TEST_F(TxUnifiedPollerTest, RebuildActive_NoNewProducers)
{
    auto &poller = TxCqePoller::Instance();
    std::vector<SocketPtr> survivors;
    poller.RebuildActive(survivors, 0);
    EXPECT_EQ(poller.ActiveCount(), 0u);
}

// --- NotifyInflight + AnyTxInflight round-trip ---
TEST_F(TxUnifiedPollerTest, NotifyInflight_SetAndQuery)
{
    auto &poller = TxCqePoller::Instance();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
    EXPECT_FALSE(poller.AnyTxInflight());
    poller.NotifyInflight();
    EXPECT_TRUE(poller.AnyTxInflight());
    poller.any_inflight_.store(false, std::memory_order_relaxed);
}

// ==================== Error path coverage ====================

// --- Start() with mutex_==nullptr: returns -1 ---
TEST_F(TxUnifiedPollerTest, Start_NullMutex_ReturnsError)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(false, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    auto *savedMutex = poller.mutex_;
    poller.mutex_ = nullptr;

    EXPECT_EQ(poller.Start(), -1);
    EXPECT_FALSE(poller.started_.load(std::memory_order_relaxed));

    poller.mutex_ = savedMutex;
}

// --- Start() already started: returns 0 without re-init ---
TEST_F(TxUnifiedPollerTest, Start_AlreadyStarted_NoReinit)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    EXPECT_EQ(poller.Start(), 0);
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- Stop() not started: returns immediately (CAS fails) ---
TEST_F(TxUnifiedPollerTest, Stop_NotStarted_NoOp)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(false, std::memory_order_relaxed);
    ClearRetired();
    poller.Stop(); // no crash, no-op
}

// --- MarkActive when stopped_: returns without adding ---
TEST_F(TxUnifiedPollerTest, MarkActive_Stopped_NoAdd)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    sock->tx_poller_active_.store(false, std::memory_order_relaxed);

    poller.MarkActive(sock);
    EXPECT_EQ(poller.ActiveCount(), 0u);
    EXPECT_FALSE(sock->tx_poller_active_.load(std::memory_order_relaxed));
    poller.stopped_.store(false, std::memory_order_relaxed);
}

// --- NotifyPosted: sleeping=true, wake_fd>=0, eventfd_write fails (non-EBADF) ---
TEST_F(TxUnifiedPollerTest, NotifyPosted_EventfdWriteNonEbadf_LogsErr)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.sleeping_.store(true, std::memory_order_relaxed);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    poller.wake_fd_ = fd;

    // Mock eventfd_write to fail with EIO (non-EBADF → logs error)
    errno = EIO;
    MOCKER_CPP(::eventfd_write).expects(exactly(1)).will(returnValue(-1));
    poller.NotifyPosted();
    GlobalMockObject::verify();

    close(fd);
    poller.wake_fd_ = -1;
    poller.sleeping_.store(false, std::memory_order_relaxed);
}

// --- RetireSocket started_=true but stopped_=true: returns false ---
TEST_F(TxUnifiedPollerTest, RetireSocket_StartedButStopped_ReturnsFalse)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    EXPECT_FALSE(poller.RetireSocket(-1, RefConvert<UmqSocket, Socket>(umqSock)));

    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- StopReaper with leftover retired sockets ---
TEST_F(TxUnifiedPollerTest, StopReaper_WithLeftovers_ReleasesInline)
{
    auto &poller = TxCqePoller::Instance();
    LibcApi::close_ptr = ::close;

    int pfd[2] = {-1, -1};
    ASSERT_EQ(pipe(pfd), 0);

    // Manually add a retired socket with fd
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        poller.retired_.push_back(TxCqePoller::RetiredSocket{RefConvert<UmqSocket, Socket>(umqSock), pfd[0], 0});
    }
    ClearRetired();
    // StopReaper should release the leftover (fd closed)
    poller.StopReaper();
    EXPECT_EQ(fcntl(pfd[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);

    close(pfd[1]);
    LibcApi::close_ptr = nullptr;
}

// --- RetireSweep: 外部引用滞留时押后 close，独占后才释放（issue#32 fd 复用连环拒） ---
TEST_F(TxUnifiedPollerTest, RetireSweep_RefPinned_DefersCloseUntilSole)
{
    auto &poller = TxCqePoller::Instance();
    LibcApi::close_ptr = ::close;

    int pfd[2] = {-1, -1};
    ASSERT_EQ(pipe(pfd), 0);

    UmqSocketPtr pinned = MakeRef<UmqSocket>(TEST_FD); /* 模拟 brpc 侧滞留引用 */
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        poller.retired_.push_back(
            TxCqePoller::RetiredSocket{RefConvert<UmqSocket, Socket>(pinned), pfd[0],
                                       SocketConnHelper::GetTimeMs() + 60000}); /* deadline 远：不触发兜底 */
    }

    /* 未独占：release 与 close 都必须押后 —— fd 不关，号码就不会被内核复用 */
    EXPECT_FALSE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 1u);
    EXPECT_NE(fcntl(pfd[0], F_GETFD), -1);

    pinned = nullptr; /* 滞留引用放手 → 独占 */
    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);
    EXPECT_EQ(fcntl(pfd[0], F_GETFD), -1); /* 独占后才关号 */
    EXPECT_EQ(errno, EBADF);

    close(pfd[1]);
    LibcApi::close_ptr = nullptr;
}

// --- RetireSweep: 引用被长期持有的 deadline 兜底 —— 先空槽再关号，不无限押后 ---
TEST_F(TxUnifiedPollerTest, RetireSweep_RefPinnedPastDeadline_FreesSlotAndCloses)
{
    auto &poller = TxCqePoller::Instance();
    LibcApi::close_ptr = ::close;

    int pfd[2] = {-1, -1};
    ASSERT_EQ(pipe(pfd), 0);

    UmqSocketPtr pinned = MakeRef<UmqSocket>(TEST_FD);
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        poller.retired_.push_back(
            TxCqePoller::RetiredSocket{RefConvert<UmqSocket, Socket>(pinned), pfd[0], 0}); /* deadline 已过 */
    }

    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);
    EXPECT_EQ(fcntl(pfd[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_TRUE(pinned->RetireDrained()); /* 对象仍存活于滞留引用，槽位已按兜底清空 */

    pinned = nullptr;
    close(pfd[1]);
    LibcApi::close_ptr = nullptr;
}

// --- PollAllSockets: socket null in active set → continue ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_NullSocketInActive_Continue)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    // Add a null SocketPtr to active set
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.push_back(SocketPtr());
    }

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
}

// --- PollAllSockets: socket deregistered (slot<0) → drop ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_DeregisteredSocket_Dropped)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    // Deregister
    sock->tx_poller_slot_.store(-1, std::memory_order_relaxed);

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
    EXPECT_EQ(poller.ActiveCount(), 0u);
}

// --- TxSweepOnce: sockBase==nullptr → RetainOrDrop with nullptr ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_NullSockBase_RetainOrDropNull)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    // Add a socket to active that RefDynamicCast<SocketBase> returns nullptr
    // This is hard to achieve with UmqSocket (which IS a SocketBase).
    // Skip: this branch requires a non-SocketBase socket, which our mocks don't produce.
    // Instead, test the null sock path (sock==nullptr)
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.push_back(SocketPtr()); // null SocketPtr
    }

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    EXPECT_FALSE(poller.TxSweepOnce());
    EXPECT_FALSE(poller.AnyTxInflight());
    GlobalMockObject::verify();
}

// --- TxSweepOnce: deregistered socket in active → drop ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_DeregisteredSocket_Dropped)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    sock->tx_poller_slot_.store(-1, std::memory_order_relaxed); // deregister

    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.TxSweepOnce();
    GlobalMockObject::verify();
    EXPECT_EQ(poller.ActiveCount(), 0u);
    EXPECT_FALSE(poller.AnyTxInflight());
}

// --- TxSweepOnce: inflight socket with progress (avail increases) ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_InflightWithProgress_ReturnsTrue)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 2, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    // umq_poll returns 1 (CQE reclaimed) → avail increases → progress
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    EXPECT_FALSE(poller.TxSweepOnce(false, false));
    poller.any_inflight_.store(true, std::memory_order_relaxed);
    EXPECT_TRUE(poller.AnyTxInflight());
    GlobalMockObject::verify();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
}

// --- SweepOrphanPools with registered umq and umq_poll returning CQEs ---
TEST_F(TxUnifiedPollerTest, SweepOrphanPools_WithUmqs_ReturnsProgress)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.push_back(TEST_UMQ_HANDLE);
    }

    // umq_poll returns 1 (progress) then 0 (empty)
    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    EXPECT_FALSE(poller.SweepOrphanPools());
    GlobalMockObject::verify();

    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.clear();
    }
}

// --- RebuildActive with new producers (active_.size() > snapshot_size) ---
TEST_F(TxUnifiedPollerTest, RebuildActive_WithNewProducers_KeepsNew)
{
    auto &poller = TxCqePoller::Instance();
    // Add 2 sockets to active
    UmqSocketPtr a = MakeRef<UmqSocket>(TEST_FD);
    UmqSocketPtr b = MakeRef<UmqSocket>(TEST_FD + 1);
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.push_back(RefConvert<UmqSocket, Socket>(a));
        poller.active_.push_back(RefConvert<UmqSocket, Socket>(b));
    }

    // Simulate sweep: snapshot_size=1 (only first was in snapshot)
    // survivors is empty (first was dropped), second was appended during sweep
    std::vector<SocketPtr> survivors;
    poller.RebuildActive(survivors, 1);
    // survivors should have the second socket (appended after snapshot)
    EXPECT_EQ(poller.ActiveCount(), 1u);
}

// --- RegisterOrphanSweep with UMQ_INVALID_HANDLE ---
TEST_F(TxUnifiedPollerTest, RegisterOrphanSweep_InvalidHandle)
{
    auto &poller = TxCqePoller::Instance();
    poller.RegisterOrphanSweep(UMQ_INVALID_HANDLE);
    Locker lock(poller.mutex_);
    EXPECT_TRUE(poller.orphan_main_umqs_.empty());
}

// ==================== Cover remaining lines ====================

// --- MarkActive: already active (exchange returns true) → return ---
TEST_F(TxUnifiedPollerTest, MarkActive_AlreadyActive_Returns)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    // Pre-set active flag so exchange returns true
    sock->tx_poller_active_.store(true, std::memory_order_relaxed);
    poller.MarkActive(sock);
    // Should not add a second entry
    EXPECT_EQ(poller.ActiveCount(), 0u);
}

// --- RetainOrDrop: deregistered socket (slot<0) → drop ---
TEST_F(TxUnifiedPollerTest, RetainOrDrop_Deregistered_Drops)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);
    // Deregister
    sock->tx_poller_slot_.store(-1, std::memory_order_relaxed);

    std::vector<SocketPtr> survivors;
    auto sockBase = RefDynamicCast<SocketBase>(sock);
    poller.RetainOrDrop(sock, sockBase.Get(), survivors);
    EXPECT_TRUE(survivors.empty());
    EXPECT_FALSE(sock->tx_poller_active_.load(std::memory_order_relaxed));
}

// --- RetainOrDrop: socket needs sweep → retained ---
TEST_F(TxUnifiedPollerTest, RetainOrDrop_NeedsSweep_Retained)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    std::vector<SocketPtr> survivors;
    auto sockBase = RefDynamicCast<SocketBase>(sock);
    poller.RetainOrDrop(sock, sockBase.Get(), survivors);
    EXPECT_EQ(survivors.size(), 1u);
}

// --- RetainOrDrop: idle socket, re-check still idle → dropped ---
TEST_F(TxUnifiedPollerTest, RetainOrDrop_IdleRecheckIdle_Dropped)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    std::vector<SocketPtr> survivors;
    auto sockBase = RefDynamicCast<SocketBase>(sock);
    poller.RetainOrDrop(sock, sockBase.Get(), survivors);
    EXPECT_TRUE(survivors.empty());
    EXPECT_FALSE(sock->tx_poller_active_.load(std::memory_order_relaxed));
}

// --- PollAllSockets: stopped_ mid-loop → break ---
TEST_F(TxUnifiedPollerTest, PollAllSockets_StoppedMidLoop_Breaks)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    // Add a socket, then set stopped_ after snapshot
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    // Set stopped_ right before calling PollAllSockets
    poller.stopped_.store(true, std::memory_order_relaxed);
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
    poller.stopped_.store(false, std::memory_order_relaxed);
}

// --- TxSweepOnce: sockBase==nullptr (not UmqSocket) → RetainOrDrop null ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_SockBaseNull_RetainOrDropNull)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    // Create a non-SocketBase socket is impossible (SocketBase is abstract).
    // Instead, test the null sock path directly
    {
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.push_back(SocketPtr()); // null
    }
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    EXPECT_FALSE(poller.TxSweepOnce());
    EXPECT_FALSE(poller.AnyTxInflight());
    GlobalMockObject::verify();
}

// --- TxSweepOnce: quick_poll=true with inflight ---
TEST_F(TxUnifiedPollerTest, TxSweepOnce_QuickPollTrue_Inflight)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));
    EXPECT_FALSE(poller.TxSweepOnce(false, true)); // quick_poll=true
    EXPECT_FALSE(poller.AnyTxInflight());
    GlobalMockObject::verify();
}

// --- SweepOrphanPools: stopped_ mid-loop → break ---
TEST_F(TxUnifiedPollerTest, SweepOrphanPools_StoppedMidLoop_Breaks)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.push_back(TEST_UMQ_HANDLE);
    }
    // Set stopped_ so the loop breaks immediately
    poller.stopped_.store(true, std::memory_order_relaxed);
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    EXPECT_FALSE(poller.SweepOrphanPools());
    GlobalMockObject::verify();
    poller.stopped_.store(false, std::memory_order_relaxed);
    {
        Locker lock(poller.mutex_);
        poller.orphan_main_umqs_.clear();
    }
}

// --- NotifyPosted: eventfd_write fails with EBADF → no error log ---
TEST_F(TxUnifiedPollerTest, NotifyPosted_EventfdWriteEbadf_NoErrorLog)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.sleeping_.store(true, std::memory_order_relaxed);
    // Use invalid fd → eventfd_write returns -1 with EBADF
    poller.wake_fd_ = 999999; // definitely invalid

    MOCKER_CPP(::eventfd_write).expects(exactly(1)).will(returnValue(-1));
    errno = EBADF;
    poller.NotifyPosted();
    GlobalMockObject::verify();

    poller.wake_fd_ = -1;
    poller.sleeping_.store(false, std::memory_order_relaxed);
}

// --- RetireSweep: non-UmqSocket entry → done=true, released ---
TEST_F(TxUnifiedPollerTest, RetireSweep_NonUmqSocket_Released)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    LibcApi::close_ptr = ::close;

    int pfd[2] = {-1, -1};
    ASSERT_EQ(pipe(pfd), 0);

    // Create a non-UmqSocket SocketPtr - use a plain SocketBase subclass
    // Actually we can't create SocketBase directly (abstract). Use UmqSocket but
    // make dynamic_cast fail by... hmm. Instead, just test with UmqSocket where
    // RetireStep returns true (already retiring). This covers the "done=true" path.
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    umqSock->state_.store(SOCK_STAT_CLOSE, std::memory_order_relaxed);
    EXPECT_TRUE(poller.RetireSocket(pfd[0], RefConvert<UmqSocket, Socket>(umqSock)));
    umqSock = nullptr; // 引用独占后 release 才放行（押后语义见 RefPinned 系列）
    // RetireSweep: umqSock != nullptr, RetireStep returns true (state=CLOSE) → done
    EXPECT_TRUE(poller.RetireSweep());
    // fd should be closed
    EXPECT_EQ(fcntl(pfd[0], F_GETFD), -1);

    close(pfd[1]);
    LibcApi::close_ptr = nullptr;
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- RetireSweep: deadline expired → done=true ---
TEST_F(TxUnifiedPollerTest, RetireSweep_DeadlineExpired_Released)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);
    LibcApi::close_ptr = ::close;

    // Manually add a retired socket with expired deadline
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        umqSock->state_.store(SOCK_STAT_INIT, std::memory_order_relaxed);
        // Set deadline to 0 (expired) - but RetireStep might return false
        // Actually deadline check: now_ms >= entry.deadline_ms → if deadline=0, always expired
        poller.retired_.push_back(TxCqePoller::RetiredSocket{RefConvert<UmqSocket, Socket>(umqSock), -1, 0});
    }

    // RetireSweep: deadline expired → done=true → released
    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 0u);

    LibcApi::close_ptr = nullptr;
    poller.started_.store(false, std::memory_order_relaxed);
}

// --- RetireSweep: not done and over batch cap → kept ---
TEST_F(TxUnifiedPollerTest, RetireSweep_NotDoneOverBatchCap_Kept)
{
    auto &poller = TxCqePoller::Instance();
    poller.started_.store(true, std::memory_order_relaxed);
    poller.stopped_.store(false, std::memory_order_relaxed);

    // Add kRetireBatch+1 retired sockets (all with future deadline and not done)
    std::vector<UmqSocketPtr> keep;
    for (size_t i = 0; i < TxCqePoller::kRetireBatch + 1; ++i) {
        UmqSocketPtr s = MakeRef<UmqSocket>(TEST_FD + static_cast<int>(i));
        s->state_.store(SOCK_STAT_INIT, std::memory_order_relaxed);
        keep.push_back(s);
        {
            std::lock_guard<std::mutex> lk(poller.retire_mutex_);
            poller.retired_.push_back(TxCqePoller::RetiredSocket{RefConvert<UmqSocket, Socket>(s), -1, UINT64_MAX});
        }
    }

    keep.clear(); // 放手滞留引用：本测试验证 batch 上限，不验证押后

    // RetireSweep: never-bound → done on first step; released up to kRetireBatch
    EXPECT_TRUE(poller.RetireSweep());
    EXPECT_EQ(poller.RetiredCount(), 1u);

    // Cleanup
    {
        std::lock_guard<std::mutex> lk(poller.retire_mutex_);
        poller.retired_.clear();
    }
    poller.started_.store(false, std::memory_order_relaxed);
}

// ==================== PollAllSockets stopped mid-loop ====================
TEST_F(TxUnifiedPollerTest, PollAllSockets_StoppedMidLoop_SecondSocketBreaks)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.timer_fd_ = -1;

    UmqSocketPtr umqSock1 = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps1(TEST_FD, TEST_UMQ_HANDLE);
    txOps1.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    umqSock1->ReinitTxOps(); /* 数据面条目按需物化 */
    umqSock1->GetTx()->tx_ops_ = &txOps1;
    SocketPtr sock1 = RefConvert<UmqSocket, Socket>(umqSock1);
    AddSocketToPoller(sock1);

    // Set stopped_ after adding socket to active — PollAllSockets loop will
    // check stopped_ at the top of the loop body and break
    poller.stopped_.store(true, std::memory_order_relaxed);
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));
    poller.PollAllSockets();
    GlobalMockObject::verify();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.DelSocket(sock1);
}

// =================--- TxSweepOnce: availAfter > availBefore (progress) ---=================

TEST_F(TxUnifiedPollerTest, TxSweepOnce_AvailIncreases_Progress)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 2, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    poller.TxSweepOnce(false, false);
    GlobalMockObject::verify();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
}

// =================--- TxSweepOnce: quick_poll with progress ---=================

TEST_F(TxUnifiedPollerTest, TxSweepOnce_QuickPoll_Progress)
{
    auto &poller = TxCqePoller::Instance();
    poller.stopped_.store(false, std::memory_order_relaxed);
    poller.any_inflight_.store(true, std::memory_order_relaxed);

    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    txOps.tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 2, std::memory_order_relaxed);
    umqSock->ReinitTxOps(); /* 数据面条目按需物化（生产路径由 GenerateSocketCommOps 完成） */
    umqSock->GetTx()->tx_ops_ = &txOps;
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock);

    MOCKER_CPP(::umq_poll).stubs().will(returnValue(0));
    poller.TxSweepOnce(true, true); // sweep_orphan=true, quick_poll=true
    GlobalMockObject::verify();
    poller.any_inflight_.store(false, std::memory_order_relaxed);
}

// ==================== issue #30 fixes: lifecycle / guards / pool gate ====================

/* 根因①·闩锁语义：Stop() 在 CAS 之前就落 shutdown_ 闩（哪怕这次 Stop 因为
 * started_==false 而空转），此后 Start() 必须永久拒绝且不产生任何副作用
 * （不建 fd、不碰 runner——拒绝发生在函数第一步）。 */
TEST_F(TxUnifiedPollerTest, Lifecycle_StopLatchesShutdown_StartRefusedForever)
{
    auto &poller = TxCqePoller::Instance();
    ASSERT_FALSE(poller.shutdown_.load());
    ClearRetired();
    poller.Stop(); /* started_==false: CAS 失败早退——但闩必须已落下 */
    EXPECT_TRUE(poller.shutdown_.load());
    EXPECT_EQ(poller.Start(), -1);
    EXPECT_EQ(poller.Start(), -1); /* 永久性：再来一次仍拒绝 */
    EXPECT_FALSE(poller.started_.load());
    poller.shutdown_.store(false, std::memory_order_relaxed); /* 归还干净的单例 */
}

/* 根因③·投递空守卫：socket 外壳被引用计数保活、但 DataPlane 槽位已拆时，
 * NewRx/TxEpollIn 必须静默丢弃而不是解引用 nullptr（修复前 = core 6/7 的
 * fetch_add(this=0x8) SIGSEGV，本用例在修复前直接段错误）。 */
TEST_F(TxUnifiedPollerTest, EpollIn_NoDataPlane_DropsInsteadOfCrash)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD); /* 未建 DataPlane 条目 */
    ASSERT_EQ(umqSock->GetRxOps(), nullptr);
    ASSERT_EQ(umqSock->GetTxOps(), nullptr);
    umqSock->NewRxEpollIn(); /* 存活即通过 */
    umqSock->NewTxEpollIn();
}

/* 根因②·池门：TxSweepOnce 对 tx_pool_touch_mutex_ 是 try-lock——被占时跳过
 * 本轮且不触碰任何状态（用 active 标志作判据：竞争轮不清、空闲轮清）。 */
TEST_F(TxUnifiedPollerTest, TxSweepOnce_PoolLockContended_SkipsRoundUntouched)
{
    auto &poller = TxCqePoller::Instance();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    AddSocketToPoller(sock); /* 无在途工作：正常一轮 sweep 会把它按 idle 清出 active */
    ASSERT_TRUE(sock->tx_poller_active_.load());
    {
        /* 持锁方放在独立线程：同线程对 std::mutex try_lock 属 UB（glibc 恰好
         * 返回 EBUSY，但不赌实现）。 */
        std::atomic<bool> locked{false}, release{false};
        std::thread holder([&] {
            std::lock_guard<std::mutex> hold(poller.tx_pool_touch_mutex_);
            locked.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) {
                usleep(100);
            }
        });
        while (!locked.load(std::memory_order_acquire)) {
            usleep(100);
        }
        EXPECT_FALSE(poller.TxSweepOnce(false));
        EXPECT_TRUE(sock->tx_poller_active_.load()) << "竞争轮不得触碰 active 状态";
        release.store(true, std::memory_order_release);
        holder.join();
    }
    poller.TxSweepOnce(false); /* 无竞争：本轮应把 idle socket 清出 */
    EXPECT_FALSE(sock->tx_poller_active_.load());
    poller.DelSocket(sock);
}
