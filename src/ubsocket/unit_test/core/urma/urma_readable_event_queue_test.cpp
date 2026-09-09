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

#include "urma_readable_event_queue.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
struct epoll_event MakeEvent(uint64_t user_data)
{
    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.u64 = user_data;
    return ev;
}
} // namespace

TEST(ReadableEventQueueTest, NotInitialized_RejectsRecordAndDrain)
{
    ReadableEventQueue queue;
    EXPECT_FALSE(queue.TryRecord(1, MakeEvent(0x1)));
    struct epoll_event out[2]{};
    EXPECT_EQ(0u, queue.Drain(out, 2, [](int) { return true; }));
    EXPECT_EQ(0u, queue.Capacity());
}

TEST(ReadableEventQueueTest, Init_SetsCapacity_Idempotent)
{
    ReadableEventQueue queue;
    EXPECT_EQ(UBS_OK, queue.Init(4));
    EXPECT_EQ(4u, queue.Capacity());
    EXPECT_EQ(0u, queue.Size());
    EXPECT_EQ(0u, queue.PendingCount());

    EXPECT_EQ(UBS_OK, queue.Init(8)); /* second init is a no-op */
    EXPECT_EQ(4u, queue.Capacity());
}

TEST(ReadableEventQueueTest, Record_DedupsPendingFd)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(4));

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    EXPECT_EQ(1u, queue.Size());
    EXPECT_EQ(1u, queue.PendingCount());
    EXPECT_TRUE(queue.IsPending(1));

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x2))); /* same fd not yet returned: de-dup */
    EXPECT_EQ(1u, queue.Size());
    EXPECT_EQ(1u, queue.PendingCount());
}

TEST(ReadableEventQueueTest, Drain_ReturnsInOrderUpToMax)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(8));
    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    ASSERT_TRUE(queue.TryRecord(2, MakeEvent(0x2)));
    ASSERT_TRUE(queue.TryRecord(3, MakeEvent(0x3)));

    struct epoll_event out[2]{};
    uint32_t got = queue.Drain(out, 2, [](int) { return true; });
    EXPECT_EQ(2u, got);
    EXPECT_EQ(static_cast<uint64_t>(0x1), out[0].data.u64);
    EXPECT_EQ(static_cast<uint64_t>(0x2), out[1].data.u64);
    EXPECT_EQ(1u, queue.Size());
    EXPECT_EQ(1u, queue.PendingCount());

    struct epoll_event rest[2]{};
    got = queue.Drain(rest, 2, [](int) { return true; });
    EXPECT_EQ(1u, got);
    EXPECT_EQ(static_cast<uint64_t>(0x3), rest[0].data.u64);
    EXPECT_EQ(0u, queue.Size());
    EXPECT_EQ(0u, queue.PendingCount());
    EXPECT_FALSE(queue.IsPending(3));
}

TEST(ReadableEventQueueTest, Drain_DropsStaleFd)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(8));
    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    ASSERT_TRUE(queue.TryRecord(2, MakeEvent(0x2)));

    struct epoll_event out[4]{};
    uint32_t got = queue.Drain(out, 4, [](int fd) { return fd == 2; }); /* fd 1 unregistered: stale */
    EXPECT_EQ(1u, got);
    EXPECT_EQ(static_cast<uint64_t>(0x2), out[0].data.u64);
    EXPECT_EQ(0u, queue.Size());
    EXPECT_EQ(0u, queue.PendingCount());
    EXPECT_FALSE(queue.IsPending(1));
}

TEST(ReadableEventQueueTest, FullRing_NewFdRejected_DedupOk)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(2)); /* one slot is reserved to distinguish empty/full */

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    EXPECT_FALSE(queue.TryRecord(2, MakeEvent(0x2))); /* full: caller should fall back to eventfd */
    EXPECT_TRUE(queue.TryRecord(1, MakeEvent(0x9)));  /* already pending: de-dup, still accepted */
    EXPECT_EQ(1u, queue.Size());
}

TEST(ReadableEventQueueTest, WrapAround_ReusesSlots)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(3)); /* two usable slots */

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    ASSERT_TRUE(queue.TryRecord(2, MakeEvent(0x2)));
    struct epoll_event out[4]{};
    EXPECT_EQ(2u, queue.Drain(out, 4, [](int) { return true; }));

    ASSERT_TRUE(queue.TryRecord(3, MakeEvent(0x3)));
    ASSERT_TRUE(queue.TryRecord(4, MakeEvent(0x4)));
    EXPECT_EQ(2u, queue.Drain(out, 4, [](int) { return true; }));
    EXPECT_EQ(static_cast<uint64_t>(0x3), out[0].data.u64);
    EXPECT_EQ(static_cast<uint64_t>(0x4), out[1].data.u64);
}

TEST(ReadableEventQueueTest, ClearPending_DropsBufferedEntry_AllowsNewRecord)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(4));

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    EXPECT_EQ(1u, queue.Size());
    EXPECT_TRUE(queue.IsPending(1));

    /* kernel eventfd path delivered fd 1: buffered entry is dropped, no duplicate delivery */
    queue.ClearPending(1);
    EXPECT_FALSE(queue.IsPending(1));
    EXPECT_EQ(0u, queue.Size());

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x2))); /* a fresh notification is accepted again */
    EXPECT_EQ(1u, queue.Size());
    EXPECT_EQ(1u, queue.PendingCount());

    struct epoll_event out[4]{};
    EXPECT_EQ(1u, queue.Drain(out, 4, [](int) { return true; }));
    EXPECT_EQ(static_cast<uint64_t>(0x2), out[0].data.u64);
    EXPECT_EQ(0u, queue.Size());
    EXPECT_EQ(0u, queue.PendingCount());
}

TEST(ReadableEventQueueTest, ForgetFd_ClearsMark_DrainDropsStaleEntry)
{
    ReadableEventQueue queue;
    ASSERT_EQ(UBS_OK, queue.Init(4));

    ASSERT_TRUE(queue.TryRecord(1, MakeEvent(0x1)));
    ASSERT_TRUE(queue.TryRecord(2, MakeEvent(0x2)));

    queue.ForgetFd(1); /* EPOLL_CTL_DEL: clears the mark; the entry is dropped by Drain as stale */
    EXPECT_FALSE(queue.IsPending(1));
    EXPECT_TRUE(queue.IsPending(2));
    EXPECT_EQ(2u, queue.Size());

    /* fd 1 is no longer registered: the drain validator drops its entry, only fd 2 is returned */
    struct epoll_event out[4]{};
    uint32_t got = queue.Drain(out, 4, [](int fd) { return fd == 2; });
    EXPECT_EQ(1u, got);
    EXPECT_EQ(static_cast<uint64_t>(0x2), out[0].data.u64);
    EXPECT_EQ(0u, queue.Size());
    EXPECT_EQ(0u, queue.PendingCount());
}
