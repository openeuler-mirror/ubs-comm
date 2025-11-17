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
#ifndef HCOM_NET_ADDR_SIZE_MAP_H
#define HCOM_NET_ADDR_SIZE_MAP_H

#include "hcom.h"

namespace ock {
namespace hcom {
constexpr uint64_t G_ADDRESS_MASK = 0xFFFFFFFFFFFF; /* mask for key */

/*
 * @brief Spin lock entry in bucket
 * used for alloc overflowed buckets
 */
struct NetHashLockEntry {
    uint64_t lock = 0;

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
} __attribute__((packed));

/*
 * @brief Store the key/value into a linked array with 6 items,
 * because 64bytes is one cache line
 */
struct NetHashBucket {
    /*
     * @brief Make entry with address and times of base size
     * first 16bits:    timesOfBaseSize
     * second 48bits:   address
     */
    static inline uint64_t MakeEntry(uint64_t address, uint64_t timesOfBaseSize)
    {
        return (timesOfBaseSize << NN_NO48) | address;
    }

    /*
     * @brief Get times of base size from entry
     * first 16bits:    timesOfBaseSize
     * second 48bits:   address
     */
    static inline uint64_t GetSize(uint64_t entry)
    {
        return entry >> NN_NO48;
    }

    uint64_t subBuck[NN_NO6] {};
    NetHashBucket *next = nullptr;
    NetHashLockEntry spinLock {};

    bool Put(uint64_t address, uint64_t timesOfBaseSize)
    {
        /*
         * There are three pre-conditions, as this is used for memory allocator
         * 1 it is NOT possible that put and remove the same address at same time
         * 2 it is NOT possible that put two same key at same time
         * 3 there is no duplicated address
         *
         * these pre-conditions make logic much simpler, two steps need:
         * 1 loop and find an empty place in the bucket
         * 2 if no free in bucket expand a new one
         */

        /* don't put them into loop, flat code is faster than loop */
        auto newEntry = MakeEntry(address, timesOfBaseSize);
        if (subBuck[NN_NO0] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO0], 0, newEntry)) {
            return true;
        }

        if (subBuck[NN_NO1] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO1], 0, newEntry)) {
            return true;
        }

        if (subBuck[NN_NO2] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO2], 0, newEntry)) {
            return true;
        }

        if (subBuck[NN_NO3] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO3], 0, newEntry)) {
            return true;
        }

        if (subBuck[NN_NO4] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO4], 0, newEntry)) {
            return true;
        }

        if (subBuck[NN_NO5] == 0 && __sync_bool_compare_and_swap(&subBuck[NN_NO5], 0, newEntry)) {
            return true;
        }

        return false;
    }

    /*
     * @brief Remove the address from the bucket and get size
     */
    bool Remove(uint64_t address, uint32_t &timesOfBaseSize)
    {
        /*
         * expand the loop, instead of put them into a for/while loop for performance
         */
        uint64_t oldValue = subBuck[NN_NO0];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO0], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        oldValue = subBuck[NN_NO1];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO1], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        oldValue = subBuck[NN_NO2];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO2], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        oldValue = subBuck[NN_NO3];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO3], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        oldValue = subBuck[NN_NO4];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO4], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        oldValue = subBuck[NN_NO5];
        if ((oldValue & G_ADDRESS_MASK) == address) {
            __sync_bool_compare_and_swap(&subBuck[NN_NO5], oldValue, 0);
            timesOfBaseSize = GetSize(oldValue);
            return true;
        }

        return false;
    }
};

/*
 * @brief Allocator template, for extend memory allocation for overflowed buckets
 */
class NetHeapAllocator {
public:
    void *Allocate(uint32_t size)
    {
        return calloc(NN_NO1, size);
    }

    void Free(void *p)
    {
        if (NN_LIKELY(p != nullptr)) {
            free(p);
            p = nullptr;
        }
    }
};

/*
 * A high performance lockless hash map to store address and size(i.e. key=address, value=size),
 * the unique things are following:
 * 1 split one hash bucket array into sub 7 bucket arrays
 * 2 store key and value into uint64_t, to minimize the memory occupation and cache miss
 * 3 instead of store key/value into linked list, we store key/value into linked array
 * 4 using CAS instead of mutex
 */
template <typename Alloc = NetHeapAllocator> class NetAddress2SizeHashMap {
public:
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

    NetAddress2SizeHashMap() = default;
    ~NetAddress2SizeHashMap()
    {
        UnInitialize();
    }

    NResult Initialize(uint32_t reserve)
    {
        /* already initialized */
        if (mOverflowEntryAlloc != nullptr) {
            return NN_OK;
        }

        /* get proper bucket count */
        uint32_t bucketCount = reserve < NN_NO128 ? NN_NO128 : reserve;
        if (bucketCount > gPrimes[NN_NO165]) {
            bucketCount = gPrimes[NN_NO165];
        } else {
            uint32_t i = 0;
            while (i < gPrimesCount - 1 && gPrimes[i] < bucketCount) {
                i++;
            }
            bucketCount = gPrimes[i];
        }

        /* allocate buckets for sub-maps */
        for (uint16_t i = 0; i < gSubMapCount; i++) {
            auto tmp = new (std::nothrow) NetHashBucket[bucketCount];
            if (NN_UNLIKELY(tmp == nullptr)) {
                for (uint16_t j = i; j < gSubMapCount; j++) {
                    mSubMaps[j] = nullptr;
                }
                FreeSubMaps();
                NN_LOG_ERROR("Failed to new hash bucket, probably out of memory");
                return NN_NEW_OBJECT_FAILED;
            }

            /* make physical page and set to zero */
            bzero(tmp, sizeof(NetHashBucket) * bucketCount);

            mSubMaps[i] = tmp;
        }

        /* create overflow entry allocator */
        mOverflowEntryAlloc = new (std::nothrow) Alloc();
        if (NN_UNLIKELY(mOverflowEntryAlloc == nullptr)) {
            FreeSubMaps();
            NN_LOG_ERROR("Failed to new overflow entry allocator, probably out of memory");
            return NN_NEW_OBJECT_FAILED;
        }

        /* set bucket count */
        mBucketCount = bucketCount;
        mCount = 0;

        NN_LOG_INFO("Initialized NetAddress2SizeHashMap with " << gSubMapCount << " sub-maps, each contains " <<
            mBucketCount << " buckets, count of items " << mCount << ", occupied memory by buckets is " <<
            (sizeof(NetHashBucket) * bucketCount * gSubMapCount) << " bytes");

        return NN_OK;
    }

    void UnInitialize()
    {
        if (mOverflowEntryAlloc == nullptr) {
            return;
        }
        /* free overflowed entries firstly */
        FreeOverFlowedEntries();

        /* free sub map secondly */
        FreeSubMaps();

        /* free overflow entry at last */
        delete mOverflowEntryAlloc;
        mOverflowEntryAlloc = nullptr;

        mBucketCount = 0;
        mCount = 0;
    }

    /*
     * @brief Put address with size in
     *
     */
    NResult Put(uintptr_t address, uint32_t timesOfBaseSize)
    {
        if (NN_UNLIKELY(address == 0)) {
            return NN_INVALID_PARAM;
        }

        /* get bucket */
        auto buck = &(mSubMaps[address % gSubMapCount][address % mBucketCount]);

        /* try 8192 times */
        for (uint16_t i = 0; i < NN_NO8192; i++) {
            /* loop all buckets linked */
            while (buck != nullptr) {
                /* if there is an entry to put, just break */
                if (buck->Put(address, timesOfBaseSize)) {
                    /* increase count of items */
                    __sync_add_and_fetch(&mCount, 1);
                    return NN_OK;
                }

                /*
                 * if no next bucket exist, just for break,
                 * else move to next bucket linked
                 */
                if (buck->next == nullptr) {
                    break;
                } else {
                    buck = buck->next;
                }
            }

            /*
             * if not put successfully in existing buckets, allocate a new one
             *
             * NOTES: just allocate memory, don't access new bucket in the spin lock scope,
             * if access new bucket, which could trigger physical memory allocation which
             * could trigger page fault, that is quite slow. In this case, spin lock
             * could occupy too much CPU
             */
            auto &lock = buck->spinLock;
            lock.Lock();
            /* if other thread allocated new buck already, unlock and continue */
            if (buck->next != nullptr) {
                buck = buck->next;
                lock.Unlock();
                continue;
            }

            /* firstly entered thread allocate new bucket */
            auto newBuck = static_cast<NetHashBucket *>(mOverflowEntryAlloc->Allocate(sizeof(NetHashBucket)));
            if (NN_UNLIKELY(newBuck == nullptr)) {
                lock.Unlock();
                NN_LOG_ERROR("Failed to alloc new overflowed bucket from allocator");
                return NN_MALLOC_FAILED;
            }

            /* link to current buck, set buck to new buck */
            buck->next = newBuck;
            buck = newBuck;

            /* unlock */
            lock.Unlock();
        }

        NN_LOG_ERROR("Failed to put key/size with " << NN_NO8192 * NN_NO6 << " times try");
        return NN_ERROR;
    }

    /*
     * @brief Remove and get size
     */
    NResult Remove(uintptr_t address, uint32_t &timesOfBaseSize)
    {
        if (NN_UNLIKELY(address == 0)) {
            return NN_INVALID_PARAM;
        }

        /* get bucket */
        auto buck = &(mSubMaps[address % gSubMapCount][address % mBucketCount]);

        /* loop all buckets linked */
        while (buck != nullptr) {
            if (buck->Remove(address, timesOfBaseSize)) {
                __sync_sub_and_fetch(&mCount, 1);
                return NN_OK;
            }

            buck = buck->next;
        }

        NN_LOG_TRACE_INFO("Not found address in address2size map, which should not happen");
        return NN_ERROR;
    }

    /*
     * @brief Get size of item in hash map
     */
    inline uint32_t Size() const
    {
        return mCount;
    }

private:
    void FreeSubMaps()
    {
        /* free all sub maps */
        for (uint16_t i = 0; i < gSubMapCount; i++) {
            auto &tmp = mSubMaps[i];
            if (tmp != nullptr) {
                delete[] tmp;
                mSubMaps[i] = nullptr;
            }
        }
    }

    void FreeOverFlowedEntries()
    {
        for (uint16_t i = 0; i < gSubMapCount; i++) {
            auto &tmp = mSubMaps[i];
            if (tmp == nullptr) {
                continue;
            }

            /* free overflow entries in one sub map */
            for (uint32_t buckIndex = 0; buckIndex < mBucketCount; ++buckIndex) {
                auto curBuck = mSubMaps[i][buckIndex].next;
                NetHashBucket *nextOverflowEntryBuck = nullptr;

                /* exit loop when curBuck is null */
                while (curBuck != nullptr) {
                    /* assign next overflow buck to tmp variable */
                    nextOverflowEntryBuck = curBuck->next;

                    /* free this overflow bucket */
                    mOverflowEntryAlloc->Free(curBuck);

                    /* assign next to current */
                    curBuck = nextOverflowEntryBuck;
                }
            }
        }
    }

private:
    static constexpr uint16_t gSubMapCount = NN_NO5; /* count of sub map */
    static constexpr uint32_t gPrimesCount = NN_NO256;

private:
    /* make sure the size of this class is 64 bytes, fit into one cache line */
    Alloc *mOverflowEntryAlloc = nullptr;     /* allocate overflowed entry in one bucket */
    NetHashBucket *mSubMaps[gSubMapCount] {}; /* sub map */
    uint32_t mBucketCount = 0;                /* bucket count of each sub map */
    uint32_t mCount = 0;                      /* bucket count of each sub map */
    uint32_t mBaseSize = NN_NO4096;           /* base size */

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    const uint32_t gPrimes[gPrimesCount] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37,
                                             41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89,
                                             97, 103, 109, 113, 127, 137, 139, 149, 157, 167,
                                             179, 193, 199, 211, 227, 241, 257, 277, 293, 313,
                                             337, 359, 383, 409, 439, 467, 503, 541, 577, 619,
                                             661, 709, 761, 823, 887, 953, 1031, 1109, 1193, 1289,
                                             1381, 1493, 1613, 1741, 1879, 2029, 2179, 2357, 2549,
                                             2753, 2971, 3209, 3469, 3739, 4027, 4349, 4703, 5087,
                                             5503, 5953, 6427, 6949, 7517, 8123, 8783, 9497, 10273,
                                             11113, 12011, 12983, 14033, 15173, 16411, 17749, 19183,
                                             20753, 22447, 24281, 26267, 28411, 30727, 33223, 35933,
                                             38873, 42043, 45481, 49201, 53201, 57557, 62233, 67307,
                                             72817, 78779, 85229, 92203, 99733, 107897, 116731, 126271,
                                             136607, 147793, 159871, 172933, 187091, 202409, 218971, 236897,
                                             256279, 277261, 299951, 324503, 351061, 379787, 410857, 444487,
                                             480881, 520241, 562841, 608903, 658753, 712697, 771049, 834181,
                                             902483, 976369, 1056323, 1142821, 1236397, 1337629, 1447153,
                                             1565659, 1693859, 1832561, 1982627, 2144977, 2320627, 2510653,
                                             2716249, 2938679, 3179303, 3439651, 3721303, 4026031, 4355707,
                                             4712381, 5098259, 5515729, 5967347, 6456007, 6984629, 7556579,
                                             8175383, 8844859, 9569143, 10352717, 11200489, 12117689,
                                             13109983, 14183539, 15345007, 16601593, 17961079, 19431899,
                                             21023161, 22744717, 24607243, 26622317, 28802401, 31160981,
                                             33712729, 36473443, 39460231, 42691603, 46187573, 49969847,
                                             54061849, 58488943, 63278561, 68460391, 74066549, 80131819,
                                             86693767, 93793069, 101473717, 109783337, 118773397, 128499677,
                                             139022417, 150406843, 162723577, 176048909, 190465427,
                                             206062531, 222936881, 241193053, 260944219, 282312799,
                                             305431229, 330442829, 357502601, 386778277, 418451333,
                                             452718089, 489790921, 529899637, 573292817, 620239453,
                                             671030513, 725980837, 785430967, 849749479, 919334987,
                                             994618837, 1076067617, 1164186217, 1259520799, 1362662261,
                                             1474249943, 1594975441, 1725587117, 1866894511, 2019773507,
                                             2185171673, 2364114217, 2557710269, 2767159799, 2993761039,
                                             3238918481, 3504151727, 3791104843, 4101556399, 4294967291};
};
}
}

#endif // HCOM_NET_ADDR_SIZE_MAP_H
