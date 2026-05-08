/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-04-23
 * Note:
 * History: 2026-04-23
 */

#ifndef BRPC_SPSC_RING_QUEUE_H
#define BRPC_SPSC_RING_QUEUE_H

#include <cstdint>
#include <atomic>
#include <vector>
#include <string>

namespace Brpc {
template <class T> class SPSCRingQueue {
public:
    explicit SPSCRingQueue(uint64_t capacity) : m_buffer(capacity), m_capacity(capacity), m_mask(capacity - 1UL)
    {
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument(std::string("input capacity invalid: ").append(std::to_string(capacity)));
        }
    }

    bool Push(const T &item) noexcept
    {
        auto rd = m_commit_read.load(std::memory_order_acquire);
        if (m_write_index - rd >= m_capacity) {
            return false;
        }

        m_buffer[m_write_index & m_mask] = item;
        m_write_index++;
        m_commit_write.store(m_write_index, std::memory_order_release);
        return true;
    }

    template <typename InputIt> uint64_t MultiPush(InputIt begin, InputIt end) noexcept
    {
        auto count = std::distance(begin, end);
        if (count == 0) {
            return 0;
        }

        auto rd = m_commit_read.load(std::memory_order_acquire);
        auto available = m_buffer.size() - (m_write_index - rd);
        auto to_write = std::min<uint64_t>(count, available);
        if (to_write == 0) {
            return 0;
        }

        for (auto i = 0; i < to_write; ++i) {
            m_buffer[(m_write_index + i) & m_mask] = *begin++;
        }

        m_write_index += to_write;
        m_commit_write.store(m_write_index, std::memory_order_release);
        return to_write;
    }

    bool Pop(T &item) noexcept
    {
        auto w = m_commit_write.load(std::memory_order_acquire);
        if (m_read_index >= w) {
            return false;
        }

        item = std::move(m_buffer[m_read_index & m_mask]);
        m_read_index++;
        m_commit_read.store(m_read_index, std::memory_order_release);
        return true;
    }

    template <typename OutputIt> uint64_t MultiPop(OutputIt output, uint64_t max_count) noexcept
    {
        auto wr = m_commit_write.load(std::memory_order_acquire);
        auto available = wr - m_read_index;
        auto to_read = std::min(max_count, available);
        if (to_read == 0) {
            return 0;
        }

        for (auto i = 0UL; i < to_read; ++i) {
            *output++ = std::move(m_buffer[(m_read_index + i) & m_mask]);
        }

        m_read_index += to_read;
        m_commit_read.store(m_read_index, std::memory_order_release);
        return to_read;
    }

    [[nodiscard]] uint64_t Size() const noexcept
    {
        auto wr = m_commit_write.load(std::memory_order_acquire);
        auto rd = m_commit_read.load(std::memory_order_acquire);
        return wr - rd;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return Size() == 0;
    }

private:
    std::vector<T> m_buffer;
    alignas(64) uint64_t m_write_index{ 0 };
    alignas(64) uint64_t m_read_index{ 0 };
    std::atomic<uint64_t> m_commit_write{ 0 };
    std::atomic<uint64_t> m_commit_read{ 0 };
    const uint64_t m_capacity;
    const uint64_t m_mask;
};
}

#endif // BRPC_SPSC_RING_QUEUE_H
