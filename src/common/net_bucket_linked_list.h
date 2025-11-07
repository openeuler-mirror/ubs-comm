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
#ifndef OCK_HCOM_NET_LINKED_LIST_H
#define OCK_HCOM_NET_LINKED_LIST_H

#include "hcom.h"

namespace ock {
namespace hcom {
/*
 * Node info for linked list
 */
struct NetLLNode {
    struct NetLLNode *next = nullptr; /* point to next node, which is memory segment */
};

/*
 * The meta info for one linked list,
 */
struct NetBucketLinkedListMeta {
    NetLLNode *next = nullptr; /* point to the real memory segment */
    NetSpinLock lock {};       /* spin lock for insertion & deletion of memory */
    uint32_t count = 0;        /* the count of current linked list */
};

/*
 * A thread safe linked list with buckets for multiple threads cases,
 * used for MR segment allocation.
 *
 * This linked list doesn't allocate extract memory for linked node,
 * the linked node info stores on the start place of free memory segment.
 * The linked node info needs to clean after allocated, since these memory segments
 * are allocated to end user possibly.
 */
#define BUCKET_COUNT 64
class NetBucketLinkedList {
public:
    NetBucketLinkedList() = default;
    ~NetBucketLinkedList() = default;

    /*
     * @brief Push one item to linked list
     *
     * @param item         [in] the address of memory to added to list
     */
    inline void PushFront(uintptr_t item)
    {
        auto *newNode = reinterpret_cast<NetLLNode *>(item);
        if (NN_UNLIKELY(newNode == nullptr)) {
            return;
        }
        NetBucketLinkedListMeta *buckets = &mBuckets[__sync_fetch_and_add(&mPushRRIdx, 1) % BUCKET_COUNT];
        buckets->lock.Lock();
        newNode->next = buckets->next;
        buckets->next = newNode;
        buckets->count++;
        buckets->lock.Unlock();
    }

    inline bool Pop(uintptr_t &item)
    {
        uint16_t leftBucketsCount = BUCKET_COUNT;
        do {
            NetBucketLinkedListMeta *buckets = &mBuckets[__sync_fetch_and_add(&mPopRRIdx, 1) % BUCKET_COUNT];

            buckets->lock.Lock();
            if (NN_UNLIKELY(buckets->count == NN_NO0)) {
                buckets->lock.Unlock();
                continue;
            }

            item = reinterpret_cast<uintptr_t>(buckets->next);

            buckets->next = buckets->next->next;
            buckets->count--;
            buckets->lock.Unlock();
            return true;
        } while (--leftBucketsCount > 0);

        return false;
    }

    inline bool PopN(uintptr_t *&items, uint32_t n)
    {
        if (NN_UNLIKELY(items == nullptr)) {
            return false;
        }

        /* traverse every bucket for balance */
        for (uint32_t i = NN_NO0; i < n; i++) {
            if (NN_UNLIKELY(!Pop(items[i]))) {
                for (uint32_t j = NN_NO0; j < i; j++) {
                    PushFront(items[j]);
                }
                return false;
            }
        }

        return true;
    }

    NetBucketLinkedList(const NetBucketLinkedList &) = delete;
    NetBucketLinkedList(NetBucketLinkedList &&) = delete;
    NetBucketLinkedList &operator = (const NetBucketLinkedList &) = delete;
    NetBucketLinkedList &operator = (NetBucketLinkedList &&) = delete;

private:
    /* NOTE: to make sure the size of this class is same with one cache line of CPU */
    uint32_t mPopRRIdx = 0;                            /* round-robin index for pop */
    uint32_t mPushRRIdx = 0;                           /* round-robin index for push */
    NetBucketLinkedListMeta mBuckets[BUCKET_COUNT] {}; /* buckets linked list */
};
}
}

#endif // OCK_HCOM_NET_LINKED_LIST_H
