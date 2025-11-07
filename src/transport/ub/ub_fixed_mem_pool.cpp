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
#ifdef UB_BUILD_ENABLED
#include "ub_fixed_mem_pool.h"
namespace ock {
namespace hcom {

UResult UBFixedMemPool::Initialize()
{
    auto tmpBuf = memalign(NN_NO4096, mTotalSize);
    if (tmpBuf == nullptr) {
        NN_LOG_ERROR("Failed to allocate memory for UBFixedMemPool with size " << mTotalSize);
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    mBuf = reinterpret_cast<uintptr_t>(tmpBuf);
    if (MakeFreeList() != UB_OK) {
        NN_LOG_ERROR("Failed to make free list");
        UnInitialize();
        return UB_ERROR;
    }
    NN_LOG_INFO("UB mempool initialized total size = " << mTotalSize << " blk size = " << mBlkSize << " blk cnt = "
        << mBlkCnt);
    return UB_OK;
}

UResult UBFixedMemPool::MakeFreeList()
{
    if (mBuf == 0 || mBlkSize * mBlkCnt != mTotalSize) {
        NN_LOG_ERROR("Failed to make free list as invalid parameter");
        return UB_PARAM_INVALID;
    }
    auto address = mBuf;
    auto iter = reinterpret_cast<UBMemPoolMinBlock *>(address);
    mHead.next = iter;
    for (uint32_t i = 1; i < mBlkCnt; ++i) {
        address += mBlkSize;
        iter->next = reinterpret_cast<UBMemPoolMinBlock *>(address);
        iter = reinterpret_cast<UBMemPoolMinBlock *>(address);
    }
    iter->next = nullptr;
    return UB_OK;
}

bool UBFixedMemPool::GetFreeBuffer(uintptr_t &buf)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (NN_UNLIKELY(mHead.next == nullptr)) {
        NN_LOG_ERROR("Failed to get buffer as no free buffer");
        return false;
    }
    auto tmp = mHead.next;
    mHead.next = tmp->next;
    buf = reinterpret_cast<uintptr_t>(tmp);
    return true;
}

bool UBFixedMemPool::ReturnBuffer(uintptr_t buf)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (NN_UNLIKELY(!((buf >= mBuf) && (buf - mBuf < mTotalSize) && ((buf - mBuf) % mBlkSize == 0)))) {
        NN_LOG_ERROR("Failed to return buffer as invalid address");
        return false;
    }
    auto tmp = reinterpret_cast<UBMemPoolMinBlock *>(buf);
    tmp->next = mHead.next;
    mHead.next = tmp;
    return true;
}
}
}
#endif