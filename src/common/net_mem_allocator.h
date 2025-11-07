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
#ifndef COMMUNICATION_NET_MEM_ALLOCATOR_H
#define COMMUNICATION_NET_MEM_ALLOCATOR_H

#include <mutex>
#ifdef ALLOCATOR_PROTECTION_ENABLED
#include <sys/mman.h>
#endif

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_utils.h"
#include "net_linked_list.h"
#include "net_rb_tree.h"

#ifdef ALLOCATOR_PROTECTION_ENABLED
#define MA_MAGIC 666666 /* magic number to check whether memory node was corrupted */
#endif

#define MEM_ALLOCATOR_ROUND_UP_TO(x, align) \
    (((x) + (align)-1) / ((align)) * ((align))) /* round x to align, etc round(3,4) = 4 */

#define MA_LEN_LEN (NN_NO4)
#define MA_CANARY_LEN (NN_NO4)
#define MA_META_DATA_RESERVE_LEN                                                    \
    (NN_NO16) /* total reserve length, for memcpy's performance, 16 bytes alignment \
            is best */

namespace ock {
namespace hcom {
const uint64_t FREE_LIST_NUM = NN_NO1024;

class NetMemAllocator;

struct MediaDescribe {
    uint64_t startAddress = 0;
    uint64_t endAddress = 0;
};

struct MemoryArea {
#ifdef ALLOCATOR_PROTECTION_ENABLED
    uint64_t magic = MA_MAGIC;
#endif
    uint64_t startAddress = 0;
    uint64_t endAddress = 0;
    uint64_t length = 0;
    uint64_t index = 0;

    MemoryArea() = default;

#ifdef ALLOCATOR_PROTECTION_ENABLED
    /*
     * @brief setup memory area's magic number, since all memory areas are
     * always cast from dirty memory
     */
    inline void Initialize()
    {
        magic = MA_MAGIC;
    }
#endif
};

using MemoryAreaRawPtr = MemoryArea *;

/*
 * MemoryRegion is an inner management class for a continuous memory, it offers
 * remove and insert functions, which will sync mRoot and freeHead inside
 * routine. This class should not be used separately, it's part of
 * NetMemAllocator
 *
 * @property mRoot stores free memory blocks in red-black tree
 * @property freeHead is an array of free memory block list,list with greater
 * array index contains bigger memory block
 *
 */
class MemoryRegion {
public:
    NetRbTree<MemoryArea> mRoot;
    NetLinkedList<NetRbNode<MemoryArea>> freeHead[FREE_LIST_NUM];
    int64_t freeCnt[FREE_LIST_NUM] {};
    uint64_t totalSize = 0;
    uint64_t freeSize = 0;

private:
    NResult MemoryAreaRemove(uint64_t *startAddress, uint64_t length, uint32_t minBlockSize, bool metaStored = true);

    NResult MemoryAreaInsert(uint64_t startAddress, uint64_t length);

    void MemoryAreaInsertPre(NetRbNode<MemoryArea> *newMa, NetRbNode<MemoryArea> *ma);

    void MemoryAreaInsertNext(NetRbNode<MemoryArea> *newMa, NetRbNode<MemoryArea> *ma);

    friend NetMemAllocator;
};

using MemoryRegionRawPtr = MemoryRegion *;

using NetMemAllocatorPtr = NetRef<NetMemAllocator>;

/*
 * NetMemAllocator is a self-scaling memory pool, supporting take and put memory
 * with different size from it within pool capacity. You should bind an already
 * allocated continuous memory to NetMemAllocator, this class will not
 * alloc/malloc any memory inside
 *
 * @property mMemRegionLock mutex lock for thread safe
 * @property mMemRegionMgr inner memory manager support base operation for take
 * and put memory
 * @property mAddrMap stores taken memory with address-length pair,which help to
 * reduce one length parameter for putback api, also it prevents unmatched
 * length with some address when putback
 */
class NetMemAllocator : public UBSHcomNetMemoryAllocator {
public:
    inline NResult Initialize(uintptr_t mrAddress, uint64_t mrSize, uint32_t minBlockSize, bool alignAddress)
    {
        if (NN_UNLIKELY(mInited)) {
            if (mrAddress == mMRAddress && mrSize == mMRSize && minBlockSize == mMinBlockSize) {
                return NN_OK;
            }
            NN_LOG_ERROR("Already initialized,can not be initialized again with different parameters");
            return NN_ERROR;
        }

        if (mrAddress == 0) {
            NN_LOG_ERROR("address can not be null");
            return NN_INVALID_PARAM;
        }

        if (!POWER_OF_2(minBlockSize)) {
            NN_LOG_ERROR("minBlockSize must be power of 2");
            return NN_INVALID_PARAM;
        }

        if (minBlockSize < NN_NO4096 || minBlockSize > NN_NO1024 * NN_NO1024 * NN_NO1024) {
            NN_LOG_ERROR("minBlockSize must be at least 4096 byte and not greater than 1 gigabyte");
            return NN_INVALID_PARAM;
        }

        if (mrSize < minBlockSize) {
            NN_LOG_ERROR("mrSize must be greater than minBlockSize");
            return NN_INVALID_PARAM;
        }

        mInited = true;
        mAlignAddress = alignAddress;
        mMRAddress = mrAddress;
        mMRSize = mrSize;
        mMinBlockSize = minBlockSize;

        MediaDescribe media = {};
        media.startAddress = mMRAddress;
        media.endAddress = mMRAddress + mMRSize;

        if (media.endAddress <= media.startAddress) {
            NN_LOG_ERROR("mrSize must be legal");
            return NN_INVALID_PARAM;
        }

        auto hr = MemoryRegionInit(media);
        if (NN_UNLIKELY(hr != NN_OK)) {
            NN_LOG_ERROR("Init mem region mgr failed " << hr);
            return hr;
        }

        NN_LOG_INFO("Init mem region mgr success, mr size " << mMRSize);
        return NN_OK;
    }

    inline void Destroy() override
    {
#ifdef ALLOCATOR_PROTECTION_ENABLED
        UnProtectAllMem();
#endif
    }

    inline uintptr_t MemOffset(uintptr_t address) const override
    {
        if (address < mMRAddress) {
            NN_LOG_ERROR("invalid address in MemOffset");
        }
        return address - mMRAddress;
    }

    inline uint64_t FreeSize() const override
    {
        return mMemRegionMgr.freeSize;
    }

    inline NResult Allocate(uint64_t size, uintptr_t &mrAddress) override
    {
        if (NN_UNLIKELY(!mInited)) {
            NN_LOG_ERROR("Allocator not initialized, Allocate failed");
            return NN_NOT_INITIALIZED;
        }

        uint64_t alignedSize = MEM_ALLOCATOR_ROUND_UP_TO(size, mMinBlockSize);
        uint64_t address = 0;

        if (mAlignAddress) {
            if (NN_UNLIKELY(NN_OK != RegionMallocWithMap(address, alignedSize))) {
                NN_LOG_ERROR("Mem allocate failed");
                return NN_ERROR;
            }
        } else {
            if (NN_UNLIKELY(NN_OK != RegionMalloc(address, alignedSize, alignedSize - size))) {
                NN_LOG_ERROR("Mem allocate failed");
                return NN_ERROR;
            }
        }

        mrAddress = static_cast<uintptr_t>(address);

        NN_LOG_TRACE_INFO("Mem allocate success, addr size " << size << " alignSize " <<
            alignedSize);
        return NN_OK;
    }

    inline NResult Free(uintptr_t mrAddress) override
    {
        if (NN_UNLIKELY(!mInited)) {
            NN_LOG_ERROR("Allocator not initialized, Free failed");
            return NN_NOT_INITIALIZED;
        }

        if (NN_UNLIKELY(mrAddress == 0)) {
            NN_LOG_WARN("mrAddress is zero, directly back");
            return NN_INVALID_PARAM;
        }

        if (NN_UNLIKELY((mrAddress < mMRAddress) || (mrAddress > mMRAddress + mMRSize))) {
            NN_LOG_ERROR("Mem free failed, because address is not overlapped");
            return NN_ERROR;
        }

        NResult hr = NN_OK;
        if (mAlignAddress) {
            hr = RegionFreeWithMap(static_cast<uint64_t>(mrAddress));
        } else {
            hr = RegionFree(static_cast<uint64_t>(mrAddress));
        }

        if (NN_UNLIKELY(hr != NN_OK)) {
            NN_LOG_ERROR("Mem free failed");
            return hr;
        }

        NN_LOG_TRACE_INFO("Mem free success");
        return NN_OK;
    }

    inline NResult GetSizeByAddressNoAlign(uint64_t startAddress, uint64_t &length)
    {
        if (startAddress != mMRAddress) {
            if (startAddress < MA_META_DATA_RESERVE_LEN) {
                NN_LOG_WARN("address don't have enough space for meta data");
                return NN_ERROR;
            }
            if (startAddress < mMRAddress || startAddress >= mMRAddress + mMRSize) {
                NN_LOG_WARN("address is illegal");
                return NN_ERROR;
            }
            auto metaReqLenAddress =
                reinterpret_cast<uint64_t *>(startAddress - MA_META_DATA_RESERVE_LEN + MA_CANARY_LEN + MA_LEN_LEN);
            length = *metaReqLenAddress;
        } else {
            if (NN_UNLIKELY(mFirstReqLength == 0)) {
                NN_LOG_ERROR("Address Invalid in GetSizeByAddressNoAlign");
                return NN_ERROR;
            }
            length = mFirstReqLength;
        }

        return NN_OK;
    }

    inline NResult GetSizeByAddressAlign(uint64_t startAddress, uint64_t &length)
    {
        std::lock_guard<std::mutex> lock(mMemRegionLock);
        auto iter = mAddrLenMap.find(startAddress);
        if (NN_LIKELY(iter != mAddrLenMap.end())) {
            length = iter->second;
            return NN_OK;
        }

        return NN_ERROR;
    }

    inline uint32_t MinBlockSize() const
    {
        return mMinBlockSize;
    }

protected:
    uintptr_t mMRAddress = 0;
    uint64_t mMRSize = 0;

private:
    NResult RegionFree(uint64_t startAddress);

    NResult RegionFreeWithMap(uint64_t startAddress);

    NResult RegionMalloc(uint64_t &startAddress, uint64_t length, uint64_t deltaLength);

    NResult RegionMallocWithMap(uint64_t &startAddress, uint64_t length);

    NResult MemoryRegionJoin(MediaDescribe *mediaDesc);

    void MemoryRegionInitial();

    NResult MemoryRegionInit(MediaDescribe &media);

#ifdef ALLOCATOR_PROTECTION_ENABLED
    /*
     * @brief traverse all (free) memory area nodes to confirm no ma node was corrupted,
     * this function works when mprotect unavaliable, such as shared memory based allocator,
     * where memory user is a different process to allocator owner, thus they have different
     * pagetables, mprotect doesn't work
     * @return false if corrupted node was found(usually caused by uaf or overwrite),
     * otherwise return true,
     */
    inline bool CheckNodes()
    {
        for (auto &i : mMemRegionMgr.freeHead) {
            auto cur = &i.head;
            do {
                if (cur->data.data.magic != MA_MAGIC) {
                    NN_LOG_ERROR("free memory node " << (uint64_t)cur <<
                        " was corrupted, usually caused by use after free.");
                    return false;
                }
                cur = cur->next;
            } while (cur != &i.head);
        }

        return true;
    }

    /*
     * @brief call mprotect on all (free) memory area nodes, which let only read operation
     * allowed for those memories, once ProtectFreeMem called, if free memories were written,
     * SIGSEGV will rise
     *
     * mprotect() changes the access protections for the calling
     * process's memory pages containing any part of the address range
     * in the interval [addr, addr+len-1].  addr must be aligned to a
     * page boundary.
     *
     * If the calling process tries to access memory in a manner that
     * violates the protections, then the kernel generates a SIGSEGV
     * signal for the process.
     */
    inline void ProtectFreeMem()
    {
        for (auto &i : mMemRegionMgr.freeHead) {
            auto cur = &i.head;
            do {
                mprotect(cur, cur->data.data.length, PROT_READ);
                cur = cur->next;
            } while (cur != &i.head);
        }
    }

    /*
     * @brief remove write protection added by ProtectFreeMem, this is only called before
     * internal manipulation on allocator, to improve performance, just change all memory
     * managed to read write is safe
     */
    inline void UnProtectAllMem()
    {
        mprotect((void *)mMRAddress, mMRSize, PROT_READ | PROT_WRITE);
    }
#endif

    std::mutex mMemRegionLock;
    MemoryRegion mMemRegionMgr;
    uint64_t mFirstAllocLength = 0;
    uint64_t mFirstReqLength = 0;
    uint32_t mMinBlockSize = NN_NO4096;
    bool mInited = false;
    bool mAlignAddress = false;
    std::unordered_map<uint64_t, uint64_t> mAddrLenMap;
};
} // namespace hcom
} // namespace ock
#endif // COMMUNICATION_NET_MEM_ALLOCATOR_H
