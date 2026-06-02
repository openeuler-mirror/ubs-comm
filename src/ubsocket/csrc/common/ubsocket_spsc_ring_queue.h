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
#ifndef UBS_COMM_UBSOCKET_SPSC_RING_QUEUE_H
#define UBS_COMM_UBSOCKET_SPSC_RING_QUEUE_H

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ock {
namespace ubs {
template <class T>
class SPSCRingQueue {
public:
    explicit SPSCRingQueue(uint64_t capacity) : buffer_(capacity), capacity_(capacity), mask_(capacity - 1UL)
    {
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument(std::string("input capacity invalid: ").append(std::to_string(capacity)));
        }
    }

    bool Push(const T &item) noexcept
    {
        auto rd = commit_read_.load(std::memory_order_acquire);
        if (write_index_ - rd >= capacity_) {
            return false;
        }

        buffer_[write_index_ & mask_] = item;
        write_index_++;
        commit_write_.store(write_index_, std::memory_order_release);
        return true;
    }

    template <typename InputIt>
    uint64_t MultiPush(InputIt begin, InputIt end) noexcept
    {
        auto count = std::distance(begin, end);
        if (count == 0) {
            return 0;
        }

        auto rd = commit_read_.load(std::memory_order_acquire);
        auto available = buffer_.size() - (write_index_ - rd);
        auto to_write = std::min<uint64_t>(count, available);
        if (to_write == 0) {
            return 0;
        }

        for (auto i = 0; i < to_write; ++i) {
            buffer_[(write_index_ + i) & mask_] = *begin++;
        }

        write_index_ += to_write;
        commit_write_.store(write_index_, std::memory_order_release);
        return to_write;
    }

    bool Pop(T &item) noexcept
    {
        auto w = commit_write_.load(std::memory_order_acquire);
        if (read_index_ >= w) {
            return false;
        }

        item = std::move(buffer_[read_index_ & mask_]);
        read_index_++;
        commit_read_.store(read_index_, std::memory_order_release);
        return true;
    }

    template <typename OutputIt>
    uint64_t MultiPop(OutputIt output, uint64_t max_count) noexcept
    {
        auto wr = commit_write_.load(std::memory_order_acquire);
        auto available = wr - read_index_;
        auto to_read = std::min(max_count, available);
        if (to_read == 0) {
            return 0;
        }

        for (auto i = 0UL; i < to_read; ++i) {
            *output++ = std::move(buffer_[(read_index_ + i) & mask_]);
        }

        read_index_ += to_read;
        commit_read_.store(read_index_, std::memory_order_release);
        return to_read;
    }

    [[nodiscard]] uint64_t Size() const noexcept
    {
        auto wr = commit_write_.load(std::memory_order_acquire);
        auto rd = commit_read_.load(std::memory_order_acquire);
        return wr - rd;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return Size() == 0;
    }

private:
    std::vector<T> buffer_;
    alignas(64) uint64_t write_index_{0};
    alignas(64) uint64_t read_index_{0};
    std::atomic<uint64_t> commit_write_{0};
    std::atomic<uint64_t> commit_read_{0};
    const uint64_t capacity_;
    const uint64_t mask_;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SPSC_RING_QUEUE_H