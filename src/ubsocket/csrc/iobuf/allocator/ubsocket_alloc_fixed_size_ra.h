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

#ifndef UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_RA_H
#define UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_RA_H

#include <memory_resource>

#include "ubsocket_alloc_fixed_size_types.h"

namespace ock {
namespace ubs {
namespace allocator {

namespace sub {
struct Range {
    const uintptr_t address;
    const uint32_t size;

    Range(uintptr_t o, uint32_t s) noexcept : address{o}, size{s} {}
};

struct RangeCompSizeFirst {
    bool operator()(const Range &r1, const Range &r2) const noexcept
    {
        if (r1.size != r2.size) {
            return r1.size < r2.size;
        }

        return r1.address < r2.address;
    }
};

class SingleAllocator : public Referable {
public:
    SingleAllocator(uintptr_t address, uint32_t size_bytes) noexcept
        : base_address_(address),
          size_bytes_(size_bytes),
          free_size_bytes_(size_bytes)
    {
        free_addresses_ = std::pmr::map<uintptr_t, uint32_t>(&address_pool_);
        free_ranges_ = std::pmr::set<Range, RangeCompSizeFirst>(&range_pool_);
        allocated_addresses_ = std::pmr::map<uintptr_t, uint32_t>(&allocated_address_pool_);
    }

    ~SingleAllocator() noexcept = default;

    /**
     * @brief Allocate one range with size
     *
     * @param size_bytes   [in] size of the range to be allocated
     * @return address if successful, 0 if failed
     */
    uintptr_t Allocate(uint32_t size_bytes) noexcept;

    /**
     * @brief Free one allocated range with size
     *
     * @param address      [in] address allocated range
     * @return true if successful, false if it is invalid range
     */
    bool Free(uintptr_t address) noexcept;

    /**
     * @brief Expand range from block
     *
     * @param address      [in] address of FixedBlock new created
     * @param size_bytes   [in] size of the range
     */
    void ExpandRange(uintptr_t address, uint32_t size_bytes) noexcept;

private:
    const uintptr_t base_address_;                                  /* address of the whole range */
    const uint32_t size_bytes_;                                     /* size of the whole range */
    uint32_t free_size_bytes_ = 0;                                  /* left free space of whole range */
    std::pmr::map<uintptr_t, uint32_t> free_addresses_;             /* free ranges for merge */
    std::pmr::map<uintptr_t, uint32_t> allocated_addresses_;        /* free ranges for merge */
    std::pmr::set<Range, RangeCompSizeFirst> free_ranges_;          /* free ranges for allocation fast lookup */
    std::pmr::unsynchronized_pool_resource address_pool_;           /* memory pool for free address */
    std::pmr::unsynchronized_pool_resource allocated_address_pool_; /* memory pool for allocated */
    std::pmr::unsynchronized_pool_resource range_pool_;             /* memory pool for free ranges */
};
using SingleAllocatorPtr = Ref<SingleAllocator>;

ALWAYS_INLINE uintptr_t SingleAllocator::Allocate(uint32_t size_bytes) noexcept
{
    /* step0: initialize free ranges set */
    if (UNLIKELY(free_size_bytes_ == size_bytes_)) {
        free_addresses_.emplace(base_address_, free_size_bytes_);
        free_ranges_.emplace(Range{base_address_, free_size_bytes_});
    }

    /* step1: no more space */
    if (free_size_bytes_ < size_bytes) {
        return 0;
    }

    /* step2: find the closest free-range */
    Range anchor{0, size_bytes};
    auto size_iter = free_ranges_.lower_bound(anchor);
    if (size_iter == free_ranges_.end()) {
        return 0;
    }

    /* step3: assign to tmp variable */
    const auto target_address = size_iter->address;
    const auto target_size = size_iter->size;

    /* step4: find the address iter for fast erase */
    auto address_iter = free_addresses_.find(target_address);
    if (address_iter == free_addresses_.end()) {
        return 0;
    }

    /* step5: erase from both range and address map */
    free_ranges_.erase(size_iter);
    free_addresses_.erase(address_iter);

    /* step6: split the range if it is larger than request size */
    if (target_size > size_bytes) {
        Range remain{target_address + size_bytes, target_size - size_bytes};
        free_addresses_.emplace(remain.address, remain.size);
        free_ranges_.emplace(remain);
    }

    /* step7: insert into allocated map */
    allocated_addresses_.emplace(target_address, target_size);

    /* step8: decrease free bytes */
    free_size_bytes_ -= size_bytes;

    return target_address;
}

ALWAYS_INLINE bool SingleAllocator::Free(uintptr_t address) noexcept
{
    /* step1: check if invalid address */
    if (address < base_address_) {
        return false;
    }

    /* step2: find from allocated map */
    auto allocated_iter = allocated_addresses_.find(address);
    if (allocated_iter == allocated_addresses_.end()) {
        return false;
    }

    /* assign to tmp variable*/
    uint32_t size_bytes = allocated_iter->second;
    uintptr_t merged_address = address;
    uint32_t merged_size = size_bytes;

    /* step3: remove from allocated map */
    allocated_addresses_.erase(allocated_iter);

    /* step4: merge forward */
    auto prev_addr_iter = free_addresses_.lower_bound(merged_address);
    if (prev_addr_iter != free_addresses_.begin()) {
        --prev_addr_iter;
        if (prev_addr_iter != free_addresses_.end() &&
            prev_addr_iter->first + prev_addr_iter->second == merged_address) {
            merged_address = prev_addr_iter->first;
            merged_size += prev_addr_iter->second;
            free_ranges_.erase(Range{prev_addr_iter->first, prev_addr_iter->second});
            free_addresses_.erase(prev_addr_iter);
        }
    }

    /*  step5: merge backward */
    auto next_addr_iter = free_addresses_.find(address + size_bytes);
    if (next_addr_iter != free_addresses_.end()) {
        merged_size += next_addr_iter->second;
        free_ranges_.erase(Range{next_addr_iter->first, next_addr_iter->second});
        free_addresses_.erase(next_addr_iter);
    }

    /* step6: insert into address and range map */
    free_addresses_.emplace(merged_address, merged_size);
    free_ranges_.emplace(Range{merged_address, merged_size});

    /* step7: increase free size */
    free_size_bytes_ += size_bytes;

    return true;
}

ALWAYS_INLINE void SingleAllocator::ExpandRange(uintptr_t address, uint32_t size_bytes) noexcept
{
    if (address == 0 && size_bytes == 0) {
        return;
    }

    /* add into free address and free range */
    free_addresses_.emplace(address, size_bytes);
    free_ranges_.emplace(Range{address, size_bytes});

    /* increase total free size */
    free_size_bytes_ += size_bytes;
}

} // namespace sub

class FixedRangeAllocator {
public:
    FixedRangeAllocator() = default;
    ~FixedRangeAllocator() = default;

    Result Init(const FixedBlock &firstBlock, const FixedExpandHandler &handler) noexcept;
    void UnInit() noexcept;

    bool DirectAllocate(uint32_t size, uintptr_t &address) noexcept;
    void DirectFree(uintptr_t address) noexcept;

private:
    std::mutex mutex_;
    sub::SingleAllocatorPtr sub_allocator_{nullptr};
    FixedExpandHandler expand_handler_ = nullptr;
    std::vector<FixedBlock> blocks_;
};

ALWAYS_INLINE Result FixedRangeAllocator::Init(const FixedBlock &firstBlock, const FixedExpandHandler &handler) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (handler == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (firstBlock.address == 0 || firstBlock.size_kb == 0 || firstBlock.size_kb != firstBlock.free_size_kb) {
        return UBS_INVALID_PARAM;
    }

    blocks_.push_back(firstBlock);

    auto allocator = MakeRef<sub::SingleAllocator>(firstBlock.address, SIZE_1K * firstBlock.size_kb);
    if (allocator == nullptr) {
        UBS_VLOG_ERR("Failed to create sub allocator, probably out of memory");
        return UBS_ERROR;
    }

    sub_allocator_ = allocator;

    expand_handler_ = handler;
    UBS_VLOG_DEBUG("Range allocator initialized");
    return UBS_OK;
}

ALWAYS_INLINE void FixedRangeAllocator::UnInit() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (expand_handler_ == nullptr) {
        return;
    }

    blocks_.clear();
    sub_allocator_ = nullptr;
    expand_handler_ = nullptr;
}

ALWAYS_INLINE bool FixedRangeAllocator::DirectAllocate(uint32_t size, uintptr_t &address) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(expand_handler_ == nullptr)) {
        UBS_VLOG_ERR("FixedRangeAllocator is not initialized");
        return UBS_ERROR;
    }

    /* step1：allocate from sub allocator */
    address = sub_allocator_->Allocate(size);
    if (address != 0) {
        UBS_VLOG_DEBUG("FixedRangeAllocator::DirectAllocate succeeded, %x", address);
        return true;
    }

    /* step2: no enough memory and expand */
    FixedBlock new_block;
    auto expand_result = expand_handler_(0, new_block);
    if (expand_result != UBS_OK) {
        UBS_VLOG_ERR("Failed to expand new block, probably out of memory");
        return false;
    }

    /* step3: allocate again */
    sub_allocator_->ExpandRange(new_block.address, SIZE_1K * new_block.size_kb);
    address = sub_allocator_->Allocate(size);
    if (address != 0) {
        UBS_VLOG_DEBUG("FixedRangeAllocator::DirectAllocate succeeded, %x", address);
        return true;
    }

    UBS_VLOG_ERR("Un-reachable path");
    return false;
}

ALWAYS_INLINE void FixedRangeAllocator::DirectFree(uintptr_t address) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(expand_handler_ == nullptr)) {
        UBS_VLOG_ERR("FixedRangeAllocator is not initialized");
        return;
    }

    if (!sub_allocator_->Free(address)) {
        UBS_VLOG_ERR("Failed to free to allocator with address %x", address);
    }
}
} // namespace allocator
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_RA_H
