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
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <vector>

#include "core/ubsocket_conn_buffer.h"
#include "core/ubsocket_conn_lp_reader.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

namespace {
static const int TEST_FD = 100;

/* 每次 recv 调用的可控行为序列：ret=1 表示从 data 缓冲逐次取出，ret=0 表示 EOF，ret=-1 表示错误(err) */
struct RecvChunk {
    int ret = 1;
    int err = 0;
    std::vector<uint8_t> data;
    size_t off = 0;
};

static std::vector<RecvChunk> g_recv_chunks;
static size_t g_recv_idx = 0;

static ssize_t MockRecvSeq(int, void *buf, size_t len, int)
{
    while (g_recv_idx < g_recv_chunks.size()) {
        RecvChunk &chunk = g_recv_chunks[g_recv_idx];
        if (chunk.ret < 0) {
            ++g_recv_idx;
            errno = chunk.err;
            return -1;
        }
        if (chunk.ret == 0) {
            ++g_recv_idx;
            return 0;
        }
        size_t avail = chunk.data.size() - chunk.off;
        if (avail > 0) {
            size_t n = (len < avail) ? len : avail;
            memcpy(buf, chunk.data.data() + chunk.off, n);
            chunk.off += n;
            return static_cast<ssize_t>(n);
        }
        ++g_recv_idx; /* 当前分片耗尽：继续服务下一个分片 */
    }
    errno = EAGAIN; /* socket 缓冲全空才是 EAGAIN */
    return -1;
}

static void PushRecvData(const void *data, size_t size)
{
    RecvChunk chunk;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    chunk.data.assign(bytes, bytes + size);
    g_recv_chunks.push_back(chunk);
}

static void PushRecvError(int err)
{
    RecvChunk chunk;
    chunk.ret = -1;
    chunk.err = err;
    g_recv_chunks.push_back(chunk);
}

static void PushRecvEof()
{
    RecvChunk chunk;
    chunk.ret = 0;
    g_recv_chunks.push_back(chunk);
}

static void ResetRecvSeq()
{
    g_recv_chunks.clear();
    g_recv_idx = 0;
}

struct TestFrame {
    uint8_t payload[8];
};

static TestFrame MakeFrame(const uint8_t *payload, size_t payload_size)
{
    TestFrame frame;
    memset(&frame, 0, sizeof(frame));
    if (payload != nullptr) {
        memcpy(frame.payload, payload, payload_size);
    }
    return frame;
}

static std::vector<uint8_t> MakeWireFrame(uint32_t body_len, const void *body, size_t body_size)
{
    std::vector<uint8_t> frame(sizeof(body_len) + body_size);
    memcpy(frame.data(), &body_len, sizeof(body_len));
    memcpy(frame.data() + sizeof(body_len), body, body_size);
    return frame;
}
} // namespace

class ConnBufferTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetRecvSeq();
        LibcApi::Load();
        LibcApi::recv_ptr = MockRecvSeq;
    }

    void TearDown() override
    {
        errno = 0;
        LibcApi::UnLoad();
        LibcApi::Load();
        ResetRecvSeq();
    }
};

/* ==================== AsyncConnBuffer ==================== */

TEST_F(ConnBufferTest, Advance_ReadsUntilExpected_ReturnsSize)
{
    const char data[] = "abcdefgh";
    PushRecvData(data, sizeof(data) - 1);

    AsyncConnBuffer buf;
    ssize_t n = buf.Advance(TEST_FD, 8);
    EXPECT_EQ(8, n);
    EXPECT_TRUE(buf.Ready(8));
    EXPECT_EQ(0, memcmp(data, buf.Data(), 8));
}

TEST_F(ConnBufferTest, Advance_SplitAcrossRecvs_ReturnsExpected)
{
    const char part1[] = "abcd";
    const char part2[] = "1234";
    PushRecvData(part1, 4);
    PushRecvData(part2, 4);

    AsyncConnBuffer buf;
    ssize_t n = buf.Advance(TEST_FD, 8);
    EXPECT_EQ(8, n);
    EXPECT_EQ(0, memcmp("abcd1234", buf.Data(), 8));
}

TEST_F(ConnBufferTest, Advance_Eagain_ReturnsPartialAndClearsErrno)
{
    const char part[] = "ab";
    PushRecvData(part, 2);

    AsyncConnBuffer buf;
    errno = 0;
    ssize_t n = buf.Advance(TEST_FD, 8);
    EXPECT_EQ(2, n);
    EXPECT_FALSE(buf.Ready(8));
    EXPECT_EQ(0, errno); /* EAGAIN 半包不被视为错误 */
}

TEST_F(ConnBufferTest, Advance_PeerClosed_ReturnsMinusOneAndErrnoEconnreset)
{
    PushRecvEof();

    AsyncConnBuffer buf;
    errno = 0;
    ssize_t n = buf.Advance(TEST_FD, 8);
    EXPECT_EQ(-1, n);
    EXPECT_EQ(ECONNRESET, errno);
}

TEST_F(ConnBufferTest, Advance_RecvError_ReturnsMinusOne)
{
    PushRecvError(ECONNREFUSED);

    AsyncConnBuffer buf;
    errno = 0;
    ssize_t n = buf.Advance(TEST_FD, 8);
    EXPECT_EQ(-1, n);
    EXPECT_EQ(ECONNREFUSED, errno);
}

TEST_F(ConnBufferTest, Advance_ExpectedExceedsBuffer_ReturnsMinusOneAndErrnoEinval)
{
    AsyncConnBuffer buf;
    errno = 0;
    ssize_t n = buf.Advance(TEST_FD, CONN_CTRL_BUF_SIZE + 1);
    EXPECT_EQ(-1, n);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(ConnBufferTest, Consume_PartialConsume_MemmoveRemaining)
{
    const char data[] = "abcdefgh";
    PushRecvData(data, 8);

    AsyncConnBuffer buf;
    EXPECT_EQ(8, buf.Advance(TEST_FD, 8));
    buf.Consume(3);
    EXPECT_EQ(0, memcmp("defgh", buf.Data(), 5));
}

TEST_F(ConnBufferTest, Consume_AllOrMore_ResetsSize)
{
    const char data[] = "abcd";
    PushRecvData(data, 4);

    AsyncConnBuffer buf;
    EXPECT_EQ(4, buf.Advance(TEST_FD, 4));
    buf.Consume(10);
    EXPECT_FALSE(buf.Ready(1)); /* 消费者全部消费后不再 ready */

    buf.Reset();
    EXPECT_FALSE(buf.Ready(1));
}

/* ==================== LengthPrefixedReader ==================== */

TEST_F(ConnBufferTest, LpReaderRead_FullFrameInOneRecv_ReturnsDone)
{
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> wire = MakeWireFrame(8, payload, 8);
    PushRecvData(wire.data(), wire.size());

    TestFrame frame = MakeFrame(nullptr, 8);
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
    EXPECT_TRUE(reader.Done());
    EXPECT_EQ(0, memcmp(payload, frame.payload, 8));
}

TEST_F(ConnBufferTest, LpReaderRead_PrefixSplitAcrossRecvs_ReturnsPendingThenDone)
{
    const uint8_t payload[] = {9, 9, 9, 9};
    std::vector<uint8_t> wire = MakeWireFrame(4, payload, 4);
    PushRecvData(wire.data(), 2); /* 先到：前缀前半 */

    TestFrame frame = MakeFrame(nullptr, 8);
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kPartial, reader.Read(TEST_FD, buf, "ut")); /* 前缀半包，等后续字节 */

    PushRecvData(wire.data() + 2, wire.size() - 2); /* 后到：前缀后半 + body */
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
    EXPECT_EQ(0, memcmp(payload, frame.payload, 4));
}

TEST_F(ConnBufferTest, LpReaderRead_BodyLenZero_ReturnsError)
{
    const uint8_t nothing = 0;
    std::vector<uint8_t> wire = MakeWireFrame(0, &nothing, 0);
    PushRecvData(wire.data(), wire.size());

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kError, reader.Read(TEST_FD, buf, "ut"));
}

TEST_F(ConnBufferTest, LpReaderRead_PeerBodyShorter_ZeroFillsTail)
{
    const uint8_t payload[] = {5, 6};
    std::vector<uint8_t> wire = MakeWireFrame(2, payload, 2);
    PushRecvData(wire.data(), wire.size());

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    frame.payload[6] = 0xAA;
    frame.payload[7] = 0xBB;
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
    EXPECT_EQ(5, frame.payload[0]);
    EXPECT_EQ(6, frame.payload[1]);
    /* 对端结构体较短：缺失字段补零 */
    EXPECT_EQ(0, frame.payload[2]);
    EXPECT_EQ(0, frame.payload[6]);
    EXPECT_EQ(0, frame.payload[7]);
}

TEST_F(ConnBufferTest, LpReaderRead_PeerBodyLonger_DiscardsExtraBytes)
{
    const uint8_t body[10] = {1, 2, 3, 4, 5, 6, 7, 8, 0xAA, 0xBB};
    std::vector<uint8_t> wire = MakeWireFrame(10, body, 10);
    PushRecvData(wire.data(), wire.size());

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
    /* 本地结构体 8B，read body 取 min(10, 8)，多余 2B 被丢弃 */
    EXPECT_EQ(0, memcmp(body, frame.payload, 8));
}

TEST_F(ConnBufferTest, LpReaderRead_BodyLenExceedsDiscardCap_ReturnsError)
{
    const uint32_t huge_len = 8 + (64 * 1024) + 1; /* extra > kMaxDiscardBytes(64K) */
    uint8_t body[8] = {0};
    std::vector<uint8_t> wire = MakeWireFrame(huge_len, body, 8);
    PushRecvData(wire.data(), wire.size());

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kError, reader.Read(TEST_FD, buf, "ut"));
}

TEST_F(ConnBufferTest, LpReaderRead_BodyEagain_ReturnsPendingThenDone)
{
    const uint8_t payload[] = {1, 2, 3, 4};
    std::vector<uint8_t> wire = MakeWireFrame(4, payload, 4);
    PushRecvData(wire.data(), 4); /* 仅前缀 */
    PushRecvData(payload, 2);     /* body 前半 */

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kPartial, reader.Read(TEST_FD, buf, "ut")); /* body 半包 */

    PushRecvData(payload + 2, 2);
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
    EXPECT_EQ(0, memcmp(payload, frame.payload, 4));
}

TEST_F(ConnBufferTest, LpReaderRead_DiscardPhaseEagain_ReturnsPendingThenDone)
{
    const uint8_t body[10] = {1, 2, 3, 4, 5, 6, 7, 8, 0xAA, 0xBB};
    std::vector<uint8_t> wire = MakeWireFrame(10, body, 10);
    PushRecvData(wire.data(), 12); /* 前缀 + body 8B */
    PushRecvData(body + 8, 1);     /* discard 仅到 1B */

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kPartial, reader.Read(TEST_FD, buf, "ut")); /* 超量字节未到齐 */

    PushRecvData(body + 9, 1);
    EXPECT_EQ(LpReadResult::kComplete, reader.Read(TEST_FD, buf, "ut"));
}

TEST_F(ConnBufferTest, LpReaderRead_PeerClosedInBody_ReturnsError)
{
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> wire = MakeWireFrame(8, payload, 8);
    PushRecvData(wire.data(), 6); /* 前缀 + body 仅 2B */

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kPartial, reader.Read(TEST_FD, buf, "ut")); /* body 半包 */

    PushRecvEof();
    EXPECT_EQ(LpReadResult::kError, reader.Read(TEST_FD, buf, "ut"));
}

TEST_F(ConnBufferTest, LpReaderRead_RecvErrorInBody_ReturnsError)
{
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> wire = MakeWireFrame(8, payload, 8);
    PushRecvData(wire.data(), 6); /* 前缀 + body 仅 2B */
    PushRecvError(ECONNREFUSED);

    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));
    EXPECT_EQ(LpReadResult::kError, reader.Read(TEST_FD, buf, "ut"));
}

TEST_F(ConnBufferTest, LpReaderRead_PrefixRecvError_ReturnsError)
{
    TestFrame frame = MakeFrame(nullptr, sizeof(frame));
    LengthPrefixedReader reader;
    AsyncConnBuffer buf;
    reader.Begin(&frame, sizeof(frame));

    PushRecvError(ECONNREFUSED);
    EXPECT_EQ(LpReadResult::kError, reader.Read(TEST_FD, buf, "ut"));
}