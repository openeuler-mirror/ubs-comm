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
#include "ubsocket_alloc_fixed_size.h"
#include "ubsocket_alloc_fixed_size_gc.h"
#include "ubsocket_alloc_fixed_size_tc.h"

namespace ock {
namespace ubs {
namespace allocator {
FixedAllocator::~FixedAllocator() noexcept
{
    UnInit();
}

Result FixedAllocator::Init(FixedAllocatorOptions &options) noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (gc_ != nullptr) {
        UBS_VLOG_DEBUG("Already initialized");
        return UBS_OK;
    }

    auto result = VerifyOptions(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to create fixed size allocator, result=%d", result);
        return result;
    }

    auto tmp_gc = new (std::nothrow) FixedGlobalCache();
    if (tmp_gc == nullptr) {
        UBS_VLOG_ERR("Failed to create global cache object, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    result = tmp_gc->Init(options);
    if (result != UBS_OK) {
        delete tmp_gc;
        UBS_VLOG_ERR("Failed to create fixed size allocator, result=%d", result);
        return result;
    }

    gc_ = tmp_gc;
    mini_block_head_mask_ = ~(options.mini_block_size_kb * 1024UL - 1UL);
    UBS_VLOG_DEBUG("Initialized");
    return UBS_OK;
}

void FixedAllocator::UnInit() noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (gc_ == nullptr) {
        UBS_VLOG_DEBUG("Not initialized");
        return;
    }

    UnInitInner();
    UBS_VLOG_DEBUG("Uninitialized");
}

void FixedAllocator::UnInitInner() noexcept
{
    auto &tc_ = GetTC();
    tc_.UnInit();

    if (gc_ != nullptr) {
        gc_->UnInit();
        delete gc_;
        gc_ = nullptr;
    }
}

bool FixedAllocator::Allocate(uintptr_t &address) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    return GetTC().Allocate(address);
}

void FixedAllocator::Free(uintptr_t address) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return;
    }

    if (UNLIKELY(address == 0)) {
        return;
    }

    GetTC().Free(address);
}

bool FixedAllocator::BatchAllocate(uint16_t batchSize, uintptr_t *&addresses) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    if (UNLIKELY(batchSize == 0 || addresses == nullptr)) {
        return false;
    }

    return GetTC().BatchAllocate(batchSize, addresses);
}

void FixedAllocator::BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return;
    }

    if (UNLIKELY(batchSize == 0 || addresses == nullptr)) {
        return;
    }

    for (int i = 0; i < batchSize; ++i) {
        GetTC().Free(addresses[i]);
    }
}

bool FixedAllocator::BatchAllocateWithLinkList(uint16_t batchSize, FixedLinkedBlock *&blocks) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    FixedMiniBlock *head_block = 0;
    if (UNLIKELY(!GetTC().BatchAllocate(batchSize, head_block))) {
        return false;
    }
    blocks = reinterpret_cast<FixedLinkedBlock *>(head_block);
    return true;
}

bool FixedAllocator::InRange(uintptr_t address, uint64_t size) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    if (UNLIKELY(address == 0 || size == 0)) {
        return false;
    }

    return gc_->InRange(address, size);
}

bool FixedAllocator::BatchInRange(uint16_t batchSize, uintptr_t *addresses, uint64_t *size) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    if (UNLIKELY(batchSize == 0 || addresses == nullptr || size == nullptr)) {
        return false;
    }

    return gc_->BatchInRange(batchSize, addresses, size);
}

bool FixedAllocator::DirectAllocate(uint32_t size, uintptr_t &address) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return false;
    }

    if (UNLIKELY(size == 0)) {
        return false;
    }

    auto aligned_size = AlignUp4K(size);
    return gc_->DirectAllocate(aligned_size, address);
}

void FixedAllocator::DirectFree(uintptr_t address) noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Not initialized");
        return;
    }

    gc_->DirectFree(address);
}

Result FixedAllocator::VerifyOptions(FixedAllocatorOptions &options) noexcept
{
    return FixedGlobalCache::VerifyOptions(options);
}

} // namespace allocator
} // namespace ubs
} // namespace ock
