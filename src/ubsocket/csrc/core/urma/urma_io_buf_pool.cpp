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
#include "urma_io_buf_pool.h"

#include "urma_backend.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaIoBufferPool::init(const UrmaContextPtr &ctx) noexcept
{
    options_.expanded_callback = [this, ctx](uintptr_t address, uint64_t size, uint32_t flags) -> Result {
        if (flags == allocator::ADD) {
            return this->register_mem(ctx, address, size);
        }
        if (flags == allocator::REMOVE) {
            return this->deregister_mem(ctx, address, size);
        }
        return UBS_ERROR;
    };

    auto block_count = (options_.pool_space_size_gb << 10) / options_.block_size_mb;
    target_segments_.local_target_segs_.resize(block_count, nullptr);

    auto res = fixed_allocator_.Init(options_);
    if (res != UBS_OK) {
        UBS_SLOG_ERR("fixed_allocator initialization failed, result: " << res);
        return res;
    }

    return UBS_OK;
}

Result UrmaIoBufferPool::init(const allocator::FixedAllocatorOptions &options, const UrmaContextPtr &ctx) noexcept
{
    options_ = options;

    return init(ctx);
}

void UrmaIoBufferPool::un_init() noexcept
{
    fixed_allocator_.UnInit();
}

void *UrmaIoBufferPool::allocate(size_t size)
{
    if (UNLIKELY(size > options_.mini_block_size_kb * 1024U)) {
        UBS_SLOG_WARN("size = " << size << ", should not greater then " << options_.mini_block_size_kb << "KB");
        return nullptr;
    }

    uintptr_t address = 0;
    if (LIKELY(fixed_allocator_.Allocate(address))) {
        return reinterpret_cast<void *>(address);
    }
    return nullptr;
}

void *UrmaIoBufferPool::allocate(size_t size, const ubs_iobuf_alloc_option_t * /*option*/)
{
    return allocate(size);
}

void UrmaIoBufferPool::deallocate(void *ptr)
{
    fixed_allocator_.Free(reinterpret_cast<uintptr_t>(ptr));
}

void *UrmaIoBufferPool::direct_allocate(size_t size) noexcept
{
    uintptr_t address = 0;
    if (LIKELY(fixed_allocator_.DirectAllocate(size, address))) {
        return reinterpret_cast<void *>(address);
    }
    return nullptr;
}

void UrmaIoBufferPool::direct_release(void *ptr) noexcept
{
    fixed_allocator_.DirectFree(reinterpret_cast<uintptr_t>(ptr));
}

urma_target_seg_t *UrmaIoBufferPool::get_local_target_seg(uintptr_t ptr) const noexcept
{
    uint32_t block_index = static_cast<uint32_t>(((ptr - local_start_addr_) >> 20) / options_.block_size_mb);
    if (UNLIKELY(block_index >= target_segments_.local_target_segs_.size())) {
        return nullptr;
    }
    return target_segments_.local_target_segs_[block_index];
}

RemoteSegmentsPtr UrmaIoBufferPool::get_remote_segments(const IdentifyNode &remote) noexcept
{
    RemoteSegmentsPtr remote_segments{nullptr};

    pthread_spin_lock(&target_segments_.remote_segments_lock_);
    auto pos = target_segments_.remote_target_segs_.find(remote);
    if (UNLIKELY(pos == target_segments_.remote_target_segs_.end())) {
        remote_segments = MakeRef<RemoteSegments>();
        remote_segments->remote_target_segs_.resize(target_segments_.local_target_segs_.size());
        target_segments_.remote_target_segs_.emplace(remote, remote_segments);
    } else {
        remote_segments = pos->second;
    }
    pthread_spin_unlock(&target_segments_.remote_segments_lock_);

    return remote_segments;
}

urma_target_seg_t *UrmaIoBufferPool::get_remote_target_seg(RemoteSegmentsPtr &remote, uintptr_t ptr) const noexcept
{
    if (UNLIKELY(remote == nullptr)) {
        return nullptr;
    }

    if (UNLIKELY(remote->remote_start_addr_ == 0)) {
        UBS_VLOG_TRACE("checking seg remote start address not set, is null.");
        return nullptr;
    }

    uint32_t block_index = static_cast<uint32_t>(((ptr - remote->remote_start_addr_) >> 20) / options_.block_size_mb);
    if (UNLIKELY(block_index >= remote->remote_target_segs_.size())) {
        UBS_VLOG_TRACE("checking seg remote ptr=0x%lx, block_index=%u, now size = %zu", ptr, block_index,
                       remote->remote_target_segs_.size());
        return nullptr;
    }

    auto res = remote->remote_target_segs_[block_index];
    UBS_VLOG_TRACE("checking seg remote ptr=0x%lx, block_index=%u, res=%p", ptr, block_index, res);
    return res;
}

Result UrmaIoBufferPool::store_remote_target_seg(RemoteSegmentsPtr &remote, urma_seg_t &seg,
                                                 urma_target_seg_t *&tgt) noexcept
{
    if (UNLIKELY(remote == nullptr)) {
        return UBS_ERROR;
    }

    uintptr_t ptr = seg.ubva.va;
    uint32_t block_index = static_cast<uint32_t>(((ptr - remote->remote_start_addr_) >> 20) / options_.block_size_mb);
    if (UNLIKELY(block_index >= remote->remote_target_segs_.size())) {
        UBS_SLOG_WARN("store_remote_target_seg: block_index = " << block_index);
        return UBS_ERROR;
    }
    if (LIKELY((tgt = remote->remote_target_segs_[block_index]) != nullptr)) {
        UBS_VLOG_INFO("store_remote_target_seg: block_index = %d already set.", block_index);
        return UBS_OK;
    }

    urma_token_t token = {seg.token_id};
    urma_import_seg_flag_t flag{};
    flag.value = 0;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;
    flag.bs.mapping = URMA_SEG_NOMAP;

    pthread_spin_lock(&remote->target_lock_);
    if (LIKELY(remote->remote_target_segs_[block_index] == nullptr)) {
        auto target =
            UrmaApi::urma_import_seg(UrmaBackend::Instance().GetUrmaContext()->Handle(), &seg, &token, 0, flag);
        if (UNLIKELY(target == nullptr)) {
            pthread_spin_unlock(&remote->target_lock_);
            UBS_SLOG_ERR("Urma urma_register_seg failed!");
            return UBS_ERROR;
        }

        tgt = remote->remote_target_segs_[block_index] = target;
    }
    pthread_spin_unlock(&remote->target_lock_);
    UBS_VLOG_TRACE("remote seg(0x%lx : 0x%lx) index(%u) import success.", seg.ubva.va, seg.len, block_index);
    return UBS_OK;
}

Result UrmaIoBufferPool::store_remote_target_seg(RemoteSegmentsPtr &remote, urma_eid_t &eid, uint32_t uasid,
                                                 uintptr_t addr, uint32_t token) noexcept
{
    if (UNLIKELY(remote == nullptr)) {
        return UBS_ERROR;
    }

    uint32_t block_index = static_cast<uint32_t>(((addr - remote->remote_start_addr_) >> 20) / options_.block_size_mb);
    if (UNLIKELY(block_index >= remote->remote_target_segs_.size())) {
        UBS_SLOG_WARN("store_remote_target_seg: block_index = " << block_index);
        return UBS_ERROR;
    }

    if (LIKELY(remote->remote_target_segs_[block_index] != nullptr)) {
        UBS_VLOG_TRACE("store_remote_target_seg: block_index = %d already set.", block_index);
        return UBS_OK;
    }

    urma_seg_t urma_seg{};
    urma_seg.ubva.eid = eid;
    urma_seg.ubva.uasid = uasid;
    urma_seg.ubva.va = remote->remote_start_addr_ + ((1UL * block_index * options_.block_size_mb) << 20);
    urma_seg.len = get_block_size();
    urma_seg.attr.value = 0;
    urma_seg.attr.bs.token_policy = URMA_TOKEN_NONE;
    urma_seg.attr.bs.cacheable = URMA_NON_CACHEABLE;
    urma_seg.attr.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;
    urma_seg.token_id = token;

    urma_token_t urma_token = {token};
    urma_import_seg_flag_t flag{};
    flag.value = 0;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;
    flag.bs.mapping = URMA_SEG_NOMAP;

    UBS_SLOG_TRACE("import segment @0x" << std::hex << urma_seg.ubva.va << ", seg=" << urma_seg);
    pthread_spin_lock(&remote->target_lock_);
    if (LIKELY(remote->remote_target_segs_[block_index] == nullptr)) {
        auto target = UrmaApi::urma_import_seg(UrmaBackend::Instance().GetUrmaContext()->Handle(), &urma_seg,
                                               &urma_token, 0, flag);
        if (UNLIKELY(target == nullptr)) {
            pthread_spin_unlock(&remote->target_lock_);
            UBS_SLOG_ERR("Urma urma_register_seg failed!");
            return UBS_ERROR;
        }
        remote->remote_target_segs_[block_index] = target;
    }
    pthread_spin_unlock(&remote->target_lock_);
    UBS_VLOG_TRACE("remote seg(0x%lx : 0x%lx) index(%u) import success.", addr, urma_seg.len, block_index);

    return UBS_OK;
}

Result UrmaIoBufferPool::register_mem(const UrmaContextPtr &ctx, uintptr_t ptr, size_t size) noexcept
{
    if (local_start_addr_ == 0) {
        local_start_addr_ = ptr;
    }
    uint32_t block_index = static_cast<uint32_t>(((ptr - local_start_addr_) >> 20) / options_.block_size_mb);

    urma_reg_seg_flag_t flag{};
    flag.value = 0;
    flag.bs.token_policy = URMA_TOKEN_NONE;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;

    urma_seg_cfg_t seg_cfg = {
        .va = ptr, .len = size, .token_id = nullptr, .token_value = 0, .flag = flag, .user_ctx = 0, .iova = 0};

    auto local_seg = UrmaApi::urma_register_seg(ctx->Handle(), &seg_cfg);
    if (local_seg == nullptr) {
        UBS_VLOG_ERR("urma register_seg(ptr=0x%lx, size=0x%lx) failed!", ptr, size);
        return UBS_ERROR;
    }

    target_segments_.local_target_segs_[block_index] = local_seg;
    UBS_VLOG_TRACE("urma register_seg(ptr=0x%lx, size=0x%lx) token-id: 0x%x OK", ptr, size, local_seg->seg.token_id);
    UBS_SLOG_TRACE("register got segment: " << local_seg->seg);
    return UBS_OK;
}

Result UrmaIoBufferPool::deregister_mem(const UrmaContextPtr &ctx, uintptr_t ptr, size_t size) noexcept
{
    UBS_SLOG_ERR("urma deregister_seg(ptr=0x" << std::hex << ptr << ", size=0x" << size << ") not supported!");
    return UBS_ERROR;
}

} // namespace urma
} // namespace ubs
} // namespace ock
