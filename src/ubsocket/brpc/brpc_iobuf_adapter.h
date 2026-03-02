/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-08-12
 *Note:
 *History: 2025-08-12
*/

#ifndef BRPC_IOBUF_ADAPTER_H
#define BRPC_IOBUF_ADAPTER_H

#include <new>
#include <atomic>
#include "rpc_adpt_vlog.h"

namespace Brpc {
namespace IOBuf {

typedef void* (*blockmem_allocate_t)(size_t);
typedef void (*blockmem_deallocate_t)(void *);

void* blockmem_allocate_zero_copy(size_t);
void  blockmem_deallocate_zero_copy(void*);

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
        : nshared(init_nshared), flags(0), abi_check(0), size(0), cap(data_size), u({NULL}), data(data_in) {}

    void IncRef()
    {
        nshared.fetch_add(1, std::memory_order_relaxed);
    } 
    
    void DecRef()
    {
        if (nshared.fetch_sub(1, std::memory_order_release) == 1){
            std::atomic_thread_fence(std::memory_order_acquire);
            this->~Block();
            blockmem_deallocate_zero_copy(this);
        }
    }

    bool Full() const { return size >= cap; }
    size_t LeftSpace() const { return cap - size; }

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
    Block* block = nullptr;

    void Reset()
    {
        offset = 0;
        length = 0;
        block = nullptr;
    }
};

}

class BlockCache {
public:
    ALWAYS_INLINE void Insert(char *data_in, uint32_t data_size)
    {
        IOBuf::Block *new_block = new (data_in - sizeof(IOBuf::Block)) IOBuf::Block(data_in,data_size);
        if (m_head_block == nullptr) {
            m_head_block = new_block;
            m_tail_block = new_block;
        } else {
            m_tail_block = m_tail_block->SetNext(new_block);
        }
        m_cache_len += data_size;
    } 
    
    ALWAYS_INLINE ssize_t CutAndInsertAfter(uint32_t cut_size, IOBuf::Block *block)
    {
        if (m_cache_len == 0) {
            return 0;
        }

        uint32_t rx_total_len = 0;
        IOBuf::Block *out_block_tail = block;
        IOBuf::Block *out_sec_block = out_block_tail->GetNext();
        (void)out_block_tail->SetNext(nullptr);
        if (m_partial_block.block != nullptr) {
            out_block_tail = out_block_tail->SetNext(m_partial_block.block);
            rx_total_len += CutPartialBlock(cut_size);
            // If partial_block still exists, it indicates that partial_block has already met the size of cut.
            if (m_partial_block.block != nullptr) {
                (void)out_block_tail->SetNext(out_sec_block);
                m_cache_len -= rx_total_len;
                return (ssize_t)rx_total_len;
            }
        }

        if (m_head_block != nullptr) {
            IOBuf::Block *last_cache_block = nullptr;
            IOBuf::Block *cache_block = m_head_block;
            do {
                if (rx_total_len + cache_block->cap < cut_size) {
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
                    m_partial_block.offset = cut_size - rx_total_len;
                    m_partial_block.length = cache_block->cap - m_partial_block.offset;
                    m_partial_block.block = cache_block;
                    m_partial_block.block->cap = m_partial_block.offset;
                    cache_block->IncRef(); 
                }
                last_cache_block = cache_block;
                rx_total_len = cut_size;
                break;
            } while ((cache_block->GetNext() != nullptr) && (cache_block = cache_block->GetNext()));

            if (last_cache_block != nullptr) {
                out_block_tail->SetNext(m_head_block);
                m_head_block = last_cache_block->GetNext();
                out_block_tail = last_cache_block;
            }
        }

        (void)out_block_tail->SetNext(out_sec_block);
        m_cache_len -= rx_total_len;

        return (ssize_t)rx_total_len;
    }

    ALWAYS_INLINE uint64_t GetCacheLen()
    {
        return m_cache_len;
    }

    ALWAYS_INLINE void Flush()
    {
        if (m_partial_block.block != nullptr) {
            m_partial_block.block->DecRef();
            m_partial_block.Reset();
        }

        if (m_head_block != nullptr) {
            IOBuf::Block *cache_block = m_head_block;
            IOBuf::Block *cache_block_next = nullptr;
            do {
                cache_block_next = cache_block->GetNext();
                cache_block->DecRef();
            } while ((cache_block_next != nullptr) && (cache_block = cache_block_next));
            m_head_block = nullptr;
            m_tail_block = nullptr;
            m_cache_len = 0;
        }
    }

private:
    ALWAYS_INLINE uint32_t CutPartialBlock(uint32_t cut_size)
    {
        uint32_t total_cut_size;
        if (cut_size >= m_partial_block.length) {
            /* When the size has not yet exceeded cut_size,
             * directly link the polled block to the end of the first block */
            total_cut_size = m_partial_block.length;
            m_partial_block.block->cap += m_partial_block.length;
            m_partial_block.Reset(); 
        } else {
            /* The first partial block whose length exceeds max_buf_size. Increase nshared to 2, and ensure cap equals
             * size. Based on the logic from brpc's dec_ref (release the block when nshared reduces to 1) and make 
             * sure the block can be removed from _block cache list to prevent it from being used by brpc again. */
            total_cut_size = cut_size;
            m_partial_block.offset += total_cut_size;
            m_partial_block.length -= total_cut_size;
            m_partial_block.block->cap += total_cut_size;
            m_partial_block.block->IncRef(); 
        }

        return total_cut_size;
    }  
    
    IOBuf::BlockRef m_partial_block;
    IOBuf::Block *m_head_block = nullptr;
    IOBuf::Block *m_tail_block = nullptr;
    uint64_t m_cache_len = 0;
};

}

#endif