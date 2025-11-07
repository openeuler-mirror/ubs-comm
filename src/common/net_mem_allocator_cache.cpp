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
#include "net_common.h"
#include "net_mem_allocator_cache.h"

namespace ock {
namespace hcom {
uint16_t NetCacheTierFuncTimes(uint64_t size, uint64_t baseSize)
{
    /*
     * 1 firstly make the times of base size
     * 2 secondly get tier index
     */
    return NetFunc::NN_RoundUpTo(size, baseSize) / baseSize - 1;
}

uint16_t NetCacheTierFuncPower(uint64_t size, uint64_t baseSize)
{
    return NetFunc::NN_PowerOfNIndex(size, baseSize);
}

NResult NetAllocatorCache::Initialize(const UBSHcomNetMemoryAllocatorOptions &options)
{
    if (mTieredBlockHead != nullptr) {
        return NN_OK;
    }

    mAligned = options.alignedAddress;

    /* validate cache tier count */
    if (options.cacheTierCount == NN_NO0 || options.cacheTierCount > NN_NO8192) {
        NN_LOG_ERROR("Invalid cacheTierCount " << options.cacheTierCount << " for allocator cache, which should <= " <<
            NN_NO8192 << " and != " << NN_NO0);
        return NN_INVALID_PARAM;
    }

    /* validate cache block count in each tier and assign */
    if (options.cacheBlockCountPerTier < NN_NO4 || options.cacheBlockCountPerTier > NN_NO8192) {
        NN_LOG_ERROR("Invalid cacheBlockCountPerTier " << options.cacheBlockCountPerTier <<
            " for allocate cache, which should between " << NN_NO4 << "~" << NN_NO8192);
        return NN_INVALID_PARAM;
    }
    mBlockCacheCountPerTier = options.cacheBlockCountPerTier;

    /* ref major allocator */
    if (mMajorAllocator == nullptr) {
        NN_LOG_ERROR("Failed to allocator cache as major allocator is null");
        return NN_INVALID_PARAM;
    }

    mBaseBlockSize = mMajorAllocator->MinBlockSize();

    /* block tier count */
    mBlockTierCount = options.cacheTierCount;
    if (options.cacheTierPolicy == TIER_TIMES) {
        mTierChooseFunc = &NetCacheTierFuncTimes;
        mMaxCacheBlockSize = mBaseBlockSize * mBlockTierCount;
    } else if (options.cacheTierPolicy == TIER_POWER) {
        if (mBlockTierCount > NN_NO31) {
            NN_LOG_ERROR("Invalid cacheTierCount " << options.cacheTierCount <<
                " for allocator cache, since the cacheTierPolicy is TIER_POWER, then it should <= " << NN_NO31 <<
                " and != " << NN_NO0);
            return NN_INVALID_PARAM;
        }
        mTierChooseFunc = &NetCacheTierFuncPower;
        uint64_t timesOfPower2 = 1 << (mBlockTierCount - 1);
        mMaxCacheBlockSize = mBaseBlockSize * timesOfPower2;
    } else {
        NN_ASSERT_LOG_RETURN(false, NN_INVALID_PARAM);
    }

    /* allocate address to size map */
    if (options.alignedAddress) {
        mAddress2SizeMap = new (std::nothrow) NetAddressSizeMap();
        if (NN_UNLIKELY(mAddress2SizeMap == nullptr)) {
            NN_LOG_ERROR("Failed to new address to size map for allocator cache");
            return NN_NEW_OBJECT_FAILED;
        }
        if (NN_UNLIKELY(mAddress2SizeMap->Initialize(options.bucketCount))) {
            delete mAddress2SizeMap;
            mAddress2SizeMap = nullptr;
            NN_LOG_ERROR("Failed to initialize address to size map for allocator cache");
            return NN_NEW_OBJECT_FAILED;
        }
    }

    /* allocate tier head and get physical memory */
    mTieredBlockHead = new (std::nothrow) NetMemAllocCacheLinkNode[mBlockTierCount];
    if (mTieredBlockHead == nullptr) {
        UnInitialize();
        NN_LOG_ERROR("Failed to new tier buckets head for allocator cache");
        return NN_NEW_OBJECT_FAILED;
    }
    bzero(mTieredBlockHead, sizeof(NetMemAllocCacheLinkNode) * mBlockTierCount);

    /* set tier size for buckets */
    for (uint16_t i = 0; i < mBlockTierCount; i++) {
        if (options.cacheTierPolicy == TIER_TIMES) {
            mTieredBlockHead[i].blockSizeInKB = (mBaseBlockSize / NN_NO1024) * (i + 1);
        } else if (options.cacheTierPolicy == TIER_POWER) {
            mTieredBlockHead[i].blockSizeInKB = (mBaseBlockSize / NN_NO1024) * (1 << i);
        }
    }

    NN_LOG_INFO("Initialized allocator cache, aligned " << mAligned << ", tierCount " << mBlockTierCount <<
        ", blockCountPerTier " << mBlockCacheCountPerTier << ", minBlockSize " << mBaseBlockSize <<
        ", maxCacheBlockSize " << mMaxCacheBlockSize << ", tier bucket heads occupied memory " <<
        (sizeof(NetMemAllocCacheLinkNode) * mBlockTierCount));

    return NN_OK;
}

void NetAllocatorCache::UnInitialize()
{
    if (mMajorAllocator != nullptr) {
        mMajorAllocator->DecreaseRef();
        mMajorAllocator = nullptr;
    }

    if (mAddress2SizeMap != nullptr) {
        delete mAddress2SizeMap;
        mAddress2SizeMap = nullptr;
    }

    if (mTieredBlockHead != nullptr) {
        delete[] mTieredBlockHead;
        mTieredBlockHead = nullptr;
    }
}
}
}