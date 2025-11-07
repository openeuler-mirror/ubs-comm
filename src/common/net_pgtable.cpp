/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <cstring>
#include <vector>
#include "hcom_def.h"
#include "hcom_err.h"
#include "hcom_log.h"
#include "securec.h"
#include "net_pgtable.h"

namespace ock {
namespace hcom {
constexpr uint64_t BitMask(uint32_t i)
{
    return (i >= NN_NO64) ? 0 : (1UL << i);
}

constexpr uint64_t OrderMask(uint32_t i)
{
    return (i >= NN_NO64) ? ~0 : (BitMask(i) - 1);
}

constexpr size_t AlignDownPow2(size_t n, size_t alignment)
{
    return n & ~(alignment - 1);
}

constexpr size_t AlignUpPow2(size_t n, size_t alignment)
{
    return AlignDownPow2(n + alignment - 1, alignment);
}

constexpr bool IsAddrAligned(size_t addr)
{
    return (addr & (PAGE_ADDR_ALIGN_MIN - 1)) == 0;
}
/**
 * @brief Returns the position of the most significant set bit in a uint64_t.
 * @param n Input value, must be greater than 0.
 * @return The bit position (0-indexed). For example: 1 -> 0, 8 (1000b) -> 3.
 */
static inline unsigned HighestBitPosition(uint64_t n)
{
    return NN_NO63 - static_cast<unsigned>(__builtin_clzll(n));
}
/**
 * @brief Returns the position of the least significant set bit in a uint64_t.
 * @param n Input value, must be greater than 0.
 * @return The bit position (0-indexed). For example: 8 (1000b) -> 3, 6 (110b) -> 1.
 */
static inline unsigned LowestBitPosition(uint64_t n)
{
    return static_cast<unsigned>(__builtin_ctzll(n));
}

static inline bool IsValidPtePointer(void *ptr)
{
    return !(reinterpret_cast<uintptr_t>(ptr) & (PGT_ENTRY_MIN_ALIGN - 1));
}

static inline void AdvanceAddrByOrder(PgtAddress &address, uint32_t order)
{
    if (order >= NN_NO64) {
        NN_LOG_ERROR("Failed pgt address advance order is >= 64");
        return;
    }

    address += 1uL << order;
}

PgtDir *PgTable::PgtDirAlloc()
{
    if (mPgdAllocCb == nullptr || mPgdReleaseCb == nullptr) {
        NN_LOG_ERROR("Failed to allocate page table directory, as allocate or release callback is null");
        return nullptr;
    }
    auto pgd = mPgdAllocCb(*this);
    if (pgd == nullptr) {
        NN_LOG_ERROR("Failed to allocate page table directory, as pgd ptr is null");
        return nullptr;
    }
    if (!IsValidPtePointer(pgd)) {
        NN_LOG_ERROR("Failed to allocate page table directory, as pgd ptr is not align");
        mPgdReleaseCb(*this, pgd);
        return nullptr;
    }

    if (memset_s(pgd, sizeof(PgtDir), 0, sizeof(PgtDir)) != 0) {
        NN_LOG_ERROR("Failed to allocate page table directory, as memset_s pgd failed");
        mPgdReleaseCb(*this, pgd);
        return nullptr;
    }

    return pgd;
}

void PgTable::PgtDirRelease(PgtDir *pgd)
{
    if (pgd == nullptr) {
        NN_LOG_ERROR("Failed to release page table directory, as dir is null");
        return;
    }
    if (mPgdReleaseCb == nullptr) {
        NN_LOG_ERROR("Failed to release page table directory, as release callback is null");
        return;
    }
    mPgdReleaseCb(*this, pgd);
    pgd = nullptr;
}

void PgTable::PgtDumpSubtree(uint32_t indent, const PgtEntry &pgtEntry, uint32_t pteIndex, PgtAddress base,
    PgtAddress mask, uint32_t shift)
{
    if (pgtEntry.HasFlag(EntryFlags::REGION)) {
        auto region = pgtEntry.GetRegion();
        if (region == nullptr) {
            return;
        }
        NN_LOG_DEBUG("indent: " << indent << " pte_index:" << pteIndex << " shift:" << shift << " is region");
    } else if (pgtEntry.HasFlag(EntryFlags::DIR)) {
        auto pgd = pgtEntry.GetDir();
        if (pgd == nullptr) {
            return;
        }
        NN_LOG_DEBUG("indent: " << indent << " pte_index:" << pteIndex << " dir count:"
        << pgd->count << " shift:" << shift);
        shift -= PTE_SHIFT_PER_DIR;
        mask |= PTE_INDEX_MASK << shift;
        for (uint32_t i = 0; i < PTE_ENTRY_NUM_PER_DIR; ++i) {
            PgtDumpSubtree(indent + NN_NO2, pgd->entries[i], i, base | (i << shift), mask, shift);
            ++base;
        }
    } else {
        NN_LOG_DEBUG("indent: " << indent << " pte_index:" << pteIndex << " not present");
    }
}

void PgTable::Dump()
{
    NN_LOG_INFO("pgtable dump, shift:" << mIndexShift << ", count:" << mRegionCount);
    PgtDumpSubtree(0, mRootEntry, 0, mVirBaseAddr, mSpaceMask, mIndexShift);
}

void PgTable::PgTableReset()
{
    mVirBaseAddr = 0;
    mSpaceMask = (static_cast<PgtAddress>(-1)) << PAGE_SHIFT_MIN;
    mIndexShift = PAGE_SHIFT_MIN;
}

void PgTable::PgtEnsureCapacity(uint32_t order, PgtAddress address)
{
    // Ensure the page table is deep enough to support the address order
    while (mIndexShift < order) {
        if (!PgtExpand()) {
            return;
        }
    }

    if (!mRootEntry.IsPresent()) {
        mVirBaseAddr = address & mSpaceMask;
        NN_LOG_INFO("pgtable initialize, shift:" << mIndexShift << ", count:" << mRegionCount);
    } else {
        // Ensure the target address falls within the current pgtable mVirBaseAddr address range
        while ((address & mSpaceMask) != mVirBaseAddr) {
            if (!PgtExpand()) {
                return;
            }
        }
    }
}

bool PgTable::PgtExpand()
{
    // shift为地址最高位，最大值为[PGT_ADDR_ORDER_MAX - PTE_SHIFT_PER_DIR]
    if (mIndexShift > (PGT_ADDR_ORDER_MAX - PTE_SHIFT_PER_DIR)) {
        NN_LOG_ERROR("failed to expand pgtable, shift is over max " << PGT_ADDR_ORDER_MAX - PTE_SHIFT_PER_DIR);
        return false;
    }

    // 如果根节点已存在，将其下沉为子目录
    if (mRootEntry.IsPresent()) {
        PgtDir *pgd = PgtDirAlloc();
        if (pgd == nullptr) {
            NN_LOG_ERROR("failed to expand pgtable, allocate pgt dir error");
            return false;
        }
        pgd->entries[(mVirBaseAddr >> mIndexShift) & PTE_INDEX_MASK] = mRootEntry;
        pgd->count = 1;
        if (!mRootEntry.SetDir(*pgd)) {
            PgtDirRelease(pgd);
            return false;
        }
    }

    mIndexShift += PTE_SHIFT_PER_DIR;
    mSpaceMask <<= PTE_SHIFT_PER_DIR; // example 0xF0 -> 0xFF0
    mVirBaseAddr &= mSpaceMask;

    NN_LOG_INFO("pgtable expand success, shift:" << mIndexShift << ", count:" << mRegionCount);
    return true;
}

bool PgTable::PgtShrink()
{
    if (!mRootEntry.IsPresent()) {
        PgTableReset();
        NN_LOG_INFO("pgtable shrink, shift:" << mIndexShift << ", count:" << mRegionCount);
        return false;
    }
    if (!mRootEntry.HasFlag(EntryFlags::DIR)) {
        return false;
    }

    auto pgd = mRootEntry.GetDir();
    if (pgd == nullptr || pgd->count != 1) {
        return false;
    }

    PgtEntry *pgtEntry = nullptr;
    uint32_t idx = 0;

    // 当页表某层目录只有一个有效entry时，找到这个entry，并移除这一层
    for (uint32_t i = 0; i < PTE_ENTRY_NUM_PER_DIR; ++i) {
        if (pgd->entries[i].IsPresent()) {
            pgtEntry = &pgd->entries[i];
            idx = i;
            break; // 因为 count == 1，最多只有一个
        }
    }

    if (pgtEntry == nullptr) {
        NN_LOG_ERROR("pgtable shrink failed, pgd entry is null");
        PgtDirRelease(pgd);
        return false;
    }

    if (mIndexShift < PTE_SHIFT_PER_DIR) {
        NN_LOG_ERROR("pgtable shrink failed, invalid shift:" << mIndexShift);
        PgtDirRelease(pgd);
        return false;
    }

    mIndexShift -= PTE_SHIFT_PER_DIR;
    mVirBaseAddr |= static_cast<PgtAddress>(idx) << mIndexShift; // 将idx的偏移
    mSpaceMask |= PTE_INDEX_MASK << mIndexShift;                 // 缩小mask不再覆盖最高的PTE_SHIFT_PER_DIR位置
    mRootEntry = *pgtEntry;
    NN_LOG_INFO("pgtable shrink, shift:" << mIndexShift << ", count:" << mRegionCount);
    PgtDirRelease(pgd);
    return true;
}

static NResult ValidatePage(PgtAddress address, uint32_t order)
{
    // 检查起始地址是否与页大小对齐
    if ((address & ((1uL << order) - 1)) != 0) {
        NN_LOG_ERROR("failed to check address, is not align with page order");
        return NN_INVALID_PARAM;
    }
    // 检查order是否为页表层级结构允许的 必须为[PAGE_SHIFT_MIN + k * PTE_SHIFT_PER_DIR]
    // 例如：起始阶 PAGE_SHIFT_MIN=4, 阶差 PTE_SHIFT_PER_DIR=4 → 对齐合法阶为 4 - 8 - 12 - 16 - 20...
    if (((order - PAGE_SHIFT_MIN) % PTE_SHIFT_PER_DIR) != 0) {
        NN_LOG_ERROR("failed to check order " << order);
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult PgTable::PgtCheckEntryDir(PgtEntry &pgtEntry, uint32_t shift, uint32_t order)
{
    if (pgtEntry.HasFlag(EntryFlags::REGION)) {
        NN_LOG_ERROR("Failed to insert entry, order is not equal to shift but pgtEntry is region.");
        return NN_ERROR;
    }

    if (shift < PTE_SHIFT_PER_DIR + order) {
        NN_LOG_ERROR("shift is less than PTE_SHIFT_PER_DIR + order");
        return NN_ERROR;
    }
    return NN_OK;
}

/**
 * @brief Returns the smallest page table level order that can cover the range [start, end).
 * If start == 0 and end == 0, it represents the entire address space.
 * @param start The start address of the range (inclusive)
 * @param end   The end address of the range (exclusive)
 * @return The page order (order), or -1 on failure
 */
static uint32_t GetNextPageOrder(PgtAddress start, PgtAddress end)
{
    if (!IsAddrAligned(start) || !IsAddrAligned(end)) {
        NN_LOG_ERROR("failed to get next page order, start or end address is not aligned");
        return -1;
    }

    uint32_t maxOrder = 0;
    if ((end == 0) && (start == 0)) {
        // entire address space
        maxOrder = PGT_ADDR_ORDER_MAX;
    } else if (end == start) {
        // min page size
        maxOrder = PAGE_SHIFT_MIN;
    } else {
        maxOrder = HighestBitPosition(end - start);
        // The lowest set bit in the start address determines its alignment order.
        // For example: start = 0x1000 (binary ...0001 0000 0000 0000), LowestBitPosition = 12 → 4KB aligned.
        // This means a page larger than 4KB cannot be used, otherwise it would cross an alignment boundary.
        if (start) {
            maxOrder = std::min(LowestBitPosition(start), maxOrder);
        }
    }

    if ((maxOrder < PAGE_SHIFT_MIN) || (maxOrder > PGT_ADDR_ORDER_MAX)) {
        NN_LOG_ERROR("failed to get next page order, log2Len is invalid");
        return -1;
    }

    // aligned down maxOrder to the nearest valid page table level.
    uint32_t alignedOrder = ((maxOrder - PAGE_SHIFT_MIN) / PTE_SHIFT_PER_DIR) * PTE_SHIFT_PER_DIR + PAGE_SHIFT_MIN;
    NN_LOG_DEBUG("Calculate max order is " << maxOrder << " alignedOrder is " << alignedOrder);
    return alignedOrder;
}

/**
 * Insert a variable-size page to the page table.
 *
 * @param address  address to insert
 * @param order    page size to insert - should be k*PTE_SHIFT for a certain k
 * @param region   region to insert
 */
NResult PgTable::InsertPage(PgtAddress address, uint32_t order, PgtRegion &region)
{
    NN_LOG_DEBUG("begin to insert page, order " << order << " region " << region.key);

    if (ValidatePage(address, order) != NN_OK) {
        return NN_INVALID_PARAM;
    }

    PgtEnsureCapacity(order, address);

    PgtDir dummyPgd = {};
    PgtDir *currentDir = &dummyPgd;
    uint32_t currentShift = mIndexShift;
    PgtEntry *pgtEntry = &mRootEntry;
    while (order != currentShift) {
        if (PgtCheckEntryDir(*pgtEntry, currentShift, order) != NN_OK) {
            goto ROLLBACK;
        }

        if (!pgtEntry->IsPresent()) {
            ++currentDir->count;
            auto dir = PgtDirAlloc();
            if (dir == nullptr) {
                goto ROLLBACK;
            }
            if (!pgtEntry->SetDir(*dir)) {
                PgtDirRelease(dir);
                goto ROLLBACK;
            }
        }

        currentDir = pgtEntry->GetDir();
        if (currentDir == nullptr) {
            goto ROLLBACK;
        }
        currentShift -= PTE_SHIFT_PER_DIR;
        uint32_t index = (address >> currentShift) & PTE_INDEX_MASK;
        if (index >= PTE_ENTRY_NUM_PER_DIR) {
            goto ROLLBACK;
        }
        pgtEntry = &currentDir->entries[index];
    }

    if (pgtEntry->IsPresent() || !pgtEntry->SetRegion(region)) {
        NN_LOG_ERROR("Failed to insert entry, entry already exist or not set region flag.");
        goto ROLLBACK;
    }

    if (currentDir) {
        ++currentDir->count;
    }

    NN_LOG_DEBUG("insert page success, order " << order << " region " << region.key);
    return NN_OK;
ROLLBACK:
    while (PgtShrink()) {}
    return NN_ERROR;
}

NResult PgTable::UnlinkRegion(PgtAddress address, uint32_t order, PgtDir &pgd, PgtEntry &pgtEntry, uint32_t shift,
    PgtRegion &region)
{
    if (pgtEntry.HasFlag(EntryFlags::REGION)) {
        if (shift != order) {
            return NN_ERROR;
        }
        if (pgtEntry.GetRegion() != &region) {
            return NN_ERROR;
        }

        --pgd.count;
        pgtEntry.Clear();
        return NN_OK;
    } else if (pgtEntry.HasFlag(EntryFlags::DIR)) {
        auto nextDir = pgtEntry.GetDir();
        if (nextDir == nullptr) {
            return NN_ERROR;
        }
        uint32_t nextShift = shift - PTE_SHIFT_PER_DIR;
        uint32_t index = (address >> nextShift) & PTE_INDEX_MASK;
        if (index >= PTE_ENTRY_NUM_PER_DIR) {
            return NN_ERROR;
        }
        auto nextPte = &nextDir->entries[index];

        auto ret = UnlinkRegion(address, order, *nextDir, *nextPte, nextShift, region);
        if (ret != NN_OK) {
            return ret;
        }

        if (nextDir->count == 0) {
            pgtEntry.Clear();
            --pgd.count;
            if (mPgdReleaseCb != nullptr) {
                mPgdReleaseCb(*this, nextDir);
            } else {
                NN_LOG_WARN("unable to call dir release cb, which is nullptr");
            }
        }
        return NN_OK;
    }
    return NN_ERROR;
}

NResult PgTable::RemovePage(PgtAddress address, uint32_t order, PgtRegion &region)
{
    if (ValidatePage(address, order) != NN_OK) {
        return NN_INVALID_PARAM;
    }

    if ((address & mSpaceMask) != mVirBaseAddr) {
        NN_LOG_ERROR("no elem in address, as address mVirBaseAddr is not pgtable base");
        return NN_ERROR;
    }

    PgtDir pgd = {};
    auto ret = UnlinkRegion(address, order, pgd, mRootEntry, mIndexShift, region);
    if (ret != NN_OK) {
        return ret;
    }

    while (PgtShrink()) {}
    return NN_OK;
}

NResult PgTable::Insert(PgtRegion &region)
{
    NN_LOG_DEBUG("begin to add region " << region.key);

    uint32_t order = 0;
    PgtAddress address = region.start;
    PgtAddress end = region.end;
    if ((address >= end) || !IsAddrAligned(address) || !IsAddrAligned(end)) {
        NN_LOG_ERROR("failed to add region maybe region start > end, or address is not 16-byte aligned");
        return NN_INVALID_PARAM;
    }

    while (address < end) {
        order = GetNextPageOrder(address, end);
        if (order < 0 || order >= NN_NO64) {
            NN_LOG_ERROR("Failed to add region, get next page order is less than 0 or over 64");
            goto ROLLBACK;
        }
        if (InsertPage(address, order, region) != NN_OK) {
            NN_LOG_ERROR("failed to insert page.");
            goto ROLLBACK;
        }

        AdvanceAddrByOrder(address, order);
    }
    ++mRegionCount;

    NN_LOG_INFO("pgtable insert success, shift:" << mIndexShift << ", count:" << mRegionCount);
    return NN_OK;

ROLLBACK:
    /* Revert all pages we've inserted by now */
    end = address;
    address = region.start;
    while (address < end) {
        order = GetNextPageOrder(address, end);
        RemovePage(address, order, region);
        AdvanceAddrByOrder(address, order);
    }
    return NN_ERROR;
}

NResult PgTable::Remove(PgtRegion &region)
{
    NN_LOG_DEBUG("begin to remove region " << region.key);

    PgtAddress address = region.start;
    PgtAddress end = region.end;
    if ((address >= end) || !IsAddrAligned(address) || !IsAddrAligned(end)) {
        NN_LOG_ERROR("failed to remove region no element with this param.");
        return NN_ERROR;
    }

    while (address < end) {
        uint32_t order = GetNextPageOrder(address, end);
        if (order >= NN_NO64) {
            NN_LOG_ERROR("Failed pgt table get next page order is >= 64");
            return NN_ERROR;
        }
        auto ret = RemovePage(address, order, region);
        if (ret != NN_OK) {
            /* Cannot be partially removed */
            if (address != region.start) {
                return NN_ERROR;
            }
            return ret;
        }

        AdvanceAddrByOrder(address, order);
    }

    if (mRegionCount > 0) {
        --mRegionCount;
    }

    NN_LOG_INFO("pgtable remove success, shift:" << mIndexShift << ", count:" << mRegionCount);
    return NN_OK;
}

PgtRegion *PgTable::Lookup(PgtAddress address) const
{
    NN_LOG_DEBUG("begin to lookup pgtable");

    if ((address & mSpaceMask) != mVirBaseAddr) {
        NN_LOG_ERROR("failed to lookup pgtable, as address is not mapped by the page table");
        return nullptr;
    }
    if (!mRootEntry.IsPresent()) {
        NN_LOG_ERROR("failed to lookup pgtable, mRootEntry is nullptr");
        return nullptr;
    }
    const PgtEntry *currentEntry = &mRootEntry;
    uint32_t currentShift = mIndexShift;
    // Descend dir level by level until a Region is found
    while (true) {
        if (currentEntry->HasFlag(EntryFlags::REGION)) {
            auto region = currentEntry->GetRegion();
            if (region == nullptr) {
                NN_LOG_ERROR("failed to lookup pgtable, as region is null");
                return nullptr;
            }
            if ((address < region->start) || (address >= region->end)) {
                NN_LOG_ERROR("failed to lookup pgtable as address is not in region");
                return nullptr;
            }
            return region;
        }
        if (currentEntry->HasFlag(EntryFlags::DIR)) {
            auto dir = currentEntry->GetDir();
            if (dir == nullptr) {
                NN_LOG_ERROR("failed to lookup pgtable, as dir is null");
                return nullptr;
            }
            currentShift -= PTE_SHIFT_PER_DIR;
            uint32_t index = (address >> currentShift) & PTE_INDEX_MASK;
            if (index >= PTE_ENTRY_NUM_PER_DIR) {
                NN_LOG_ERROR("failed to lookup entry index is over entry array bound");
                return nullptr;
            }
            currentEntry = &dir->entries[index];
            continue;
        }
        NN_LOG_DEBUG("Lookup failed: entry is invalid no REGION or DIR flag");
        return nullptr;
    }
}

void PgTable::SearchSubtree(PgtAddress address, uint32_t order, const PgtEntry &pgtEntry, uint32_t currentShift,
    PgtSearchCb cb, void *arg, PgtRegion *&lastRegion)
{
    NN_LOG_DEBUG("Begin to search subtree, order " << order << " currentShift " << currentShift <<
        " entry is region " << pgtEntry.HasFlag(EntryFlags::REGION));
    if (pgtEntry.HasFlag(EntryFlags::REGION)) {
        auto region = pgtEntry.GetRegion();
        if (region == nullptr || lastRegion == region) {
            return;
        }
        if (lastRegion != nullptr && region->start < lastRegion->end) {
            NN_LOG_ERROR("Failed to search, as regions is overlap, now region start is less than previous region end");
            return;
        }
        lastRegion = region;

        // ensure region is not overlaps with address [address, address + 2^order - 1]
        if (std::max(region->start, address) > std::min(region->end - 1, address + OrderMask(order))) {
            NN_LOG_ERROR("Failed to search, region start end is not overlaps with the address");
            return;
        }
        if (cb == nullptr) {
            NN_LOG_WARN("Unable to call the search cb, as cb is null");
            return;
        }
        cb(*this, *region, arg);
    } else if (pgtEntry.HasFlag(EntryFlags::DIR)) {
        auto dir = pgtEntry.GetDir();
        if (dir == nullptr) {
            NN_LOG_ERROR("Failed to search, current dir is nullptr.");
            return;
        }
        if (currentShift < PTE_SHIFT_PER_DIR) {
            NN_LOG_ERROR("Failed to search, current shift " << currentShift
            << " is less than entry mIndexShift per level " << PTE_SHIFT_PER_DIR);
            return;
        }

        uint32_t nextShift = currentShift - PTE_SHIFT_PER_DIR;
        if (order < currentShift) {
            // search region is less than current dir span, it can only in dir sub entry
            uint32_t index = (address >> nextShift) & PTE_INDEX_MASK;
            if (index >= PTE_ENTRY_NUM_PER_DIR) {
                NN_LOG_ERROR("Failed to search, index is over dir entry size.");
                return;
            }
            auto nextPte = &dir->entries[index];
            SearchSubtree(address, order, *nextPte, nextShift, cb, arg, lastRegion);
        } else {
            // search region covers the range of current dir, need to search all entries.
            for (const auto& nextPte : dir->entries) {
                SearchSubtree(address, order, nextPte, nextShift, cb, arg, lastRegion);
            }
        }
    }
}

void PgTable::SearchRange(PgtAddress from, PgtAddress to, PgtSearchCb cb, void *arg)
{
    // 确保搜索操作在页对齐的边界上进行
    PgtAddress address = AlignDownPow2(from, PAGE_ADDR_ALIGN_MIN);
    PgtAddress end = AlignUpPow2(to, PAGE_ADDR_ALIGN_MIN);

    // 与页表实际管理的范围 [base, base + 2^shift) 进行交集操作，确保搜索不会超出页表的边界
    if (mIndexShift < (sizeof(uint64_t) * NN_NO8)) {
        address = std::max(address, mVirBaseAddr);
        end = std::min(end, mVirBaseAddr + BitMask(mIndexShift));
    } else {
        if (mVirBaseAddr != 0) {
            NN_LOG_ERROR("Failed to search range,shift is whole address base should be 0");
            return;
        }
    }

    PgtRegion *lastRegion = nullptr;
    while (address <= to) {
        uint32_t order = GetNextPageOrder(address, end);
        if ((address & mSpaceMask) == mVirBaseAddr) {
            SearchSubtree(address, order, mRootEntry, mIndexShift, cb, arg, lastRegion);
        }

        if (order >= PGT_ADDR_ORDER_MAX) {
            break;
        }

        AdvanceAddrByOrder(address, order);
    }
}

static void PgtCleanupCallback(const PgTable &pgtable, PgtRegion &region, void *arg)
{
    if (arg == nullptr) {
        NN_LOG_ERROR("Failed to call the page table purge callback, arg is nullptr");
        return;
    }
    auto *regionVector = static_cast<std::vector<PgtRegion *> *>(arg);
    regionVector->push_back(&region);
    NN_LOG_DEBUG("call the clean cb success, push region to vector region " << region.key);
}

void PgTable::Cleanup()
{
    NN_LOG_INFO("begin to cleanup pgtable numRegions " << mRegionCount);
    if (mRegionCount == 0) {
        NN_LOG_INFO("page table is empty, nothing to cleanup.");
        return;
    }
    std::vector<PgtRegion *> cleanRegions;
    cleanRegions.reserve(mRegionCount);

    PgtAddress from = mVirBaseAddr;
    PgtAddress to = mVirBaseAddr + (BitMask(mIndexShift) & mSpaceMask) - 1;
    SearchRange(from, to, PgtCleanupCallback, &cleanRegions);
    if (cleanRegions.size() != mRegionCount) {
        NN_LOG_ERROR("Found size " << cleanRegions.size() << " regions, expected size" << mRegionCount);
        return;
    }

    for (auto region : cleanRegions) {
        auto ret = Remove(*region);
        if (ret != NN_OK) {
            NN_LOG_ERROR("failed to remove region during cleanup");
        }
    }

    // 最终检查状态
    if (mRootEntry.IsPresent()) {
        NN_LOG_WARN("unable to purge pgtable, entry already exist after clean");
    }
    if (mIndexShift != PAGE_SHIFT_MIN || mVirBaseAddr != 0 || mRegionCount != 0) {
        NN_LOG_WARN("unable to purge pgtable, patable mIndexShift:" << mIndexShift << " base:"
        << mVirBaseAddr << " num regions " << mRegionCount);
    }
}
}
}
