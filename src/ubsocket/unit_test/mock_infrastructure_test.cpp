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

#include <gtest/gtest.h>
#include <cerrno>
#include <cstring>

#include "fake_epoll.h"
#include "libc_api_helper.h"
#include "socket_test_helper.h"
#include "umq_api_helper.h"

#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_ref.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_wakeup_event.h"
#include "core/umq/umq_socket.h"
#include "include/ubsocket.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;
using namespace ock::ubs::test;

namespace {
constexpr uint32_t TEST_ALLOC_SIZE = 1024;
constexpr int REFCOUNT_AFTER_COPY = 2;
constexpr int FD_COUNT_AFTER_TWO_ALLOCS = 2;
} // namespace

class HelperMockcppTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
        GlobalMockObject::verify();
    }
};

class StaticFakeEpollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
    }
};

class BlockHelperTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetMockBufWithBlockIndex();
    }

    void TearDown() override
    {
        errno = 0;
        ResetMockBufWithBlockIndex();
    }
};

class SocketHelperTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
    }
};

TEST_F(HelperMockcppTest, LibcApiClosePtrMockedReturnsSuccess)
{
    int testFd = TEST_FD;
    LibcApi::close_ptr = [](int fd) -> int {
        return 0;
    };

    int ret = UB_API_WRAP(close)(testFd);
    EXPECT_EQ(ret, 0);

    LibcApi::close_ptr = nullptr;
}

TEST_F(HelperMockcppTest, LibcApiClosePtrMockedReturnsError)
{
    int testFd = TEST_FD;
    LibcApi::close_ptr = [](int fd) -> int {
        errno = EBADF;
        return -1;
    };

    int ret = UB_API_WRAP(close)(testFd);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);

    LibcApi::close_ptr = nullptr;
}

TEST_F(StaticFakeEpollTest, WakeupEventInitializeUsesFakeEpoll)
{
    FakeEpollCtl::SetNextEpollCreateReturn(TEST_EPOLL_FD);
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_EQ(epfd, TEST_EPOLL_FD);

    UbsocketWakeupEvent wakeup;
    int ret = wakeup.Initialize(epfd);
    EXPECT_EQ(ret, 0);

    wakeup.CleanUp();
    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, WakeupEventInitializeEventfdFailure)
{
    int epfd = FakeEpollCtl::AllocFakeFd();
    FakeEpollCtl::SetNextEventfdReturn(-1);

    UbsocketWakeupEvent wakeup;
    int ret = wakeup.Initialize(epfd);
    EXPECT_EQ(ret, -1);
}

TEST_F(StaticFakeEpollTest, WakeupEventWakeUpWorks)
{
    FakeEpollCtl::SetNextEpollCreateReturn(TEST_EPOLL_FD);
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_EQ(epfd, TEST_EPOLL_FD);

    UbsocketWakeupEvent wakeup;
    ASSERT_EQ(wakeup.Initialize(epfd), 0);

    wakeup.WakeUpReadyEventFd(TEST_FD);

    wakeup.CleanUp();
    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, DirectEpollCtlAddModDelCycle)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    struct epoll_event ev = MakeTestEpollEvent(TEST_FD, EPOLLIN | EPOLLET);
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, TEST_FD, &ev);
    EXPECT_EQ(ret, 0);

    ev.events = EPOLLOUT | EPOLLET;
    ret = epoll_ctl(epfd, EPOLL_CTL_MOD, TEST_FD, &ev);
    EXPECT_EQ(ret, 0);

    ret = epoll_ctl(epfd, EPOLL_CTL_DEL, TEST_FD, nullptr);
    EXPECT_EQ(ret, 0);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, EpollCtlDuplicateAddReturnsEexist)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    struct epoll_event ev = MakeTestEpollEvent(TEST_FD, EPOLLIN | EPOLLET);
    ASSERT_EQ(epoll_ctl(epfd, EPOLL_CTL_ADD, TEST_FD, &ev), 0);

    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, TEST_FD, &ev);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EEXIST);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, EpollCtlDelNonexistentReturnsEnoent)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, TEST_FD, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOENT);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, EpollCtlInvalidEpfdReturnsEbadf)
{
    int ret = epoll_ctl(9999, EPOLL_CTL_ADD, TEST_FD, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(StaticFakeEpollTest, EpollWaitReturnsInjectedEvents)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    struct epoll_event ev = MakeTestEpollEvent(TEST_FD, EPOLLIN | EPOLLET);
    ASSERT_EQ(epoll_ctl(epfd, EPOLL_CTL_ADD, TEST_FD, &ev), 0);

    std::vector<struct epoll_event> waitEvents;
    waitEvents.push_back(MakeTestEpollEvent(TEST_FD, EPOLLIN));
    FakeEpollCtl::SetNextEpollWaitEvents(waitEvents);

    struct epoll_event outEvents[2] = {};
    int count = epoll_wait(epfd, outEvents, 2, 0);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(outEvents[0].data.fd, TEST_FD);
    EXPECT_EQ(outEvents[0].events, EPOLLIN);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, CloseNonFakeFdCallsRealClose)
{
    int realPipe[2] = {};
    int ret = pipe(realPipe);
    ASSERT_EQ(ret, 0);
    EXPECT_EQ(close(realPipe[0]), 0);
    EXPECT_EQ(close(realPipe[1]), 0);
}

TEST_F(StaticFakeEpollTest, EventfdAutoAllocateAndClose)
{
    int fd1 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    EXPECT_GE(fd1, FakeEpollCtl::FAKE_FD_BASE);
    EXPECT_TRUE(FakeEpollCtl::IsFakeFd(fd1));

    int fd2 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    EXPECT_GE(fd2, fd1 + 1);

    EXPECT_EQ(close(fd1), 0);
    EXPECT_FALSE(FakeEpollCtl::IsFakeFd(fd1));

    EXPECT_EQ(close(fd2), 0);
    EXPECT_FALSE(FakeEpollCtl::IsFakeFd(fd2));
}

TEST_F(StaticFakeEpollTest, EventfdWriteReadOnFakeFd)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    EXPECT_EQ(eventfd_write(fd, 1), 0);

    uint64_t val = 0;
    EXPECT_EQ(eventfd_read(fd, &val), sizeof(uint64_t));
    EXPECT_EQ(val, 1);

    EXPECT_EQ(close(fd), 0);
}

TEST_F(StaticFakeEpollTest, EpollWaitErrorReturnInjected)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    FakeEpollCtl::SetNextEpollWaitReturn(-1);

    struct epoll_event outEvents[1] = {};
    errno = 0;
    int count = epoll_wait(epfd, outEvents, 1, 0);
    EXPECT_EQ(count, -1);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, ReleaseFakeFdRemovesFromIsFakeFd)
{
    int fd = FakeEpollCtl::AllocFakeFd();
    EXPECT_TRUE(FakeEpollCtl::IsFakeFd(fd));

    FakeEpollCtl::ReleaseFakeFd(fd);
    EXPECT_FALSE(FakeEpollCtl::IsFakeFd(fd));
}

TEST_F(StaticFakeEpollTest, GetFdCountTracksAllocations)
{
    EXPECT_EQ(FakeEpollCtl::GetFdCount(), 0);

    int fd1 = FakeEpollCtl::AllocFakeFd();
    EXPECT_EQ(FakeEpollCtl::GetFdCount(), 1);

    int fd2 = FakeEpollCtl::AllocFakeFd();
    EXPECT_EQ(FakeEpollCtl::GetFdCount(), FD_COUNT_AFTER_TWO_ALLOCS);

    FakeEpollCtl::ReleaseFakeFd(fd1);
    EXPECT_EQ(FakeEpollCtl::GetFdCount(), 1);

    FakeEpollCtl::ReleaseFakeFd(fd2);
    EXPECT_EQ(FakeEpollCtl::GetFdCount(), 0);
}

TEST_F(StaticFakeEpollTest, EpollCtlModNonexistentReturnsEnoent)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    struct epoll_event ev = MakeTestEpollEvent(TEST_FD, EPOLLOUT | EPOLLET);
    int ret = epoll_ctl(epfd, EPOLL_CTL_MOD, TEST_FD, &ev);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOENT);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, EpollCtlInvalidOpReturnsEinval)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, FakeEpollCtl::FAKE_FD_BASE);

    struct epoll_event ev = MakeTestEpollEvent(TEST_FD, EPOLLIN);
    int ret = epoll_ctl(epfd, 9999, TEST_FD, &ev);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);

    EXPECT_EQ(close(epfd), 0);
}

TEST_F(StaticFakeEpollTest, EpollWaitInvalidEpfdReturnsEbadf)
{
    struct epoll_event outEvents[1] = {};
    errno = 0;
    int count = epoll_wait(9999, outEvents, 1, 0);
    EXPECT_EQ(count, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(StaticFakeEpollTest, EventfdWriteOnNonFakeFdReturnsEbadf)
{
    errno = 0;
    int ret = eventfd_write(9999, 1);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(StaticFakeEpollTest, EventfdReadOnNonFakeFdReturnsEbadf)
{
    uint64_t val = 0;
    errno = 0;
    int ret = eventfd_read(9999, &val);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(StaticFakeEpollTest, CloseRemovesFdFromOtherEpollRegistrations)
{
    int epfd1 = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd1, FakeEpollCtl::FAKE_FD_BASE);
    int epfd2 = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd2, FakeEpollCtl::FAKE_FD_BASE);

    int targetFd = FakeEpollCtl::AllocFakeFd();

    struct epoll_event ev = MakeTestEpollEvent(targetFd, EPOLLIN);
    ASSERT_EQ(epoll_ctl(epfd1, EPOLL_CTL_ADD, targetFd, &ev), 0);
    ASSERT_EQ(epoll_ctl(epfd2, EPOLL_CTL_ADD, targetFd, &ev), 0);

    EXPECT_EQ(close(targetFd), 0);
    EXPECT_FALSE(FakeEpollCtl::IsFakeFd(targetFd));

    int modRet = epoll_ctl(epfd1, EPOLL_CTL_MOD, targetFd, &ev);
    EXPECT_EQ(modRet, -1);
    EXPECT_EQ(errno, ENOENT);

    EXPECT_EQ(close(epfd1), 0);
    EXPECT_EQ(close(epfd2), 0);
}

TEST_F(BlockHelperTest, AllocMockBufWithBlockReturnsNonNull)
{
    umq_buf_t *buf = AllocMockBufWithBlock(TEST_ALLOC_SIZE);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(buf->buf_data, nullptr);
    EXPECT_EQ(buf->data_size, TEST_ALLOC_SIZE);
    EXPECT_EQ(buf->status, UMQ_BUF_SUCCESS);
}

TEST_F(BlockHelperTest, PtrFloorToBoundaryFindsBlock)
{
    umq_buf_t *buf = AllocMockBufWithBlock(TEST_ALLOC_SIZE);
    ASSERT_NE(buf, nullptr);

    Block *block = GetBlockFromMockBuf(buf);
    ASSERT_NE(block, nullptr);

    uint64_t floorAddr = reinterpret_cast<uint64_t>(buf->buf_data) & ~static_cast<uint64_t>(BLOCK_ALIGNMENT_SIZE - 1);
    EXPECT_EQ(reinterpret_cast<uint64_t>(block), floorAddr);
    EXPECT_EQ(block->data, buf->buf_data);
    EXPECT_EQ(block->nshared.load(), 1);
}

TEST_F(BlockHelperTest, BlockIncRefDecRefLifecycle)
{
    umq_buf_t *buf = AllocMockBufWithBlock(TEST_ALLOC_SIZE);
    ASSERT_NE(buf, nullptr);

    Block *block = GetBlockFromMockBuf(buf);
    ASSERT_NE(block, nullptr);

    block->IncRef();
    EXPECT_EQ(block->nshared.load(), REFCOUNT_AFTER_COPY);

    block->DecRef();
    EXPECT_EQ(block->nshared.load(), 1);
}

TEST_F(BlockHelperTest, MultipleAllocMockBufWithBlockIndependent)
{
    umq_buf_t *buf1 = AllocMockBufWithBlock(512);
    umq_buf_t *buf2 = AllocMockBufWithBlock(TEST_ALLOC_SIZE);
    ASSERT_NE(buf1, nullptr);
    ASSERT_NE(buf2, nullptr);

    EXPECT_NE(buf1->buf_data, buf2->buf_data);

    Block *block1 = GetBlockFromMockBuf(buf1);
    Block *block2 = GetBlockFromMockBuf(buf2);
    ASSERT_NE(block1, nullptr);
    ASSERT_NE(block2, nullptr);

    block1->IncRef();
    EXPECT_EQ(block1->nshared.load(), REFCOUNT_AFTER_COPY);
    EXPECT_EQ(block2->nshared.load(), 1);
}

TEST_F(BlockHelperTest, AllocMockBufWithBlockWithErrorStatus)
{
    umq_buf_t *buf = AllocMockBufWithBlock(TEST_ALLOC_SIZE, UMQ_BUF_LOC_ACCESS_ERR);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf->status, UMQ_BUF_LOC_ACCESS_ERR);

    Block *block = GetBlockFromMockBuf(buf);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->nshared.load(), 1);
}

TEST_F(SocketHelperTest, MakeTestUmqSocketCreatesValidSocketPtr)
{
    SocketPtr sock = MakeTestUmqSocket();
    ASSERT_NE(sock.Get(), nullptr);

    EXPECT_EQ(sock.Get()->raw_socket_, TEST_FD);
    EXPECT_EQ(sock.Get()->type_, SocketType::SOCK_TYPE_UMQ);
    EXPECT_EQ(sock.Get()->state_, SOCK_STAT_ESTABLISHED);
    EXPECT_TRUE(FakeEpollCtl::IsFakeFd(sock.Get()->event_fd_));
}

TEST_F(SocketHelperTest, MakeTestUmqSocketHasUmqHandle)
{
    SocketPtr sock = MakeTestUmqSocket(TEST_FD, TEST_UMQ_HANDLE);
    ASSERT_NE(sock.Get(), nullptr);

    UmqSocket *umqSock = dynamic_cast<UmqSocket *>(sock.Get());
    ASSERT_NE(umqSock, nullptr);
    EXPECT_EQ(umqSock->UmqHandle(), TEST_UMQ_HANDLE);
}

TEST_F(SocketHelperTest, MakeTestUmqSocketHasTxRxOps)
{
    SocketPtr sock = MakeTestUmqSocket(TEST_FD, TEST_UMQ_HANDLE);
    ASSERT_NE(sock.Get(), nullptr);

    SocketBase *base = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(base, nullptr);

    DataTxOps *txOps = base->GetTx()->GetTxOps();
    ASSERT_NE(txOps, nullptr);
    EXPECT_EQ(txOps->fd_, TEST_FD);

    DataRxOps *rxOps = base->GetRx()->GetRxOps();
    ASSERT_NE(rxOps, nullptr);
    EXPECT_EQ(rxOps->fd_, TEST_FD);
}

TEST_F(SocketHelperTest, MakeTestUmqSocketRefCountManaged)
{
    SocketPtr sock = MakeTestUmqSocket();
    ASSERT_NE(sock.Get(), nullptr);

    Socket *raw = sock.Get();
    EXPECT_EQ(raw->ref_count_.load(), 1);

    SocketPtr sock2 = sock;
    EXPECT_EQ(raw->ref_count_.load(), REFCOUNT_AFTER_COPY);
}

TEST_F(SocketHelperTest, DestroyTestSocketOpsCleansUp)
{
    SocketPtr sock = MakeTestUmqSocket();
    ASSERT_NE(sock.Get(), nullptr);

    SocketBase *base = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(base, nullptr);
    ASSERT_NE(base->GetTx()->GetTxOps(), nullptr);
    ASSERT_NE(base->GetRx()->GetRxOps(), nullptr);

    int eventFd = sock.Get()->event_fd_;
    EXPECT_TRUE(FakeEpollCtl::IsFakeFd(eventFd));

    DestroyTestSocketOps(sock);

    EXPECT_EQ(base->GetTx()->GetTxOps(), nullptr);
    EXPECT_EQ(base->GetRx()->GetRxOps(), nullptr);
    EXPECT_FALSE(FakeEpollCtl::IsFakeFd(eventFd));
}

TEST_F(SocketHelperTest, MakeTestUmqSocketWithCustomFd)
{
    const int customFd = 55;
    const uint64_t customHandle = 99999ULL;

    SocketPtr sock = MakeTestUmqSocket(customFd, customHandle);
    ASSERT_NE(sock.Get(), nullptr);

    EXPECT_EQ(sock.Get()->raw_socket_, customFd);

    UmqSocket *umqSock = dynamic_cast<UmqSocket *>(sock.Get());
    ASSERT_NE(umqSock, nullptr);
    EXPECT_EQ(umqSock->UmqHandle(), customHandle);
}