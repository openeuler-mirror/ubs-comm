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
#include "shm_mr_pool.h"
#include "shm_handle.h"

namespace ock {
namespace hcom {
std::atomic<uint32_t> ShmMemoryRegion::shmLocalKeyIndex(0);

NResult ShmMemoryRegion::Create(const std::string &name, uint64_t size, ShmMemoryRegion *&buf)
{
    if (NN_UNLIKELY(size == 0)) {
        NN_LOG_ERROR("Failed to create shm memory region as size is zero");
        return NN_INVALID_PARAM;
    }

    auto tmpBuf = new (std::nothrow) ShmMemoryRegion(name, false, 0, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        return NN_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return NN_OK;
}

NResult ShmMemoryRegion::Create(const std::string &name, uintptr_t address, uint64_t size, ShmMemoryRegion *&buf)
{
    if (NN_UNLIKELY(address == 0 || size == 0)) {
        NN_LOG_ERROR("Failed to create shm memory region as size or address is zero");
        return NN_INVALID_PARAM;
    }

    auto tmpBuf = new (std::nothrow) ShmMemoryRegion(name, true, address, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        return NN_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return NN_OK;
}

NResult ShmMemoryRegion::Initialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mInited) {
        return NN_OK;
    }

    if (mExternalMemory) {
        if ((mBuf == 0 || mSize == 0)) {
            NN_LOG_ERROR("Invalid external memory address or size for Shm memory region " << mName);
            return NN_INVALID_PARAM;
        }

        mLKey = GenerateKey();
        mInited = true;

        /* don't do bzero to external memory, because this may clean user's data */
        return NN_OK;
    }

    /* allocate memory */
    uint64_t newId = NetUuid::GenerateUuid();
    ShmHandlePtr mrHandle = new (std::nothrow) ShmHandle(mName, "mr_" + mName, newId, mSize, true);
    if (NN_UNLIKELY(mrHandle == nullptr)) {
        NN_LOG_ERROR("Failed to create shm handle for shm memory region " << mName);
        return NN_NEW_OBJECT_FAILED;
    }

    if (mrHandle->Initialize() != NN_OK) {
        NN_LOG_ERROR("Failed to initialize shm handle for shm memory region " << mName);
        return NN_NOT_INITIALIZED;
    }

    auto tmpBuf = mrHandle->ShmAddress();
    if (tmpBuf == 0) {
        NN_LOG_ERROR("Failed to allocate memory for Shm memory region " << mName << " with size " << mSize);
        return NN_MALLOC_FAILED;
    }

    mBuf = tmpBuf;
    mLKey = GenerateKey();
    mMrHandle = mrHandle;
    mInited = true;
    return NN_OK;
}

void ShmMemoryRegion::UnInitialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        return;
    }

    if (!mExternalMemory && mMrHandle != nullptr) {
        mMrHandle->UnInitialize();
        mMrHandle = nullptr;
        mBuf = 0;
    }

    mInited = false;
}

inline uint32_t ShmMemoryRegion::GenerateKey()
{
    // 获取完整PID并使用哈希混合
    uint32_t pid = static_cast<uint32_t>(getpid());
    std::hash<uint32_t> hashCount;

    // 混合PID、索引和时间哈希
    uint32_t mix =
        hashCount(pid) ^ hashCount(shmLocalKeyIndex.fetch_add(1)) ^ (static_cast<uint32_t>(time(nullptr)) & 0xFFFF);

    // 二次混合确保均匀分布
    return (mix ^ (mix >> NN_NO16)) * 0x45d9f3b;
}
}
}