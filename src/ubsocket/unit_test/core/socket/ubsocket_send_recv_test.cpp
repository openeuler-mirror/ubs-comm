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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_socket.h"

using namespace ock::ubs;

namespace {
/* fake fd: the managed cases never touch the kernel, the raw cases use their own socketpair fd */
static const int TEST_FD = 4096;

/* records the internal flags handed to the transport, and answers with a fixed length */
class FakeTxOps : public DataTxOps {
public:
    ssize_t WriteV(const SocketPtr &, const struct iovec *iov, int, uint32_t flags) noexcept override
    {
        lastFlags = flags;
        return static_cast<ssize_t>(iov->iov_len);
    }
    Result Flush(const SocketPtr &, uint32_t) noexcept override
    {
        return UBS_OK;
    }
    Result TriggerPollOut(const SocketPtr &, uint32_t) noexcept override
    {
        return UBS_OK;
    }

    uint32_t lastFlags = 0;
};

/* answers with at most chunkSize bytes per call, so the MSG_WAITALL loop can be observed */
class FakeRxOps : public DataRxOps {
public:
    explicit FakeRxOps(size_t chunkSize) : chunkSize_(chunkSize) {}

    ssize_t ReadV(const SocketPtr &, const struct iovec *iov, int, uint32_t flags) override
    {
        lastFlags = flags;
        ++calls;
        const size_t size = iov->iov_len < chunkSize_ ? iov->iov_len : chunkSize_;
        memset(iov->iov_base, 'r', size);
        return static_cast<ssize_t>(size);
    }
    Result TriggerPollIn(const SocketPtr &, uint32_t) override
    {
        return UBS_OK;
    }

    uint32_t lastFlags = 0;
    uint32_t calls = 0;

private:
    size_t chunkSize_;
};

/* SocketBase is abstract; this double only plugs the pure virtuals */
class TestSocket : public SocketBase {
public:
    TestSocket(int fd, SocketType type) : SocketBase(fd, type) {}
    ~TestSocket() override = default;

    Result Initialize() noexcept override
    {
        return UBS_OK;
    }
    void UnInitialize() noexcept override {}
    bool IsBindRemote() override
    {
        return false;
    }
};
} // namespace

class UbsocketSendRecvTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
    }
};

/* managed send: flags outside MSG_DONTWAIT|MSG_NOSIGNAL must fail instead of being ignored */
TEST_F(UbsocketSendRecvTest, ManagedSendRejectsUnsupportedFlags)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);

    char buf[16] = {};
    const int unsupportedFlags[] = {MSG_PEEK, MSG_MORE, MSG_OOB, MSG_TRUNC, MSG_EOR, MSG_ZEROCOPY, 0x40000000};
    for (int flags : unsupportedFlags) {
        errno = 0;
        EXPECT_EQ(-1, raw->Send(sock, buf, sizeof(buf), flags)) << "flags=" << flags;
        EXPECT_EQ(EINVAL, errno) << "flags=" << flags;
    }
}

/* managed send: supported bits are accepted, they reach the data layer (no ops attached -> ENOTCONN) */
TEST_F(UbsocketSendRecvTest, ManagedSendAcceptsSupportedFlags)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);

    char buf[16] = {};
    const int supportedFlags[] = {0, MSG_DONTWAIT, MSG_NOSIGNAL, MSG_DONTWAIT | MSG_NOSIGNAL};
    for (int flags : supportedFlags) {
        errno = 0;
        EXPECT_EQ(-1, raw->Send(sock, buf, sizeof(buf), flags)) << "flags=" << flags;
        EXPECT_EQ(ENOTCONN, errno) << "flags=" << flags;
    }
}

/* managed recv: flags outside MSG_DONTWAIT|MSG_WAITALL (MSG_NOSIGNAL is a tolerated send-side no-op) */
TEST_F(UbsocketSendRecvTest, ManagedRecvRejectsUnsupportedFlags)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);

    char buf[16] = {};
    const int unsupportedFlags[] = {MSG_PEEK, MSG_OOB, MSG_TRUNC, MSG_ERRQUEUE, MSG_EOR, 0x40000000};
    for (int flags : unsupportedFlags) {
        errno = 0;
        EXPECT_EQ(-1, raw->Recv(sock, buf, sizeof(buf), flags)) << "flags=" << flags;
        EXPECT_EQ(EINVAL, errno) << "flags=" << flags;
    }
}

/* managed recv: supported bits are accepted, they reach the data layer (no ops attached -> ENOTCONN) */
TEST_F(UbsocketSendRecvTest, ManagedRecvAcceptsSupportedFlags)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);

    char buf[16] = {};
    const int supportedFlags[] = {0, MSG_DONTWAIT, MSG_WAITALL, MSG_NOSIGNAL, MSG_DONTWAIT | MSG_WAITALL};
    for (int flags : supportedFlags) {
        errno = 0;
        EXPECT_EQ(-1, raw->Recv(sock, buf, sizeof(buf), flags)) << "flags=" << flags;
        EXPECT_EQ(ENOTCONN, errno) << "flags=" << flags;
    }
}

/*
 * Degraded socket (SOCK_STAT_RAW_ESTABLISHED): flags must reach the kernel instead of being dropped.
 * Every recv below carries MSG_DONTWAIT so that a regression fails fast instead of blocking the UT.
 */
TEST_F(UbsocketSendRecvTest, DegradedSocketForwardsFlagsToKernel)
{
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    /* keep the test-side end non-blocking: a silently failed send must fail the UT, not hang it */
    ASSERT_NE(-1, ::fcntl(fds[1], F_SETFL, O_NONBLOCK));

    auto raw = MakeRef<TestSocket>(fds[0], SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_RAW_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);

    /* MSG_DONTWAIT: empty socket returns EAGAIN straight away (readv would have blocked) */
    char buf[32] = {};
    errno = 0;
    EXPECT_EQ(-1, raw->Recv(sock, buf, sizeof(buf), MSG_DONTWAIT));
    EXPECT_EQ(EAGAIN, errno);

    /* MSG_PEEK: the same bytes are visible twice (readv would have consumed them) */
    errno = 0;
    ssize_t sent = ::write(fds[1], "peek", 4);
    ASSERT_EQ(4, sent) << "write errno=" << errno;
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, raw->Recv(sock, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT));
    EXPECT_EQ(0, memcmp(buf, "peek", 4));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, raw->Recv(sock, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT));
    EXPECT_EQ(0, memcmp(buf, "peek", 4));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, raw->Recv(sock, buf, sizeof(buf), MSG_DONTWAIT));
    EXPECT_EQ(0, memcmp(buf, "peek", 4));

    /* send side keeps its flags as well */
    errno = 0;
    EXPECT_EQ(4, raw->Send(sock, "send", 4, MSG_NOSIGNAL)) << "send errno=" << errno;
    char peer[8] = {};
    ASSERT_EQ(4, ::read(fds[1], peer, sizeof(peer)));
    EXPECT_EQ(0, memcmp(peer, "send", 4));

    ::close(fds[0]);
    ::close(fds[1]);
}

/* managed send: MSG_DONTWAIT must reach the transport as UBS_IO_FLAG_DONT_WAIT */
TEST_F(UbsocketSendRecvTest, ManagedSendMapsDontWaitFlagToTransport)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);
    auto *txOps = new FakeTxOps();
    raw->GetTx()->tx_ops_ = txOps;

    char buf[16] = {};
    EXPECT_EQ(16, raw->Send(sock, buf, sizeof(buf), 0));
    EXPECT_EQ(UBS_IO_FLAG_USE_BRPC_BLOCK, txOps->lastFlags);

    EXPECT_EQ(16, raw->Send(sock, buf, sizeof(buf), MSG_NOSIGNAL));
    EXPECT_EQ(UBS_IO_FLAG_USE_BRPC_BLOCK, txOps->lastFlags);

    EXPECT_EQ(16, raw->Send(sock, buf, sizeof(buf), MSG_DONTWAIT));
    EXPECT_EQ(UBS_IO_FLAG_USE_BRPC_BLOCK | UBS_IO_FLAG_DONT_WAIT, txOps->lastFlags);

    raw->GetTx()->tx_ops_ = nullptr;
    delete txOps;
}

/* managed recv: MSG_WAITALL keeps reading until len is filled, MSG_DONTWAIT stops after one chunk */
TEST_F(UbsocketSendRecvTest, ManagedRecvWaitAllLoopAndDontWaitMapping)
{
    auto raw = MakeRef<TestSocket>(TEST_FD, SocketType::SOCK_TYPE_URMA);
    raw->State(SOCK_STAT_ESTABLISHED);
    SocketPtr sock = RefStaticCast<Socket>(raw);
    auto *rxOps = new FakeRxOps(4);
    raw->GetRx()->rx_ops_ = rxOps;

    char buf[16] = {};
    EXPECT_EQ(12, raw->Recv(sock, buf, 12, MSG_WAITALL));
    EXPECT_EQ(3, rxOps->calls);
    EXPECT_EQ(UBS_IO_FLAG_USE_BRPC_BLOCK, rxOps->lastFlags);

    /* MSG_DONTWAIT returns as soon as the first chunk arrives, even together with MSG_WAITALL */
    rxOps->calls = 0;
    EXPECT_EQ(4, raw->Recv(sock, buf, sizeof(buf), MSG_WAITALL | MSG_DONTWAIT));
    EXPECT_EQ(1, rxOps->calls);
    EXPECT_EQ(UBS_IO_FLAG_USE_BRPC_BLOCK | UBS_IO_FLAG_DONT_WAIT, rxOps->lastFlags);

    raw->GetRx()->rx_ops_ = nullptr;
    delete rxOps;
}
