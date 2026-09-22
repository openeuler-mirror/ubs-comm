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
#include "ubsocket_lock.h"
#include "ubsocket_logger.h"
#include "ubsocket_ref.h"

#include <sys/resource.h>

#include <sys/mman.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <type_traits>

#include <functional>
#include <memory>
#include <vector>

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
        /* 零页惰性分配：表直接 mmap 匿名内存，由内核零页支撑，只有被登记过的 fd 所在的页才进 RSS。
         * new[]() 会把整表零填充一遍，nofile=1M 时两张表白白摸脏 16MB（issue #44/#26）；
         * 不用 calloc 是因为 tcmalloc 的 calloc 总是 memset，惰性只有 mmap 能保证。
         * std::atomic<T*> 平凡可析构，全零字节就是合法的 nullptr 表示。 */
        static_assert(std::is_trivially_destructible<std::atomic<T *>>::value, "ArraySet slot must be trivial");
        const size_t bytes = static_cast<size_t>(capacity_) * sizeof(std::atomic<T *>);
        void *raw = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw == MAP_FAILED) {
            UBS_VLOG_ERR("ArraySet Init mmap(%zu bytes) failed, errno: %d\n", bytes, errno);
            capacity_ = 0;
            return -1;
        }
        set_obj_ = std::unique_ptr<std::atomic<T *>[], UnmapDeleter>(static_cast<std::atomic<T *> *>(raw), UnmapDeleter{bytes});
        deferred_mtx_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        if (deferred_mtx_ == nullptr) {
            UBS_VLOG_ERR("ArraySet Init failed to create deferred_mtx_\n");
            set_obj_.reset();
            capacity_ = 0;
            return -1;
        }
        UBS_VLOG_INFO("ArraySet Init capacity: %u (rlim_cur: %llu, hard_limit: %u)\n", capacity_,
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
        if (old_item != nullptr) {
            old_item->IncreaseRef();
            EnqueueDeferred(old_item);
        }
        return Ref<T>(old_item, Ref<T>::adopt_ref);
    }

    Ref<T> RemoveItem(int idx)
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return Ref<T>();
        }
        T *item = set_obj_[idx].exchange(nullptr, std::memory_order_acq_rel);
        if (item != nullptr) {
            item->IncreaseRef();
            EnqueueDeferred(item);
        }
        return Ref<T>(item, Ref<T>::adopt_ref);
    }

    void ReleaseAll()
    {
        for (uint32_t i = 0; i < capacity_; ++i) {
            T *old_item = set_obj_[i].exchange(nullptr, std::memory_order_acq_rel);
            if (old_item != nullptr) {
                old_item->DecreaseRef();
            }
        }
        DrainDeferredRelease();
    }

    /* Releases every parked reference; returns how many. */
    size_t DrainDeferredRelease()
    {
        if (deferred_mtx_ == nullptr) {
            return 0;
        }
        std::vector<T *> batch;
        {
            Locker lk(deferred_mtx_);
            batch.swap(deferred_pending_);
            lk.Unlock();
        }
        for (auto *p : batch) {
            p->DecreaseRef();
        }
        return batch.size();
    }

    size_t DeferredCount()
    {
        if (deferred_mtx_ == nullptr) {
            return 0;
        }
        Locker lk(deferred_mtx_);
        size_t n = deferred_pending_.size();
        lk.Unlock();
        return n;
    }

    /* 延迟释放入队通知。表是通用容器，不认识 reaper：由监听方（TxCqePoller）在
     * Start() 装上、Stop() 摘掉。没有监听者时入队照旧，只是没人被叫醒。
     * 安装顺序 ctx 先于 fn，卸载时先清 fn，生产者侧读到 fn 非空即可安全调用。 */
    using DeferredNotifier = void (*)(void *);
    void SetDeferredNotifier(DeferredNotifier fn, void *ctx)
    {
        if (fn == nullptr) {
            notifier_fn_.store(nullptr, std::memory_order_release);
            notifier_ctx_.store(nullptr, std::memory_order_release);
            return;
        }
        notifier_ctx_.store(ctx, std::memory_order_release);
        notifier_fn_.store(fn, std::memory_order_release);
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

    /* 容量随 RLIMIT_NOFILE 软限取，但不超过此硬上限。旧值 65536 是隐蔽天花板：
     * 全互联 4W 出 + 4W 入 的进程把 nofile 抬到 1M 后，fd>=65536 的 socket 仍登记不进来，
     * 首次 ubs_poll 才以 EPIPE 报出（issue #44）。1M 与 DataPlaneTable(MAX_PAGES*PAGE_SIZE) 对齐；
     * 每槽 8B，rlimit=1M 时单表 8MB，默认 65536 时 512KB 不变。 */
    static constexpr uint32_t FD_CAPACITY_HARD_LIMIT = 1U << 20;
    uint32_t capacity_ = 0;
    struct UnmapDeleter {
        size_t bytes = 0;
        void operator()(std::atomic<T *> *p) const noexcept
        {
            if (p != nullptr && bytes != 0) {
                (void)munmap(p, bytes);
            }
        }
    };
    std::unique_ptr<std::atomic<T *>[], UnmapDeleter> set_obj_;

    void EnqueueDeferred(T *p)
    {
        if (deferred_mtx_ == nullptr) {
            return;
        }
        Locker lk(deferred_mtx_);
        deferred_pending_.push_back(p);
        lk.Unlock();
        /* 叫醒排空者。以前这个队列只在 RetireSweep 末尾排空，而 sweep 只由"成功链路
         * 的 close"驱动：建链失败的 socket 自己摘表、不经 Retire，全进程没有一条成功
         * 链路 close 时队列永不排空——每个失败 socket 连同它预建的 umq/id 一起滞留
         * （issue #49，3772 全互联 umq id 耗尽的机制）。 */
        DeferredNotifier fn = notifier_fn_.load(std::memory_order_acquire);
        if (fn != nullptr) {
            fn(notifier_ctx_.load(std::memory_order_acquire));
        }
    }

    u_mutex_t *deferred_mtx_ = nullptr;
    std::vector<T *> deferred_pending_;
    std::atomic<DeferredNotifier> notifier_fn_{nullptr};
    std::atomic<void *> notifier_ctx_{nullptr};
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SET_H
