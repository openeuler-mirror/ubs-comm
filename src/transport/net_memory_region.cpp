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
#include "net_memory_region.h"

namespace ock {
namespace hcom {
std::atomic<uint16_t> NormalMemoryRegion::KEY_ID(0);
std::atomic<uint32_t> NormalMemoryRegion::LOCAL_KEY_INDEX(0);

NResult NormalMemoryRegion::Create(const std::string &name, uint64_t size, NormalMemoryRegion *&buf)
{
    if (NN_UNLIKELY(size == 0)) {
        NN_LOG_ERROR("Failed to create normal memory region as size is zero");
        return NN_INVALID_PARAM;
    }

    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, false, 0, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create normal memory region");
        return NN_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return NN_OK;
}

NResult NormalMemoryRegion::Create(const std::string &name, uintptr_t address, uint64_t size, NormalMemoryRegion *&buf)
{
    if (NN_UNLIKELY(address == 0 || size == 0)) {
        NN_LOG_ERROR("Failed to create normal memory region as address or size is zero");
        return NN_INVALID_PARAM;
    }

    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, true, address, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create normal memory region");
        return NN_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return NN_OK;
}

NResult NormalMemoryRegion::Initialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mInited) {
        return NN_OK;
    }

    if (mExternalMemory) {
        if ((mBuf == 0 || mSize == 0)) {
            NN_LOG_ERROR("Invalid external memory address or size for normal memory region " << mName);
            return NN_INVALID_PARAM;
        }

        mLKey = LOCAL_KEY_INDEX.fetch_add(1);
        mInited = true;

        /* don't do bzero to external memory, because this may clean user's data */
        return NN_OK;
    }

    /* allocate memory */
    auto tmpBuf = memalign(NN_NO4096, mSize);
    if (tmpBuf == nullptr) {
        NN_LOG_ERROR("Failed to allocate memory for normal memory region " << mName << " with size " << mSize);
        return NN_MALLOC_FAILED;
    }

    bzero(tmpBuf, mSize);
    mBuf = reinterpret_cast<uintptr_t>(tmpBuf);
    mLKey = LOCAL_KEY_INDEX.fetch_add(1);
    mInited = true;
    return NN_OK;
}

void NormalMemoryRegion::UnInitialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        return;
    }

    if (!mExternalMemory) {
        free(reinterpret_cast<void *>(mBuf));
        mBuf = 0;
    }
    mInited = false;
}

/* NormalMemoryRegionFixedBuffer */
NResult NormalMemoryRegionFixedBuffer::Create(const std::string &name, uint32_t singleSegSize, uint32_t segCount,
    NormalMemoryRegionFixedBuffer *&buf)
{
    auto tmp = new (std::nothrow) NormalMemoryRegionFixedBuffer(name, singleSegSize, segCount);
    if (tmp == nullptr) {
        return NN_NEW_OBJECT_FAILED;
    }

    buf = tmp;
    return NN_OK;
}


NResult NormalMemoryRegionFixedBuffer::Initialize()
{
    NResult result = NN_OK;
    if ((result = NormalMemoryRegion::Initialize()) != NN_OK) {
        return result;
    }

    /* init unAllocated container */
    if ((result = mUnAllocated.Initialize()) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize un-allocated ring buffer in NormalMemoryRegionFixedBuffer " << mName);
        return result;
    }

    /* init un-allocated */
    uintptr_t address = mBuf;
    for (uint32_t i = 0; i < mSegCount; i++) {
        mUnAllocated.PushBack(address);
        address += mSingleSegSize;
    }

    return NN_OK;
}

void NormalMemoryRegionFixedBuffer::UnInitialize()
{
    mUnAllocated.UnInitialize();
    NormalMemoryRegion::UnInitialize();
}
}
}