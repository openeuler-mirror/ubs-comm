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

#ifndef UBS_COMM_UBSOCKET_ALLOC_FIXED_TYPES_H
#define UBS_COMM_UBSOCKET_ALLOC_FIXED_TYPES_H

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace allocator {

enum BlockExpandFlag : uint32_t {
    ADD = 0,
    REMOVE
};

using BlockExpandCallback = std::function<Result(uintptr_t address, uint64_t size, uint32_t flags)>;

struct FixedAllocatorOptions {
    uint32_t pool_space_start_tb = 2;                /* desired start virtual address of pool from os */
    uint32_t pool_space_size_gb = 4;                 /* capacity of pool */
    uint16_t block_size_mb = 8;                      /* size of one block, expand one from os one time */
    uint16_t init_block_count = 32;                  /* how many blocks expand (mini-block pool) */
    uint16_t init_direct_block_count = 1;            /* how many blocks expand for direct pool */
    uint32_t mini_block_size_kb = 8;                 /* size of one mini block */
    bool async_expand = false;                       /* expand in dedicated thread */
    float async_expand_threshold = 0.8;              /* expand memory when the threshold reached */
    BlockExpandCallback expanded_callback = nullptr; /* expand call back for register to NIC */

    friend std::ostream &operator<<(std::ostream &os, const FixedAllocatorOptions &o);
};

struct FixedTwinBornAllocOptions {
    uintptr_t brother_space_base_address = 0;
    uint16_t brother_space_size_gb = 4;
    uint16_t brother_single_data_size_kb = 8;
    uint16_t brother_init_space_mb = 8;
    uint16_t my_single_data_size_bytes = 64;
};

struct FixedLinkedBlock {
    FixedLinkedBlock *next = nullptr;
};

/***********************************************/
/* inner data structure */
struct FixedBlock {
    uintptr_t address = 0;
    uint32_t size_kb = 0;
    uint32_t free_size_kb = 0;
    bool mini_block_created = false;

    FixedBlock() = default;
    FixedBlock(uintptr_t address, uint32_t size_kb) : address(address), size_kb(size_kb), free_size_kb(size_kb) {}
    FixedBlock(uintptr_t address, uint32_t size_kb, uint32_t free_size_kb)
        : address(address),
          size_kb(size_kb),
          free_size_kb(free_size_kb)
    {
    }

    friend std::ostream &operator<<(std::ostream &os, const FixedBlock &o);
};

struct FixedMiniBlock {
    FixedMiniBlock *next = nullptr;     /* link to next mini block */
    FixedMiniBlock *next_n = nullptr;   /* link to next n, used in thread cache */
    FixedMiniBlock *next_n_m = nullptr; /* link to next n * m, used in global cache */
    uint16_t next_n_count = 0;          /* how many of next N */
    uint16_t next_n_m_count = 0;        /* next n * m count */
    uint32_t size_kb = 0;               /* size in KB of this mini block */
    FixedMiniBlock *tail = nullptr;     /* tail of the list chain, only tmp used during in gc */

    friend std::ostream &operator<<(std::ostream &os, const FixedMiniBlock &o);
};

using FixedExpandHandler = std::function<Result(uint32_t flags, FixedBlock &expandedBlock)>;

/* how many mini linked blocks in thread cache, this must be 2, 4, 8, 16 (i.e. 2^x) */
constexpr uint16_t FIX_TC_MINI_BLOCK_LINKED_COUNT = 4;
/* free 16 mini blocks at one time, this must be multiple times of FIX_TC_MINI_BLOCK_LINKED_COUNT */
constexpr uint16_t FIX_GC_BATCH_FREE_SIZE = 16;
/* threshold to trigger free to global cache,
 * if count of mini blocks of current thread beyond the threshold, free FIX_GC_BATCH_FREE_SIZE */
constexpr uint16_t FIX_GC_BATCH_FREE_THRESHOLD = 20;
/* allocate 64 mini blocks at one time, this must be multiple times of FIX_GC_BATCH_FREE_SIZE */
constexpr uint16_t FIX_GC_BATCH_ALLOC_SIZE = 16;

inline std::ostream &operator<<(std::ostream &os, const FixedAllocatorOptions &o)
{
    os << "FixedAllocatorOptions [pool_space_start_tb: " << o.pool_space_start_tb
       << ", pool_space_size_gb: " << o.pool_space_size_gb << ", block_size_mb: " << o.block_size_mb
       << ", init_block_count: " << o.init_block_count << ", init_direct_block_count: " << o.init_direct_block_count
       << ", mini_block_size_kb: " << o.mini_block_size_kb
       << ", expanded_callback: " << (o.expanded_callback == nullptr ? 0 : 1) << "]";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const FixedBlock &o)
{
    os << "Fixed Block [address: " << std::hex << o.address << std::dec << ", size_kb: " << o.size_kb
       << ", free_size_kb: " << o.free_size_kb << ", mini_block_created: " << o.mini_block_created << "]";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const FixedMiniBlock &o)
{
    os << "FixedMiniBlock [next: " << std::hex << o.next_n << ", next_n: " << o.next_n << ", next_n_m: " << o.next_n_m
       << std::dec << ", next_n_count: " << o.next_n_count << ", next_n_m_count: " << o.next_n_m_count
       << ", size_kb: " << o.size_kb << "]";
    return os;
}
} // namespace allocator
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ALLOC_FIXED_TYPES_H
