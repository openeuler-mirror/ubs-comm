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
#include "urma_readable_event_queue.h"

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace urma {

Result ReadableEventQueue::Init(uint32_t capacity) noexcept
{
    if (capacity_ != 0) {
        return UBS_OK;
    }
    try {
        ring_.resize(capacity);
    } catch (...) {
        return UBS_ERROR;
    }
    capacity_ = capacity;
    head_ = 0;
    tail_ = 0;
    return UBS_OK;
}

bool ReadableEventQueue::TryRecord(int fd, const struct epoll_event &evt) noexcept
{
    if (UNLIKELY(capacity_ == 0)) {
        return false;
    }
    /* Already recorded and not yet returned: de-dup. */
    if (pending_.count(fd) != 0) {
        return true;
    }
    auto next_tail = (tail_ + 1) % capacity_;
    if (UNLIKELY(next_tail == head_)) {
        return false; /* ring full */
    }
    ring_[tail_].fd = fd;
    ring_[tail_].event = evt;
    tail_ = next_tail;
    pending_.insert(fd);
    return true;
}

uint32_t ReadableEventQueue::Drain(struct epoll_event *out, uint32_t maxevents,
                                   const std::function<bool(int)> &is_registered) noexcept
{
    if (UNLIKELY(capacity_ == 0 || head_ == tail_)) {
        return 0;
    }
    auto left = (tail_ + capacity_ - head_) % capacity_;
    uint32_t placed = 0;
    while (left > 0 && placed < maxevents) {
        auto &entry = ring_[head_];
        head_ = (head_ + 1) % capacity_;
        left--;
        pending_.erase(entry.fd);
        if (UNLIKELY(!is_registered(entry.fd))) {
            continue; /* stale: fd no longer registered on this epoll, drop */
        }
        out[placed++] = entry.event;
    }
    return placed;
}

void ReadableEventQueue::ClearPending(int fd) noexcept
{
    RemoveBuffered(fd);
}

void ReadableEventQueue::ForgetFd(int fd) noexcept
{
    /*
     * epoll_ctl may run on a different thread than the epoll_wait thread that owns the ring, so
     * this only clears the pending mark here. The buffered entry is left for Drain, which drops
     * it through the stale (not-registered) check once the fd has been unregistered.
     */
    pending_.erase(fd);
}

void ReadableEventQueue::RemoveBuffered(int fd) noexcept
{
    if (UNLIKELY(capacity_ == 0 || pending_.count(fd) == 0)) {
        return; /* dedup invariant: no pending mark means nothing is buffered for this fd */
    }

    uint32_t written = head_;
    auto total = Size();
    for (auto i = 0U; i < total; i++) {
        auto idx = (head_ + i) % capacity_;
        if (ring_[idx].fd == fd) {
            pending_.erase(fd);
            continue;
        }
        if (written != idx) {
            ring_[written] = ring_[idx];
        }
        written = (written + 1) % capacity_;
    }
    tail_ = written;
}

bool ReadableEventQueue::IsPending(int fd) const noexcept
{
    return pending_.count(fd) != 0;
}

uint32_t ReadableEventQueue::PendingCount() const noexcept
{
    return static_cast<uint32_t>(pending_.size());
}

uint32_t ReadableEventQueue::Size() const noexcept
{
    if (capacity_ == 0) {
        return 0;
    }
    return (tail_ + capacity_ - head_) % capacity_;
}

uint32_t ReadableEventQueue::Capacity() const noexcept
{
    return capacity_;
}

} // namespace urma
} // namespace ubs
} // namespace ock
