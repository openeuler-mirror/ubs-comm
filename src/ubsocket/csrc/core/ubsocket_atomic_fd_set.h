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

#ifndef UBS_COMM_UBSOCKET_ATOMIC_FD_SET_H
#define UBS_COMM_UBSOCKET_ATOMIC_FD_SET_H

#include <sys/resource.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <new>

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
/**
 * @brief Reference-counted atomic fd set. Packs the object address (48 bits)
 *        and ref_count (16 bits) into a single uint64_t, with atomic operations
 *        for thread-safe shared ownership.
 *
 * The internal union Place packs obj_addr (48bit) and ref_count (16bit) into
 * a single uint64_t. All reads/writes use __atomic_* builtins, no mutex needed.
 *
 * @tparam T  Managed object type.
 */
template <typename T>
class AtomicFd final {
    /**
     * @brief Internal representation: 64-bit integer, low 48 bits = obj_addr,
     *        high 16 bits = ref_count.
     */
    union Place {
        uint64_t value; ///< Raw 64-bit value.
        struct {
            uint64_t obj_addr : 48;  ///< Object address (low 48 bits).
            uint64_t ref_count : 16; ///< Reference count (high 16 bits).
        };

        /** @brief Default constructor, zero-initialized. */
        explicit Place(const uint64_t v = 0) noexcept : value{v} {}

        /** @brief Construct from object pointer, ref_count starts at 1. */
        explicit Place(void *addr) noexcept
            : obj_addr{static_cast<uint64_t>(reinterpret_cast<uintptr_t>(addr))},
              ref_count{1}
        {
        }
    };

public:
    /** @brief Construct an empty object. */
    AtomicFd() noexcept : raw_placement_{nullptr} {}

    /**
     * @brief Check whether this object manages a placement.
     * @retval true   Object manages a non-null placement.
     * @retval false  Object is empty (placement is null).
     */
    explicit operator bool() const noexcept
    {
        return raw_placement_ != nullptr;
    }

    /**
     * @brief Place a new object at the given location, ref_count = 1.
     * @param placement  uint64_t-aligned storage address.
     * @param new_obj    Pointer to the managed object (ownership transferred).
     */
    AtomicFd(uint64_t *placement, T *new_obj) noexcept : raw_placement_{placement}
    {
        if (UNLIKELY(placement == nullptr)) {
            return;
        }
        Place place{new_obj};
        __atomic_store_n(raw_placement_, place.value, __ATOMIC_RELEASE);
    }

    /**
     * @brief Copy constructor, increments ref_count.
     * @note  If ref_count is already 0 (object released), constructs empty.
     */
    AtomicFd(const AtomicFd<T> &other) noexcept : raw_placement_{other.raw_placement_}
    {
        if (UNLIKELY(raw_placement_ == nullptr)) {
            return;
        }

        Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
        if (UNLIKELY(curr.ref_count == 0)) {
            raw_placement_ = nullptr;
            return;
        }

        Place next{curr.value};
        ++next.ref_count;
        while (!__atomic_compare_exchange_n(raw_placement_, &curr.value, next.value, false, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED)) {
            next.value = curr.value;
            if (next.ref_count++ == 0) {
                raw_placement_ = nullptr;
                return;
            }
        }
    }

    /**
     * @brief Move constructor, transfers ownership.
     * @param other  Source object (set to empty after move).
     */
    AtomicFd(AtomicFd<T> &&other) noexcept : raw_placement_{other.raw_placement_}
    {
        other.raw_placement_ = nullptr;
    }

    /**
     * @brief Destructor, decrements ref_count; frees object and storage when ref_count reaches 0.
     * @note  Uses __ATOMIC_ACQ_REL in the CAS loop to ensure proper release semantics
     *        before deleting the managed object.
     */
    ~AtomicFd()
    {
        if (UNLIKELY(raw_placement_ == nullptr)) {
            return;
        }
        Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
        Place next{curr.value};
        --next.ref_count;
        while (!__atomic_compare_exchange_n(raw_placement_, &curr.value, next.value, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
            next.value = curr.value;
            --next.ref_count;
        }
        if (next.ref_count == 0) {
            delete static_cast<T *>(reinterpret_cast<void *>(static_cast<uintptr_t>(next.obj_addr)));
            __atomic_exchange_n(raw_placement_, 0, __ATOMIC_RELEASE);
        }
    }

    /**
     * @brief Copy assignment, increments ref_count.
     * @param other  Source object whose reference is shared.
     * @return       Reference to this object.
     */
    ALWAYS_INLINE AtomicFd<T> &operator=(const AtomicFd<T> &other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        // (1) fast path: this object is empty
        if (UNLIKELY(raw_placement_ == nullptr)) {
            raw_placement_ = other.raw_placement_;
            if (LIKELY(raw_placement_ != nullptr)) {
                Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
                Place next{curr.value};
                ++next.ref_count;
                while (!__atomic_compare_exchange_n(raw_placement_, &curr.value, next.value, true, __ATOMIC_ACQUIRE,
                                                    __ATOMIC_RELAXED)) {
                    next.value = curr.value;
                    if (next.ref_count++ == 0) {
                        raw_placement_ = nullptr;
                        return *this;
                    }
                }
            }
            return *this;
        }

        // (2) release old reference
        const auto old_placement = raw_placement_;
        raw_placement_ = other.raw_placement_;
        Place old{__atomic_load_n(old_placement, __ATOMIC_RELAXED)};
        Place next{old.value};
        --next.ref_count;
        while (!__atomic_compare_exchange_n(old_placement, &old.value, next.value, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
            next.value = old.value;
            --next.ref_count;
        }

        if (next.ref_count == 0) {
            delete static_cast<T *>(reinterpret_cast<void *>(static_cast<uintptr_t>(next.obj_addr)));
        }

        if (UNLIKELY(raw_placement_ == nullptr)) {
            return *this;
        }

        Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
        if (UNLIKELY(curr.ref_count == 0)) {
            raw_placement_ = nullptr;
            return *this;
        }

        // (3) acquire new reference
        next.value = curr.value;
        ++next.ref_count;
        while (!__atomic_compare_exchange_n(raw_placement_, &curr.value, next.value, true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED)) {
            next.value = curr.value;
            if (next.ref_count++ == 0) {
                raw_placement_ = nullptr;
                return *this;
            }
        }

        return *this;
    }

    /**
     * @brief Move assignment, transfers ownership.
     * @param other  Source object (set to empty after move).
     * @return       Reference to this object.
     */
    ALWAYS_INLINE AtomicFd<T> &operator=(AtomicFd<T> &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        // release old reference
        if (UNLIKELY(raw_placement_ != nullptr)) {
            Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
            Place next{curr.value};
            --next.ref_count;
            while (!__atomic_compare_exchange_n(raw_placement_, &curr.value, next.value, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                next.value = curr.value;
                --next.ref_count;
            }
            if (next.ref_count == 0) {
                delete static_cast<T *>(reinterpret_cast<void *>(static_cast<uintptr_t>(next.obj_addr)));
            }
        }

        raw_placement_ = other.raw_placement_;
        other.raw_placement_ = nullptr;
        return *this;
    }

    /**
     * @brief Get the managed object pointer.
     * @return Pointer to the managed object, or nullptr if empty / released.
     */
    ALWAYS_INLINE T *Get() const noexcept
    {
        if (UNLIKELY(raw_placement_ == nullptr)) {
            return nullptr;
        }

        Place curr{__atomic_load_n(raw_placement_, __ATOMIC_RELAXED)};
        if (UNLIKELY(curr.ref_count == 0)) {
            return nullptr;
        }

        return reinterpret_cast<T *>(static_cast<uintptr_t>(curr.obj_addr));
    }

    /**
     * @brief Obtain an AtomicFd from a storage location (increments ref_count).
     * @param placement  uint64_t-aligned storage address.
     * @return  Associated AtomicFd.
     */
    ALWAYS_INLINE static AtomicFd<T> GetObj(uint64_t *placement) noexcept
    {
        AtomicFd obj;
        if (UNLIKELY(placement == nullptr)) {
            return obj;
        }

        obj.raw_placement_ = placement;
        Place curr{__atomic_load_n(placement, __ATOMIC_RELAXED)};
        Place next{curr.value};
        ++next.ref_count;
        while (!__atomic_compare_exchange_n(obj.raw_placement_, &curr.value, next.value, true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED)) {
            next.value = curr.value;
            if (next.ref_count++ == 0) {
                obj.raw_placement_ = nullptr;
                return obj;
            }
        }

        return obj;
    }

    /**
     * @brief Read the reference count from a placement address.
     * @param placement  uint64_t-aligned storage address.
     * @return  Current reference count, or 0 if @p placement is nullptr or obj_addr is zero.
     */
    ALWAYS_INLINE static uint64_t GetRefCount(const uint64_t *placement) noexcept
    {
        if (UNLIKELY(placement == nullptr)) {
            return 0;
        }

        Place curr{__atomic_load_n(placement, __ATOMIC_RELAXED)};
        if (curr.obj_addr == 0) {
            return 0;
        }

        return curr.ref_count;
    }

private:
    uint64_t *raw_placement_; ///< Pointer to storage (externally provided, uint64_t-aligned).
};

/**
 * @brief Construct an object at the given location and create an AtomicFd.
 * @tparam T     Object type.
 * @tparam ARGS  Constructor argument types.
 * @param placement  uint64_t-aligned storage address.
 * @param args       Arguments forwarded to T's constructor.
 * @return  AtomicFd managing the newly created object.
 * @note  Returns empty AtomicFd if placement is nullptr or construction fails.
 */
template <typename T, typename... ARGS>
ALWAYS_INLINE AtomicFd<T> MakeAtomicFdOn(uint64_t *placement, ARGS... args) noexcept
{
    if (UNLIKELY(placement == nullptr)) {
        return AtomicFd<T>{};
    }
    T *obj = new (std::nothrow) T(args...);
    if (UNLIKELY(obj == nullptr)) {
        return AtomicFd<T>{};
    }

    return AtomicFd<T>{placement, obj};
}

/**
 * @brief  A fixed-capacity set of AtomicFd objects indexed by fd.
 *
 * Singleton container. Capacity is determined at initialization from
 * RLIMIT_NOFILE (capped at FD_CAPACITY_HARD_LIMIT). Each slot holds a
 * placement (uint64_t) plus a pointer to an AtomicFd<T>.
 *
 * @tparam T  Managed object type within each AtomicFd slot.
 */
template <typename T>
class AtomicFdSet {
    /**
     * @brief  A single slot in the set.
     * @note   Cache-line aligned (64B) to avoid false sharing.
     */
    struct alignas(64) AtomicFdSlot {
        uint64_t placement{0};           ///< Storage for AtomicFd's packed (addr + ref_count).
        AtomicFd<T> *atomic_fd{nullptr}; ///< Cached AtomicFd handle for fast lookup.

        /** @brief Destructor, releases the cached AtomicFd handle. */
        ~AtomicFdSlot()
        {
            if (atomic_fd != nullptr) {
                delete atomic_fd;
                atomic_fd = nullptr;
            }
        }
    };

public:
    /**
     * @brief  Get the singleton instance.
     * @return Reference to the global AtomicFdSet.
     */
    static AtomicFdSet &Instance()
    {
        static AtomicFdSet instance;
        return instance;
    }

    /**
     * @brief  Initialize the set with capacity determined by RLIMIT_NOFILE.
     * @retval 0   Success.
     * @retval -1  Failure (getrlimit error or allocation failure).
     */
    ALWAYS_INLINE int Initialize() noexcept
    {
        if (atomic_fd_slots_ != nullptr) {
            UBS_SLOG_WARN("AtomicFdSet already initialized, capacity: " << capacity_);
            return 0;
        }

        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
            UBS_SLOG_ERR("ArraySet Init getrlimit failed, errno: " << errno << ": " << strerror(errno));
            return -1;
        }

        capacity_ = std::min(static_cast<uint32_t>(rl.rlim_cur), static_cast<uint32_t>(FD_CAPACITY_HARD_LIMIT));
        if (capacity_ == 0) {
            UBS_SLOG_ERR("ArraySet Init invalid capacity: 0 rlim_cur: " << rl.rlim_cur);
            return -1;
        }

        auto ptr = new (std::nothrow) AtomicFdSlot[capacity_];
        if (UNLIKELY(ptr == nullptr)) {
            UBS_SLOG_ERR("allocate memory failed with capacity : " << capacity_);
            return -1;
        }

        __atomic_store_n(&atomic_fd_slots_, ptr, __ATOMIC_RELEASE);
        UBS_SLOG_DEBUG("AtomicFdSet Init capacity: " << capacity_ << " (rlim_cur: " << rl.rlim_cur
                                                     << ", hard_limit: " << FD_CAPACITY_HARD_LIMIT);
        return 0;
    }

    /**
     * @brief  Destroy the set and release all slots.
     */
    ALWAYS_INLINE void Uninitialize() noexcept
    {
        if (atomic_fd_slots_ != nullptr) {
            delete[] atomic_fd_slots_;
            atomic_fd_slots_ = nullptr;
        }
    }

    /**
     * @brief  Get the AtomicFd object at the given index.
     * @param idx  File-descriptor index.
     * @return  Copy of the AtomicFd handle, or an empty handle if out of range.
     */
    ALWAYS_INLINE AtomicFd<T> GetItem(int idx) const noexcept
    {
        if (UNLIKELY(idx < 0 || static_cast<uint32_t>(idx) >= capacity_)) {
            return AtomicFd<T>();
        }

        auto *slot = &atomic_fd_slots_[idx];
        if (UNLIKELY(slot->atomic_fd == nullptr)) {
            return AtomicFd<T>();
        }

        return AtomicFd<T>::GetObj(&slot->placement);
    }

    /**
     * @brief  Set the item at the given index. Only succeeds if the slot is empty.
     * @param idx   File-descriptor index.
     * @param item  Pointer to the managed object (ownership transferred).
     * @retval true   Success, item was placed.
     * @retval false  Slot already in use or @p idx out of range.
     */
    ALWAYS_INLINE bool SetItem(int idx, T *item) noexcept
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return false;
        }

        auto *slot = &atomic_fd_slots_[idx];
        if (UNLIKELY(slot->atomic_fd != nullptr)) {
            UBS_SLOG_ERR("FdSet index: " << idx << " already in use.");
            return false;
        }

        slot->atomic_fd = new (std::nothrow) AtomicFd<T>(&slot->placement, item);
        return true;
    }

    /**
     * @brief Remove the item at the given index and return its handle.
     * @param idx  File-descriptor index.
     * @return  The AtomicFd handle previously at this slot,
     *          or an empty handle if @p idx is out of range or the slot is empty.
     */
    ALWAYS_INLINE AtomicFd<T> RemoveItem(int idx) noexcept
    {
        if (idx < 0 || static_cast<uint32_t>(idx) >= capacity_) {
            return AtomicFd<T>();
        }

        auto *slot = &atomic_fd_slots_[idx];
        if (UNLIKELY(slot->atomic_fd == nullptr)) {
            return AtomicFd<T>();
        }

        AtomicFd<T> old(*slot->atomic_fd);
        delete slot->atomic_fd;
        slot->atomic_fd = nullptr;
        return old;
    }

    /**
     * @brief  Iterate over all non-empty slots and invoke @p callback.
     * @param callback  Functor called as callback(fd, object_pointer).
     */
    ALWAYS_INLINE void ForEach(const std::function<void(int fd, T *)> &callback) const noexcept
    {
        for (auto i = 0U; i < capacity_; ++i) {
            auto obj = GetItem(i);
            if (auto p = obj.Get(); p != nullptr) {
                callback(static_cast<int>(i), p);
            }
        }
    }

    /**
     * @brief  Count the number of slots with a positive reference count.
     * @return  Number of active (non-zero ref_count) entries.
     */
    ALWAYS_INLINE size_t Size() const noexcept
    {
        size_t count = 0;
        for (uint32_t i = 0; i < capacity_; ++i) {
            if (AtomicFd<T>::GetRefCount(&atomic_fd_slots_[i].placement) > 0) {
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief  Get the maximum capacity of the set.
     * @return  Capacity determined at Initialize() time.
     */
    ALWAYS_INLINE uint32_t Capacity() const noexcept
    {
        return capacity_;
    }

private:
    /** @brief Default constructor (private, use GetInstance()). */
    AtomicFdSet() = default;

    /** @brief Destructor, releases all slots via Uninitialize(). */
    ~AtomicFdSet()
    {
        Uninitialize();
    }

    AtomicFdSet(const AtomicFdSet &) = delete;            ///< Non-copyable.
    AtomicFdSet(AtomicFdSet &&) = delete;                 ///< Non-movable.
    AtomicFdSet &operator=(const AtomicFdSet &) = delete; ///< Non-copy-assignable.
    AtomicFdSet &operator=(AtomicFdSet &&) = delete;      ///< Non-move-assignable.

    static constexpr uint32_t FD_CAPACITY_HARD_LIMIT = 65536; ///< Hard upper bound on capacity.
    uint32_t capacity_{0};                                    ///< Current capacity (0 until Initialize()).
    AtomicFdSlot *atomic_fd_slots_{nullptr}; ///< Pointer to the slot array (nullptr until Initialize()).
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ATOMIC_FD_SET_H
