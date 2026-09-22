/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the license at:
 * http://license.coscl.org.cn/MulanPSL2
 */

#include "include/ubsocket_data.h"

#include <gtest/gtest.h>
#include <cerrno>
#include <cstring>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_lock.h"
#include "core/ubsocket_bigdata.h"
#include "core/ubsocket_core_types.h"
#include "core/umq/umq_buffer_receive_queue.h"
#include "core/umq/umq_socket.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_libc_api.h"

using ock::ubs::ArraySet;
using ock::ubs::Block;
using ock::ubs::LockRegistry;
using ock::ubs::Socket;
using ock::ubs::SocketPtr;
using ock::ubs::UbsBigdata;
using ock::ubs::umq::UmqSocket;

namespace {

ssize_t MockRecvEagain(int, void *, size_t, int)
{
    errno = EAGAIN;
    return -1;
}

ssize_t MockRecvEof(int, void *, size_t, int)
{
    return 0;
}

void MockBufFree(umq_buf_t *) {}

int gShutdownFd = -1;
int MockShutdown(int fd, int)
{
    gShutdownFd = fd;
    return 0;
}

bool MockTrySenderPost(int, const ubs_data_list_t *, ssize_t *out)
{
    *out = 123;
    return true;
}

bool MockTrySenderDeclined(int, const ubs_data_list_t *, ssize_t *)
{
    return false;
}

bool MockHandleRxConsumed(const SocketPtr &, umq_buf_t *)
{
    return true;
}

bool MockHandleRxNotConsumed(const SocketPtr &, umq_buf_t *)
{
    return false;
}

/* Layout: [Block headroom][payload] so ubs_poll can placement-new Block. */
struct PollTestBuf {
    alignas(alignof(Block)) char storage[sizeof(Block) + 128];
    umq_buf_t qbuf{};

    void InitPayload(const char *payload, size_t len, uint32_t sn = 0)
    {
        umq_buf_pro_t pro{};
        memset(&pro, 0, sizeof(pro));
        pro.imm.user_data = sn;
        memcpy(qbuf.qbuf_ext, &pro, sizeof(pro));
        qbuf.buf_data = storage + sizeof(Block);
        memcpy(qbuf.buf_data, payload, len);
        qbuf.data_size = static_cast<uint32_t>(len);
        qbuf.headroom_size = static_cast<uint16_t>(sizeof(Block));
        qbuf.status = 0;
    }
};

TEST(UbsocketDataTest, PostNullListCurrentlyCrashes)
{
    EXPECT_DEATH(ubs_post(1, nullptr), "");
}

TEST(UbsocketDataTest, PostSegmentsNullReturnsEinval)
{
    ubs_data_list_t list{};
    list.nsegs = 1;
    errno = 0;
    EXPECT_EQ(ubs_post(1, &list), -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(UbsocketDataTest, PostUnknownFdReturnsEpipe)
{
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    errno = 0;
    EXPECT_EQ(ubs_post(9999, &list), -1);
    EXPECT_EQ(errno, EPIPE);
}

TEST(UbsocketDataTest, PostRegisteredSocketEmptyBatchReturnsZero)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(41);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(41, socket);
    ubs_data_list_t list{};
    EXPECT_EQ(ubs_post(41, &list), 0);
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PostClosedSocketReturnsEpipe)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(42);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(42, socket);
    socket->State(ock::ubs::SOCK_STAT_CLOSE);
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    errno = 0;
    EXPECT_EQ(ubs_post(42, &list), -1);
    EXPECT_EQ(errno, EPIPE);
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PostRnrBlockedReturnsEagain)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(43);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(43, socket);
    socket->SetRnrBlocked(true);
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    errno = 0;
    EXPECT_EQ(ubs_post(43, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PostRnrFatalClosesSocket)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(51);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(51, socket);
    socket->SetRnrBlocked(true);
    MOCKER_CPP(&UmqSocket::TryRnrBlockFatal).stubs().will(returnValue(true));
    auto oldShutdown = ock::ubs::LibcApi::shutdown_ptr;
    ock::ubs::LibcApi::shutdown_ptr = MockShutdown;
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    EXPECT_EQ(ubs_post(51, &list), -1);
    EXPECT_EQ(gShutdownFd, 51);
    EXPECT_EQ(socket->State(), ock::ubs::SOCK_STAT_CLOSE);
    ock::ubs::LibcApi::shutdown_ptr = oldShutdown;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PostBigdataPathReturnsEngineResult)
{
    GlobalMockObject::verify();
    MOCKER(&UbsBigdata::TrySenderPost).stubs().will(invoke(MockTrySenderPost));
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(52);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(52, socket);
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    EXPECT_EQ(ubs_post(52, &list), 123);
    ArraySet<Socket>::GetInstance().ReleaseAll();
    GlobalMockObject::verify();
}

TEST(UbsocketDataTest, PostBigdataDeclinedReturnsEpipe)
{
    GlobalMockObject::verify();
    MOCKER(&UbsBigdata::TrySenderPost).stubs().will(invoke(MockTrySenderDeclined));
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(53);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(53, socket);
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.nsegs = 1;
    list.segments = &segment;
    errno = 0;
    EXPECT_EQ(ubs_post(53, &list), -1);
    EXPECT_EQ(errno, EPIPE);
    ArraySet<Socket>::GetInstance().ReleaseAll();
    GlobalMockObject::verify();
}

TEST(UbsocketDataTest, PollClosedSocketReturnsEof)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(44);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(44, socket);
    socket->State(ock::ubs::SOCK_STAT_CLOSE);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    errno = EIO;
    EXPECT_EQ(ubs_poll(44, &list), 0);
    EXPECT_EQ(errno, 0);
    EXPECT_EQ(list.nsegs, 0U);
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollRegisteredSocketWithoutQueueReturnsEagain)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(45);
    SocketPtr keep(socket);
    ArraySet<Socket>::GetInstance().OverrideItem(45, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    errno = 0;
    EXPECT_EQ(ubs_poll(45, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollInitializedEmptyQueueReturnsEagain)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(46);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(46, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    errno = 0;
    EXPECT_EQ(ubs_poll(46, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollDeliversQueuedPayload)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(47);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(47, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    PollTestBuf tbuf;
    tbuf.InitPayload("payload", sizeof("payload"));
    ASSERT_EQ(socket->rxQueue->Enqueue(&tbuf.qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    EXPECT_EQ(ubs_poll(47, &list), 1);
    EXPECT_EQ(list.nsegs, 1U);
    EXPECT_EQ(list.segments[0].len, sizeof("payload"));
    EXPECT_EQ(list.segments[0].start_pos, tbuf.qbuf.buf_data);
    auto *blk = static_cast<Block *>(list.segments[0].block);
    ASSERT_NE(blk, nullptr);
    EXPECT_EQ(blk->data, tbuf.qbuf.buf_data);
    EXPECT_EQ(reinterpret_cast<char *>(blk), tbuf.qbuf.buf_data - sizeof(Block));
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollReturnsSegmentCountForMultipleSegments)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(58);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(58, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    PollTestBuf first{};
    first.InitPayload("one", sizeof("one"), 1);
    PollTestBuf second{};
    second.InitPayload("two", sizeof("two"), 2);
    ASSERT_EQ(socket->rxQueue->Enqueue(&first.qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ASSERT_EQ(socket->rxQueue->Enqueue(&second.qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segments[2]{};
    ubs_data_list_t list{segments, 2};
    const int rc = ubs_poll(58, &list);
    EXPECT_EQ(rc, 2);
    EXPECT_EQ(list.nsegs, 2U);
    EXPECT_EQ(rc, static_cast<int>(list.nsegs));
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollErrorBufferReturnsEconnreset)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(48);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(48, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFree));
    umq_buf_t qbuf{};
    qbuf.status = 1;
    qbuf.data_size = 1;
    qbuf.buf_data = reinterpret_cast<char *>(0x1);
    umq_buf_pro_t pro{};
    std::memcpy(qbuf.qbuf_ext, &pro, sizeof(pro));
    ASSERT_EQ(socket->rxQueue->Enqueue(&qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    errno = 0;
    EXPECT_EQ(ubs_poll(48, &list), -1);
    EXPECT_EQ(errno, ECONNRESET);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollEmptyQueueAfterPeerCloseReturnsEof)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(49);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(49, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEof;
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    errno = EIO;
    EXPECT_EQ(ubs_poll(49, &list), 0);
    EXPECT_EQ(errno, 0);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollZeroLengthBufferIsSkipped)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(50);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(50, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFree));
    umq_buf_t qbuf{};
    qbuf.buf_data = reinterpret_cast<char *>(0x1);
    qbuf.data_size = 0;
    umq_buf_pro_t pro{};
    std::memcpy(qbuf.qbuf_ext, &pro, sizeof(pro));
    ASSERT_EQ(socket->rxQueue->Enqueue(&qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    EXPECT_EQ(ubs_poll(50, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollReturnsPayloadBeforeLaterError)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(54);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(54, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFree));
    PollTestBuf first;
    first.InitPayload("ok", sizeof("ok"));
    umq_buf_t second{};
    umq_buf_pro_t secondPro{};
    second.buf_data = reinterpret_cast<char *>(0x1);
    second.data_size = 1;
    second.status = 1;
    std::memcpy(second.qbuf_ext, &secondPro, sizeof(secondPro));
    ASSERT_EQ(socket->rxQueue->Enqueue(&first.qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ASSERT_EQ(socket->rxQueue->Enqueue(&second), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segments[2]{};
    ubs_data_list_t list{segments, 2};
    EXPECT_EQ(ubs_poll(54, &list), 1);
    EXPECT_EQ(list.nsegs, 1U);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollConsumesBigdataControl)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(55);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(55, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    MOCKER(&UbsBigdata::HandleRxControl).stubs().will(invoke(MockHandleRxConsumed));
    umq_buf_t qbuf{};
    umq_buf_pro_t pro{};
    pro.imm_data = (1ULL << 20);
    qbuf.buf_data = reinterpret_cast<char *>(0x1);
    qbuf.data_size = 1;
    std::memcpy(qbuf.qbuf_ext, &pro, sizeof(pro));
    ASSERT_EQ(socket->rxQueue->Enqueue(&qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    EXPECT_EQ(ubs_poll(55, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollBigdataControlFallsThroughWhenNotConsumed)
{
    GlobalMockObject::verify();
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(56);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(56, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEof;
    MOCKER(&UbsBigdata::HandleRxControl).stubs().will(invoke(MockHandleRxNotConsumed));
    PollTestBuf tbuf;
    tbuf.InitPayload("x", 1);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext);
    pro->imm_data = (1ULL << 20);
    ASSERT_EQ(socket->rxQueue->Enqueue(&tbuf.qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    EXPECT_EQ(ubs_poll(56, &list), 1);
    EXPECT_EQ(list.nsegs, 1U);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollNullOutputReturnsEinval)
{
    errno = 0;
    EXPECT_EQ(ubs_poll(1, nullptr), -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(UbsocketDataTest, PollNullSegmentsReturnsEinval)
{
    ubs_data_list_t list{};
    list.nsegs = 1;
    errno = 0;
    EXPECT_EQ(ubs_poll(1, &list), -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(UbsocketDataTest, PollZeroCapacityReturnsEinval)
{
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.segments = &segment;
    list.nsegs = 0;
    errno = 0;
    EXPECT_EQ(ubs_poll(1, &list), -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(UbsocketDataTest, PollInsufficientHeadroomSkipsSegment)
{
    LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ArraySet<Socket>::GetInstance().Init(), 0);
    UmqSocket *socket = new UmqSocket(57);
    SocketPtr keep(socket);
    socket->rxQueue.reset(new ock::ubs::umq::UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(57, socket);
    auto oldRecv = ock::ubs::LibcApi::recv_ptr;
    ock::ubs::LibcApi::recv_ptr = MockRecvEagain;
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFree));
    umq_buf_t qbuf{};
    umq_buf_pro_t pro{};
    char payload[] = "no-headroom";
    memset(&pro, 0, sizeof(pro));
    memcpy(qbuf.qbuf_ext, &pro, sizeof(pro));
    qbuf.buf_data = payload;
    qbuf.data_size = sizeof(payload);
    qbuf.headroom_size = 0;
    ASSERT_EQ(socket->rxQueue->Enqueue(&qbuf), ock::ubs::umq::UmqBufferReceiveQueue::OpResult::OK);
    ubs_segment_t segment{};
    ubs_data_list_t list{&segment, 1};
    EXPECT_EQ(ubs_poll(57, &list), -1);
    EXPECT_EQ(errno, EAGAIN);
    ock::ubs::LibcApi::recv_ptr = oldRecv;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(UbsocketDataTest, PollUnknownFdReturnsEpipe)
{
    ubs_data_list_t list{};
    ubs_segment_t segment{};
    list.segments = &segment;
    list.nsegs = 1;
    errno = 0;
    EXPECT_EQ(ubs_poll(9999, &list), -1);
    EXPECT_EQ(errno, EPIPE);
    EXPECT_EQ(list.nsegs, 0U);
}

} // namespace
