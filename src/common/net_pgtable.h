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
#ifndef HCOM_NET_PGTABLE_H_
#define HCOM_NET_PGTABLE_H_

#include "hcom_def.h"
#include "hcom_err.h"
#include "hcom_log.h"

namespace ock {
namespace hcom {
/* Define the address type */
using PgtAddress = uintptr_t;

enum class EntryFlags : PgtAddress {
    REGION = 1uL << 0,
    DIR = 1uL << 1
};

/* Address alignment requirements */
constexpr uint32_t PAGE_SHIFT_MIN = 4;
constexpr PgtAddress PAGE_ADDR_ALIGN_MIN = (1uL << PAGE_SHIFT_MIN);
constexpr uint32_t PGT_ADDR_ORDER_MAX = (sizeof(PgtAddress) * 8);  // Total number of bits in the PgtAddress type
constexpr PgtAddress PGT_ADDR_MAX = (static_cast<PgtAddress>(-1)); // maximum addressable space of PgtAddress

/* Page table entry/directory constants */
constexpr uint32_t PTE_SHIFT_PER_DIR = 4;
constexpr uint32_t PTE_ENTRY_NUM_PER_DIR = (1uL << (PTE_SHIFT_PER_DIR));
constexpr PgtAddress PTE_INDEX_MASK = (PTE_ENTRY_NUM_PER_DIR - 1);

/* Page table pointers constants and flags */
constexpr PgtAddress PGT_ENTRY_FLAGS_MASK =
    (static_cast<PgtAddress>(EntryFlags::REGION) | static_cast<PgtAddress>(EntryFlags::DIR));

constexpr PgtAddress PGT_ENTRY_PTR_MASK = (~PGT_ENTRY_FLAGS_MASK);
constexpr PgtAddress PGT_ENTRY_MIN_ALIGN = (PGT_ENTRY_FLAGS_MASK + 1);

constexpr int Log2Static(uint64_t n)
{
    return (n <= 1) ? 0 : 1 + Log2Static(n >> 1);
}

constexpr bool IsPowerOfTwoOrZero(uint64_t n)
{
    return (n & (n - 1)) == 0;
}

constexpr bool IsPowerOfTwo(uint64_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

using PgTable = struct NetPgTable;
using PgtDir = struct PgtDir;

/**
 * Memory region in the page table.
 * The structure itself, and the pointers in it, must be aligned to 2^PTR_SHIFT.
 */
struct PgtRegion {
    PgtAddress start; /* *< Region start address */
    PgtAddress end;   /* *< Region end address */
    uint64_t key;
    uint64_t token;
};

/**
 * Page table entry:
 *
 * +--------------------+---+---+
 * |    pointer (MSB)   | d | r |
 * +--------------------+---+---+
 * |                    |   |   |
 * 64                   2   1   0
 *
 */
class PgtEntry {
public:
    PgtEntry() : mValue(0) {}

    ~PgtEntry()
    {
        mValue = 0;
    }

    PgtRegion *GetRegion() const
    {
        if (!HasFlag(EntryFlags::REGION)) {
            NN_LOG_ERROR("Failed to get region, value is not set region flag");
            return nullptr;
        }
        return reinterpret_cast<PgtRegion *>(mValue & PGT_ENTRY_PTR_MASK);
    }

    bool SetRegion(PgtRegion &region)
    {
        if (!CheckPtrValueAlign(&region)) {
            NN_LOG_ERROR("Failed to check region, value is not align");
            return false;
        }

        SetPointerAndFlags(&region, EntryFlags::REGION);
        return true;
    }

    PgtDir *GetDir() const
    {
        if (!HasFlag(EntryFlags::DIR)) {
            NN_LOG_ERROR("Failed to get directory, value is not set dir flag");
            return nullptr;
        }
        return reinterpret_cast<PgtDir *>(mValue & PGT_ENTRY_PTR_MASK);
    }

    bool SetDir(PgtDir &dir)
    {
        if (!CheckPtrValueAlign(&dir)) {
            NN_LOG_ERROR("Failed to check dir, value is not align");
            return false;
        }
        SetPointerAndFlags(&dir, EntryFlags::DIR);
        return true;
    }

    bool HasFlag(EntryFlags flag) const
    {
        return (mValue & static_cast<PgtAddress>(flag)) != 0;
    }

    void SetFlag(EntryFlags flag)
    {
        mValue |= static_cast<PgtAddress>(flag);
    }

    void ClearFlag(EntryFlags flag)
    {
        mValue &= ~static_cast<PgtAddress>(flag);
    }

    bool IsPresent() const
    {
        constexpr PgtAddress PRESENT_MASK =
            static_cast<PgtAddress>(EntryFlags::REGION) | static_cast<PgtAddress>(EntryFlags::DIR);
        return (mValue & PRESENT_MASK) != 0;
    }

    void Clear()
    {
        mValue = 0;
    }

private:
    bool CheckPtrValueAlign(void *ptr)
    {
        return !(reinterpret_cast<uintptr_t>(ptr) & (PGT_ENTRY_MIN_ALIGN - 1));
    }

    void SetPointerAndFlags(void *ptr, EntryFlags flag)
    {
        mValue = (reinterpret_cast<PgtAddress>(ptr) & PGT_ENTRY_PTR_MASK) | static_cast<PgtAddress>(flag);
    }

    PgtAddress mValue = 0;
};

/**
 * Page table directory.
 * Each directory contains a fixed number of page table entries (PTEs) and tracks
 * the count of valid entries.
 */
struct PgtDir {
    PgtEntry entries[PTE_ENTRY_NUM_PER_DIR]; // Array of page table entries
    uint32_t count;                          // Number of valid (present) entries in this directory
};

/**
 * Callback type: Allocates a page table directory.
 *
 * This function is responsible for allocating memory for a new PgtDir.
 *
 * @param pgtable [in] Reference to the page table requesting allocation.
 * @return Pointer to the newly allocated PgtDir, or nullptr on failure.
 * The returned pointer must be aligned to PGT_ENTRY_ALIGN bytes.
 */
using PgDirAllocCb = PgtDir *(*)(const PgTable &pgtable);

/**
 * Callback type: Releases a page table directory.
 *
 * Frees memory associated with a previously allocated PgtDir.
 *
 * @param pgtable [in] Reference to the page table that owns the directory.
 * @param pgdir   [in] Pointer to the directory to release. May be nullptr.
 */
using PgDirReleaseCb = void (*)(const PgTable &pgtable, PgtDir *pgdir);

/**
 * Callback type: Invoked when a valid memory region is found during traversal.
 *
 * Used in search or walk operations to process matching regions.
 *
 * @param pgtable [in] Reference to the current page table.
 * @param region  [in] The memory region that was found (contains base, size, attrs).
 * @param arg     [in] User-provided context or data (e.g., accumulator, flag).
 */
using PgtSearchCb = void (*)(const PgTable &pgtable, PgtRegion &region, void *arg);

/**
 * The page table data structure organizes non-overlapping memory regions
 * using an efficient radix tree, optimized for large and/or naturally aligned regions.
 *
 * Each page table entry (PTE) can be in one of three states:
 * - Points to a memory region      (indicated by PGT_PTE_FLAG_REGION)
 * - Points to a child directory    (indicated by PGT_PTE_FLAG_DIR)
 * - Empty (null)                   (if neither flag is set)
 *
 * Entries are mutually exclusive: a PTE cannot be both a region and a directory.
 * This ensures a clear hierarchical structure and prevents ambiguity during traversal.
 */
class NetPgTable {
public:
    /**
     * Constructor.
     * Initializes the page table with allocation and release callbacks.
     *
     * @param [in]  allocCb    Callback for allocating page directories.
     * @param [in]  releaseCb  Callback for releasing page directories.
     */
    explicit NetPgTable(PgDirAllocCb allocCb, PgDirReleaseCb releaseCb)
        : mRegionCount(0),
          mRootEntry {},
          mVirBaseAddr(0),
          mSpaceMask(0),
          mIndexShift(PAGE_SHIFT_MIN)
    {
        static_assert(IsPowerOfTwo(PGT_ENTRY_MIN_ALIGN));

        static_assert(IsPowerOfTwoOrZero(PGT_ADDR_MAX + 1));
        // We must cover all bits of the address up to ADDR_MAX
        static_assert(((Log2Static(PGT_ADDR_MAX) + 1 - PAGE_SHIFT_MIN) % PTE_SHIFT_PER_DIR) == 0);

        if (allocCb == nullptr || releaseCb == nullptr) {
            throw std::invalid_argument("invalid param, directory allocate or release callback is null");
        }

        mSpaceMask = (static_cast<PgtAddress>(-1)) << PAGE_SHIFT_MIN;

        mPgdAllocCb = allocCb;
        mPgdReleaseCb = releaseCb;
    }

    /**
     * DeConstructor.
     */
    ~NetPgTable()
    {
        if (mRootEntry.IsPresent()) {
            try {
                Cleanup();
            } catch (const std::exception& ex) {
                NN_LOG_ERROR("NetPgTable DeConstructor caught exception in Cleanup: " << ex.what());
            }
        }
        mPgdAllocCb = nullptr;
        mPgdReleaseCb = nullptr;
    }

    /**
     * Add a memory region to the page table.
     *
     * @param [in]  region      Memory region to insert. The region must remain valid
     * and unchanged as long as it's in the page table.
     *
     * @return NN_OK - region was added.
     * NN_INVALID_PARAM - memory region address is invalid (misaligned or empty)
     */
    NResult Insert(PgtRegion &region);

    /**
     * Remove a memory region from the page table.
     *
     * @param [in]  region      Memory region to remove. This must be the same pointer passed to Insert.
     * @return NN_OK - region was removed.
     * NN_INVALID_PARAM - memory region address is invalid (misaligned or empty)
     */
    NResult Remove(PgtRegion &region);

    /**
     * Find a region which contains the given address.
     *
     * @param [in]  address     Address to search.
     * @return Pointer to the region which contains 'address', or nullptr if not found.
     */
    PgtRegion *Lookup(PgtAddress address) const;

    /**
     * Search for all regions overlapping with a given address range.
     *
     * @param [in]  from        Lower bound of the range.
     * @param [in]  to          Upper bound of the range (inclusive).
     * @param [in]  cb          Callback to be called for every region found.
     * The callback must not modify the page table.
     * @param [in]  arg         User-defined argument to the callback.
     */
    void SearchRange(PgtAddress from, PgtAddress to, PgtSearchCb cb, void *arg) const;

    /**
     * Remove all regions from the page table and call the provided callback for each.
     */
    void Cleanup();

    /**
     * Dump page table to log.
     */
    void Dump();

    DEFINE_RDMA_REF_COUNT_FUNCTIONS;

private:
    NResult InsertPage(PgtAddress address, uint32_t order, PgtRegion &region);

    NResult RemovePage(PgtAddress address, uint32_t order, PgtRegion &region);
    NResult UnlinkRegion(PgtAddress address, uint32_t order, PgtDir &pgd, PgtEntry &pte, uint32_t shift,
        PgtRegion &region);

    void SearchRange(PgtAddress from, PgtAddress to, PgtSearchCb cb, void *arg);
    void SearchSubtree(PgtAddress address, uint32_t order, const PgtEntry &pte, uint32_t shift, PgtSearchCb cb,
        void *arg, PgtRegion *&lastRegion);

    void PgtEnsureCapacity(uint32_t order, PgtAddress address);
    bool PgtExpand();
    bool PgtShrink();

    NResult PgtCheckEntryDir(PgtEntry &pte, uint32_t shift, uint32_t order);
    void PgTableReset();

    PgtDir *PgtDirAlloc();
    void PgtDirRelease(PgtDir *pgd);

    void PgtDumpSubtree(uint32_t indent, const PgtEntry &pte, uint32_t pteIndex, PgtAddress base, PgtAddress mask,
        uint32_t shift);

    PgtEntry mRootEntry {};
    PgtAddress mVirBaseAddr = 0;
    PgtAddress mSpaceMask = 0;
    uint32_t mIndexShift = 0;
    uint32_t mRegionCount = 0;

    PgDirAllocCb mPgdAllocCb = nullptr;
    PgDirReleaseCb mPgdReleaseCb = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif
