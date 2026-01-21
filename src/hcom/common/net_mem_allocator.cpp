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
#include "net_mem_allocator.h"

#define MEM_ALLOCATOR_ATOMIC_INC(x) __sync_add_and_fetch((x), 1)
#define MEM_ALLOCATOR_ATOMIC_DEC(x) __sync_sub_and_fetch((x), 1)

/*  MetaInfo layout || indicates memory node separate, | indicates information
 * part separate
 * ---- || -----|#c_a(4 bytes canary)|4096(4 bytes length of next memory block)
 * || X(allocated memory)-------- || ------
 */

#define CAST_TO_LIST_NODE(any) (reinterpret_cast<NetLinkedListNode<NetRbNode<MemoryArea>> *>(any))
#define CAST_TO_LIST(any) (reinterpret_cast<NetLinkedList<NetRbNode<MemoryArea>> *>(any))
#define CAST_TO_RBNODE(any) (reinterpret_cast<NetRbNode<MemoryArea> *>(any))
#define NODE_SIZE sizeof(NetLinkedList<NetRbNode<MemoryArea>>)

namespace ock {
namespace hcom {
const uint64_t MEM_ALLOCATOR_BASE_SHIFT = NN_NO12; /* 1 << 12 == 4096 */

/*
 * @brief init member MemoryRegion
 */
void NetMemAllocator::MemoryRegionInitial()
{
    mMemRegionMgr.mRoot.ref = nullptr;

    for (auto &index : mMemRegionMgr.freeCnt) {
        index = 0LL;
    }

    mMemRegionMgr.totalSize = 0UL;
    mMemRegionMgr.freeSize = 0UL;
}

#define VALIDATE_MEM_AREA(ma)                                      \
    if (NN_UNLIKELY((ma) == nullptr)) {                            \
        NN_LOG_ERROR("Invalid param, ma must be correct address"); \
        return;                                                    \
    }

#define VALIDATE_NEW_MEM_AREA(newMa)                                  \
    if (NN_UNLIKELY((newMa) == nullptr)) {                            \
        NN_LOG_ERROR("Invalid param, newMa must be correct address"); \
        return;                                                       \
    }

#define VALIDATE_MEDIA_DESC(mediaDesc)                                    \
    if (NN_UNLIKELY((mediaDesc) == nullptr)) {                            \
        NN_LOG_ERROR("Invalid param, mediaDesc must be correct address"); \
        return NN_INVALID_PARAM;                                          \
    }

#define VALIDATE_ADDRESS(address)                                       \
    if (NN_UNLIKELY((address) == nullptr)) {                            \
        NN_LOG_ERROR("Invalid param, address must be correct address"); \
        return NN_INVALID_PARAM;                                        \
    }
/*
 * @brief when recycled memory is adjacent behind some memories block, we should
 * merge the two block to larger block for potential large block demand in the
 * future, as we said, self-scaling
 *
 * @param newMa recycled memory block, which should be merged
 * @param ma memory block adjacent ahead newMa, which should be merged
 * @param newNode
 */
void MemoryRegion::MemoryAreaInsertPre(NetRbNode<MemoryArea> *newMa, NetRbNode<MemoryArea> *ma)
{
    VALIDATE_NEW_MEM_AREA(newMa)
    VALIDATE_MEM_AREA(ma)

    auto root = &mRoot;
    NetRbNode<MemoryArea> *neighbNode = nullptr;
    MemoryAreaRawPtr neighMa;
    uint32_t index = 0;
    newMa->data.endAddress = ma->data.endAddress;
    newMa->data.length += ma->data.length;
    neighbNode = ma->Prev();
    /*
     * check whether the memory block to be extended forward has a prev node,
     * which may also adjacent to recycled memory, as its end address is recycled
     * start address, so, the merge process change from   new + ma to  ma.prev +
     * new + ma
     */
    if (neighbNode != nullptr) {
        neighMa = &neighbNode->data;
        if (neighMa->endAddress == newMa->data.startAddress) {
            neighMa->endAddress = newMa->data.endAddress;
            neighMa->length += newMa->data.length;
            CAST_TO_LIST_NODE(&ma->data)->RemoveSelf();
            MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);
            CAST_TO_LIST_NODE(neighMa)->RemoveSelf();
            MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[neighMa->index]);
            index = neighMa->length >> MEM_ALLOCATOR_BASE_SHIFT;
            index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index - 1);
            neighMa->index = index;
            CAST_TO_LIST(&freeHead[index])->Append(CAST_TO_LIST_NODE(neighMa));
            MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[index]);
            root->Erase(ma);
            return;
        }
    }

    CAST_TO_LIST_NODE(&ma->data)->RemoveSelf();
    MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);
    index = newMa->data.length >> MEM_ALLOCATOR_BASE_SHIFT;
    index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index - 1);
    newMa->data.index = index;
    CAST_TO_LIST(&freeHead[index])->Append(CAST_TO_LIST_NODE(newMa));
    MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[index]);
    root->Replace(ma, newMa);
}

/*
 * @brief same as MemoryAreaInsertPre
 */
void MemoryRegion::MemoryAreaInsertNext(NetRbNode<MemoryArea> *newMa, NetRbNode<MemoryArea> *ma)
{
    VALIDATE_NEW_MEM_AREA(newMa)
    VALIDATE_MEM_AREA(ma)

    auto root = &mRoot;
    NetRbNode<MemoryArea> *neighbNode;
    MemoryAreaRawPtr neighMa = nullptr;
    uint32_t index = 0;
    ma->data.endAddress = newMa->data.endAddress;
    ma->data.length += newMa->data.length;
    neighbNode = ma->Next();
    if (neighbNode != nullptr) {
        neighMa = &neighbNode->data;
        if (neighMa->startAddress == ma->data.endAddress) {
            ma->data.endAddress = neighMa->endAddress;
            ma->data.length += neighMa->length;
            CAST_TO_LIST_NODE(ma)->RemoveSelf();
            MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);
            CAST_TO_LIST_NODE(neighMa)->RemoveSelf();
            MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[neighMa->index]);
            index = ma->data.length >> MEM_ALLOCATOR_BASE_SHIFT;
            index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index > 0 ? index - 1 : 0);
            ma->data.index = index;
            CAST_TO_LIST(&freeHead[index])->Append(CAST_TO_LIST_NODE(ma));
            MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[index]);
            root->Erase(neighbNode);
            return;
        }
    }

    CAST_TO_LIST_NODE(ma)->RemoveSelf();

    MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);
    index = ma->data.length >> MEM_ALLOCATOR_BASE_SHIFT;
    index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index > 0 ? index - 1 : 0);
    ma->data.index = index;
    CAST_TO_LIST(&freeHead[index])->Append(CAST_TO_LIST_NODE(ma));
    MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[index]);
}

/*
 * @brief expand pool, just a wrapper of MemoryAreaInsert with some initial work
 * now it's private,only invoked by MemoryRegionInit, may be opened later for
 * dynamic expanding feature
 */
NResult NetMemAllocator::MemoryRegionJoin(MediaDescribe *mediaDesc)
{
    VALIDATE_MEDIA_DESC(mediaDesc)

    int32_t ret = NN_OK;

    {
        std::lock_guard<std::mutex> lock(mMemRegionLock);
        MemoryRegionInitial();

        uint64_t dataLength = mediaDesc->endAddress - mediaDesc->startAddress;
        mMemRegionMgr.totalSize += dataLength;
        ret = mMemRegionMgr.MemoryAreaInsert(mediaDesc->startAddress, dataLength);
    }

    if (ret != NN_OK) {
        NN_LOG_ERROR("Region join failed, new address is not overlapped with the existed");
        return ret;
    }
    NN_LOG_INFO("Region join succeed");
    return NN_OK;
}

NResult NetMemAllocator::MemoryRegionInit(MediaDescribe &media)
{
    NResult hr = NN_OK;
    hr = MemoryRegionJoin(&media);
    if (hr != NN_OK) {
        NN_LOG_ERROR("Memory region init failed, ret " << hr);
        return hr;
    }

    NN_LOG_INFO("Memory region init succeed.");
    return NN_OK;
}

/*
 * @brief take memory from pool
 * @param length  rounded request memory length
 * @param deltaLength  rounded length - request length
 *
 * "#c_a"  no reserve canary,aligned size minus applied size enough to store
 * "#c_r"  reserve canary
 */
NResult NetMemAllocator::RegionMalloc(uint64_t &startAddress, uint64_t length, uint64_t deltaLength)
{
    MemoryRegionRawPtr memoryRegion = &mMemRegionMgr;
    startAddress = 0;
    int32_t ret = NN_OK;
    uint64_t lengthWithMeta = length + MA_META_DATA_RESERVE_LEN;
    auto needReserve = deltaLength < MA_META_DATA_RESERVE_LEN;

    {
        std::lock_guard<std::mutex> lock(mMemRegionLock);

        if (!needReserve || NN_UNLIKELY(length == mMemRegionMgr.freeSize && length == mMemRegionMgr.totalSize)) {
            lengthWithMeta -= MA_META_DATA_RESERVE_LEN;
        }

        ret = memoryRegion->MemoryAreaRemove(&startAddress, lengthWithMeta, mMinBlockSize);
        if (NN_UNLIKELY(ret != NN_OK)) {
            NN_LOG_WARN("Areas scan invalid, length " << lengthWithMeta << " remain " << memoryRegion->freeSize);
            return NN_ERROR;
        }

        if (startAddress == mMRAddress) {
            mFirstAllocLength = lengthWithMeta;
            mFirstReqLength = length - deltaLength;
        } else {
            if (startAddress < MA_META_DATA_RESERVE_LEN) {
                NN_LOG_WARN("startAddress don't have enough space for meta data");
                return NN_ERROR;
            }
            auto metaBaseAddr = startAddress - MA_META_DATA_RESERVE_LEN;
            auto metaCanaryAddress = reinterpret_cast<char *>(metaBaseAddr);
            auto metaLenAddress = reinterpret_cast<uint32_t *>(metaBaseAddr + MA_CANARY_LEN);
            auto metaReqLenAddress = reinterpret_cast<uint64_t *>(metaBaseAddr + MA_CANARY_LEN + MA_LEN_LEN);

            if (NN_UNLIKELY(memcpy_s(metaCanaryAddress, MA_CANARY_LEN, needReserve ? "#c_r" : "#c_a", MA_CANARY_LEN) !=
                NN_OK)) {
                NN_LOG_WARN("Invalid operation to memcpy_s in RegionMalloc");
                return NN_ERROR;
            }
            *metaLenAddress = length / mMinBlockSize;
            *metaReqLenAddress = length - deltaLength;
        }
    }
    return NN_OK;
}

NResult NetMemAllocator::RegionMallocWithMap(uint64_t &startAddress, uint64_t length)
{
    MemoryRegionRawPtr memoryRegion = &mMemRegionMgr;
    startAddress = 0;
    int32_t ret = NN_OK;
    {
        std::lock_guard<std::mutex> lock(mMemRegionLock);
#ifdef ALLOCATOR_PROTECTION_ENABLED
        if (NN_UNLIKELY(!CheckNodes())) {
            NN_LOG_ERROR("Allocator corrupted, Allocate failed");
            return NN_ERROR;
        }
        UnProtectAllMem();
#endif
        ret = memoryRegion->MemoryAreaRemove(&startAddress, length, mMinBlockSize, false);
        if (NN_UNLIKELY(ret != NN_OK)) {
            NN_LOG_WARN("Areas scan invalid, length " << length << " remain " << memoryRegion->freeSize);
#ifdef ALLOCATOR_PROTECTION_ENABLED
            ProtectFreeMem();
#endif
            return NN_ERROR;
        }

        mAddrLenMap[startAddress] = length;
#ifdef ALLOCATOR_PROTECTION_ENABLED
        ProtectFreeMem();
#endif
    }
    return NN_OK;
}

NResult NetMemAllocator::RegionFreeWithMap(uint64_t startAddress)
{
    std::lock_guard<std::mutex> lock(mMemRegionLock);
#ifdef ALLOCATOR_PROTECTION_ENABLED
    if (NN_UNLIKELY(!CheckNodes())) {
        NN_LOG_ERROR("Allocator corrupted, Allocate failed");
        return NN_ERROR;
    }
    UnProtectAllMem();
#endif
    if (mAddrLenMap.count(startAddress) == 0) {
        NN_LOG_ERROR("Areas scan failed, address not malloc!");
#ifdef ALLOCATOR_PROTECTION_ENABLED
        ProtectFreeMem();
#endif
        return NN_ERROR;
    }
    auto length = mAddrLenMap[startAddress];

    auto ret = mMemRegionMgr.MemoryAreaInsert(startAddress, length);
    if (ret == NN_OK) {
        mAddrLenMap.erase(startAddress);
    }
#ifdef ALLOCATOR_PROTECTION_ENABLED
    ProtectFreeMem();
#endif

    return ret;
}

NResult NetMemAllocator::RegionFree(uint64_t startAddress)
{
    uint64_t length = 0;
    int32_t ret = NN_OK;
    bool freeingFirstBlock = false;

    {
        std::lock_guard<std::mutex> lock(mMemRegionLock);

        if (startAddress == mMRAddress) {
            if (NN_UNLIKELY(mFirstAllocLength == 0)) {
                NN_LOG_ERROR("Address Invalid");
                return NN_ERROR;
            }
            length = mFirstAllocLength;
            freeingFirstBlock = true;
        } else {
            if (startAddress < MA_META_DATA_RESERVE_LEN) {
                NN_LOG_WARN("startAddress don't have enough space for meta data");
                return NN_ERROR;
            }
            auto metaCanaryAddress = reinterpret_cast<char *>(startAddress - MA_META_DATA_RESERVE_LEN);
            auto metaLenAddress = reinterpret_cast<uint32_t *>(startAddress - MA_META_DATA_RESERVE_LEN + MA_CANARY_LEN);

            if (memcmp(metaCanaryAddress, "#c_r", MA_CANARY_LEN) == 0) {
                length = mMinBlockSize * (*metaLenAddress) + MA_META_DATA_RESERVE_LEN;
            } else if (memcmp(metaCanaryAddress, "#c_a", MA_CANARY_LEN) == 0) {
                length = mMinBlockSize * (*metaLenAddress);
            } else {
                NN_LOG_ERROR("Address Invalid");
                return NN_ERROR;
            }
        }

        ret = mMemRegionMgr.MemoryAreaInsert(startAddress, length);
        if (ret == NN_OK && freeingFirstBlock) {
            mFirstAllocLength = 0;
            mFirstReqLength = 0;
        }
    }

    if (NN_UNLIKELY(ret != NN_OK)) {
        NN_LOG_ERROR("Areas scan failed, length " << length);
        return NN_ERROR;
    }

    NN_LOG_TRACE_INFO("Mem free success, length " << length);
    return NN_OK;
}

/*
 * @brief take operate,traverse freeHead to find available memory block,
 * then remove the related node from freeHead lists and red-black tree
 */
NResult MemoryRegion::MemoryAreaRemove(uint64_t *startAddress, uint64_t length, uint32_t minBlockSize, bool metaStored)
{
    VALIDATE_ADDRESS(startAddress)

    auto root = &mRoot;
    NetRbNode<MemoryArea> *ma = nullptr;
    NetRbNode<MemoryArea> *newMa = nullptr;
    uint32_t index = 0;
    uint32_t areaIndex = 0;
    uint32_t nIndex = 0;

    /*
     * calculate start index to traverse free memory list's array
     */
    index = length >> MEM_ALLOCATOR_BASE_SHIFT;
    index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index > 0 ? index - 1 : 0);

    /*
     * traverse memory array<linkedlist> until find a memory block has enough
     * size, then take all or part from it
     */
    for (areaIndex = index; areaIndex < FREE_LIST_NUM; areaIndex++) {
        if (freeHead[areaIndex].IsEmpty()) {
            continue;
        }
        NetLinkedListNode<NetRbNode<MemoryArea>> *areaNode = freeHead[areaIndex].head.next;
        while (areaNode != &freeHead[areaIndex].head) {
            ma = CAST_TO_RBNODE(areaNode);

            /*
             * if current checked memory block has enough size for request length, we
             * take three condition in consider:
             * 1. equal to length, we just take address and remove the node
             * 2. greater than length, but not enough for record a new node, we just
             * take address and remove the node
             * 3. enough for length and new node data, thus we take address and record
             * a new node in spare memory
             *
             */
            bool firstCond = ma->data.length == length;
            if (metaStored) {
                firstCond = ma->data.length >= length && ma->data.length < length + NODE_SIZE + minBlockSize;
            }
            if (firstCond) {
                *startAddress = ma->data.startAddress;

                CAST_TO_LIST_NODE(ma)->RemoveSelf();
                MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);

                root->Erase(ma);
                if (freeSize < length) {
                    NN_LOG_ERROR("the length " << length << " is bigger than the size remaining " << freeSize);
                    return NN_ERROR;
                }
                freeSize -= length;
                return NN_OK;
            } else if (ma->data.length > length) {
                newMa = CAST_TO_RBNODE(ma->data.startAddress + length);
                newMa->data.startAddress = ma->data.startAddress + length;
                newMa->data.endAddress = ma->data.endAddress;
                newMa->data.length = ma->data.length - length;
#ifdef ALLOCATOR_PROTECTION_ENABLED
                newMa->data.Initialize();
#endif
                *startAddress = ma->data.startAddress;

                CAST_TO_LIST_NODE(ma)->RemoveSelf();
                MEM_ALLOCATOR_ATOMIC_DEC(&freeCnt[ma->data.index]);
                nIndex = newMa->data.length >> MEM_ALLOCATOR_BASE_SHIFT;
                nIndex = (nIndex >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : nIndex;
                newMa->data.index = nIndex;

                freeHead[nIndex].Append((CAST_TO_LIST_NODE(newMa)));
                MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[nIndex]);

                root->Replace(ma, newMa);
                if (freeSize < length) {
                    NN_LOG_ERROR("the length " << length << " is bigger than the size remaining " << freeSize);
                    return NN_ERROR;
                }
                freeSize -= length;
                return NN_OK;
            } else {
                areaNode = areaNode->next;
                continue;
            }
        }
    }

    return NN_ERROR;
}

/*
 * @brief putback operate,traverse red-black tree to find the correct position
 * to insert memory block, then insert the related node to red-black tree and
 * freeHead lists
 */
NResult MemoryRegion::MemoryAreaInsert(uint64_t startAddress, uint64_t length)
{
    auto root = &mRoot;
    auto newMa = reinterpret_cast<MemoryAreaRawPtr>(startAddress);
    uint32_t index = 0;
#ifdef ALLOCATOR_PROTECTION_ENABLED
    newMa->Initialize();
#endif
    newMa->startAddress = startAddress;
    newMa->endAddress = startAddress + length;
    newMa->length = length;

    CAST_TO_LIST_NODE(newMa)->ReLinkSelf();

    NetRbNode<MemoryArea> **newNode = &(root->ref);
    NetRbNode<MemoryArea> *parentNode = nullptr;

    /*
     * traverse red-black tree to find the right place to insert returned memory
     * block
     */
    MemoryAreaRawPtr ma = nullptr;

    while (*newNode) {
        ma = &(*newNode)->data;
        parentNode = *newNode;

        if (newMa->endAddress == ma->startAddress) {
            MemoryAreaInsertPre(CAST_TO_RBNODE(newMa), CAST_TO_RBNODE(ma));
            freeSize += length;
            return NN_OK;
        } else if (newMa->endAddress < ma->startAddress) {
            newNode = &((*newNode)->left);
        } else if (newMa->startAddress == ma->endAddress) {
            MemoryAreaInsertNext(CAST_TO_RBNODE(newMa), CAST_TO_RBNODE(ma));
            freeSize += length;
            return NN_OK;
        } else if (newMa->startAddress > ma->endAddress) {
            newNode = &((*newNode)->right);
        } else {
            NN_LOG_ERROR("Areas overlapped failed");
            return NN_ERROR;
        }
    }

    /*
     * free memory block has been inserted to red-black tree, then we sync
     * freeHead, which speed up taking operation
     */
    index = newMa->length >> MEM_ALLOCATOR_BASE_SHIFT;
    index = (index >= FREE_LIST_NUM) ? (FREE_LIST_NUM - 1) : (index - 1);
    newMa->index = index;
    freeHead[index].Append(CAST_TO_LIST_NODE(newMa));
    MEM_ALLOCATOR_ATOMIC_INC(&freeCnt[index]);

    CAST_TO_RBNODE(newMa)->Link(parentNode, newNode);
    root->Insert(CAST_TO_RBNODE(newMa));
    freeSize += length;

    return NN_OK;
}
} // namespace hcom
} // namespace ock
