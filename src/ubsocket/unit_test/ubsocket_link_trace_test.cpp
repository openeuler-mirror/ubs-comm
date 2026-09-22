/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include <mockcpp/mockcpp.hpp>

#include <netinet/tcp.h>
#include "common/ubsocket_link_trace.h"
#include "common/ubsocket_logger.h"

using namespace ock::ubs;

// ==================== Noinline helpers ====================

__attribute__((noinline)) static bool CallEnabled()
{
    return LinkTrace::Enabled();
}

__attribute__((noinline)) static uint64_t CallNowUs()
{
    return LinkTrace::NowUs();
}

__attribute__((noinline)) static long CallTid()
{
    return LinkTrace::Tid();
}

__attribute__((noinline)) static long CallAcceptQueueWaitMs(int fd)
{
    return LinkTrace::AcceptQueueWaitMs(fd);
}

__attribute__((noinline)) static long CallPendingBytes(int fd)
{
    return LinkTrace::PendingBytes(fd);
}

static int MockGetsockoptSuccess(int, int, int, void *optval, socklen_t *optlen)
{
    auto *info = static_cast<tcp_info *>(optval);
    std::memset(info, 0, sizeof(*info));
    info->tcpi_last_ack_recv = 7;
    *optlen = sizeof(*info);
    return 0;
}

__attribute__((noinline)) static void CallLinkTraceMacro(int fd, const char *stage)
{
    UBS_LINK_TRACE(fd, stage, "val=%d", 42);
}

// ==================== LinkTrace Tests ====================

class LinkTraceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Reset the cached state so Enabled() re-checks the env var
        LinkTrace::state_.store(0, std::memory_order_relaxed);
        LinkTrace::base_us_.store(0, std::memory_order_relaxed);
        unsetenv("UBSOCKET_LINK_TRACE");
    }

    void TearDown() override
    {
        LinkTrace::state_.store(0, std::memory_order_relaxed);
        LinkTrace::base_us_.store(0, std::memory_order_relaxed);
        unsetenv("UBSOCKET_LINK_TRACE");
    }
};

TEST_F(LinkTraceTest, Enabled_EnvNotSet_ReturnsFalse)
{
    EXPECT_FALSE(CallEnabled());
}

TEST_F(LinkTraceTest, Enabled_EnvSetTo1_ReturnsTrue)
{
    setenv("UBSOCKET_LINK_TRACE", "1", 1);
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    EXPECT_TRUE(CallEnabled());
}

TEST_F(LinkTraceTest, Enabled_EnvSetToOther_ReturnsFalse)
{
    setenv("UBSOCKET_LINK_TRACE", "0", 1);
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    EXPECT_FALSE(CallEnabled());

    setenv("UBSOCKET_LINK_TRACE", "true", 1);
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    EXPECT_FALSE(CallEnabled());
}

TEST_F(LinkTraceTest, Enabled_CachedAfterFirstCheck)
{
    setenv("UBSOCKET_LINK_TRACE", "1", 1);
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    EXPECT_TRUE(CallEnabled());
    unsetenv("UBSOCKET_LINK_TRACE");
    EXPECT_TRUE(CallEnabled());
}

TEST_F(LinkTraceTest, NowUs_ReturnsValidValue)
{
    uint64_t t1 = CallNowUs();
    EXPECT_EQ(t1, 0u);
    uint64_t t2 = CallNowUs();
    EXPECT_GE(t2, t1);
    LinkTrace::base_us_.store(0, std::memory_order_relaxed);
    uint64_t t3 = CallNowUs();
    EXPECT_GE(t3, 0u);
}

TEST_F(LinkTraceTest, Tid_ReturnsValidTid)
{
    long tid = CallTid();
    EXPECT_GT(tid, 0L);
}

TEST_F(LinkTraceTest, AcceptQueueWaitMs_InvalidFd_ReturnsMinus1)
{
    EXPECT_EQ(CallAcceptQueueWaitMs(-1), -1L);
    EXPECT_EQ(CallAcceptQueueWaitMs(999999), -1L);
}

TEST_F(LinkTraceTest, AcceptQueueWaitMs_ValidTcpSocket)
{
    MOCKER_CPP(::getsockopt).stubs().will(invoke(MockGetsockoptSuccess));
    EXPECT_EQ(CallAcceptQueueWaitMs(42), 7L);
    GlobalMockObject::verify();
}

TEST_F(LinkTraceTest, PendingBytes_InvalidFd_ReturnsMinus1)
{
    EXPECT_EQ(CallPendingBytes(-1), -1L);
    EXPECT_EQ(CallPendingBytes(999999), -1L);
}

TEST_F(LinkTraceTest, PendingBytes_ValidSocket_NoData)
{
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    EXPECT_EQ(CallPendingBytes(pipefd[0]), 0L);
    close(pipefd[0]);
    close(pipefd[1]);
}

TEST_F(LinkTraceTest, PendingBytes_ValidSocketPipe_WithData)
{
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    ASSERT_EQ(write(pipefd[1], "hello", 5), 5);
    EXPECT_EQ(CallPendingBytes(pipefd[0]), 5L);
    close(pipefd[0]);
    close(pipefd[1]);
}

TEST_F(LinkTraceTest, UBS_LINK_TRACE_Macro_Disabled)
{
    unsetenv("UBSOCKET_LINK_TRACE");
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    CallLinkTraceMacro(0, "TEST_DISABLED");
}

TEST_F(LinkTraceTest, UBS_LINK_TRACE_Macro_Enabled)
{
    setenv("UBSOCKET_LINK_TRACE", "1", 1);
    LinkTrace::state_.store(0, std::memory_order_relaxed);
    CallLinkTraceMacro(42, "TEST_ENABLED");
}
