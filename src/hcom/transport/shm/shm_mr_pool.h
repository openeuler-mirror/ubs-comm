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
#ifndef OCK_HCOM_SHM_MEMORY_REGION_H_23234
#define OCK_HCOM_SHM_MEMORY_REGION_H_23234

#include <atomic>

#include "hcom.h"
#include "shm_channel.h"

namespace ock {
namespace hcom {
class ShmMemoryRegion : public UBSHcomNetMemoryRegion {
public:
    static NResult Create(const std::string &name, uint64_t size, ShmMemoryRegion *&buf);
    static NResult Create(const std::string &name, uintptr_t address, uint64_t size, ShmMemoryRegion *&buf);

    void *GetMemorySeg() override
    {
        return nullptr;
    }

    void GetVa(uint64_t &va, uint64_t &vaLen, uint32_t &tokenId) override
    {
        return;
    }

public:
    ShmMemoryRegion(const std::string &name, bool extMem, uintptr_t extMemAddress, uint64_t size)
        : UBSHcomNetMemoryRegion(name, extMem, extMemAddress, size)
    {
        OBJ_GC_INCREASE(ShmMemoryRegion);
    }

    ~ShmMemoryRegion() override
    {
        OBJ_GC_DECREASE(ShmMemoryRegion);
    }

    NResult Initialize() override;
    void UnInitialize() override;

    virtual ShmHandlePtr GetMrHandle()
    {
        return mMrHandle;
    }

private:
    uint32_t GenerateKey();

private:
    std::mutex mMutex;
    bool mInited = false;
    ShmHandlePtr mMrHandle = nullptr;
    static std::atomic<uint32_t> shmLocalKeyIndex;
};
}
}

#endif // OCK_HCOM_SHM_MEMORY_REGION_H_23234
