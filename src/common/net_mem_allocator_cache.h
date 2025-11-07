/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
change * Author: bao, lu
 */
#ifndef HCOM_NET_MEM_ALLOCATOR_CACHE_H
#define HCOM_NET_MEM_ALLOCATOR_CACHE_H

#include "hcom.h"
#include "net_addr_size_map.h"
#include "net_mem_allocator.h"

namespace ock {
namespace hcom {
/*
 * @brief Link node of blocks which are same size of block
 *
 * NOTE: make sure all memory is aligned
 */
struct NetMemAllocCacheLinkNode {
    NetMemAllocCacheLinkNode *next = nullptr; /* link node for block allocated from major allocator */
    uint32_t lock = 0;
    uint32_t accessCount = 0;        /* access count, only for head */
    uint32_t blockSizeInKB = NN_NO4; /* size of the block in KB, only for head */
    uint16_t currentBlocks = 0;      /* current cached count of blocks */
    /*
     * @brief Spin lock
     */
    void Lock()
    {
        while (!__sync_bool_compare_and_swap(&lock, 0, NN_NO1)) {
        }
    }

    /*
     * @brief Unlock
     */
    void Unlock()
    {
        __atomic_store_n(&lock, 0, __ATOMIC_SEQ_CST);
    }
};

/*
 * @brief Tier choose interface and functions
 */
using NetCacheTierFunc = uint16_t(uint64_t size, uint64_t baseSize);

/*
 * @brief This is a lockless allocator cache, which allocates several size of block in batch from major allocator
 */
class NetAllocatorCache : public UBSHcomNetMemoryAllocator {
public:
    explicit NetAllocatorCache(NetMemAllocator *majorAllocator) : mMajorAllocator(majorAllocator)
    {
        if (mMajorAllocator != nullptr) {
            mMajorAllocator->IncreaseRef();
        }
    }

    ~NetAllocatorCache() override
    {
        UnInitialize();
    }

    NResult Initialize(const UBSHcomNetMemoryAllocatorOptions &options);
    void UnInitialize();

    NResult Allocate(uint64_t size, uintptr_t &address) override
    {
        NN_ASSERT_LOG_RETURN(mMajorAllocator != nullptr, NN_ERROR)

        /* if the size is larger than max cache block size */
        if (NN_UNLIKELY(size > mMaxCacheBlockSize)) {
            return mMajorAllocator->Allocate(size, address);
        }

        /* get tier index, increase access count */
        auto tierIndex = mTierChooseFunc(size, mBaseBlockSize);
        NN_ASSERT_LOG_RETURN(tierIndex < mBlockTierCount, NN_INVALID_PARAM);

        auto &oneList = mTieredBlockHead[tierIndex];
        oneList.accessCount++;

        /* allocated from tiered block linked list */
        oneList.Lock();
        if (oneList.next != nullptr) {
            address = reinterpret_cast<uintptr_t>(oneList.next);
            oneList.next = oneList.next->next;
            --oneList.currentBlocks;
            mTotalCacheSizeKB -= oneList.blockSizeInKB;
            oneList.Unlock();
            /* it is aligned need to record size with address into hashmap */
            if (mAligned) {
                uint32_t timesOfBaseSize = oneList.blockSizeInKB * NN_NO1024 / mBaseBlockSize;
                mAddress2SizeMap->Put(address, timesOfBaseSize);
            }
            return NN_OK;
        }

        /* if not allocated then allocate from major pool */
        uintptr_t majorAddress = 0;
        NetMemAllocCacheLinkNode *newNode = nullptr;
        for (uint16_t i = 0; i < mBlockCacheCountPerTier; i++) {
            if (mMajorAllocator->Allocate(oneList.blockSizeInKB * NN_NO1024, majorAddress) != NN_OK) {
                break;
            }

            /* added to one list, if it is first allocate from major
             * and remember this as newNode for link next allocated memory block
             */
            ++oneList.currentBlocks;
            if (newNode == nullptr) {
                newNode = reinterpret_cast<NetMemAllocCacheLinkNode *>(majorAddress);
                newNode->next = nullptr;
                oneList.next = newNode;
            } else {
                newNode->next = reinterpret_cast<NetMemAllocCacheLinkNode *>(majorAddress);
                newNode = newNode->next;
                newNode->next = nullptr;
            }

            mTotalCacheSizeKB += oneList.blockSizeInKB;
        }

        /* allocate from cache again, it happens when allocated from major */
        if (oneList.next != nullptr) {
            address = reinterpret_cast<uintptr_t>(oneList.next);
            oneList.next = oneList.next->next;
            --oneList.currentBlocks;
            mTotalCacheSizeKB -= oneList.blockSizeInKB;
            oneList.Unlock();
            /* it is aligned need to remember size with address into hashmap */
            if (mAligned) {
                uint32_t timesOfBaseSize = oneList.blockSizeInKB * NN_NO1024 / mBaseBlockSize;
                mAddress2SizeMap->Put(address, timesOfBaseSize);
            }
            return NN_OK;
        }

        /* do later, free some bigger from cache */

        /* unlock */
        oneList.Unlock();

        return NN_ERROR;
    }

    inline NResult Free(uintptr_t address) override
    {
        NN_ASSERT_LOG_RETURN(mMajorAllocator != nullptr, NN_ERROR);

        uint64_t size = 0;
        /* firstly get size */
        if (!mAligned) {
            auto result = mMajorAllocator->GetSizeByAddressNoAlign(address, size);
            if (NN_UNLIKELY(result != NN_OK)) {
                NN_LOG_WARN("Try to free invalid address in allocator cache");
                return result;
            }

            if (size > mMaxCacheBlockSize) {
                return mMajorAllocator->Free(address);
            }

            goto FREE_TO_CACHE;
        } else {
            /* find from address to size map for cache */
            uint32_t timesOfBaseSize = 0;
            if (mAddress2SizeMap->Remove(address, timesOfBaseSize) == NN_OK) {
                size = timesOfBaseSize * mBaseBlockSize;
                goto FREE_TO_CACHE;
            }

            /* if not found, try to find from major allocator */
            auto result = mMajorAllocator->GetSizeByAddressAlign(address, size);
            if (NN_UNLIKELY(result != NN_OK)) {
                NN_LOG_WARN("Try to free invalid address in allocator cache");
                return result;
            }

            /* if found in major allocator, free it */
            return mMajorAllocator->Free(address);
        }

    FREE_TO_CACHE:
        /*
         * attach to linked list for this
         * step1: get tiered index
         */
        auto tierIndex = mTierChooseFunc(size, mBaseBlockSize);
        NN_ASSERT_LOG_RETURN(tierIndex < mBlockTierCount, NN_INVALID_PARAM);

        /* step2: attach */
        auto &oneList = mTieredBlockHead[tierIndex];
        oneList.Lock();
        auto tmp = reinterpret_cast<NetMemAllocCacheLinkNode *>(address);
        tmp->next = oneList.next;
        oneList.next = tmp;
        mTotalCacheSizeKB += oneList.blockSizeInKB;
        /* if not two times cached, return */
        if (++oneList.currentBlocks < mBlockCacheCountPerTier * NN_NO2) {
            oneList.Unlock();
            return NN_OK;
        }

        NetMemAllocCacheLinkNode *returnHead = oneList.next;
        for (uint16_t i = 0; i < mBlockCacheCountPerTier - NN_NO2; ++i) {
            returnHead = returnHead->next;
        }

        /* set last node will not be returned */
        NetMemAllocCacheLinkNode *lastRetained = returnHead->next;
        returnHead = lastRetained->next;
        lastRetained->next = nullptr;
        mTotalCacheSizeKB -= oneList.blockSizeInKB * mBlockCacheCountPerTier;
        oneList.currentBlocks -= mBlockCacheCountPerTier;
        /* unlock */
        oneList.Unlock();

        NetMemAllocCacheLinkNode *next = nullptr;
        /* free to major allocator */
        while (returnHead != nullptr) {
            next = returnHead->next;
            (void)mMajorAllocator->Free(reinterpret_cast<uintptr_t>(returnHead));
            returnHead = next;
        }

        return NN_OK;
    }

    inline uintptr_t MemOffset(uintptr_t address) const override
    {
        NN_ASSERT_LOG_RETURN(mMajorAllocator != nullptr, 0)
        return mMajorAllocator->MemOffset(address);
    }

    uint64_t FreeSize() const override
    {
        NN_ASSERT_LOG_RETURN(mMajorAllocator != nullptr, 0)
        return mTotalCacheSizeKB * NN_NO1024 + mMajorAllocator->FreeSize();
    }

private:
    using NetAddressSizeMap = NetAddress2SizeHashMap<NetHeapAllocator>;

    NetMemAllocCacheLinkNode *mTieredBlockHead = nullptr; /* tiered buckets */
    NetMemAllocator *mMajorAllocator = nullptr;           /* major allocator */
    NetAddressSizeMap *mAddress2SizeMap = nullptr;        /* hash map for address to size, for unaligned allocator */
    NetCacheTierFunc *mTierChooseFunc = nullptr;          /* tier choose function */
    uint64_t mMaxCacheBlockSize = NN_NO4096;              /* max block size, if >= this, allocate from major */
    uint64_t mTotalCacheSizeKB = 0;                       /* total cache size in KB */
    uint64_t mBaseBlockSize = NN_NO4096;                  /* min block size */
    uint16_t mBlockCacheCountPerTier = NN_NO16;           /* cached block count in each tier */
    uint16_t mBlockTierCount = 0;                         /* timer count */
    bool mAligned = true;                                 /* address is aligned or not */
};
}
}

#endif // HCOM_NET_MEM_ALLOCATOR_CACHE_H
