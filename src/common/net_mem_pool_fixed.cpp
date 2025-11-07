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
#include "net_mem_pool_fixed.h"
#include "net_monotonic.h"

namespace ock {
namespace hcom {
NetMemPoolFixed::NetMemPoolFixed(const std::string &name, const NetMemPoolFixedOptions &options)
    : mOptions(options), mName(name)
{
    OBJ_GC_INCREASE(NetMemPoolFixed);
}

NResult NetMemPoolFixed::Initialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mInited) {
        return NN_OK;
    }

    /* validate options */
    NResult result = Validate();
    if (result != NN_OK) {
        return result;
    }

    /* reserve to avoid reallocate memory when expanding vector */
    mSuperBlocks.reserve(NN_NO1024);

    /* expand one super block from os */
    if ((result = ExpandFromOs(false)) != NN_OK) {
        DoUnInitialize();
        return result;
    }

    mInited = true;
    return NN_OK;
}

void NetMemPoolFixed::DoUnInitialize()
{
    for (auto &iter : mSuperBlocks) {
        free(iter.buffer);
        iter.buffer = nullptr;
    }

    mSuperBlocks.clear();
    mTotalSuperBlkSize = 0;
    mFreeCount = 0;
}

NResult NetMemPoolFixed::Validate()
{
    /* validate super block size, which must between 1 and 256 MB including 256 MB */
    if (mOptions.superBlkSizeMB == 0 || mOptions.superBlkSizeMB > NN_NO256) {
        NN_LOG_ERROR("Invalid superBlkSizeMB " << mOptions.superBlkSizeMB << " in mem pool " << mName <<
            ", which be 1~" << NN_NO256 << ", reset to " << NN_NO4);
        return NN_INVALID_PARAM;
    }

    /* validate thread cache expand and shrink steps, which between 8 and 256 MB */
    if (mOptions.tcExpandBlkCnt < NN_NO8 || mOptions.tcExpandBlkCnt > NN_NO256) {
        NN_LOG_ERROR("Invalid tcExpandBlkCnt " << mOptions.tcExpandBlkCnt << " in mem pool " << mName <<
            ", which be " << NN_NO8 << "~" << NN_NO256 << ", reset to " << NN_NO128);
        return NN_INVALID_PARAM;
    }

    /* validate size of min block */
    if (mOptions.minBlkSize < sizeof(NetMemPoolMinBlock)) {
        NN_LOG_ERROR("Invalid minBlkSize " << mOptions.minBlkSize << " in mem pool " << mName <<
            ", which be larger than " << sizeof(NetMemPoolMinBlock));
        return NN_INVALID_PARAM;
    }

    /* validate relation of min block size and super block size */
    uint64_t tmp = mOptions.minBlkSize * mOptions.tcExpandBlkCnt * NN_NO16;
    tmp = tmp / NN_NO1024 / NN_NO1024; /* in MB */
    if (tmp > mOptions.superBlkSizeMB) {
        NN_LOG_ERROR("Invalid minBlkSize " << mOptions.minBlkSize << " in mem pool " << mName);
        return NN_INVALID_PARAM;
    }

    uint64_t superBlkSize = mOptions.superBlkSizeMB * NN_NO1024 * NN_NO1024;
    if (superBlkSize % (mOptions.minBlkSize * mOptions.tcExpandBlkCnt)) {
        NN_LOG_ERROR("Invalid minBlkSize " << mOptions.minBlkSize << " or tcExpandBlkCnt " << mOptions.tcExpandBlkCnt <<
            " in mem pool " << mName << ", super block size is not times of " <<
            mOptions.minBlkSize * mOptions.tcExpandBlkCnt);
        return NN_INVALID_PARAM;
    }

    return NN_OK;
}

NResult NetMemPoolFixed::ExpandFromOs(bool holdFreeListLock)
{
    uint64_t startTime = NetMonotonic::TimeNs();
    /* allocate memory */
    auto superBlkSize = (mTotalSuperBlkSize == 0) ?
        (mOptions.superBlkSizeMB * NN_NO1024 * NN_NO1024) : mTotalSuperBlkSize;
    auto mem = memalign(NN_NO4096, superBlkSize);
    if (mem == nullptr) {
        NN_LOG_ERROR("Failed to malloc memory for supper block in mem pool " << mName);
        return NN_MALLOC_FAILED;
    }

    /* get physical memory */
    bzero(mem, superBlkSize);

    /* insert to super block list */
    NetMemPoolSuperBlock blk(mem, superBlkSize);
    mSuperBlocks.emplace_back(blk);

    /* add size and count */
    mTotalSuperBlkSize += superBlkSize;

    /* make free linked list */
    NetMemPoolMinBlock *head = nullptr;
    NetMemPoolMinBlock *tail = nullptr;
    uint32_t count = 0;
    NResult result = NN_OK;
    if ((result = MakeFreeList(blk, head, tail, count)) != NN_OK) {
        return result;
    }

    /* attach free linked list */
    if (holdFreeListLock) {
        mTcMutex.Lock();
    }

    if ((result = AttacheToFreeList(head, tail, count)) != NN_OK) {
        if (holdFreeListLock) {
            mTcMutex.Unlock();
        }
        return result;
    }

    if (holdFreeListLock) {
        mTcMutex.Unlock();
    }

    NN_LOG_INFO("Fixed size memory pool " << mName << " allocated " << mOptions.superBlkSizeMB <<
        "MB memory from os, total block size " << mTotalSuperBlkSize << " and split to " << count <<
        " min block with size " << mOptions.minBlkSize << " which took " <<
        (NetMonotonic::TimeNs() - startTime) / NN_NO1000 << "us, current free min block is " << mFreeCount);

    return NN_OK;
}

NResult NetMemPoolFixed::TCAlloc(NetMemPoolMinBlock &head)
{
    NN_ASSERT_LOG_RETURN(mInited, NN_NOT_INITIALIZED)
    bool flag = true;
    do {
        /* step 1: allocate from free list */
        mTcMutex.Lock();
        if (mFreeCount > 0) {
            head.next = mFreeMinBlkList.next;
            mFreeCount -= mFreeMinBlkList.next->count;
            mFreeMinBlkList.next = head.next->nextN->next;
            head.next->nextN->next = nullptr;
            mTcMutex.Unlock();
            flag = false;
            return NN_OK;
        }
        mTcMutex.Unlock();

        /* step 2: if there is no free in list, allocate from OS */
        {
            std::unique_lock<std::mutex> locker(mMutex);
            /* wait if already allocating from os by another thread */
            mCondForOs.wait(locker, [&]() { return !mAllocatingFromOs; });
            if (mFreeCount > 0) {
                continue;
            }
            mAllocatingFromOs = true;

            NResult result = ExpandFromOs(true);
            mAllocatingFromOs = false;
            mCondForOs.notify_all();
            if (result != NN_OK) {
                flag = false;
                return result;
            }
        }
    } while (flag);

    NN_LOG_ERROR("Unreachable code path");
    return NN_ERROR;
}

std::string NetMemPoolFixed::ToString()
{
    std::ostringstream oss;
    oss << "fixed-size-memory-pool [name: " << mName << ", options: [" << mOptions.ToString() <<
        "], super-block-count: " << mSuperBlocks.size() << ", super-block-size: " <<
        mTotalSuperBlkSize / NN_NO1024 / NN_NO1024 << "MB, free-min-block-count: " << mFreeCount;

    uint32_t blkIndex = 0;
    oss << " super-blocks: [";
    for (auto &iter : mSuperBlocks) {
        oss << "superBlk" << blkIndex++ << ":" << iter.size << "," << iter.buffer << " ";
    }

    oss << "] min-blocks: [";
    auto iter = mFreeMinBlkList.next;
    blkIndex = 0;
    while (iter != nullptr) {
        if (iter->nextN != nullptr) {
            oss << "** " << blkIndex++ << ":" << iter << "," << iter->nextN << "," << iter->count << " ";
        } else {
            oss << blkIndex++ << ":" << iter << " ";
        }
        iter = iter->next;
    }
    oss << "]]";

    return oss.str();
}

/* NetTCacheFixed */
NetTCacheFixed::NetTCacheFixed(NetMemPoolFixed *sharePool) : mSharedPool(sharePool)
{
    if (NN_UNLIKELY(mSharedPool == nullptr)) {
        return;
    }

    mSharedPool->IncreaseRef();

    mFreeSteps = mSharedPool->mOptions.tcExpandBlkCnt;
}

std::string NetTCacheFixed::ToString()
{
    std::ostringstream oss;
    oss << "net-thread-cache: [free-steps: " << mFreeSteps << ", current-free: " << mCurrentFree;

    auto iter = mHead.next;
    uint16_t minBlkIndex = 0;
    oss << " mini-blocks: [";
    while (iter != nullptr) {
        oss << "blk" << minBlkIndex++ << ":" << iter << " ";
        iter = iter->next;
    }
    oss << "]]";
    return oss.str();
}
}
}