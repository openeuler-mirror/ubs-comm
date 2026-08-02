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
#ifndef UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TWIN_H
#define UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TWIN_H

#include "ubsocket_alloc_fixed_size_types.h"

namespace ock {
namespace ubs {
namespace allocator {

class FixedTwinBornAllocator {
public:
    explicit FixedTwinBornAllocator() noexcept = default;
    ~FixedTwinBornAllocator() noexcept
    {
        UnInit();
    }

    /**
     * @brief Init my memory based on the info of brother
     *
     * @param options
     * @return
     */
    Result Init(const FixedTwinBornAllocOptions &options) noexcept;

    /**
     * @brief Free my memory
     */
    void UnInit() noexcept;

    /**
     * @brief Check if the address belong to this allocator
     *
     * @param myAddress    [in] my address to be checked
     *
     * @return true if in range
     */
    bool InRange(uintptr_t myAddress) noexcept;

    /**
     * @brief Allocate
     *
     * @param brotherAddress
     * @param myAddress
     * @return
     */
    bool AllocateTwin(uintptr_t brotherAddress, uintptr_t &myAddress) noexcept;

    /**
     * @brief Handle function for expand, i.e. allocate physical memory
     *
     * @param brotherAddress   [in] address of brother
     * @param brotherSizeBytes [in] size
     * @param flags            [in] add/remove
     * @return 0 if successful
     */
    Result HandleExpand(uintptr_t brotherAddress, uint64_t brotherSizeBytes, uint32_t flags) noexcept;

private:
    static Result VerifyOptions(const FixedTwinBornAllocOptions &options) noexcept;
    Result CreateMemory(const FixedTwinBornAllocOptions &options) noexcept;
    void DestroyMemory();

private:
    /* make the size of this class is 64 bytes*/
    uintptr_t brother_space_base_address_ = 0;
    uintptr_t brother_space_base_address_end_ = 0;
    uint64_t brother_single_size_bytes_ = 0;
    uintptr_t my_space_base_address_ = 0;
    uintptr_t my_space_base_address_end_ = 0;
    uint32_t my_space_size_bytes_ = 0;
    uint32_t my_single_data_size_bytes_ = 0;
    uint64_t reserved[2]{};
};

ALWAYS_INLINE Result
FixedTwinBornAllocator::VerifyOptions(const ock::ubs::allocator::FixedTwinBornAllocOptions &options) noexcept
{
    // TODO
    return UBS_OK;
}

ALWAYS_INLINE Result FixedTwinBornAllocator::Init(const FixedTwinBornAllocOptions &options) noexcept
{
    if (my_space_base_address_ != 0) {
        UBS_VLOG_DEBUG("Already initialized, skip");
        return UBS_OK;
    }

    auto result = VerifyOptions(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Options for fixed twin born allocator is invalid, result(%)", result);
        return result;
    }

    result = CreateMemory(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize fixed twin born allocator as create memory failed, result(%)", result);
        return result;
    }

    UBS_VLOG_TRACE("inited");
    return UBS_OK;
}

ALWAYS_INLINE void FixedTwinBornAllocator::UnInit() noexcept
{
    if (my_space_base_address_ == 0) {
        UBS_VLOG_DEBUG("Not initialized, skip");
        return;
    }

    DestroyMemory();
    UBS_VLOG_TRACE("un-inited");
}

ALWAYS_INLINE Result
FixedTwinBornAllocator::CreateMemory(const ock::ubs::allocator::FixedTwinBornAllocOptions &options) noexcept
{
    auto tmp_bro_data_size = (SIZE_1K * options.brother_single_data_size_kb);
    auto tmp_my_init_size = SIZE_1M * options.brother_init_space_mb / tmp_bro_data_size;
    auto tmp_bro_space_size = (SIZE_1G * options.brother_space_size_gb);
    auto tmp_my_space_size = tmp_bro_space_size / tmp_bro_data_size;

    /* get memory space */
    auto tmp_address = mmap(NULL, tmp_my_space_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (tmp_address == MAP_FAILED) {
        UBS_VLOG_ERR("Failed to mmap memory with size(%ld) bytes failed, errno(%d)", tmp_address, errno);
        return UBS_MALLOC_FAILED;
    }

    /* alloc physical memory, partially */
    bzero(tmp_address, tmp_my_init_size);

    /* assign member variables */
    brother_space_base_address_ = options.brother_space_base_address;
    brother_space_base_address_end_ = brother_space_base_address_ + tmp_bro_space_size - tmp_bro_data_size;
    brother_single_size_bytes_ = tmp_bro_data_size;
    my_space_base_address_ = reinterpret_cast<uintptr_t>(tmp_address);
    my_space_size_bytes_ = tmp_my_space_size;
    my_single_data_size_bytes_ = options.my_single_data_size_bytes;
    my_space_base_address_end_ = my_space_base_address_ + my_space_size_bytes_ - my_single_data_size_bytes_;

    UBS_VLOG_TRACE("created memory with brother_space_base_address_ (%x), brother_space_base_address_end_ (%x), "
                   "brother_single_size_bytes_ (%x), "
                   "my_space_base_address_ (%x), my_space_size_bytes_ (%x), my_single_data_size_bytes_ (%x), "
                   "my_space_base_address_end_ (%x)",
                   brother_space_base_address_, brother_space_base_address_end_, brother_single_size_bytes_,
                   my_space_base_address_, my_space_size_bytes_, my_single_data_size_bytes_,
                   my_space_base_address_end_);

    return UBS_OK;
}

ALWAYS_INLINE void FixedTwinBornAllocator::DestroyMemory()
{
    if (my_space_base_address_ == 0) {
        UBS_VLOG_DEBUG("memory not created yet");
        return;
    }

    munmap(reinterpret_cast<void *>(my_space_base_address_), my_space_size_bytes_);

    brother_space_base_address_ = 0;
    brother_space_base_address_end_ = 0;
    brother_single_size_bytes_ = 0;
    my_space_base_address_end_ = 0;
    my_space_base_address_ = 0;
    my_space_size_bytes_ = 0;
    my_single_data_size_bytes_ = 0;

    UBS_VLOG_TRACE("Destroy memory finished");
}

ALWAYS_INLINE bool FixedTwinBornAllocator::InRange(uintptr_t myAddress) noexcept
{
    if (UNLIKELY(my_space_base_address_ == 0)) {
        UBS_VLOG_ERR("Not initialized, my space address(%x)", my_space_base_address_);
        return false;
    }

    return myAddress >= my_space_base_address_ && myAddress <= my_space_base_address_end_;
}

ALWAYS_INLINE bool FixedTwinBornAllocator::AllocateTwin(uintptr_t brotherAddress, uintptr_t &myAddress) noexcept
{
    if (UNLIKELY(my_space_base_address_ == 0)) {
        UBS_VLOG_ERR("Not initialized, my space address(%x)", my_space_base_address_);
        return false;
    }

    if (UNLIKELY(brotherAddress < brother_space_base_address_ || brotherAddress > brother_space_base_address_end_)) {
        UBS_VLOG_ERR("Invalid brother address(%x), should be in range (%x)~(%x)", brotherAddress,
                     brother_space_base_address_, brother_space_base_address_end_);
        return false;
    }

    const uint64_t offset = (brotherAddress - brother_space_base_address_) / brother_single_size_bytes_;
    return my_space_base_address_ + offset * my_single_data_size_bytes_;
}

ALWAYS_INLINE Result FixedTwinBornAllocator::HandleExpand(uintptr_t brotherAddress, uint64_t brotherSizeBytes,
                                                          uint32_t flags) noexcept
{
    if (UNLIKELY(my_space_base_address_ == 0)) {
        UBS_VLOG_ERR("Not initialized, my space address(%x)", my_space_base_address_);
        return UBS_ERROR;
    }

    if (UNLIKELY(brotherAddress < brother_space_base_address_ || brotherAddress > brother_space_base_address_end_)) {
        UBS_VLOG_ERR("Invalid brother address(%x), should be in range (%x)~(%x)", brotherAddress,
                     brother_space_base_address_, brother_space_base_address_end_);
        return UBS_ERROR;
    }

    const uint64_t offset = (brotherAddress - brother_space_base_address_) / brother_single_size_bytes_;
    const uintptr_t my_address = my_space_base_address_ + offset * my_single_data_size_bytes_;
    const uintptr_t my_address_end = my_address + brotherSizeBytes - my_single_data_size_bytes_;

    if (UNLIKELY(my_address_end > my_space_base_address_end_)) {
        UBS_VLOG_ERR("Invalid brother address(%x) with size(%x)", brotherAddress, brotherSizeBytes);
        return UBS_ERROR;
    }

    UBS_VLOG_TRACE("brotherAddress (%x), brotherSizeBytes (%x), my_address_base (%x), my_address (%x)", brotherAddress,
                   brotherSizeBytes, my_space_base_address_, my_address);

    if (flags == BlockExpandFlag::ADD) {
        bzero(reinterpret_cast<void *>(my_address), brotherSizeBytes);
    }

    return UBS_OK;
}

} // namespace allocator
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TWIN_H
