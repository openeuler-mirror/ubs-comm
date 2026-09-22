/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <new>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>

/* snprintf is variadic — mockcpp cannot intercept it.  Use a preprocessor
 * macro so the inline ConnInfo::SetPeerIp (compiled from the header below)
 * calls our wrapper, which can be told to emulate failure. */
static bool g_snprintf_fail = false;
static int test_snprintf(char *str, size_t size, const char *format, ...)
{
    if (g_snprintf_fail) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

#define snprintf test_snprintf
#include "core/ubsocket_core_types.h"
#undef snprintf

using namespace ock::ubs;

/* ====== operator new(std::nothrow_t&) override ======
 * Used to cover defensive branches in Socket::EnsureExt
 * that require allocation failure or CAS race.  Mode 0 = normal (pass-through
 * to malloc), mode 1 = always return nullptr, mode 2 = inject a pre-allocated
 * SocketExt into the target atomic during the allocation to force CAS failure.
 */
static int g_newNoThrowMode = 0;
static std::atomic<SocketExt *> *g_casRaceTarget = nullptr;
static SocketExt *g_casRaceExt = nullptr;

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    if (g_newNoThrowMode == 1) {
        return nullptr;
    }
    void *p = malloc(size);
    if (g_newNoThrowMode == 2 && p != nullptr && g_casRaceTarget != nullptr && g_casRaceExt != nullptr) {
        g_casRaceTarget->store(g_casRaceExt, std::memory_order_release);
    }
    return p;
}

class CoreTypesSocketStateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CoreTypesSocketStateTest, SocketStateValid_AllDefinedStates)
{
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_INIT));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_RAW_ESTABLISHED));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_ESTABLISHED));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_SHUTDOWN));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_CLOSE));
}

TEST_F(CoreTypesSocketStateTest, SocketStateValid_OutOfRange)
{
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(SOCK_STATE_COUNT)));
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(SOCK_STATE_COUNT + 1)));
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(255)));
}

TEST_F(CoreTypesSocketStateTest, SocketStateToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_INIT), "init");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_RAW_ESTABLISHED), "raw_socket_established");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_ESTABLISHED), "established");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_SHUTDOWN), "shutdown");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_CLOSE), "closed");
}

TEST_F(CoreTypesSocketStateTest, SocketStateToStr_OutOfRangeReturnsUnknown)
{
    EXPECT_EQ(SocketStateToStr(static_cast<SocketState>(SOCK_STATE_COUNT)), "unknown");
    EXPECT_EQ(SocketStateToStr(static_cast<SocketState>(SOCK_STATE_COUNT + 1)), "unknown");
}

// ==================== SocketType ====================

class CoreTypesSocketTypeTest : public ::testing::Test {
};

TEST_F(CoreTypesSocketTypeTest, SocketTypeValid_AllDefinedTypes)
{
    EXPECT_TRUE(SocketTypeValid(SocketType::SOCK_TYPE_TCP));
    EXPECT_TRUE(SocketTypeValid(SocketType::SOCK_TYPE_UMQ));
}

TEST_F(CoreTypesSocketTypeTest, SocketTypeValid_OutOfRange)
{
    EXPECT_FALSE(SocketTypeValid(SocketType::SOCK_TYPE_COUNT));
}

TEST_F(CoreTypesSocketTypeTest, SocketTypeToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketTypeToStr(SocketType::SOCK_TYPE_TCP), "TCP");
    EXPECT_EQ(SocketTypeToStr(SocketType::SOCK_TYPE_UMQ), "UMQ");
}

TEST_F(CoreTypesSocketTypeTest, SocketTypeToStr_OutOfRangeReturnsUnknown)
{
    EXPECT_EQ(SocketTypeToStr(SocketType::SOCK_TYPE_COUNT), "");
    EXPECT_EQ(SocketTypeToStr(static_cast<SocketType>(255)), "");
}

// ==================== SocketCreateType ====================

class CoreTypesCreateTypeTest : public ::testing::Test {
};

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeValid_AllDefined)
{
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_UNKNOWN));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_LISTEN));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_CONNECT));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_ACCEPT));
}

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeValid_OutOfRange)
{
    EXPECT_FALSE(SocketCreateTypeValid(static_cast<SocketCreateType>(SOCK_CREATE_TYPE_COUNT)));
}

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_UNKNOWN), "unknown");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_LISTEN), "listen");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_CONNECT), "client");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_ACCEPT), "accept");
}

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeToStr_OutOfRangeReturnsUnknown)
{
    EXPECT_EQ(SocketCreateTypeToStr(static_cast<SocketCreateType>(SOCK_CREATE_TYPE_COUNT)), "unknown");
    EXPECT_EQ(SocketCreateTypeToStr(static_cast<SocketCreateType>(255)), "unknown");
}

// ==================== Socket class methods ====================

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_data_rx.h"
#include "core/ubsocket_data_tx.h"

class TestSocketBase : public SocketBase {
public:
    TestSocketBase(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_TCP) {}
    int GetTxFd() override { return raw_socket_; }
    bool IsBindRemote() override { return false; }
    ock::ubs::Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override { return UBS_OK; }
    ock::ubs::Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override { return UBS_OK; }
    bool ShouldRegisterTxEvent() override { return false; }
    ock::ubs::Result ProcessEpollEvent(struct epoll_event &event) override { return UBS_OK; }
    ock::ubs::Result Initialize() noexcept override { return UBS_OK; }
    void UnInitialize() noexcept override {}
};

class SocketMethodsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        savedTraceEnabled_ = GlobalSetting::UBS_MONITOR_ENABLE;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
    }
    void TearDown() override
    {
        GlobalSetting::UBS_MONITOR_ENABLE = savedTraceEnabled_;
        g_newNoThrowMode = 0;
        g_casRaceTarget = nullptr;
        g_casRaceExt = nullptr;
        GlobalMockObject::verify();
    }
private:
    bool savedTraceEnabled_;
};

// --- SetPeerIp with valid IP ---
TEST_F(SocketMethodsTest, SetPeerIp_ValidIp)
{
    RawConnInfoV4 conn;
    conn.SetPeerIp("192.168.1.1");
    char buf[INET6_ADDRSTRLEN];
    EXPECT_STREQ(conn.GetPeerIpStr(buf, sizeof(buf)), "192.168.1.1");
}

// --- SetPeerIp with null ---
TEST_F(SocketMethodsTest, SetPeerIp_NullIp)
{
    RawConnInfoV4 conn;
    conn.SetPeerIp(nullptr);
    char buf[INET6_ADDRSTRLEN];
    EXPECT_STREQ(conn.GetPeerIpStr(buf, sizeof(buf)), "");
}

// --- SetPeerIp with empty string ---
TEST_F(SocketMethodsTest, SetPeerIp_EmptyString)
{
    RawConnInfoV4 conn;
    conn.SetPeerIp("");
    char buf[INET6_ADDRSTRLEN];
    EXPECT_STREQ(conn.GetPeerIpStr(buf, sizeof(buf)), "");
}

// --- ConnInfo SetPeerIp ---
TEST_F(SocketMethodsTest, ConnInfo_SetPeerIp_Valid)
{
    ConnInfo conn;
    conn.SetPeerIp("10.0.0.1");
    EXPECT_STREQ(conn.peer_ip, "10.0.0.1");
}

TEST_F(SocketMethodsTest, ConnInfo_SetPeerIp_Null)
{
    ConnInfo conn;
    conn.SetPeerIp(nullptr);
    EXPECT_STREQ(conn.peer_ip, "");
}

// --- Socket State/Type/Fd/IsClient ---
TEST_F(SocketMethodsTest, Socket_State_GetSet)
{
    TestSocketBase sock(42);
    EXPECT_EQ(sock.State(), SOCK_STAT_INIT);
    sock.State(SOCK_STAT_RAW_ESTABLISHED);
    EXPECT_EQ(sock.State(), SOCK_STAT_RAW_ESTABLISHED);
    sock.State(SOCK_STAT_CLOSE);
    EXPECT_EQ(sock.State(), SOCK_STAT_CLOSE);
}

TEST_F(SocketMethodsTest, Socket_Type_Fd)
{
    TestSocketBase sock(99);
    EXPECT_EQ(sock.Type(), SocketType::SOCK_TYPE_TCP);
    EXPECT_EQ(sock.Fd(), 99);
}

TEST_F(SocketMethodsTest, Socket_IsClient_DefaultFalse)
{
    TestSocketBase sock(1);
    EXPECT_FALSE(sock.IsClient());
    auto *ci = sock.MutableConnInfo();
    ASSERT_NE(ci, nullptr);
    ci->type_fd = 1;
    EXPECT_TRUE(sock.IsClient());
}

TEST_F(SocketMethodsTest, Socket_IsClient_SetTrue)
{
    TestSocketBase sock(1);
    auto *ci = sock.MutableConnInfo();
    ASSERT_NE(ci, nullptr);
    ci->type_fd = 1;
    EXPECT_TRUE(sock.IsClient());
}

// ==================== RawConnInfoV4 additional tests ====================

TEST_F(SocketMethodsTest, RawConnInfoV4_SetPeerIp_IPv6)
{
    RawConnInfoV4 conn;
    conn.SetPeerIp("::1");
    char buf[INET6_ADDRSTRLEN];
    EXPECT_STREQ(conn.GetPeerIpStr(buf, sizeof(buf)), "::1");
}

TEST_F(SocketMethodsTest, RawConnInfoV4_GetPeerIpStr_ZeroLen)
{
    RawConnInfoV4 conn;
    conn.SetPeerIp("192.168.1.1");
    char buf[INET6_ADDRSTRLEN] = "x";
    const char *result = conn.GetPeerIpStr(buf, 0);
    EXPECT_EQ(result, buf);
}

TEST_F(SocketMethodsTest, RawConnInfoV4_SetCreateTimeNow)
{
    RawConnInfoV4 conn;
    EXPECT_EQ(conn.create_time_sec, 0u);
    conn.SetCreateTimeNow();
    EXPECT_GT(conn.create_time_sec, 0u);
}

// ==================== ConnInfo snprintf failure ====================

TEST_F(SocketMethodsTest, ConnInfo_SetPeerIp_SnprintfFail)
{
    g_snprintf_fail = true;
    ConnInfo conn;
    conn.SetPeerIp("10.0.0.1");
    EXPECT_STREQ(conn.peer_ip, "");
    g_snprintf_fail = false;
}

// ==================== Socket::EnsureExt paths ====================

TEST_F(SocketMethodsTest, Socket_EnsureExt_AlreadyExists)
{
    TestSocketBase sock(1);
    SocketExt *ext1 = sock.EnsureExt();
    ASSERT_NE(ext1, nullptr);
    SocketExt *ext2 = sock.EnsureExt();
    EXPECT_EQ(ext2, ext1);
}

TEST_F(SocketMethodsTest, Socket_EnsureExt_AllocFail_ReturnsNull)
{
    g_newNoThrowMode = 1;
    TestSocketBase sock(1);
    SocketExt *ext = sock.EnsureExt();
    EXPECT_EQ(ext, nullptr);
    g_newNoThrowMode = 0;
}

TEST_F(SocketMethodsTest, Socket_EnsureExt_CasFail_ReturnsExisting)
{
    TestSocketBase sock(1);
    auto *raceExt = new SocketExt();
    g_casRaceExt = raceExt;
    g_casRaceTarget = &sock.ext_;
    g_newNoThrowMode = 2;
    SocketExt *result = sock.EnsureExt();
    g_newNoThrowMode = 0;
    g_casRaceTarget = nullptr;
    g_casRaceExt = nullptr;
    EXPECT_EQ(result, raceExt);
}

// ==================== Socket::ConnInfoOrNull ====================

TEST_F(SocketMethodsTest, Socket_ConnInfoOrNull_WithExt)
{
    TestSocketBase sock(1);
    sock.MutableConnInfo();
    const RawConnInfoV4 *ci = sock.ConnInfoOrNull();
    ASSERT_NE(ci, nullptr);
}

TEST_F(SocketMethodsTest, Socket_ConnInfoOrNull_NoExt)
{
    TestSocketBase sock(1);
    const RawConnInfoV4 *ci = sock.ConnInfoOrNull();
    EXPECT_EQ(ci, nullptr);
}

// ==================== Socket::CreateTimeSec ====================

TEST_F(SocketMethodsTest, Socket_CreateTimeSec_WithExt)
{
    TestSocketBase sock(1);
    auto *ci = sock.MutableConnInfo();
    ASSERT_NE(ci, nullptr);
    ci->create_time_sec = 12345u;
    EXPECT_EQ(sock.CreateTimeSec(), 12345u);
}

TEST_F(SocketMethodsTest, Socket_CreateTimeSec_NoExt)
{
    TestSocketBase sock(1);
    EXPECT_EQ(sock.CreateTimeSec(), 0u);
}

// ==================== Socket::PeerIpStr ====================

TEST_F(SocketMethodsTest, Socket_PeerIpStr_WithExt)
{
    TestSocketBase sock(1);
    auto *ci = sock.MutableConnInfo();
    ASSERT_NE(ci, nullptr);
    ci->SetPeerIp("10.0.0.42");
    char buf[INET6_ADDRSTRLEN];
    EXPECT_STREQ(sock.PeerIpStr(buf, sizeof(buf)), "10.0.0.42");
}

TEST_F(SocketMethodsTest, Socket_PeerIpStr_NoExt_NonZeroLen)
{
    TestSocketBase sock(1);
    char buf[INET6_ADDRSTRLEN] = "x";
    EXPECT_STREQ(sock.PeerIpStr(buf, sizeof(buf)), "");
}

TEST_F(SocketMethodsTest, Socket_PeerIpStr_NoExt_ZeroLen)
{
    TestSocketBase sock(1);
    char buf[INET6_ADDRSTRLEN] = "x";
    const char *result = sock.PeerIpStr(buf, 0);
    EXPECT_EQ(result, buf);
}
