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

#ifndef UBS_COMM_UBSOCKET_ALLOC_FIXED_H
#define UBS_COMM_UBSOCKET_ALLOC_FIXED_H

#include "ubsocket_alloc_fixed_size_gc.h"
#include "ubsocket_alloc_fixed_size_tc.h"
#include "ubsocket_alloc_fixed_size_tc2.h"

namespace ock {
namespace ubs {
namespace allocator {

class FixedGlobalCache;
class FixedThreadCache;
class FixedAllocator {
public:
    FixedAllocator() noexcept = default;
    ~FixedAllocator() noexcept;

    /**
     * @brief Initialize the allocator with global cache
     *
     * @param options      [in] options of the allocator
     * @return 0 if successful
     */
    Result Init(FixedAllocatorOptions &options) noexcept;

    /**
     * @brief Un-initialize the allocator
     */
    void UnInit() noexcept;

    /**
     * @brief Allocate one mini block
     *
     * @param address      [out] address that allocated
     * @return 0 if successful
     */
    bool Allocate(uintptr_t &address) noexcept;

    /**
     * @brief Free one mini block
     *
     * @param address      [in] address of the mini block
     */
    void Free(uintptr_t address) noexcept;

    /**
     * @brief Allocate batch of mini blocks
     *
     * @param batchSize    [in] batch size to be allocated
     * @param addresses    [out] addresses that allocated mini blocks
     * @return true if allocated
     */
    bool BatchAllocate(uint16_t batchSize, uintptr_t *&addresses) noexcept;

    /**
     * @brief Free batch of mini blocks
     *
     * @param batchSize    [in] batch size of mini blocks to be freed
     * @param addresses    [in] addresses of the mini blocks to be freed
     */
    void BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept;

    /**
     * @brief Allocate batch of mini blocks, return chain head address
     *
     * @param batchSize    [in] batch size to be allocated
     * @param address      [out] address of the first mini block (chain head)
     * @return true if allocated
     */
    bool BatchAllocateWithLinkList(uint16_t batchSize, uintptr_t &address) noexcept;

    /**
     * @brief Check if the address in the address range of this allocator
     *
     * @param address      [in] the address to be checked
     * @return true if in range
     */
    bool InRange(uintptr_t address, uint64_t size) noexcept;

    /**
     * @brief Check if a batch of addresses in range or not
     *
     * @param batchSize    [in] batch of the addresses
     * @param addresses    [in] addresses
     * @param size         [in] sizes
     * @return true if all addresses in range
     */
    bool BatchInRange(uint16_t batchSize, uintptr_t *addresses, uint64_t *size) noexcept;

    /**
     * @brief Directly allocate from global cache
     *
     * @param size         [in] size of memory to be allocated
     * @param address      [out] allocated memory address
     * @return true if allocated
     */
    bool DirectAllocate(uint32_t size, uintptr_t &address) noexcept;

    /**
     * @brief Directly free memory global cache
     * @param address      [in] address of memory to be freed
     */
    void DirectFree(uintptr_t address) noexcept;

private:
    Result VerifyOptions(FixedAllocatorOptions &options) noexcept;
    void UnInitInner() noexcept;
    FixedThreadCache &GetTC() noexcept;

    static uint32_t AlignUp4K(uint32_t size) noexcept;

private:
    FixedGlobalCache *gc_ = nullptr;
    std::mutex mutex_;
};

ALWAYS_INLINE FixedThreadCache &FixedAllocator::GetTC() noexcept
{
    static thread_local FixedThreadCache tc(gc_, FIX_GC_BATCH_ALLOC_SIZE, FIX_GC_BATCH_FREE_SIZE);
    return tc;
}

ALWAYS_INLINE uint32_t FixedAllocator::AlignUp4K(uint32_t size) noexcept
{
    constexpr uint32_t ALIGN_4K_MASK = ~(static_cast<uint32_t>(SIZE_4K) - 1);
    return (size + SIZE_4K - 1UL) & ALIGN_4K_MASK;
}
} // namespace allocator
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_UBSOCKET_ALLOC_FIXED_H
