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
#ifndef UBS_COMM_UBSOCKET_FIXED_MIN_HEAP_H
#define UBS_COMM_UBSOCKET_FIXED_MIN_HEAP_H

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {

/**
 * fast heap
 * @tparam T element type of Heap
 * @tparam Compare Comparator for heap
 */
template <typename T, typename Compare>
class FastHeap {
public:
    explicit FastHeap(size_t initial_capacity, size_t max_capacity = MAX_CAPACITY)
        : m_size(0),
          m_capacity(initial_capacity),
          m_max_capacity(max_capacity)
    {
        if (m_capacity < MIN_CAPACITY) {
            m_capacity = MIN_CAPACITY;
        }
        m_heap = InitHeap(m_capacity);
    }

    ~FastHeap()
    {
        if (m_heap) {
            free(m_heap);
            m_heap = nullptr;
        }
    }

    FastHeap(const FastHeap &) = delete;

    FastHeap &operator=(const FastHeap &) = delete;

    FastHeap(FastHeap &&other) noexcept : m_heap(other.m_heap), m_size(other.m_size), m_capacity(other.m_capacity)
    {
        other.m_heap = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    FastHeap &operator=(FastHeap &&other) noexcept
    {
        if (this != &other) {
            if (m_heap) {
                free(m_heap);
            }
            m_heap = other.m_heap;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_heap = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    inline bool IsEmpty() const noexcept
    {
        return m_size == 0;
    }

    inline size_t Size() const noexcept
    {
        return m_size;
    }

    inline const T &Top() const noexcept
    {
        return m_heap[0];
    }

    inline void clear() noexcept
    {
        m_size = 0;
    }

    int Push(const T &item)
    {
        return PushImpl(item);
    }

    int Push(T &&item)
    {
        return PushImpl(std::move(item));
    }

    void Pop() noexcept
    {
        if (m_heap == nullptr) {
            UBS_VLOG_ERR("Failed to pop heap, reason: heap is null\n");
            return;
        }
        if (m_size == 0) {
            return;
        }
        m_size--;
        if (m_size > 0) {
            size_t parent = 0;
            size_t child = 1;
            T target = std::move(m_heap[m_size]);

            while (child < m_size) {
                if (child + 1 < m_size && m_comp(m_heap[child + 1], m_heap[child])) {
                    child++;
                }
                if (m_comp(target, m_heap[child])) {
                    break;
                }
                m_heap[parent] = std::move(m_heap[child]);
                parent = child;
                child = (parent << 1) + 1; // 左节点：2 * i + 1
            }
            m_heap[parent] = std::move(target);
        }
    }

    template <typename Predicate>
    inline bool Contains(Predicate &&pred) const noexcept
    {
        if (m_size == 0 || m_heap == nullptr) {
            return false;
        }
        // 连续内存线性扫描， Cache Line缓存加速
        for (size_t i = 0; i < m_size; ++i) {
            if (pred(m_heap[i])) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr uint32_t MIN_CAPACITY = 4;
    static constexpr uint32_t MAX_CAPACITY = 0x3FFFFFFF;
    static constexpr uint32_t RESERVE_FACTOR = 2;

    static inline T *InitHeap(size_t capacity)
    {
        size_t pageSize = sysconf(_SC_PAGESIZE);
        size_t len = (capacity + 1) * sizeof(T);
        void *ptr = nullptr;
        if (posix_memalign(&ptr, pageSize, len) != 0) {
            UBS_VLOG_ERR("Failed to init heap caused by memalign exception, pageSize: %zu, headLen: %zu, errno: %d\n",
                         pageSize, len, errno);
            return nullptr;
        }
        return static_cast<T *>(ptr);
    }

    template <typename U>
    int PushImpl(U &&item)
    {
        if (m_heap == nullptr) {
            UBS_VLOG_ERR("Failed to push element into heap, reason: heap is null\n");
            return UBS_ERROR;
        }
        if (__builtin_expect(m_size >= m_capacity, 0)) {
            if (Reserve((m_capacity - 1) * RESERVE_FACTOR) != 0) {
                UBS_VLOG_ERR("Failed to push element to heap, reason: heap is full and resize failed.\n");
                return UBS_ERROR;
            }
        }
        size_t child = m_size++;
        while (child > 0) {
            size_t parent = (child - 1) >> 1;
            if (!m_comp(item, m_heap[parent])) {
                break;
            }
            m_heap[child] = std::move(m_heap[parent]);
            child = parent;
        }
        m_heap[child] = std::forward<U>(item);
        return UBS_OK;
    }

    int Reserve(size_t new_capacity)
    {
        uint32_t reserve_capacity = (new_capacity > MAX_CAPACITY) ? MAX_CAPACITY : new_capacity;
        if (reserve_capacity == m_capacity) {
            UBS_VLOG_ERR("Failed to resize heap, new capacity is equal to old capacity: %d\n", reserve_capacity);
            return UBS_ERROR;
        }

        T *new_heap = InitHeap(reserve_capacity);
        if (m_size > 0) {
            std::move(m_heap, m_heap + m_size, new_heap);
        }
        free(m_heap);
        m_heap = new_heap;
        m_capacity = reserve_capacity;
        return UBS_OK;
    }

    int GetMaxCapacity()
    {
        if (m_max_capacity != 0 && m_max_capacity <= MAX_CAPACITY) {
            return m_max_capacity;
        } else {
            return MAX_CAPACITY;
        }
    }

    uint32_t m_max_capacity;
    T *m_heap;
    size_t m_size;
    size_t m_capacity;
    Compare m_comp;
};

} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_FIXED_MIN_HEAP_H