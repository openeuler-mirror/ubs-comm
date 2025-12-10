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
#ifndef OCK_HCOM_NET_MEMORY_REGION_H_23234
#define OCK_HCOM_NET_MEMORY_REGION_H_23234

#include <atomic>

#include "hcom.h"

namespace ock {
namespace hcom {
class NormalMemoryRegion : public UBSHcomNetMemoryRegion {
public:
    static NResult Create(const std::string &name, uint64_t size, NormalMemoryRegion *&buf);
    static NResult Create(const std::string &name, uintptr_t address, uint64_t size, NormalMemoryRegion *&buf);

public:
    NormalMemoryRegion(const std::string &name, bool extMem, uintptr_t extMemAddress, uint64_t size)
        : UBSHcomNetMemoryRegion(name, extMem, extMemAddress, size)
    {}

    NResult Initialize() override;
    void UnInitialize() override;

    void *GetMemorySeg() override
    {
        return nullptr;
    }

    void GetVa(uint64_t &va, uint64_t &vaLen, uint32_t &tokenId) override
    {
        return;
    }

    uint8_t *GetEidRaw() override
    {
        return nullptr;
    }

private:
    std::mutex mMutex;
    bool mInited = false;

    static std::atomic<uint32_t> shmLocalKeyIndex;
};

/* ***************************************************************************************************** */
class NormalMemoryRegionFixedBuffer : public NormalMemoryRegion {
public:
    static NResult Create(const std::string &name, uint32_t singleSegSize, uint32_t segCount,
        NormalMemoryRegionFixedBuffer *&buf);

public:
    NormalMemoryRegionFixedBuffer(const std::string &name, uint32_t singleSegSize, uint32_t segCount)
        : NormalMemoryRegion(name, false, 0, static_cast<uint64_t>(singleSegSize) * static_cast<uint64_t>(segCount)),
          mSingleSegSize(singleSegSize),
          mSegCount(segCount),
          mUnAllocated(segCount)
    {}

    ~NormalMemoryRegionFixedBuffer() override
    {
        UnInitialize();
    }

    NResult Initialize() override;
    void UnInitialize() override;

    inline uint32_t GetFreeBufferCount()
    {
        return mUnAllocated.Size();
    }

    inline bool GetFreeBuffer(uintptr_t &item)
    {
        return mUnAllocated.PopFront(item);
    }

    inline bool GetFreeBufferN(uintptr_t *items, uint32_t n)
    {
        if (NN_UNLIKELY(items == nullptr)) {
            return false;
        }
        return mUnAllocated.PopFrontN(items, n);
    }

    inline bool ReturnBuffer(uintptr_t value)
    {
        return mUnAllocated.PushFront(value);
    }

    std::string ToString()
    {
        std::ostringstream oss;
        oss << "NormalMemoryRegionFixedBuffer info: mBuf " << mBuf << ", mSingleSegSize " << mSingleSegSize <<
            ", mSegCount " << mSegCount << ", unAllocatedSize " << mUnAllocated.Size() << ", total buf size " << mSize;
        return oss.str();
    }

    inline uint32_t GetSingleSegSize() const
    {
        return mSingleSegSize;
    }

private:
    uint32_t mSingleSegSize = 0;
    uint32_t mSegCount = 0;

    // uintptr_p store the start address of each mr segment
    NetRingBuffer<uintptr_t> mUnAllocated;
};
}
}

#endif // OCK_HCOM_NET_MEMORY_REGION_H_23234
