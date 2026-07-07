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
#ifndef UBS_COMM_UBSOCKET_SET_H
#define UBS_COMM_UBSOCKET_SET_H

#include "ubsocket_defines.h"
#include "ubsocket_leaky_singleton.h"
#include "ubsocket_logger.h"
#include "ubsocket_ref.h"

#include <sys/resource.h>

#include <functional>
#include <memory>

namespace ock {
namespace ubs {

template <typename T>
class ArraySet : public LeakySingleton<ArraySet<T>> {
    friend LeakySingleton<ArraySet>;

public:
    static ArraySet &GetInstance()
    {
        return LeakySingleton<ArraySet>::Instance();
    }

    int Init()
    {
        if (capacity_ != 0) {
            UBS_VLOG_WARN("ArraySet already initialized, capacity: %u\n", capacity_);
            return 0;
        }
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
            UBS_VLOG_ERR("ArraySet Init getrlimit failed, errno: %d\n", errno);
            return -1;
        }
        capacity_ = std::min(static_cast<uint32_t>(rl.rlim_cur), static_cast<uint32_t>(FD_CAPACITY_HARD_LIMIT));
        if (capacity_ == 0) {
            UBS_VLOG_ERR("ArraySet Init invalid capacity: 0 (rlim_cur: %llu)\n",
                         static_cast<unsigned long long>(rl.rlim_cur));
            return -1;
        }
        set_obj_.reset(new std::atomic<T *>[capacity_]());
        UBS_VLOG_DEBUG("ArraySet Init capacity: %u (rlim_cur: %llu, hard_limit: %u)\n", capacity_,
                       static_cast<unsigned long long>(rl.rlim_cur), static_cast<uint32_t>(FD_CAPACITY_HARD_LIMIT));
        return 0;
    }

    ALWAYS_INLINE Ref<T> GetItem(int idx)
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return Ref<T>();
        }
        return Ref<T>(set_obj_[idx].load(std::memory_order_acquire));
    }

    Ref<T> OverrideItem(int idx, T *new_item)
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return Ref<T>();
        }
        if (new_item != nullptr) {
            new_item->IncreaseRef();
        }
        T *old_item = set_obj_[idx].exchange(new_item, std::memory_order_acq_rel);
        Ref<T> ref(old_item);
        if (old_item != nullptr) {
            old_item->DecreaseRef();
        }
        return ref;
    }

    Ref<T> RemoveItem(int idx)
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return Ref<T>();
        }
        T *item = set_obj_[idx].exchange(nullptr, std::memory_order_acq_rel);
        return Ref<T>(item);
    }

    void ReleaseAll()
    {
        for (uint32_t i = 0; i < capacity_; ++i) {
            T *old_item = set_obj_[i].exchange(nullptr, std::memory_order_acq_rel);
            if (old_item != nullptr) {
                old_item->DecreaseRef();
            }
        }
    }

    void ForEach(const std::function<void(int fd, T *)> &callback)
    {
        for (uint32_t i = 0; i < capacity_; ++i) {
            Ref<T> ref = GetItem(static_cast<int>(i));
            T *p = ref.Get();
            if (p != nullptr) {
                callback(static_cast<int>(i), p);
            }
        }
    }

    size_t Size()
    {
        size_t count = 0;
        for (uint32_t i = 0; i < capacity_; ++i) {
            if (set_obj_[i].load(std::memory_order_acquire) != nullptr) {
                count++;
            }
        }
        return count;
    }

    uint32_t Capacity() const
    {
        return capacity_;
    }

private:
    ArraySet() = default;

    ~ArraySet()
    {
        ReleaseAll();
    }

    ArraySet(const ArraySet &) = delete;
    ArraySet &operator=(const ArraySet &) = delete;

    static constexpr uint32_t FD_CAPACITY_HARD_LIMIT = 65536;
    uint32_t capacity_ = 0;
    std::unique_ptr<std::atomic<T *>[]> set_obj_;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SET_H
