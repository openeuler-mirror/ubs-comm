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

#include "ubsocket_data_tx.h"

#include <array>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_lock.h"
#include "ubsocket_socket.h"
#include "umq_socket.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
constexpr int TEST_FD = 42;
constexpr uint32_t TEST_IOBUF_SIZE = 4064;
constexpr size_t TEST_MESSAGE_SIZE = 64;

class TestIovConverter : public IovConverter {
public:
    TestIovConverter(const struct iovec *iov, int iovcnt) : IovConverter(iov, iovcnt) {}

    bool MemCopy(uint32_t len, uintptr_t buf) override
    {
        return true;
    }
};

class TestDataTxOps : public DataTxOps {
public:
    ConverterPtr BuildIovConverter(const struct iovec *iov, int iovcnt) override
    {
        auto converter = MakeRef<TestIovConverter>(iov, iovcnt);
        return RefConvert<TestIovConverter, UbSocketBufConverter>(converter);
    }

    ConverterPtr BuildBufferConverter(const void *buf, size_t size) override
    {
        return nullptr;
    }

    uintptr_t AllocTxBuf(uint32_t size, uint32_t count) override
    {
        return 1;
    }

    int PostSend(const SocketPtr &sock, uintptr_t bufList, uint32_t batch, const ConverterPtr &cvt) override
    {
        if (wakeDuringPost_) {
            RefConvert<Socket, SocketBase>(sock)->SetWritableReady(true);
        }
        errno = postErrno_;
        return postResult_;
    }

    int PollTx(Socket *sock) override
    {
        return 0;
    }

    uint32_t IOBufSize() override
    {
        return TEST_IOBUF_SIZE;
    }

    void FlushTx(Socket *sock, uint32_t timeoutMs) override {}

    void WakeUpTx(Socket *sock) override {}

    int postResult_ = static_cast<int>(TEST_MESSAGE_SIZE);
    int postErrno_ = 0;
    bool wakeDuringPost_ = false;
};

class DataTxWritableTest : public testing::Test {
protected:
    void SetUp() override
    {
        savedTraceEnabled_ = GlobalSetting::UBS_TRACE_ENABLED;
        savedSplitTraceEnabled_ = GlobalSetting::UBS_SPLIT_TRACE_ENABLED;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        block_.fill(0);
        errno = 0;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        GlobalSetting::UBS_TRACE_ENABLED = savedTraceEnabled_;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = savedSplitTraceEnabled_;
        errno = 0;
    }

    SocketPtr MakeEstablishedSocket(UmqSocketPtr &umqSocket)
    {
        umqSocket = MakeRef<UmqSocket>(TEST_FD);
        umqSocket->State(SOCK_STAT_ESTABLISHED);
        return RefConvert<UmqSocket, Socket>(umqSocket);
    }

    void ExpectOneAllocation()
    {
        MOCKER(ubsocket_iobuf_allocate).expects(once()).will(returnValue(static_cast<void *>(block_.data())));
    }

    alignas(64) std::array<uint8_t, IOBUF_DIFF + TEST_MESSAGE_SIZE> block_{};
    TestDataTxOps ops_;
    bool savedTraceEnabled_ = false;
    bool savedSplitTraceEnabled_ = false;
};

TEST_F(DataTxWritableTest, FullWriteRestoresLatentWritableToken)
{
    UmqSocketPtr umqSocket;
    SocketPtr sock = MakeEstablishedSocket(umqSocket);
    DataTx dataTx(sock, &ops_);
    umqSocket->SetWritableReady(false);
    ExpectOneAllocation();

    std::array<char, TEST_MESSAGE_SIZE> message{};
    struct iovec iov = {message.data(), message.size()};

    EXPECT_EQ(dataTx.WriteV(sock, &iov, 1), static_cast<ssize_t>(message.size()));
    EXPECT_TRUE(umqSocket->ExchangeWritableReady(false));
}

TEST_F(DataTxWritableTest, EagainLeavesWritableTokenCleared)
{
    UmqSocketPtr umqSocket;
    SocketPtr sock = MakeEstablishedSocket(umqSocket);
    DataTx dataTx(sock, &ops_);
    ops_.postResult_ = -1;
    ops_.postErrno_ = EAGAIN;
    ExpectOneAllocation();

    std::array<char, TEST_MESSAGE_SIZE> message{};
    struct iovec iov = {message.data(), message.size()};

    EXPECT_EQ(dataTx.WriteV(sock, &iov, 1), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_FALSE(umqSocket->ExchangeWritableReady(false));
}

TEST_F(DataTxWritableTest, ConcurrentWakeDuringPostIsNotOverwritten)
{
    UmqSocketPtr umqSocket;
    SocketPtr sock = MakeEstablishedSocket(umqSocket);
    DataTx dataTx(sock, &ops_);
    ops_.postResult_ = -1;
    ops_.postErrno_ = EAGAIN;
    ops_.wakeDuringPost_ = true;
    ExpectOneAllocation();

    std::array<char, TEST_MESSAGE_SIZE> message{};
    struct iovec iov = {message.data(), message.size()};

    EXPECT_EQ(dataTx.WriteV(sock, &iov, 1), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_TRUE(umqSocket->ExchangeWritableReady(false));
}
} // namespace
