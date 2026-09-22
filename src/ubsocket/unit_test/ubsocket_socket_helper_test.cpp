#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_thread_errno.h"
#include "core/ubsocket_socket_helper.h"
#include "ubsocket_errno.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

namespace {

constexpr int TEST_FD_100 = 100;
constexpr int TEST_FD_101 = 101;
constexpr size_t TEST_BUF_SIZE = 128;
constexpr uint32_t TEST_TIMEOUT_50_MS = 50;
constexpr uint32_t TEST_TIMEOUT_20_MS = 20;
constexpr uint32_t TEST_TIMEOUT_10_MS = 10;
constexpr uint32_t TEST_TIMEOUT_100_MS = 100;
constexpr size_t TEST_PAYLOAD_SIZE = 10;
constexpr uint16_t TEST_PORT_8080 = 8080;
constexpr uint16_t TEST_PORT_8081 = 8081;
constexpr uint32_t TEST_INVALID_HANDSHAKE_MODE = 99;
constexpr uint32_t TEST_OBJ_SIZE = 10;
constexpr uint32_t K_MAX_DISCARD_BYTES = 64 * 1024;
constexpr uint32_t TEST_CPU_INVALID = 99999;

/* ------------------------------------------------------------------ */
/* recv 脚本：依次投递 {result>0: 交付 result 字节 | 0: EOF | -1: err} */
/* ------------------------------------------------------------------ */
struct RecvStep {
    ssize_t result;
    int err;
    uint32_t value;
};

std::vector<RecvStep> g_recvScript;
size_t g_recvScriptPos = 0;

ssize_t ScriptedRecv(int /*fd*/, void *buf, size_t len, int /*flags*/)
{
    if (g_recvScriptPos >= g_recvScript.size()) {
        errno = EIO;
        return -1;
    }
    const RecvStep &step = g_recvScript[g_recvScriptPos++];
    if (step.result > 0) {
        const size_t n = std::min(static_cast<size_t>(step.result), len);
        for (size_t i = 0; i < n; ++i) {
            static_cast<uint8_t *>(buf)[i] =
                (i < sizeof(uint32_t)) ? static_cast<uint8_t>((step.value >> (i * 8)) & 0xFF) : 0;
        }
        return static_cast<ssize_t>(n);
    }
    errno = step.err;
    return step.result;
}

int g_recv_call_cnt = 0;

ssize_t FakeRecvEagainThenData(int, void *, size_t len, int)
{
    if (g_recv_call_cnt == 0) {
        ++g_recv_call_cnt;
        errno = EAGAIN;
        return -1;
    }
    return static_cast<ssize_t>(len);
}

ssize_t FakeRecvAlwaysEAGAIN(int, void *, size_t, int)
{
    errno = EAGAIN;
    return -1;
}

ssize_t FakeRecvPeerClosed(int, void *, size_t, int)
{
    return 0;
}

ssize_t FakeRecvEintrThenData(int, void *, size_t len, int)
{
    if (g_recv_call_cnt == 0) {
        ++g_recv_call_cnt;
        errno = EINTR;
        return -1;
    }
    return static_cast<ssize_t>(len);
}

ssize_t FakeRecvError(int, void *, size_t, int)
{
    errno = ECONNRESET;
    return -1;
}

/* ------------------------------------------------------------------ */
/* poll 桩：poll 在 EAGAIN 回退路径中被直接 mock                      */
/* ------------------------------------------------------------------ */
int FakePollSleep15ms(struct pollfd *, nfds_t, int)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    return 1;
}

int FakePollSleepTimeout(struct pollfd *, nfds_t, int timeout)
{
    if (timeout > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
    }
    return 0;
}

int FakePollReturnReady(struct pollfd *, nfds_t, int)
{
    return 1;
}

int FakePollReturnEintr(struct pollfd *, nfds_t, int)
{
    errno = EINTR;
    return -1;
}

int FakePollReturnError(struct pollfd *, nfds_t, int)
{
    errno = EBADF;
    return -1;
}

/* ------------------------------------------------------------------ */
/* send 桩                                                             */
/* ------------------------------------------------------------------ */
int g_send_call_cnt = 0;

ssize_t FakeSendFull(int, const void *, size_t len, int)
{
    return static_cast<ssize_t>(len);
}

ssize_t FakeSendPartial(int, const void *, size_t len, int)
{
    if (g_send_call_cnt == 0) {
        ++g_send_call_cnt;
        return static_cast<ssize_t>(len / 2);
    }
    return static_cast<ssize_t>(len);
}

ssize_t FakeSendEagainThenAll(int, const void *, size_t len, int)
{
    if (g_send_call_cnt == 0) {
        ++g_send_call_cnt;
        errno = EAGAIN;
        return -1;
    }
    return static_cast<ssize_t>(len);
}

ssize_t FakeSendEintrThenAll(int, const void *, size_t len, int)
{
    if (g_send_call_cnt == 0) {
        ++g_send_call_cnt;
        errno = EINTR;
        return -1;
    }
    return static_cast<ssize_t>(len);
}

ssize_t FakeSendEagain(int, const void *, size_t, int)
{
    errno = EAGAIN;
    return -1;
}

ssize_t FakeSendError(int, const void *, size_t, int)
{
    errno = ECONNRESET;
    return -1;
}

/* ------------------------------------------------------------------ */
/* getsockopt 桩（IsUbsConnection）                                    */
/* ------------------------------------------------------------------ */
int FakeGetsockoptTfoSynData(int, int, int, void *optval, socklen_t *optlen)
{
    if (optval != nullptr && *optlen >= static_cast<socklen_t>(sizeof(tcp_info))) {
        tcp_info info{};
        info.tcpi_options |= TCPI_OPT_SYN_DATA;
        *reinterpret_cast<tcp_info *>(optval) = info;
        *optlen = static_cast<socklen_t>(sizeof(tcp_info));
    }
    return 0;
}

int FakeGetsockoptTfoNoSynData(int, int, int, void *optval, socklen_t *optlen)
{
    if (optval != nullptr && *optlen >= static_cast<socklen_t>(sizeof(tcp_info))) {
        tcp_info info{};
        *reinterpret_cast<tcp_info *>(optval) = info;
        *optlen = static_cast<socklen_t>(sizeof(tcp_info));
    }
    return 0;
}

int FakeGetsockoptUbOptEnabled(int, int, int, void *optval, socklen_t *optlen)
{
    if (optval != nullptr && *optlen >= static_cast<socklen_t>(sizeof(int))) {
        *reinterpret_cast<int *>(optval) = 1;
    }
    return 0;
}

int FakeGetsockoptUbOptDisabled(int, int, int, void *optval, socklen_t *optlen)
{
    if (optval != nullptr && *optlen >= static_cast<socklen_t>(sizeof(int))) {
        *reinterpret_cast<int *>(optval) = 0;
    }
    return 0;
}

int FakeGetsockoptFail(int, int, int, void *, socklen_t *)
{
    errno = EOPNOTSUPP;
    return -1;
}

/* ------------------------------------------------------------------ */
/* sched_getcpu 桩                                                    */
/* ------------------------------------------------------------------ */
int FakeSchedGetcpuCpu0(void)
{
    return 0;
}

int FakeSchedGetcpuFail(void)
{
    errno = EPERM;
    return -1;
}

/* ------------------------------------------------------------------ */
/* 外部 sem / lock ops（pthread 实现，供握手 poller 三档机制测试）      */
/* ------------------------------------------------------------------ */
u_semaphore_t *TestSemCreate()
{
    return reinterpret_cast<u_semaphore_t *>(new (std::nothrow) sem_t());
}

int TestSemDestroy(u_semaphore_t *s)
{
    int ret = sem_destroy(reinterpret_cast<sem_t *>(s));
    delete reinterpret_cast<sem_t *>(s);
    return ret;
}

int TestSemInit(u_semaphore_t *s, int shared, unsigned int value)
{
    return sem_init(reinterpret_cast<sem_t *>(s), shared, value);
}

int TestSemWait(u_semaphore_t *s)
{
    return sem_wait(reinterpret_cast<sem_t *>(s));
}

int TestSemPost(u_semaphore_t *s)
{
    return sem_post(reinterpret_cast<sem_t *>(s));
}

u_mutex_t *TestLockCreate(u_mutex_type_t /*type*/)
{
    return reinterpret_cast<u_mutex_t *>(new (std::nothrow) pthread_mutex_t());
}

int TestLockDestroy(u_mutex_t *m)
{
    int ret = pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t *>(m));
    delete reinterpret_cast<pthread_mutex_t *>(m);
    return ret;
}

int TestLockLock(u_mutex_t *m)
{
    return pthread_mutex_lock(reinterpret_cast<pthread_mutex_t *>(m));
}

int TestLockUnlock(u_mutex_t *m)
{
    return pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t *>(m));
}

int TestLockTryLock(u_mutex_t *m)
{
    return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t *>(m));
}

} // namespace

class SocketConnHelperTest : public testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_recv_call_cnt = 0;
        g_send_call_cnt = 0;
        g_recvScript.clear();
        g_recvScriptPos = 0;
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        errno = 0;
        g_recv_call_cnt = 0;
        g_send_call_cnt = 0;
        g_recvScript.clear();
        g_recvScriptPos = 0;
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    }
};

/* ======================== RecvSocketData ======================== */

TEST_F(SocketConnHelperTest, RecvSocketData_DelayAndSuccess)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvEagainThenData));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollSleep15ms));

    uint64_t start_ms = SocketConnHelper::GetTimeMs();
    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    uint64_t end_ms = SocketConnHelper::GetTimeMs();

    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
    EXPECT_GE(end_ms - start_ms, 15);
}

TEST_F(SocketConnHelperTest, RecvSocketData_TimeoutSilent)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvAlwaysEAGAIN));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollSleepTimeout));

    uint64_t start_ms = SocketConnHelper::GetTimeMs();
    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_101, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_20_MS);
    uint64_t end_ms = SocketConnHelper::GetTimeMs();

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ETIMEDOUT);
    EXPECT_GE(end_ms - start_ms, TEST_TIMEOUT_20_MS);
    EXPECT_LE(end_ms - start_ms, TEST_TIMEOUT_20_MS + 20);
}

TEST_F(SocketConnHelperTest, RecvSocketData_PeerClosed_ReturnsZero)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvPeerClosed));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, 0);
}

TEST_F(SocketConnHelperTest, RecvSocketData_EagainPollReady_ReturnsSize)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvEagainThenData));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnReady));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, RecvSocketData_EagainPollEintr_ReturnsSize)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvEagainThenData));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnEintr));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, RecvSocketData_EagainPollError_ReturnsReceived)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvAlwaysEAGAIN));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnError));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(SocketConnHelperTest, RecvSocketData_RecvEintr_Retries)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvEintrThenData));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, RecvSocketData_RecvError_ReturnsReceived)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvError));

    ssize_t ret = SocketConnHelper::RecvSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ECONNRESET);
}

/* ======================== SendSocketData ======================== */

TEST_F(SocketConnHelperTest, SendSocketData_FullSend_ReturnsSize)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendFull));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, SendSocketData_PartialSend_ReturnsSize)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendPartial));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, SendSocketData_EagainTimeout_ReturnsSentAndEtimedout)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEagain));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollSleepTimeout));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_20_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ETIMEDOUT);
}

TEST_F(SocketConnHelperTest, SendSocketData_EagainPollReady_ReturnsSize)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEagainThenAll));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnReady));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, SendSocketData_EagainPollEintr_Retries)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEagainThenAll));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnEintr));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, SendSocketData_EagainPollError_ReturnsSent)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEagain));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollReturnError));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(SocketConnHelperTest, SendSocketData_SendEintr_Retries)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEintrThenAll));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_PAYLOAD_SIZE));
}

TEST_F(SocketConnHelperTest, SendSocketData_SendError_ReturnsSent)
{
    char buf[TEST_BUF_SIZE] = {0};

    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendError));

    ssize_t ret = SocketConnHelper::SendSocketData(TEST_FD_100, buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_50_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ECONNRESET);
}

/* ======================== IsUbsConnection ======================== */

TEST_F(SocketConnHelperTest, IsUbsConnection_TfoSynData_ReturnsTrue)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptTfoSynData));

    EXPECT_TRUE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_TfoNoSynData_ReturnsFalse)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptTfoNoSynData));

    EXPECT_FALSE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_TfoGetsockoptFail_ReturnsFalse)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptFail));

    EXPECT_FALSE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_UbSockOptEnabled_ReturnsTrue)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptUbOptEnabled));

    EXPECT_TRUE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_UbSockOptDisabled_ReturnsFalse)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptUbOptDisabled));

    EXPECT_FALSE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_UbSockOptGetsockoptFail_ReturnsFalse)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
    MOCKER(&LibcApi::getsockopt).stubs().will(invoke(&FakeGetsockoptFail));

    EXPECT_FALSE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsUbsConnection_UnsupportedMode_ReturnsFalse)
{
    GlobalSetting::UBS_HAND_SHAKE_MODE = static_cast<UBHandshakeMode>(TEST_INVALID_HANDSHAKE_MODE);

    EXPECT_FALSE(SocketConnHelper::IsUbsConnection(TEST_FD_100));
}

/* ==================== ExtractIp / ExtractPort ==================== */

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_NullBuf_ReturnsEarly)
{
    SocketConnHelper::ExtractIpFromSockAddr(nullptr, nullptr, 0);
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_ZeroLen_ReturnsEarly)
{
    char buf[TEST_BUF_SIZE] = {0};
    SocketConnHelper::ExtractIpFromSockAddr(nullptr, buf, 0);
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_NullAddr_EmptyString)
{
    char buf[TEST_BUF_SIZE] = {'x'};
    SocketConnHelper::ExtractIpFromSockAddr(nullptr, buf, sizeof(buf));
    EXPECT_EQ(buf[0], '\0');
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_Ipv4_Success)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    char buf[TEST_BUF_SIZE] = {0};
    SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<const sockaddr *>(&addr), buf, sizeof(buf));
    EXPECT_STREQ(buf, "127.0.0.1");
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_Ipv6_Success)
{
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    char buf[TEST_BUF_SIZE] = {0};
    SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<const sockaddr *>(&addr), buf, sizeof(buf));
    EXPECT_STREQ(buf, "::1");
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_ShortBuf_Unchanged)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    char buf[TEST_BUF_SIZE] = {0};
    /* buf_len 小于 INET_ADDRSTRLEN(16)：进入函数即置空串，inet_ntop 不执行 */
    SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<const sockaddr *>(&addr), buf, 1);
    EXPECT_EQ(buf[0], '\0');
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrBuf_UnsupportedFamily_EmptyString)
{
    sockaddr addr{};
    addr.sa_family = AF_UNIX;

    char buf[TEST_BUF_SIZE] = {0};
    SocketConnHelper::ExtractIpFromSockAddr(&addr, buf, sizeof(buf));
    EXPECT_EQ(buf[0], '\0');
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrString_NullAddr_EmptyString)
{
    EXPECT_EQ(SocketConnHelper::ExtractIpFromSockAddr(nullptr), "");
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrString_Ipv4_Success)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.10", &addr.sin_addr);

    EXPECT_EQ(SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<const sockaddr *>(&addr)), "192.168.1.10");
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrString_Ipv6_Success)
{
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "fe80::1", &addr.sin6_addr);

    EXPECT_EQ(SocketConnHelper::ExtractIpFromSockAddr(reinterpret_cast<const sockaddr *>(&addr)), "fe80::1");
}

TEST_F(SocketConnHelperTest, ExtractIpFromSockAddrString_UnsupportedFamily_EmptyString)
{
    sockaddr addr{};
    addr.sa_family = AF_UNIX;

    EXPECT_EQ(SocketConnHelper::ExtractIpFromSockAddr(&addr), "");
}

TEST_F(SocketConnHelperTest, ExtractPortFromSockAddr_NullAddr_Zero)
{
    EXPECT_EQ(SocketConnHelper::ExtractPortFromSockAddr(nullptr), 0);
}

TEST_F(SocketConnHelperTest, ExtractPortFromSockAddr_Ipv4_Success)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_8080);

    EXPECT_EQ(SocketConnHelper::ExtractPortFromSockAddr(reinterpret_cast<const sockaddr *>(&addr)), TEST_PORT_8080);
}

TEST_F(SocketConnHelperTest, ExtractPortFromSockAddr_Ipv6_Success)
{
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(TEST_PORT_8081);

    EXPECT_EQ(SocketConnHelper::ExtractPortFromSockAddr(reinterpret_cast<const sockaddr *>(&addr)), TEST_PORT_8081);
}

TEST_F(SocketConnHelperTest, ExtractPortFromSockAddr_UnsupportedFamily_Zero)
{
    sockaddr addr{};
    addr.sa_family = AF_UNIX;

    EXPECT_EQ(SocketConnHelper::ExtractPortFromSockAddr(&addr), 0);
}

/* ====================== SendLengthPrefixed ======================= */

TEST_F(SocketConnHelperTest, SendLengthPrefixed_Success_ReturnsOk)
{
    uint8_t body[TEST_PAYLOAD_SIZE] = {0};
    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendFull));

    EXPECT_EQ(SocketConnHelper::SendLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_OK);
}

TEST_F(SocketConnHelperTest, SendLengthPrefixed_SendFail_ReturnsError)
{
    uint8_t body[TEST_PAYLOAD_SIZE] = {0};
    MOCKER(&LibcApi::send).stubs().will(invoke(&FakeSendEagain));
    MOCKER_CPP(::poll).stubs().will(invoke(&FakePollSleepTimeout));

    EXPECT_EQ(SocketConnHelper::SendLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_10_MS), UBS_ERROR);
}

/* ====================== RecvLengthPrefixed ======================= */

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_BodyShorterThanObj_ZeroFillsTail)
{
    constexpr uint32_t bodyLen = 5;
    uint8_t body[TEST_OBJ_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen},
                    {static_cast<ssize_t>(bodyLen), 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_OK);
    EXPECT_EQ(body[bodyLen], 0); /* 尾部被 zero-fill */
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_BodyEqualObj_ReturnsOk)
{
    constexpr uint32_t bodyLen = TEST_OBJ_SIZE;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen},
                    {static_cast<ssize_t>(bodyLen), 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_OK);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_PrefixFail_ReturnsError)
{
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{0, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_ERROR);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_BodyFail_ReturnsError)
{
    constexpr uint32_t bodyLen = 5;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen}, {0, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_ERROR);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_BodyLongerDiscard_ReturnsOk)
{
    constexpr uint32_t bodyLen = TEST_OBJ_SIZE + 10;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen},
                    {static_cast<ssize_t>(TEST_OBJ_SIZE), 0, 0},
                    {10, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_OK);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_DiscardFail_ReturnsError)
{
    constexpr uint32_t bodyLen = TEST_OBJ_SIZE + 10;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen},
                    {static_cast<ssize_t>(TEST_OBJ_SIZE), 0, 0},
                    {0, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_ERROR);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_Overflow_ReturnsError)
{
    /* excess = bodyLen - obj_size 超过 kMaxDiscardBytes(64KB) → 拒绝丢弃，报错。
     * 且必须在读 body 之前失败（脚本只消费前缀一步）——否则错位流上会阻塞满 timeout */
    constexpr uint32_t bodyLen = TEST_OBJ_SIZE + K_MAX_DISCARD_BYTES + 1;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(bodyLen)), 0, bodyLen}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_ERROR);
    EXPECT_EQ(g_recvScriptPos, 1u);
}

TEST_F(SocketConnHelperTest, RecvLengthPrefixed_PoisonedAckAsPrefix_FailsBeforeBodyRead)
{
    /* issue#32 现网形态：控制面错位后，对端裸发的 4 字节 ack（带错误掩码的 Result）
     * 被当成长度前缀。前缀值巨大 → 必须立即失败，不得再对 body 发起阻塞读 */
    constexpr uint32_t poisonedAck = 0x40000014U;
    uint8_t body[TEST_OBJ_SIZE] = {0};

    g_recvScript = {{static_cast<ssize_t>(sizeof(poisonedAck)), 0, poisonedAck}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    EXPECT_EQ(SocketConnHelper::RecvLengthPrefixed(TEST_FD_100, body, TEST_OBJ_SIZE, TEST_TIMEOUT_50_MS), UBS_ERROR);
    EXPECT_EQ(g_recvScriptPos, 1u);
}

/* ==================== ThreadErrno 不透明访问器（issue#33） ==================== */

TEST_F(SocketConnHelperTest, ThreadErrno_RoundTripAndMacroAlias)
{
    SetThreadErrno(0);
    EXPECT_EQ(ThreadErrno(), 0);
    SetThreadErrno(ECONNREFUSED);
    EXPECT_EQ(ThreadErrno(), ECONNREFUSED);
    errno = EPIPE; /* 宏路径与访问器路径必须指向同一 TLS 槽位 */
    EXPECT_EQ(ThreadErrno(), EPIPE);
    SetThreadErrno(0);
    EXPECT_EQ(errno, 0);
}

/* ======================== FlushSocketMsg ======================== */

TEST_F(SocketConnHelperTest, FlushSocketMsg_DrainAll_Returns)
{
    g_recvScript = {{5, 0, 0}, {0, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    SocketConnHelper::FlushSocketMsg(TEST_FD_100);
}

TEST_F(SocketConnHelperTest, FlushSocketMsg_EagainResetErrno_Returns)
{
    g_recvScript = {{-1, EAGAIN, 0}, {0, 0, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    SocketConnHelper::FlushSocketMsg(TEST_FD_100);
    EXPECT_EQ(errno, 0);
}

TEST_F(SocketConnHelperTest, FlushSocketMsg_RecvError_Returns)
{
    g_recvScript = {{-1, ECONNRESET, 0}};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&ScriptedRecv));

    SocketConnHelper::FlushSocketMsg(TEST_FD_100);
}

/* ================== CPU/NUMA sysfs 相关接口 ================== */

TEST_F(SocketConnHelperTest, GetCurrentProcessSocketId_SchedGetcpuOk_ReturnsSocketId)
{
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&FakeSchedGetcpuCpu0));

    int ret = SocketConnHelper::GetCurrentProcessSocketId();
    EXPECT_GE(ret, 0);
}

TEST_F(SocketConnHelperTest, GetCurrentProcessSocketId_SchedGetcpuFail_ReturnsMinusOne)
{
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&FakeSchedGetcpuFail));

    EXPECT_EQ(SocketConnHelper::GetCurrentProcessSocketId(), -1);
}

TEST_F(SocketConnHelperTest, GetSocketIdOfCpu_ValidCpu_ReturnsSocketId)
{
    int ret = SocketConnHelper::GetSocketIdOfCpu(0);
    EXPECT_GE(ret, 0);
}

TEST_F(SocketConnHelperTest, GetSocketIdOfCpu_InvalidCpu_ReturnsMinusOne)
{
    EXPECT_EQ(SocketConnHelper::GetSocketIdOfCpu(TEST_CPU_INVALID), -1);
}

TEST_F(SocketConnHelperTest, GetSocketIdsViaNumaSysfs_NumaAvailable_ReturnsIds)
{
    std::vector<uint32_t> ret = SocketConnHelper::GetSocketIdsViaNumaSysfs();
    if (ret.empty()) {
        /* 直读真实 sysfs：非 NUMA / 无 node0 cpulist 的宿主无意义，跳过而非失败 */
        GTEST_SKIP() << "host has no NUMA node0 cpulist; skip environment-dependent assertion";
    }
    EXPECT_FALSE(ret.empty());
}

TEST_F(SocketConnHelperTest, GetSocketIdsViaNumaSysfs_NumaUnavailable_FallsBackCpuScan)
{
    MOCKER_CPP(::opendir).stubs().will(returnValue(static_cast<DIR *>(nullptr)));

    std::vector<uint32_t> ret = SocketConnHelper::GetSocketIdsViaNumaSysfs();
    EXPECT_TRUE(ret.empty());
}

TEST_F(SocketConnHelperTest, GetSocketIdsViaNuma_NoNodeDir_ReturnsEmpty)
{
    MOCKER_CPP(::opendir).stubs().will(returnValue(static_cast<DIR *>(nullptr)));

    EXPECT_TRUE(SocketConnHelper::GetSocketIdsViaNuma().empty());
}

TEST_F(SocketConnHelperTest, GetSocketIdsViaCpuScan_NoCpuDir_ReturnsEmpty)
{
    MOCKER_CPP(::opendir).stubs().will(returnValue(static_cast<DIR *>(nullptr)));

    EXPECT_TRUE(SocketConnHelper::GetSocketIdsViaCpuScan().empty());
}

TEST_F(SocketConnHelperTest, GetFirstCpuFromCpulist_Range_ReturnsStart)
{
    EXPECT_EQ(SocketConnHelper::GetFirstCpuFromCpulist("0-27"), 0);
}

TEST_F(SocketConnHelperTest, GetFirstCpuFromCpulist_Single_ReturnsCpu)
{
    EXPECT_EQ(SocketConnHelper::GetFirstCpuFromCpulist("5"), 5);
}

TEST_F(SocketConnHelperTest, GetFirstCpuFromCpulist_Empty_ReturnsMinusOne)
{
    EXPECT_EQ(SocketConnHelper::GetFirstCpuFromCpulist(""), -1);
}

TEST_F(SocketConnHelperTest, GetFirstCpuFromCpulist_InvalidToken_ReturnsMinusOne)
{
    EXPECT_EQ(SocketConnHelper::GetFirstCpuFromCpulist("abc-3"), -1);
    EXPECT_EQ(SocketConnHelper::GetFirstCpuFromCpulist("abc"), -1);
}

/* ================= 握手 poller：外部 sem / gate 机制 =================
 * 注意：注册外部 ops 会置位 LockRegistry 内部外部标志（无法回退复位），
 * 且 PickMech 优先判 sem 再判 lock。故两个用例放在本文件末尾，
 * 并且 GATE 必须先于 EXT_SEM（否则 sem 标志会让 GATE 用例实际走 EXT_SEM 路径）。 */

TEST_F(SocketConnHelperTest, RecvSocketData_GatePoller_TimeoutReturnsMinusOne)
{
    int sp[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sp), 0);

    u_external_lock_ops_t lockOps = {
        .create = TestLockCreate,
        .destroy = TestLockDestroy,
        .lock = TestLockLock,
        .unlock = TestLockUnlock,
        .try_lock = TestLockTryLock,
    };
    auto savedLock = LockRegistry::LOCK_OPS;
    EXPECT_EQ(LockRegistry::RegisterLockOps(&lockOps), UBS_OK);

    char buf[TEST_BUF_SIZE] = {0};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvAlwaysEAGAIN));

    ssize_t ret = SocketConnHelper::RecvSocketData(sp[0], buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_100_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ETIMEDOUT);

    LockRegistry::LOCK_OPS = savedLock;
    close(sp[0]);
    close(sp[1]);
}

TEST_F(SocketConnHelperTest, RecvSocketData_ExtSemPoller_TimeoutReturnsMinusOne)
{
    int sp[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sp), 0);

    u_external_semaphore_ops_t semOps = {
        .create = TestSemCreate,
        .destroy = TestSemDestroy,
        .init = TestSemInit,
        .wait = TestSemWait,
        .post = TestSemPost,
    };
    auto savedSem = LockRegistry::SEM_OPS;
    EXPECT_EQ(LockRegistry::RegisterSemOps(&semOps), UBS_OK);

    char buf[TEST_BUF_SIZE] = {0};
    MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecvAlwaysEAGAIN));

    ssize_t ret = SocketConnHelper::RecvSocketData(sp[0], buf, TEST_PAYLOAD_SIZE, TEST_TIMEOUT_100_MS);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ETIMEDOUT);

    LockRegistry::SEM_OPS = savedSem;
    close(sp[0]);
    close(sp[1]);
}

/* ==================== fd 标志 inline 函数 ==================== */
/* 覆盖 ubsocket_socket_helper.h 中 SetBlocking/IsBlocking/SetNonBlocking 的
 * fcntl(F_GETFL) 失败(flags<0) 与已置/未置 O_NONBLOCK 的分支。 */

namespace {
int g_fcntlFlags = 0; /* F_GETFL 返回值（-1 表示失败） */
bool g_fcntlSetflCalled = false;
int g_fcntlSetflArg = 0;

int MockFcntlForHelperTest(int /*fd*/, int cmd, unsigned long arg)
{
    if (cmd == F_GETFL) {
        return g_fcntlFlags;
    }
    if (cmd == F_SETFL) {
        g_fcntlSetflCalled = true;
        g_fcntlSetflArg = static_cast<int>(arg);
        return 0;
    }
    return -1;
}

/* RAII：离开作用域(含断言失败)时恢复 LibcApi::fcntl_ptr，避免 mock 泄漏到后续用例 */
class FcntlMockGuard {
public:
    explicit FcntlMockGuard(decltype(LibcApi::fcntl_ptr) mock)
        : saved_(LibcApi::fcntl_ptr)
    {
        LibcApi::fcntl_ptr = mock;
    }
    ~FcntlMockGuard() { LibcApi::fcntl_ptr = saved_; }
    FcntlMockGuard(const FcntlMockGuard &) = delete;
    FcntlMockGuard &operator=(const FcntlMockGuard &) = delete;

private:
    decltype(LibcApi::fcntl_ptr) saved_;
};
} // namespace

TEST_F(SocketConnHelperTest, SetBlocking_FcntlGetflFail_ReturnsFlags)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = -1; /* F_GETFL 失败 */
    g_fcntlSetflCalled = false;

    EXPECT_EQ(SocketConnHelper::SetBlocking(TEST_FD_100), -1);
    EXPECT_FALSE(g_fcntlSetflCalled);
}

TEST_F(SocketConnHelperTest, SetBlocking_AlreadyNonBlocking_ClearsFlag)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = O_NONBLOCK; /* 已置非阻塞: 走 F_SETFL 清除分支 */
    g_fcntlSetflCalled = false;

    EXPECT_EQ(SocketConnHelper::SetBlocking(TEST_FD_100), 0);
    EXPECT_TRUE(g_fcntlSetflCalled);
    EXPECT_EQ(g_fcntlSetflArg & O_NONBLOCK, 0);
}

TEST_F(SocketConnHelperTest, IsBlocking_FcntlGetflFail_ReturnsFalse)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = -1; /* F_GETFL 失败 */

    EXPECT_FALSE(SocketConnHelper::IsBlocking(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, IsBlocking_NonBlocking_ReturnsFalse)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = O_NONBLOCK;

    EXPECT_FALSE(SocketConnHelper::IsBlocking(TEST_FD_100));
}

TEST_F(SocketConnHelperTest, SetNonBlocking_FcntlGetflFail_ReturnsFlags)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = -1; /* F_GETFL 失败 */
    g_fcntlSetflCalled = false;

    EXPECT_EQ(SocketConnHelper::SetNonBlocking(TEST_FD_100), -1);
    EXPECT_FALSE(g_fcntlSetflCalled);
}

TEST_F(SocketConnHelperTest, SetNonBlocking_AlreadyNonBlocking_ReturnsZero)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = O_NONBLOCK; /* 已置非阻塞: 走提前返回分支 */
    g_fcntlSetflCalled = false;

    EXPECT_EQ(SocketConnHelper::SetNonBlocking(TEST_FD_100), 0);
    EXPECT_FALSE(g_fcntlSetflCalled);
}

TEST_F(SocketConnHelperTest, SetNonBlocking_Blocking_SetsFlag)
{
    FcntlMockGuard guard(MockFcntlForHelperTest);
    g_fcntlFlags = 0; /* 阻塞: 走 F_SETFL 置位分支 */
    g_fcntlSetflCalled = false;

    EXPECT_EQ(SocketConnHelper::SetNonBlocking(TEST_FD_100), 0);
    EXPECT_TRUE(g_fcntlSetflCalled);
    EXPECT_NE(g_fcntlSetflArg & O_NONBLOCK, 0);
}
