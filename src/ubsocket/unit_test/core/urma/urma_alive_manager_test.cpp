/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * Mulan PSL v2 licensed.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: urma alive manager unit test
 * Author: hcom
 * Create: 2025-01-01
 */

#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <condition_variable>
#include <mutex>

#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_alive_manager.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_socket.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static char g_mock_buf[256 * 1024];
static UrmaJetty *dummy_jetty_ = nullptr;

/* 线程用例中 worker 与主线程并发读写计数, 必须用 atomic 避免 data race */
static std::atomic<int> g_postSendWrCalls{0};
static std::atomic<int> g_postRecvWrCalls{0};
static std::atomic<uint64_t> g_lastNotifyData{0};
static std::atomic<int> g_shutdownCalls{0};
static std::atomic<int> g_lastShutdownHow{-1};
static int (*g_origShutdown)(int, int) = nullptr;
static std::atomic<bool> g_pausePost{false};
static std::atomic<bool> g_postEntered{false};
static std::atomic<bool> g_releasePost{false};
static std::atomic<bool> g_destroyEntered{false};
static std::mutex g_postMutex;
static std::condition_variable g_postCv;

static void MockDestroyJetty()
{
    g_destroyEntered.store(true);
}

/* 测试内部 helper 需要访问 UrmaApi 函数指针: 生产代码通过 UrmaApi::xxx_ptr 全局直连 */

static urma_status_t MockPostSendWrCapture(urma_jetty_t *, urma_jfs_wr_t *wr, urma_jfs_wr_t **)
{
    if (wr != nullptr) {
        g_postSendWrCalls.fetch_add(1);
        g_lastNotifyData.store(wr->rw.notify_data);
    }
    if (g_pausePost.load()) {
        std::unique_lock<std::mutex> lock(g_postMutex);
        g_postEntered.store(true);
        g_postCv.notify_all();
        g_postCv.wait_for(lock, std::chrono::milliseconds(100), []() { return g_releasePost.load(); });
    }
    return URMA_SUCCESS;
}

static urma_status_t MockPostSendWrFail(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_FAIL;
}

static urma_status_t MockPostRecvWrSuccess(urma_jetty_t *, urma_jfr_wr_t *, urma_jfr_wr_t **)
{
    g_postRecvWrCalls.fetch_add(1);
    return URMA_SUCCESS;
}

static int MockShutdownCapture(int fd, int how)
{
    (void)fd;
    g_shutdownCalls.fetch_add(1);
    g_lastShutdownHow.store(how);
    return 0;
}

static void *MockDirectAllocate(size_t)
{
    return g_mock_buf;
}

static void MockDirectRelease(void *) {}

/* 构造 receiver 侧 WRITE_WITH_IMM CQE: imm 携带 opcode, sender ctx 携带 fd 路由 */
static urma_cr_t MakeRecvImmCqe(IOOpcode opcode, int fd)
{
    urma_cr_t cr = {};
    cr.status = URMA_CR_SUCCESS;
    cr.flag.bs.s_r = 1; /* receiver side */
    cr.opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    cr.user_ctx = SenderCRContext{OneSideSenderType::CTRL_DATA_RECEiVE, 0, fd}.value;
    cr.imm_data = WriteImmData{opcode, 0}.data;
    return cr;
}

/* 构造 sender 侧 ctrl 发送完成 CQE: seq_no 携带 buf_offset_units 或数据 seq */
static urma_cr_t MakeSenderCtrlDoneCqe(OneSideSenderType type, uint64_t seqNo, int fd)
{
    urma_cr_t cr = {};
    cr.status = URMA_CR_SUCCESS;
    cr.flag.bs.s_r = 0; /* sender side */
    cr.opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    cr.user_ctx = SenderCRContext{type, seqNo, fd}.value;
    return cr;
}

/* 给 socket 挂 dummy jetty (PostCtrlOnlyNotification 需要 jetty_->PostWriteImm) */
static void AttachDummyJetty(const UrmaSocketPtr &sock)
{
    dummy_jetty_ = static_cast<UrmaJetty *>(malloc(sizeof(UrmaJetty)));
    memset(dummy_jetty_, 0, sizeof(UrmaJetty));
    dummy_jetty_->ref_count_ = 1;
    sock->jetty_ = dummy_jetty_;
}

/* 探测包收发环境: send_buf + dummy jetty 就位, PostCtrlOnlyNotification 可真实工作 */
struct ProbeTestEnv {
    UrmaSocketPtr sock;
    static constexpr uint32_t SEND_BUF_SIZE = 8192;
};

static ProbeTestEnv MakeProbeReadySocket(int fd = TEST_FD)
{
    ProbeTestEnv env;
    env.sock = MakeRef<UrmaSocket>(fd);
    env.sock->recv_buf_addr_ = 0;
    env.sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_mock_buf);
    env.sock->send_buf_size_ = ProbeTestEnv::SEND_BUF_SIZE;
    env.sock->send_buf_ = new UrmaSockRingBuf(ProbeTestEnv::SEND_BUF_SIZE);
    env.sock->rmt_recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_mock_buf);
    AttachDummyJetty(env.sock);
    env.sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    env.sock->State(SOCK_STAT_ESTABLISHED);
    return env;
}

static void DestroyProbeEnv(ProbeTestEnv &env)
{
    env.sock->jetty_ = nullptr; /* 防 UnInitialize 走 dummy jetty Destroy */
    free(dummy_jetty_);
    dummy_jetty_ = nullptr;
    UrmaBackend::Instance().ClearPendingClose();
}

static void RegisterProbeSocket(ProbeTestEnv &env)
{
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, RefConvert<UrmaSocket, Socket>(env.sock).Get());
}

/* 回拨活跃时间模拟空闲: 线程用例中必须在 Init (线程启动) 之前完成, 依赖线程创建的 happens-before */
static void MakeSocketIdle(ProbeTestEnv &env, uint64_t idleSeconds)
{
    const uint64_t stale = ubsocket_get_timeNs() - idleSeconds * UBSOCKET_S_TO_NS;
    env.sock->alive_Info_->last_active_time_ns_ = stale;
    env.sock->alive_Info_->last_probes_sent_ns_ = stale;
}

/* 短超时轮询 (<=100ms), 满足单用例 <=1s 约束 */
static bool PollFor(const std::function<bool()> &pred, uint32_t timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        usleep(2000);
    }
    return true;
}

static UrmaAliveConf MakeConf(uint32_t time, uint32_t intvl, uint32_t probes)
{
    UrmaAliveConf conf;
    conf.keepalive_time = time;
    conf.keepalive_intvl = intvl;
    conf.keepalive_probes = probes;
    return conf;
}
} // namespace

class UrmaAliveManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();
        (void)LibcApi::Load();
        g_origShutdown = LibcApi::shutdown_ptr;
        g_postSendWrCalls.store(0);
        g_pausePost.store(false);
        g_postEntered.store(false);
        g_releasePost.store(false);
        g_destroyEntered.store(false);
        g_postRecvWrCalls.store(0);
        g_lastNotifyData.store(0);
        g_shutdownCalls.store(0);
        g_lastShutdownHow.store(-1);
        LibcApi::shutdown_ptr = MockShutdownCapture;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        UrmaApi::urma_post_jetty_recv_wr_ptr = MockPostRecvWrSuccess;
    }

    void TearDown() override
    {
        /* 先停掉 backend 单例中可能在跑的 alive 线程 (join), 再做 mock 校验,
         * 避免线程与 mock 生命周期竞争 */
        UrmaBackend::Instance().UnInitKeepAliveManager();
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        UrmaApi::urma_post_jetty_send_wr_ptr = nullptr;
        UrmaApi::urma_post_jetty_recv_wr_ptr = nullptr;
        LibcApi::shutdown_ptr = g_origShutdown; /* 恢复真实 shutdown_ptr */
        errno = 0;
    }
};

/* ==================== UrmaAliveInfo ==================== */

TEST_F(UrmaAliveManagerTest, AliveInfo_Initial_ZeroProbesRecentTimestamps)
{
    auto info = MakeRef<UrmaAliveInfo>();
    ASSERT_NE(nullptr, info.Get());
    const uint64_t now = ubsocket_get_timeNs();
    EXPECT_EQ(0u, info->GetProbesSent());
    EXPECT_GE(now, info->GetLastActiveTime());
    EXPECT_LE(now - info->GetLastActiveTime(), UBSOCKET_S_TO_NS);
    EXPECT_GE(now, info->GetProbesSendTime());
}

TEST_F(UrmaAliveManagerTest, AliveInfo_IncProbesSent_IncrementsCounterAndTime)
{
    auto info = MakeRef<UrmaAliveInfo>();
    const uint64_t before = info->GetProbesSendTime();
    info->IncProbesSent();
    info->IncProbesSent();
    EXPECT_EQ(2u, info->GetProbesSent());
    EXPECT_GE(info->GetProbesSendTime(), before);
}

TEST_F(UrmaAliveManagerTest, AliveInfo_ResetProbes_ZeroesCounter)
{
    auto info = MakeRef<UrmaAliveInfo>();
    info->IncProbesSent();
    info->IncProbesSent();
    info->IncProbesSent();
    EXPECT_EQ(3u, info->GetProbesSent());
    info->ResetProbes();
    EXPECT_EQ(0u, info->GetProbesSent());
}

TEST_F(UrmaAliveManagerTest, AliveInfo_UpdateActiveTime_AdvancesTimestamp)
{
    auto info = MakeRef<UrmaAliveInfo>();
    const uint64_t before = info->GetLastActiveTime();
    usleep(2000);
    info->UpdateActiveTime();
    EXPECT_GT(info->GetLastActiveTime(), before);
}

/* ==================== Init / UnInit / KeepAliveEnable ==================== */

TEST_F(UrmaAliveManagerTest, KeepAliveEnable_DefaultDisabled_FollowsOptions)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    EXPECT_FALSE(mgr->KeepAliveEnable()); /* keepalive_time 默认 0 */
    mgr->options.keepalive_time = 1;
    EXPECT_TRUE(mgr->KeepAliveEnable());
    mgr->options.keepalive_time = 0;
    EXPECT_FALSE(mgr->KeepAliveEnable());
}

TEST_F(UrmaAliveManagerTest, Init_TimeZero_DisabledNoThread)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    EXPECT_EQ(UBS_OK, mgr->Init(MakeConf(0, 75, 9)));
    EXPECT_FALSE(mgr->KeepAliveEnable());
    EXPECT_FALSE(mgr->mRunning.load());
    mgr->UnInit(); /* 应为安全 no-op */
    EXPECT_FALSE(mgr->mRunning.load());
}

TEST_F(UrmaAliveManagerTest, Init_EnabledConf_StartsStopsCleanly)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    EXPECT_EQ(UBS_OK, mgr->Init(MakeConf(30, 5, 3)));
    EXPECT_TRUE(mgr->KeepAliveEnable());
    EXPECT_TRUE(mgr->mRunning.load());
    mgr->UnInit();
    EXPECT_FALSE(mgr->mRunning.load());
}

TEST_F(UrmaAliveManagerTest, Init_SecondCallWhileRunning_Idempotent)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    EXPECT_EQ(UBS_OK, mgr->Init(MakeConf(30, 5, 3)));
    EXPECT_EQ(UBS_OK, mgr->Init(MakeConf(60, 10, 6))); /* running 中直接返回 */
    mgr->UnInit(); /* 只应 join 唯一一个线程, 干净退出 */
    EXPECT_FALSE(mgr->mRunning.load());
}

TEST_F(UrmaAliveManagerTest, UnInit_WithoutInit_Noop)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->UnInit();
    EXPECT_FALSE(mgr->mRunning.load());
}

TEST_F(UrmaAliveManagerTest, UnInit_Twice_Idempotent)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(30, 5, 3)));
    mgr->UnInit();
    mgr->UnInit(); /* 第二次: mRunning 已为 false, 仅 sem_post 一次 */
    EXPECT_FALSE(mgr->mRunning.load());
}

/* ==================== HandleAckPacket ==================== */

TEST_F(UrmaAliveManagerTest, HandleAck_NullSocket_Safe)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->HandleAckPacket(nullptr); /* 不应 crash */
}

TEST_F(UrmaAliveManagerTest, HandleAck_Disabled_DoesNotResetProbes)
{
    auto mgr = MakeRef<UrmaAliveManager>(); /* time=0 disabled */
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    mgr->HandleAckPacket(RefConvert<UrmaSocket, Socket>(sock).Get());
    EXPECT_EQ(2u, sock->alive_Info_->GetProbesSent()); /* 禁用时不应重置 */
}

TEST_F(UrmaAliveManagerTest, HandleAck_NotRunning_DoesNotResetProbes)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->options.keepalive_time = 30; /* enabled 但未 Init 线程 */
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    mgr->HandleAckPacket(RefConvert<UrmaSocket, Socket>(sock).Get());
    EXPECT_EQ(3u, sock->alive_Info_->GetProbesSent()); /* 线程未跑不应重置 */
}

TEST_F(UrmaAliveManagerTest, HandleAck_Running_ResetsProbes)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(30, 5, 3)));
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    mgr->HandleAckPacket(RefConvert<UrmaSocket, Socket>(sock).Get());
    EXPECT_EQ(0u, sock->alive_Info_->GetProbesSent()); /* 运行中收到 ACK 应重置 */
    mgr->UnInit();
}

/* ==================== SendResponsePacket ==================== */

TEST_F(UrmaAliveManagerTest, SendResponse_NullSocket_ReturnsError)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    EXPECT_EQ(UBS_ERROR, mgr->SendResponsePacket(nullptr));
}

TEST_F(UrmaAliveManagerTest, SendResponse_PostsKeepAliveAckImm)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    auto env = MakeProbeReadySocket();
    g_postSendWrCalls.store(0);
    EXPECT_EQ(UBS_OK, mgr->SendResponsePacket(RefConvert<UrmaSocket, Socket>(env.sock).Get()));
    EXPECT_EQ(1, g_postSendWrCalls.load());
    const WriteImmData imm{g_lastNotifyData.load()};
    EXPECT_EQ(static_cast<uint64_t>(IOOpcode::KEEP_ALIVE_ACK), imm.ubsocket.opcode);
    EXPECT_GT(env.sock->send_buf_->Used(), 0u); /* ctrl 槽占用, 由发送 CQE 释放 */
    DestroyProbeEnv(env);
}

/* ==================== SendProbePacket (私有, -fno-access-control 直调) ==================== */

TEST_F(UrmaAliveManagerTest, SendProbe_NullSocket_ReturnsError)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    EXPECT_EQ(UBS_ERROR, mgr->SendProbePacket(nullptr));
}

TEST_F(UrmaAliveManagerTest, SendProbe_IncsProbesAndPostsKeepAliveImm)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    auto env = MakeProbeReadySocket();
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    g_postSendWrCalls.store(0);
    EXPECT_EQ(UBS_OK, mgr->SendProbePacket(RefConvert<UrmaSocket, Socket>(env.sock).Get()));
    EXPECT_EQ(1, g_postSendWrCalls.load());
    EXPECT_EQ(1u, env.sock->alive_Info_->GetProbesSent()); /* post 前先自增计数 */
    const WriteImmData imm{g_lastNotifyData.load()};
    EXPECT_EQ(static_cast<uint64_t>(IOOpcode::KEEP_ALIVE), imm.ubsocket.opcode);
    DestroyProbeEnv(env);
}

TEST_F(UrmaAliveManagerTest, SendProbe_PostFail_ReturnsErrorStillCounts)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    auto env = MakeProbeReadySocket();
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrFail;
    EXPECT_EQ(UBS_ERROR, mgr->SendProbePacket(RefConvert<UrmaSocket, Socket>(env.sock).Get()));
    EXPECT_EQ(1u, env.sock->alive_Info_->GetProbesSent()); /* 计数先于 post, 失败不回退 */
    EXPECT_EQ(0u, env.sock->send_buf_->Used()); /* 失败路径应 Release ctrl 槽 */
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
    DestroyProbeEnv(env);
}

/* ==================== RunInThread (worker 线程行为) ====================
 * 时序约定: socket 构造/注册/时间回拨必须在线程 Init 之前完成,
 * 依赖 std::thread 创建自带的 happens-before 保证可见性 */

TEST_F(UrmaAliveManagerTest, RunInThread_IdleExpired_SendsProbe)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    RegisterProbeSocket(env);
    MakeSocketIdle(env, 10); /* 空闲 10s, 超过 keepalive_time=1s 与 intvl=1s */

    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(1, 1, 9)));
    ASSERT_TRUE(PollFor([&]() { return g_postSendWrCalls.load() > 0; }, 100));
    const WriteImmData imm{g_lastNotifyData.load()};
    EXPECT_EQ(static_cast<uint64_t>(IOOpcode::KEEP_ALIVE), imm.ubsocket.opcode);
    EXPECT_GT(env.sock->alive_Info_->GetProbesSent(), 0u);

    mgr->UnInit();
    DestroyProbeEnv(env);
}

TEST_F(UrmaAliveManagerTest, RunInThread_RecentActivity_NoProbe)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>(); /* 初始时间即 now, 未空闲 */
    RegisterProbeSocket(env);

    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(1, 1, 9)));
    usleep(50000); /* 5 个 worker 周期, keepalive_time=1s 内不应探测 */
    EXPECT_EQ(0, g_postSendWrCalls.load());
    EXPECT_EQ(0u, env.sock->alive_Info_->GetProbesSent());

    mgr->UnInit();
    DestroyProbeEnv(env);
}

TEST_F(UrmaAliveManagerTest, RunInThread_ProbeIntervalNotElapsed_NoRepeatProbe)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    RegisterProbeSocket(env);
    MakeSocketIdle(env, 10); /* 已空闲 */
    /* 活跃时间回拨 10s, 但探测发送时间回拨到 now-10s: keepalive_intvl=60s 未到 */
    const uint64_t staleActive = ubsocket_get_timeNs() - 10ULL * UBSOCKET_S_TO_NS;
    const uint64_t recentProbe = ubsocket_get_timeNs() - 1ULL * UBSOCKET_S_TO_NS;
    env.sock->alive_Info_->last_active_time_ns_ = staleActive;
    env.sock->alive_Info_->last_probes_sent_ns_ = recentProbe;
    env.sock->alive_Info_->IncProbesSent(); /* 历史探测计数非零 */

    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(1, 60, 9)));
    usleep(50000);
    EXPECT_EQ(0, g_postSendWrCalls.load()); /* intvl 未到, 不重复探测 */
    EXPECT_EQ(1u, env.sock->alive_Info_->GetProbesSent());

    mgr->UnInit();
    DestroyProbeEnv(env);
}

/* 保活耗尽的用户可见语义 = OnPeerReset: peer_close_→RESET + 注入 EPOLLHUP|EPOLLRDHUP + 唤醒阻塞读。
 * 该路径不置 SOCK_STAT_BROKEN、不调 shutdown(SHUT_RD) —— 那是 HandleErrorCqe 链路错误分支的语义 */
TEST_F(UrmaAliveManagerTest, RunInThread_ProbesExhausted_InjectsHupAndPeerReset)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
    sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent();
    sock->alive_Info_->IncProbesSent(); /* 预置 3 次, probes=3: 下一轮判定已耗尽 */
    const uint64_t stale = ubsocket_get_timeNs() - 10ULL * UBSOCKET_S_TO_NS;
    sock->alive_Info_->last_active_time_ns_ = stale;
    sock->alive_Info_->last_probes_sent_ns_ = stale;
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, RefConvert<UrmaSocket, Socket>(sock).Get());

    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(1, 1, 3)));
    /* EXPECT 而非 ASSERT: PollFor 偶发失败时仍须走到 UnInit, 避免 joinable 线程残留 */
    EXPECT_TRUE(PollFor([&]() {
        return (sock->GetReachEpollEvents() & (EPOLLHUP | EPOLLRDHUP)) == (EPOLLHUP | EPOLLRDHUP);
    }, 100));
    EXPECT_TRUE(sock->IsPeerReset()); /* public 谓词: peer_close_ 已推进到 RESET 终态 */
    EXPECT_EQ(0, g_shutdownCalls.load()); /* 本路径不走 shutdown(SHUT_RD) */

    mgr->UnInit();
}

TEST_F(UrmaAliveManagerTest, RunInThread_ListenSocket_Skipped)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->mIntervalMs = 10;
    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_LISTEN; /* listen socket 不探测 */
    env.sock->socket_flags.store(0);                  /* listeners have no bound peer */
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    RegisterProbeSocket(env);
    MakeSocketIdle(env, 10);

    ASSERT_EQ(UBS_OK, mgr->Init(MakeConf(1, 1, 9)));
    usleep(50000);
    EXPECT_EQ(0, g_postSendWrCalls.load());
    EXPECT_EQ(0u, env.sock->alive_Info_->GetProbesSent());

    mgr->UnInit();
    DestroyProbeEnv(env);
}

/* ==================== Backend / Socket 集成 ==================== */

TEST_F(UrmaAliveManagerTest, Backend_InitKeepAliveManager_EnabledBySetting)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 30;
    UrmaSetting::UBS_KEEPALIVE_INTVL = 5;
    UrmaSetting::UBS_KEEPALIVE_PROBES = 3;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());
    auto mgr = UrmaBackend::Instance().GetAliveManager();
    ASSERT_NE(nullptr, mgr.Get());
    EXPECT_TRUE(mgr->KeepAliveEnable());
    UrmaBackend::Instance().UnInitKeepAliveManager();
    EXPECT_EQ(nullptr, UrmaBackend::Instance().GetAliveManager().Get());
}

TEST_F(UrmaAliveManagerTest, Backend_UnInitKeepAliveManager_WithoutInit_Safe)
{
    UrmaBackend::Instance().UnInitKeepAliveManager(); /* 未 Init 时应安全 no-op */
    EXPECT_EQ(nullptr, UrmaBackend::Instance().GetAliveManager().Get());
}

TEST_F(UrmaAliveManagerTest, SocketInitialize_KeepAliveEnabled_CreatesAliveInfo)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 30;
    UrmaSetting::UBS_KEEPALIVE_INTVL = 5;
    UrmaSetting::UBS_KEEPALIVE_PROBES = 3;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());

    MOCKER_CPP(&UrmaIoBufferPool::direct_allocate).stubs().will(invoke(MockDirectAllocate));
    MOCKER_CPP(&UrmaIoBufferPool::direct_release).stubs().will(invoke(MockDirectRelease));

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_EQ(UBS_OK, sock->Initialize());
    EXPECT_NE(nullptr, sock->GetAliveInfo().Get());
    sock->UnInitialize();

    UrmaBackend::Instance().UnInitKeepAliveManager();
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, SocketInitialize_KeepAliveDisabled_NoAliveInfo)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 0; /* 关闭保活 */
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());
    ASSERT_NE(nullptr, UrmaBackend::Instance().GetAliveManager().Get());
    EXPECT_FALSE(UrmaBackend::Instance().GetAliveManager()->KeepAliveEnable());

    MOCKER_CPP(&UrmaIoBufferPool::direct_allocate).stubs().will(invoke(MockDirectAllocate));
    MOCKER_CPP(&UrmaIoBufferPool::direct_release).stubs().will(invoke(MockDirectRelease));

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_EQ(UBS_OK, sock->Initialize());
    EXPECT_EQ(nullptr, sock->GetAliveInfo().Get());
    sock->UnInitialize();

    UrmaBackend::Instance().UnInitKeepAliveManager();
    GlobalMockObject::verify();
}

/* receiver 侧收到 KEEP_ALIVE: 回 KEEP_ALIVE_ACK + 刷新活跃时间 */
TEST_F(UrmaAliveManagerTest, HandleCqe_KeepAliveImm_RespondsAckAndRefreshesActive)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 30;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());

    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    const uint64_t stale = ubsocket_get_timeNs() - 10ULL * UBSOCKET_S_TO_NS;
    env.sock->alive_Info_->last_active_time_ns_ = stale;
    RegisterProbeSocket(env);

    auto cr = MakeRecvImmCqe(IOOpcode::KEEP_ALIVE, TEST_FD);
    g_postSendWrCalls.store(0);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_EQ(1, g_postSendWrCalls.load()); /* 回了 ACK */
    const WriteImmData ackImm{g_lastNotifyData.load()};
    EXPECT_EQ(static_cast<uint64_t>(IOOpcode::KEEP_ALIVE_ACK), ackImm.ubsocket.opcode);
    EXPECT_GT(env.sock->GetAliveInfo()->GetLastActiveTime(), stale); /* receiver 侧刷新 */

    UrmaBackend::Instance().UnInitKeepAliveManager();
    DestroyProbeEnv(env);
}

/* receiver 侧收到 KEEP_ALIVE_ACK: 本端探测计数被对端 ACK 响应重置 */
TEST_F(UrmaAliveManagerTest, HandleCqe_KeepAliveAckImm_ResetsProbes)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 30;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());

    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    env.sock->alive_Info_->IncProbesSent();
    env.sock->alive_Info_->IncProbesSent();
    RegisterProbeSocket(env);

    auto cr = MakeRecvImmCqe(IOOpcode::KEEP_ALIVE_ACK, TEST_FD);
    g_postSendWrCalls.store(0);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_EQ(0, g_postSendWrCalls.load()); /* ACK 不再触发回包 */
    EXPECT_EQ(0u, env.sock->GetAliveInfo()->GetProbesSent());

    UrmaBackend::Instance().UnInitKeepAliveManager();
    DestroyProbeEnv(env);
}

/* sender 侧 KEEP_ALIVE_PROBE 发送完成 CQE: 释放 ctrl 槽 */
TEST_F(UrmaAliveManagerTest, HandleCqe_ProbeSendDone_ReleasesCtrlSlot)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    RegisterProbeSocket(env);

    /* 先真实发一个探测包占用 ctrl 槽 (imm 中的 buffer_offset 即释放载体) */
    ASSERT_EQ(UBS_OK, mgr->SendProbePacket(RefConvert<UrmaSocket, Socket>(env.sock).Get()));
    ASSERT_GT(env.sock->send_buf_->Used(), 0u);
    const WriteImmData imm{g_lastNotifyData.load()};

    auto cr = MakeSenderCtrlDoneCqe(KEEP_ALIVE_PROBE, imm.ubsocket.buffer_offset, TEST_FD);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
    EXPECT_EQ(0u, env.sock->send_buf_->Used()); /* 发送完成应释放 ctrl 槽 */

    DestroyProbeEnv(env);
}

/* sender 侧普通数据发送完成 CQE + 保活开启: 数据流也刷新活跃时间 */
TEST_F(UrmaAliveManagerTest, HandleCqe_CtrlDataRequestDone_RefreshesActiveTime)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 30;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    const uint64_t stale = ubsocket_get_timeNs() - 10ULL * UBSOCKET_S_TO_NS;
    sock->alive_Info_->last_active_time_ns_ = stale;
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, RefConvert<UrmaSocket, Socket>(sock).Get());

    auto cr = MakeSenderCtrlDoneCqe(OneSideSenderType::CTRL_DATA_REQUEST, 7, TEST_FD);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
    EXPECT_GT(sock->GetAliveInfo()->GetLastActiveTime(), stale);

    UrmaBackend::Instance().UnInitKeepAliveManager();
}

/* 保活关闭: receiver 侧 KEEP_ALIVE 仍回 ACK (协议语义), 但不刷新活跃时间 */
TEST_F(UrmaAliveManagerTest, HandleCqe_KeepAliveDisabled_RespondsButNoRefresh)
{
    UrmaSetting::UBS_KEEPALIVE_TIME = 0;
    ASSERT_EQ(UBS_OK, UrmaBackend::Instance().InitKeepAliveManager());
    ASSERT_FALSE(UrmaBackend::Instance().GetAliveManager()->KeepAliveEnable());

    auto env = MakeProbeReadySocket();
    env.sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    const uint64_t stale = ubsocket_get_timeNs() - 10ULL * UBSOCKET_S_TO_NS;
    env.sock->alive_Info_->last_active_time_ns_ = stale;
    RegisterProbeSocket(env);

    auto cr = MakeRecvImmCqe(IOOpcode::KEEP_ALIVE, TEST_FD);
    g_postSendWrCalls.store(0);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_EQ(1, g_postSendWrCalls.load()); /* ACK 仍回 */
    EXPECT_EQ(stale, env.sock->GetAliveInfo()->GetLastActiveTime()); /* 不刷新 */

    UrmaBackend::Instance().UnInitKeepAliveManager();
    DestroyProbeEnv(env);
}

/* ==================== backend 未初始化 KeepAliveManager 的空保护 ====================
 * 修复前: alive_manager_ 为空时 GetAliveManager()-> / alive_manager_-> 直接解引用 crash,
 * 影响 UrmaSocketTest.Initialize_Success_ReturnsOk 等不 init backend 的既有用例 */

/* backend 未 init manager 时 socket Initialize: 不 crash, 不挂 alive info */
TEST_F(UrmaAliveManagerTest, SocketInitialize_ManagerNotInited_NoAliveInfoNoCrash)
{
    EXPECT_EQ(nullptr, UrmaBackend::Instance().GetAliveManager().Get()); /* 确认 manager 为空 */

    MOCKER_CPP(&UrmaIoBufferPool::direct_allocate).stubs().will(invoke(MockDirectAllocate));
    MOCKER_CPP(&UrmaIoBufferPool::direct_release).stubs().will(invoke(MockDirectRelease));

    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_EQ(UBS_OK, sock->Initialize()); /* 修复前: 空指针 crash */
    EXPECT_EQ(nullptr, sock->GetAliveInfo().Get()); /* manager 为空不应挂 alive info */
    sock->UnInitialize();

    GlobalMockObject::verify();
}

// Reproduce a client published to ArraySet before its asynchronous URMA setup.
TEST_F(UrmaAliveManagerTest, CheckSocket_ConnectingWithoutJetty_SkipsProbeAndReset)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->options = MakeConf(180, 0, 1);
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->recv_buf_addr_ = 0;
    sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    sock->State(SOCK_STAT_CONNECTING);
    sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    const uint64_t scanTime = sock->alive_Info_->GetLastActiveTime() - 1;
    mgr->CheckSocket(sock.Get(), scanTime);
    EXPECT_EQ(0u, sock->alive_Info_->GetProbesSent());
    EXPECT_EQ(0, g_postSendWrCalls.load());
    EXPECT_EQ(SOCK_STAT_CONNECTING, sock->State());
    EXPECT_EQ(UBS_ERROR, mgr->SendProbePacket(sock.Get()));
    EXPECT_EQ(0u, sock->alive_Info_->GetProbesSent());
    sock->alive_Info_->IncProbesSent();
    mgr->CheckSocket(sock.Get(), scanTime);
    EXPECT_FALSE(sock->IsPeerReset());
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, CheckSocket_TimestampBoundaries_OnlyExpiredConnectionsProbe)
{
    constexpr uint64_t SCAN_TIME = 20 * UBSOCKET_S_TO_NS;
    struct TimeCase {
        uint64_t activeTime;
        uint64_t sendTime;
        uint32_t interval;
        bool expectProbe;
    };
    const TimeCase cases[] = {
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS + 1, SCAN_TIME - 2 * UBSOCKET_S_TO_NS, 2, false},
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS, SCAN_TIME - 2 * UBSOCKET_S_TO_NS, 2, true},
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS - 1, SCAN_TIME - 2 * UBSOCKET_S_TO_NS, 2, true},
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS, SCAN_TIME - 2 * UBSOCKET_S_TO_NS + 1, 2, false},
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS, SCAN_TIME - 2 * UBSOCKET_S_TO_NS - 1, 2, true},
        {SCAN_TIME + 1, SCAN_TIME - 2 * UBSOCKET_S_TO_NS, 2, false},
        {SCAN_TIME - 5 * UBSOCKET_S_TO_NS, SCAN_TIME + 1, 0, false},
        {SCAN_TIME + 1, SCAN_TIME + 1, 0, false},
    };
    for (const auto &c : cases) {
        SCOPED_TRACE(c.activeTime);
        auto mgr = MakeRef<UrmaAliveManager>();
        mgr->options = MakeConf(5, c.interval, 9);
        auto env = MakeProbeReadySocket();
        env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
        env.sock->alive_Info_->last_active_time_ns_ = c.activeTime;
        env.sock->alive_Info_->last_probes_sent_ns_ = c.sendTime;
        g_postSendWrCalls.store(0);
        mgr->CheckSocket(env.sock.Get(), SCAN_TIME);
        EXPECT_EQ(c.expectProbe ? 1 : 0, g_postSendWrCalls.load());
        EXPECT_EQ(c.expectProbe ? 1u : 0u, env.sock->alive_Info_->GetProbesSent());
        DestroyProbeEnv(env);
    }
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, PostCtrlOnlyNotification_MissingResources_ReturnsErrorWithoutAllocation)
{
    constexpr int MISSING_RESOURCE_COUNT = 3;
    for (int missing = 0; missing < MISSING_RESOURCE_COUNT; ++missing) {
        auto env = MakeProbeReadySocket();
        if (missing == 0) {
            env.sock->socket_flags.store(0);
        } else if (missing == 1) {
            env.sock->jetty_ = nullptr;
        } else {
            env.sock->send_buf_ = nullptr;
        }
        EXPECT_EQ(UBS_ERROR, env.sock->PostCtrlOnlyNotification(KEEP_ALIVE, KEEP_ALIVE_PROBE));
        EXPECT_EQ(UBS_ERROR, env.sock->PostCtrlOnlyNotification(KEEP_ALIVE_ACK, KEEP_ALIVE_PROBE));
        EXPECT_EQ(0, g_postSendWrCalls.load());
        if (env.sock->send_buf_ != nullptr) {
            EXPECT_EQ(0u, env.sock->send_buf_->Used());
        }
        DestroyProbeEnv(env);
    }
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, CheckSocket_LocallyClosed_SkipsKeepAlive)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->options = MakeConf(1, 0, 1);
    auto env = MakeProbeReadySocket();
    env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
    env.sock->OnLocalClose();
    mgr->CheckSocket(env.sock.Get(), env.sock->alive_Info_->GetLastActiveTime() + UBSOCKET_S_TO_NS);
    EXPECT_EQ(UBS_ERROR, mgr->SendResponsePacket(env.sock.Get()));
    EXPECT_EQ(0u, env.sock->alive_Info_->GetProbesSent());
    EXPECT_EQ(0, g_postSendWrCalls.load());
    DestroyProbeEnv(env);
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, PostCtrlOnlyNotification_ConcurrentCleanup_WaitsForInFlightPost)
{
    auto env = MakeProbeReadySocket();
    MOCKER_CPP(&UrmaJetty::Destroy).expects(once()).will(invoke(MockDestroyJetty));
    g_pausePost.store(true);
    ock::ubs::Result postResult = UBS_ERROR;
    std::thread sender([&]() { postResult = env.sock->PostCtrlOnlyNotification(KEEP_ALIVE, KEEP_ALIVE_PROBE); });
    EXPECT_TRUE(PollFor([]() { return g_postEntered.load(); }, 100));
    std::atomic<bool> cleanupStarted{false};
    std::thread cleanup([&]() {
        cleanupStarted.store(true);
        env.sock->DestroyUrmaResources();
    });
    EXPECT_TRUE(PollFor([&]() { return cleanupStarted.load(); }, 100));
    EXPECT_FALSE(PollFor([]() { return g_destroyEntered.load(); }, 20));
    {
        std::lock_guard<std::mutex> lock(g_postMutex);
        g_releasePost.store(true);
    }
    g_postCv.notify_all();
    sender.join();
    cleanup.join();
    EXPECT_EQ(UBS_OK, postResult);
    EXPECT_TRUE(g_destroyEntered.load());
    EXPECT_FALSE(env.sock->IsBindRemote());
    EXPECT_EQ(UBS_ERROR, env.sock->PostCtrlOnlyNotification(KEEP_ALIVE, KEEP_ALIVE_PROBE));
    EXPECT_EQ(1, g_postSendWrCalls.load());
    DestroyProbeEnv(env);
    GlobalMockObject::verify();
}

TEST_F(UrmaAliveManagerTest, CheckSocket_PeerClosedOrReset_SkipsProbeAndAck)
{
    auto mgr = MakeRef<UrmaAliveManager>();
    mgr->options = MakeConf(1, 0, 1);
    for (auto state : {PeerCloseState::CLOSED, PeerCloseState::RESET}) {
        auto env = MakeProbeReadySocket();
        env.sock->alive_Info_ = MakeRef<UrmaAliveInfo>();
        env.sock->peer_close_.store(state);
        mgr->CheckSocket(env.sock.Get(), env.sock->alive_Info_->GetLastActiveTime() + UBSOCKET_S_TO_NS);
        EXPECT_EQ(UBS_ERROR, mgr->SendResponsePacket(env.sock.Get()));
        EXPECT_EQ(0u, env.sock->alive_Info_->GetProbesSent());
        EXPECT_EQ(0, g_postSendWrCalls.load());
        DestroyProbeEnv(env);
    }
    GlobalMockObject::verify();
}
