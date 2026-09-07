/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_IOBUF_ADAPTER_H
#define UBS_COMM_IOBUF_ADAPTER_H

#include <atomic>
#include <new>

#include "common/ubsocket_common_includes.h"
#include "include/ubsocket.h"

namespace ock {
namespace ubs {

const uint16_t IOBUF_BLOCK_FLAGS_UB = 1 << 2;
const uint16_t IOBUF_BLOCK_FLAGS_UB_TINY_POOL = 1 << 3;
struct Block {
    std::atomic<int> nshared;
    uint16_t flags;
    uint16_t abi_check;
    uint32_t size;
    uint32_t cap;
    union {
        Block *portal_next;
        uint64_t data_meta;
    } u;
    char *data;

    Block(char *data_in, uint32_t data_size, int init_nshared = 1)
        : nshared(init_nshared),
          flags(IOBUF_BLOCK_FLAGS_UB),
          abi_check(0),
          size(0),
          cap(data_size),
          u({nullptr}),
          data(data_in)
    {
    }

    void IncRef()
    {
        nshared.fetch_add(1, std::memory_order_relaxed);
    }

    void DecRef()
    {
        if (nshared.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            this->~Block();
            ubsocket_iobuf_deallocate(this);
        }
    }

    bool Full() const
    {
        return size >= cap;
    }
    size_t LeftSpace() const
    {
        return cap - size;
    }

    ALWAYS_INLINE Block *SetNext(Block *next)
    {
        u.portal_next = next;
        return next;
    }

    ALWAYS_INLINE Block *GetNext()
    {
        return u.portal_next;
    }
};

struct BlockRef {
    uint32_t offset = 0;
    uint32_t length = 0;
    Block *block = nullptr;

    void Reset()
    {
        offset = 0;
        length = 0;
        block = nullptr;
    }
};

class BlockCache {
public:
    void Insert(char *data_in, uint32_t data_size)
    {
        Block *new_block = nullptr;
        new_block = new (data_in - sizeof(Block)) Block(data_in, data_size);
        if (head_block_ == nullptr) {
            head_block_ = new_block;
            tail_block_ = new_block;
        } else {
            tail_block_ = tail_block_->SetNext(new_block);
        }
        cache_len_ += data_size;
    }

    ssize_t CutAndInsertAfter(uint32_t cut_size, Block *block)
    {
        if (cache_len_ == 0 || block == nullptr) {
            return 0;
        }

        uint32_t rx_total_len = 0;
        Block *out_block_tail = block;
        Block *out_sec_block = out_block_tail->GetNext();
        (void)out_block_tail->SetNext(nullptr);
        if (partial_block_.block != nullptr) {
            out_block_tail = out_block_tail->SetNext(partial_block_.block);
            rx_total_len += CutPartialBlock(cut_size);
            // If partial_block still exists, it indicates that partial_block has already met the size of cut.
            if (partial_block_.block != nullptr) {
                (void)out_block_tail->SetNext(out_sec_block);
                cache_len_ -= rx_total_len;
                return (ssize_t)rx_total_len;
            }
        }

        if (head_block_ != nullptr) {
            Block *last_cache_block = nullptr;
            Block *cache_block = head_block_;
            do {
                if (rx_total_len + cache_block->cap <= cut_size) {
                    // When the size has not yet exceeded cut_size, directly link the block to the end of the list.
                    rx_total_len += cache_block->cap;
                    last_cache_block = cache_block;
                    continue;
                }

                if (rx_total_len < cut_size) {
                    /* Non-first blocks are not allowed to be cut
                     * (Sub,it complete message to enhance the efficiency of brpc in parsing message each time) */
                    if (rx_total_len != 0) {
                        break;
                    }
                    /* Increase nshared to 2, and ensure cap equals size. Based on the logic from brpc's dec_ref
                     * (release the block when nshared reduces to 1) and make sure the block can be removed from 
                     * _block cache list to prevent it from being used by brpc again. */
                    partial_block_.offset = cut_size - rx_total_len;
                    partial_block_.length = cache_block->cap - partial_block_.offset;
                    partial_block_.block = cache_block;
                    partial_block_.block->cap = partial_block_.offset;
                    cache_block->IncRef();
                }
                last_cache_block = cache_block;
                rx_total_len = cut_size;
                break;
            } while ((cache_block->GetNext() != nullptr) && (cache_block = cache_block->GetNext()));

            if (last_cache_block != nullptr) {
                out_block_tail->SetNext(head_block_);
                head_block_ = last_cache_block->GetNext();
                out_block_tail = last_cache_block;
            }
        }

        (void)out_block_tail->SetNext(out_sec_block);
        cache_len_ -= rx_total_len;

        return (ssize_t)rx_total_len;
    }

    ALWAYS_INLINE uint64_t GetCacheLen()
    {
        return cache_len_;
    }

    ALWAYS_INLINE void Flush()
    {
        if (partial_block_.block != nullptr) {
            partial_block_.block->DecRef();
            partial_block_.Reset();
        }

        if (head_block_ != nullptr) {
            Block *cache_block = head_block_;
            Block *cache_block_next = nullptr;
            do {
                cache_block_next = cache_block->GetNext();
                cache_block->DecRef();
            } while ((cache_block_next != nullptr) && (cache_block = cache_block_next));
            head_block_ = nullptr;
            tail_block_ = nullptr;
            cache_len_ = 0;
        }
    }

private:
    ALWAYS_INLINE uint32_t CutPartialBlock(uint32_t cut_size)
    {
        uint32_t total_cut_size;
        if (cut_size >= partial_block_.length) {
            /* When the size has not yet exceeded cut_size,
             * directly link the polled block to the end of the first block */
            total_cut_size = partial_block_.length;
            partial_block_.block->cap += partial_block_.length;
            partial_block_.Reset();
        } else {
            /* The first partial block whose length exceeds max_buf_size. Increase nshared to 2, and ensure cap equals
             * size. Based on the logic from brpc's dec_ref (release the block when nshared reduces to 1) and make 
             * sure the block can be removed from _block cache list to prevent it from being used by brpc again. */
            total_cut_size = cut_size;
            partial_block_.offset += total_cut_size;
            partial_block_.length -= total_cut_size;
            partial_block_.block->cap += total_cut_size;
            partial_block_.block->IncRef();
        }

        return total_cut_size;
    }

    BlockRef partial_block_;
    Block *head_block_ = nullptr;
    Block *tail_block_ = nullptr;
    uint64_t cache_len_ = 0;
};
} // namespace ubs
} // namespace ock

#endif
