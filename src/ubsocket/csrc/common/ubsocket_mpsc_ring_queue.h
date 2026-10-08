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
#ifndef UBS_COMM_UBSOCKET_MPSC_RING_QUEUE_H
#define UBS_COMM_UBSOCKET_MPSC_RING_QUEUE_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ock {
namespace ubs {
template <class T>
class MPSCRingQueue {
public:
    explicit MPSCRingQueue(uint64_t capacity) : buffer_(capacity), capacity_(capacity), mask_(capacity - 1UL)
    {
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument(std::string("input capacity invalid: ").append(std::to_string(capacity)));
        }
    }

    bool Push(const T &item) noexcept
    {
        return PushImpl(item);
    }

    bool Push(T &&item) noexcept
    {
        return PushImpl(std::move(item));
    }

    bool Pop(T &item) noexcept
    {
        auto cons_head = __atomic_load_n(&cons_head_, __ATOMIC_RELAXED);
        // 用当前的 prod_tail_ 判断是否有可用数据
        auto prod_tail = __atomic_load_n(&prod_tail_, __ATOMIC_ACQUIRE);

        if (cons_head == prod_tail) {
            return false; // 队列为空 或 生产者预留了位置但没完成写入
        }

        auto idx = cons_head & mask_;
        item = std::move(buffer_[idx]);
        __atomic_store_n(&cons_head_, cons_head + 1, __ATOMIC_RELEASE);
        return true;
    }

    template <typename OutputIt>
    uint64_t MultiPop(OutputIt output, uint64_t max_count) noexcept
    {
        if (max_count == 0) {
            return 0;
        }

        auto cons_head = __atomic_load_n(&cons_head_, __ATOMIC_RELAXED);
        auto prod_tail = __atomic_load_n(&prod_tail_, __ATOMIC_ACQUIRE);

        if (cons_head == prod_tail) {
            return 0;
        }

        uint64_t available = prod_tail - cons_head;
        uint64_t popped_count = (available < max_count) ? available : max_count;

        for (uint64_t i = 0; i < popped_count; ++i) {
            auto idx = (cons_head + i) & mask_;
            *output++ = std::move(buffer_[idx]);
        }

        __atomic_store_n(&cons_head_, cons_head + popped_count, __ATOMIC_RELEASE);
        return popped_count;
    }

    [[nodiscard]] uint64_t Size() const noexcept
    {
        auto cons = __atomic_load_n(&cons_head_, __ATOMIC_RELAXED);
        auto prod = __atomic_load_n(&prod_tail_, __ATOMIC_RELAXED);
        return (prod > cons) ? (prod - cons) : 0;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return __atomic_load_n(&cons_head_, __ATOMIC_RELAXED) == __atomic_load_n(&prod_tail_, __ATOMIC_RELAXED);
    }

private:
    template <typename U>
    bool PushImpl(U &&item) noexcept
    {
        uint64_t current_head;
        do {
            auto cons_head = __atomic_load_n(&cons_head_, __ATOMIC_ACQUIRE);
            current_head = __atomic_load_n(&prod_head_, __ATOMIC_RELAXED);
            if (current_head - cons_head >= capacity_) {
                return false;
            }
            // CAS 抢占
        } while (!__atomic_compare_exchange_n(&prod_head_, &current_head, current_head + 1, true, __ATOMIC_RELAXED,
                                              __ATOMIC_RELAXED));
        buffer_[current_head & mask_] = std::forward<U>(item);

        // 严格按照抢占顺序更新 prod_tail_ (Commit)，如果前面的预留位置不是 prod_tail_，说明前面的生产者还没写完
        uint32_t counter = 0;
        while (__atomic_load_n(&prod_tail_, __ATOMIC_ACQUIRE) != current_head) {
            if (counter < 64) {
#ifdef __x86_64__
                asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#endif
                counter++;
            } else {
                // 让出当前 CPU 时间片给同优先级线程
                std::this_thread::yield();
            }
        }

        __atomic_store_n(&prod_tail_, current_head + 1, __ATOMIC_RELEASE);
        return true;
    }

private:
    std::vector<T> buffer_;

    alignas(64) uint64_t prod_head_{0};
    alignas(64) uint64_t prod_tail_{0};
    alignas(64) uint64_t cons_head_{0};

    const uint64_t capacity_;
    const uint64_t mask_;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_MPSC_RING_QUEUE_H