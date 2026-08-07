/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <cstring>
#include <stdexcept>
#include <string>

#include "urma_socket_buf.h"

#include "common/ubsocket_logger.h"

namespace ock {
namespace ubs {
namespace urma {

/**
 * @brief UrmaSockRingBuf implementation — Ring Buffer + Bitmap lock-free SPSC allocator
 *
 * ============================================================================
 * Core Algorithms
 * ============================================================================
 *
 * --- Allocate(size, offset) — allocator hot path ---
 *
 * Input:  size (requested bytes), offset (out physical offset)
 * Output: true=success, false=insufficient space
 *
 * Steps:
 *   1. aligned = (size + kChunkSize - 1) & ~(kChunkSize - 1)               // round up to 64B
 *   2. reclaimed = reclaim_tail_.load(acquire)                             // read reclaimer progress
 *   3. if (alloc_tail_ - reclaimed + aligned > capacity_) -> return false  // insufficient space
 *   4. offset = alloc_tail_ & cap_mask_                                    // logical -> physical offset
 *   5. alloc_tail_ += aligned                                              // advance write pointer
 *   6. alloc_tail_atomic_.store(alloc_tail_, release)                      // notify reclaimer
 *   7. return true
 *
 * Key: Allocator never touches bitmap — zero write conflict with reclaimer.
 * Complexity: O(1), 1x load(acquire) + 1x store(release) + 3 ALU ops.
 *
 *
 * --- Release(offset, size) — reclaimer path ---
 *
 * Input: offset (physical offset), size (original byte count, same as passed to Allocate)
 *
 * Steps:
 *   1. aligned = (size + kChunkSize - 1) & ~(kChunkSize - 1)               // round up
 *   2. MarkReleased(offset / kChunkSize, aligned / kChunkSize)             // mark chunks as released
 *      -> bitmap_[idx >> 6] |= (1ULL << (idx & 63))
 *   3. allocated = alloc_tail_atomic_.load(acquire)                        // read allocator progress as scan bound
 *   4. pos = reclaim_tail_ (last position advanced to by this thread)
 *   5. while (pos < allocated && IsChunkReleased(pos)):                    // scan contiguous released range
 *         ClearChunk(pos)                                                  // clear bitmap bit
 *         pos += kChunkSize
 *   6. reclaim_tail_.store(pos, release)                                   // notify allocator
 *
 * Key: Reclaimer owns bitmap read/write — allocator never touches it -> zero contention.
 * Complexity:
 *   - No advance: O(1), bitmap OR only
 *   - Advance N chunks: O(N), N = contiguous released range filled by this Release
 *
 *
 * --- Bitmap coordinate system ---
 *
 * bitmap[i] bit j corresponds to physical chunk number (i*64 + j), range [0, num_chunks_).
 * For logical offset logical_pos:
 *   ChunkIndex(logical_pos) = (logical_pos & cap_mask_) >> 6
 *   i.e. (logical_pos & cap_mask_) / kChunkSize
 *
 * Since cap_mask_ = capacity_ - 1 and capacity_ is a power of 2 (lower 6 bits preserved),
 * alloc_tail_ is always a multiple of kChunkSize, therefore (alloc_tail_ & cap_mask_) is also
 * a multiple of kChunkSize, guaranteeing all offsets land on chunk boundaries.
 *
 *
 * ============================================================================
 * Correctness Arguments
 * ============================================================================
 *
 * --- Consistency model ---
 *
 *   Allocator                                      Reclaimer
 *   ─────────                                      ─────────
 *   alloc_tail_ += aligned    ──┐
 *   alloc_tail_atomic_.store    │                alloc_tail_atomic_.load(acquire)
 *   (release)                   └──────────────→ scan upper bound
 *                                                pos scan + advance
 *                               ┌──────────────→ reclaim_tail_.store(release)
 *   reclaim_tail_.load(acquire) │               (release semantics delivered to allocator)
 *   space check               ──┘
 *
 * acquire/release pairing guarantees:
 *   - Allocator load(acquire) reclaim_tail_: never sees a value beyond what reclaimer actually wrote
 *     -> Allocate will not overwrite not-yet-released data
 *   - Reclaimer load(acquire) alloc_tail_atomic_: never sees a value beyond what allocator actually wrote
 *     -> Release scan will not cross unallocated regions (won't misclassify unallocated chunks as "released")
 *
 * Additional: Allocator data writes into the allocated region happen before
 * alloc_tail_atomic_.store(release), so when reclaimer sees alloc_tail_ via acquire, the
 * data is guaranteed to be fully written.
 *
 *
 * --- Out-of-order release correctness ---
 *
 * Scenario: Allocator allocates A(addr=0), B(addr=64), C(addr=128) sequentially.
 *           Reclaimer releases out of order: Release(C) -> Release(A)
 *
 * Timeline:
 *   Alloc(A)    -> alloc_tail_ += 64    [bitmap: 000]
 *   Alloc(B)    -> alloc_tail_ += 64    [bitmap: 000]
 *   Alloc(C)    -> alloc_tail_ += 64    [bitmap: 000]
 *   Release(C)  -> bitmap[2]=1, reclaim_tail_ stays at 0 (chunk[0] not released, no advance)
 *   Release(A)  -> bitmap[0]=1, scan: chunk[0]✓ chunk[1]✗ stop => reclaim_tail_ advances to chunk[1]
 *   Release(B)  -> bitmap[1]=1, scan: chunk[1]✓ chunk[2]✓ chunk[3] stop => reclaim_tail_ advances to chunk[3]
 *
 * Key property: reclaim_tail_ only advances to the end of the contiguous released range.
 * Early release of individual chunks does not cause fragmentation or phantom available space.
 *
 *
 * --- Wrap-around correctness ---
 *
 * alloc_tail_ and reclaim_tail_ are logical offsets that monotonically increase without modulo.
 * Only when returning to the caller or writing to the bitmap is cap_mask_ used to extract
 * the physical offset. Therefore:
 *   - alloc_tail_ - reclaim_tail_ always equals the number of allocated-but-not-released bytes
 *   - (alloc_tail_ - reclaimed + aligned > capacity_) correctly determines space availability in all cases
 *   - Even if logical values eventually wrap around via uint64_t overflow (after 2^64 bytes),
 *     unsigned wraparound semantics still guarantee correct subtraction
 *
 * Bitmap wrap: MarkReleased uses (start_chunk + i) & (num_chunks_ - 1) to handle allocations
 * spanning the buffer end. Similarly, IsChunkReleased/ClearChunk auto-wrap via
 * (logical_pos & cap_mask_) >> 6.
 *
 *
 * --- Wrapped allocation (discontiguous physical memory) ---
 *
 * When alloc_tail_ is near the buffer tail, Allocate may return a region that wraps
 * around the physical buffer end. The returned (offset, size) spans two non-contiguous
 * physical segments:
 *
 *   segment 1: [offset, capacity_)
 *   segment 2: [0, offset + size - capacity_)
 *
 * Example (capacity = 4096):
 *   alloc_tail_ = 12224  ->  offset = 12224 & 4095 = 4032
 *   reclaim_tail_ = 12160, Available = 4096 - (12224-12160) = 4032
 *   Allocate(4032):
 *     offset = 4032
 *     physical segments: [4032, 4096) + [0, 3968)
 *
 *   Allocated [--------]   Free [====]
 *
 *          3968                  4032     4096
 *     0     |                     |        |
 *     [=====|---------------------|========]
 *           |----- 3968B ---------|-- 64B -|
 *
 * Caller responsibility: if offset + aligned_size > capacity_, the caller must split
 * the RDMA operation into two contiguous segments and update reclaim_tail_ only after
 * both segments are released. This is inherent to the ring buffer model — the allocator
 * does not provide a scatter-gather abstraction.
 *
 *
 * --- Bitmap initialization constraint ---
 *
 * bitmap_[0..kMaxChunks/64-1] is zero-initialized in the constructor (memset).
 * MarkReleased only sets bitmap bits for actually-allocated regions; ClearChunk only
 * clears bits that were previously set. Therefore bitmap bits are only 1 for regions
 * that were actually allocated — there are never "phantom releases" (unallocated regions
 * incorrectly marked as 1).
 *
 *
 * ============================================================================
 * Performance Characteristics
 * ============================================================================
 *
 * ┌──────────────────────────┬──────────┬────────────────────────────────────────┐
 * │ Metric                   │ Value    │ Notes                                  │
 * ├──────────────────────────┼──────────┼────────────────────────────────────────┤
 * │ Allocate latency         │ ~10ns    │ 1x load(acquire)+1x store(release)+3ALU│
 * │ Release no-advance       │ ~5ns     │ 1x bitmap OR + 1x load(acquire)        │
 * │ Release advance N chunks │ ~2ns x N │ per chunk: 1 AND + 1 compare           │
 * │ Atomic ops (Allocate)    │ 2        │ 1 load + 1 store                       │
 * │ Atomic ops (Release)     │ 2        │ 1 load + 1 store (independent of N)    │
 * │ Inline memory footprint  │ ~8.3KB   │ bitmap 8KB + members ~300B             │
 * │ Space overhead (vs cap)  │ 0.2%     │ 2KB bitmap / 1MB buffer                │
 * │ Cache line conflicts     │ 2        │ alloc_tail_atomic_ + reclaim_tail_     │
 * │ NUMA remote access       │ 2cl/call │ one atomic var read per side           │
 * └──────────────────────────┴──────────┴────────────────────────────────────────┘
 *
 * alloc_tail_ and bitmap are each only accessed by their respective thread, so they
 * stay in local cache without MESI state transitions.
 *
 *
 * ============================================================================
 * Error Handling Contract
 * ============================================================================
 *
 * ┌────────────────────────────────┬────────────────────────────────────────────────┐
 * │ Scenario                       │ Behavior                                       │
 * ├────────────────────────────────┼────────────────────────────────────────────────┤
 * │ Invalid constructor args       │ throw std::invalid_argument                    │
 * │ Allocate insufficient space    │ return false                                   │
 * │ Release invalid offset/size    │ undefined behavior (hot path, caller ensures)  │
 * │ capacity_ > kMaxCapacity       │ throw at construction time                     │
 * │ Available() called by reclaimer│ reads non-atomic alloc_tail_, may be stale     │
 * └────────────────────────────────┴────────────────────────────────────────────────┘
 *
 * Rationale: Allocate and Release are hot paths — every additional check adds latency.
 * The caller ensures Release parameter validity via credit/flow-control protocol.
 */
UrmaSockBuf::~UrmaSockBuf() = default;

UrmaSockRingBuf::UrmaSockRingBuf(uint64_t capacity)
    : capacity_(capacity),
      cap_mask_(capacity - 1),
      num_chunks_(capacity / UrmaSockBuf::kChunkSize)
{
    ValidateArgs(capacity);
    std::memset(bitmap_, 0, sizeof(bitmap_));
}

void UrmaSockRingBuf::ValidateArgs(uint64_t capacity) const
{
    if (capacity == 0 || capacity > UrmaSockBuf::kMaxCapacity) {
        throw std::invalid_argument(
            std::string("UrmaSockRingBuf capacity out of range: ").append(std::to_string(capacity)));
    }
    if ((capacity & (capacity - 1)) != 0) {
        throw std::invalid_argument(
            std::string("UrmaSockRingBuf capacity must be power of 2: ").append(std::to_string(capacity)));
    }
    if (capacity % UrmaSockBuf::kChunkSize != 0) {
        throw std::invalid_argument(
            std::string("UrmaSockRingBuf capacity must be 64B aligned: ").append(std::to_string(capacity)));
    }
}

bool UrmaSockRingBuf::Allocate(uint64_t size, uint64_t &offset) noexcept
{
    uint64_t aligned = (size + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);
    uint64_t reclaimed = reclaim_tail_.load(std::memory_order_acquire);
    if (alloc_tail_ - reclaimed + aligned > capacity_) {
        return false;
    }
    offset = alloc_tail_ & cap_mask_;
    alloc_tail_ += aligned;
    alloc_tail_atomic_.store(alloc_tail_, std::memory_order_release);
    return true;
}

uint64_t UrmaSockRingBuf::Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept
{
    uint64_t alignedMin = (minSize + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);
    uint64_t alignedMax = (maxSize + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);
    uint64_t reclaimed = reclaim_tail_.load(std::memory_order_acquire);
    uint64_t available = capacity_ - (alloc_tail_ - reclaimed);
    if (available < alignedMin) {
        return 0;
    }
    uint64_t allocated = (available < alignedMax) ? available : alignedMax;
    offset = alloc_tail_ & cap_mask_;
    alloc_tail_ += allocated;
    alloc_tail_atomic_.store(alloc_tail_, std::memory_order_release);
    return allocated;
}

void UrmaSockRingBuf::Release(uint64_t offset, uint64_t size) noexcept
{
    uint64_t aligned = (size + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);

    uint64_t start_chunk = offset / UrmaSockBuf::kChunkSize;
    uint64_t chunk_count = aligned / UrmaSockBuf::kChunkSize;
    MarkReleased(start_chunk, chunk_count);

    uint64_t pos = reclaim_tail_.load(std::memory_order_relaxed);
    uint64_t allocated = alloc_tail_atomic_.load(std::memory_order_acquire);
    while (pos < allocated && IsChunkReleased(pos)) {
        ClearChunk(pos);
        pos += UrmaSockBuf::kChunkSize;
    }
    reclaim_tail_.store(pos, std::memory_order_release);
}

uint64_t UrmaSockRingBuf::Available() const noexcept
{
    uint64_t reclaimed = reclaim_tail_.load(std::memory_order_acquire);
    return capacity_ - (alloc_tail_ - reclaimed);
}

uint64_t UrmaSockRingBuf::Used() const noexcept
{
    return capacity_ - Available();
}

uint64_t UrmaSockRingBuf::ChunkIndex(uint64_t logical_pos) const noexcept
{
    return (logical_pos & cap_mask_) >> 6;
}

void UrmaSockRingBuf::MarkReleased(uint64_t start_chunk, uint64_t count) noexcept
{
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t idx = (start_chunk + i) & (num_chunks_ - 1);
        bitmap_[idx >> 6] |= (1ULL << (idx & 63));
    }
}

bool UrmaSockRingBuf::IsChunkReleased(uint64_t logical_pos) const noexcept
{
    uint64_t idx = ChunkIndex(logical_pos);
    return (bitmap_[idx >> 6] >> (idx & 63)) & 1ULL;
}

void UrmaSockRingBuf::ClearChunk(uint64_t logical_pos) noexcept
{
    uint64_t idx = ChunkIndex(logical_pos);
    bitmap_[idx >> 6] &= ~(1ULL << (idx & 63));
}

/**
 * @brief UrmaSockRbTreeBuf implementation — Red-Black Tree based SPSC allocator
 *
 * ============================================================================
 * Data Structures
 * ============================================================================
 *
 * --- addressTree_ (std::map<uint64_t, uint64_t>) ---
 *
 * Keyed by offset (address), value is block size. Used during Release for
 * O(log n) predecessor/successor lookup to coalesce adjacent free blocks.
 * Ensures free blocks are always merged to prevent fragmentation.
 *
 *
 * --- sizeTree_ (std::set<SpaceRange, RangeSizeFirst>) ---
 *
 * Sorted by size first (ascending), offset second (ascending). Used during
 * Allocate for O(log n) best-fit search via lower_bound: finds the smallest
 * free block whose size >= requested_aligned.
 *
 * Comparator RangeSizeFirst: (sr1.size < sr2.size) || (equal && sr1.offset < sr2.offset)
 *
 *
 * --- free_space_ (uint64_t) ---
 *
 * Running O(1) counter of total free bytes. Updated on Allocate (-= aligned)
 * and Release (+= aligned). Read under spinlock by Available().
 *
 *
 * --- lock_ (pthread_spinlock_t) ---
 *
 * Protects all operations. Unlike UrmaSockRingBuf which is lock-free via
 * atomic variables, RbTree uses a spinlock because STL trees are not
 * thread-safe and cannot be modified atomically.
 *
 *
 * ============================================================================
 * Core Algorithms
 * ============================================================================
 *
 * --- Allocate(size, offset) — O(log n) best-fit ---
 *
 * Input:  size (requested bytes), offset (out physical offset)
 * Output: true=success, false=insufficient space
 *
 * Steps:
 *   1. aligned = (size + kChunkSize - 1) & ~(kChunkSize - 1)
 *   2. lock spinlock
 *   3. it = sizeTree_.lower_bound(SpaceRange(0, aligned))
 *      -> finds first free block with size >= aligned (smallest fitting)
 *   4. if it == sizeTree_.end() -> unlock, return false
 *   5. Extract block: offset = it->offset, block_size = it->size
 *   6. Remove from both trees (addressTree_ by key, sizeTree_ by iterator)
 *   7. free_space_ -= aligned
 *   8. if block_size > aligned:
 *        Split remainder: insert (offset + aligned, block_size - aligned) into both trees
 *   9. unlock, return true
 *
 * Key: Best-fit minimizes external fragmentation by choosing the smallest
 *      block that satisfies the request.
 *
 *
 * --- Release(offset, size) — O(log n) coalescing ---
 *
 * Input: offset (physical offset, from Allocate), size (original byte count)
 *
 * Steps:
 *   1. aligned = (size + kChunkSize - 1) & ~(kChunkSize - 1)
 *   2. lock spinlock
 *   3. Initialize merged region: new_offset = offset, new_size = aligned
 *   4. Find and merge predecessor:
 *        pred = addressTree_.lower_bound(offset)
 *        if pred != begin() && --pred->first + pred->second == offset:
 *          new_offset = pred->first, new_size += pred->second
 *          erase predecessor from both trees
 *   5. Find and merge successor:
 *        succ = addressTree_.lower_bound(offset + aligned)
 *        if succ != end() && succ->first == offset + aligned:
 *          new_size += succ->second
 *          erase successor from both trees
 *   6. Insert merged block into both trees
 *   7. free_space_ += aligned  (pred/succ were already in free_space_)
 *   8. unlock
 *
 * Key: Merging with adjacent free blocks prevents fragmentation — the set
 *      of free blocks always remains a set of disjoint, non-adjacent intervals.
 *
 *
 * ============================================================================
 * Correctness Arguments
 * ============================================================================
 *
 * --- Invariant ---
 *
 * The set of allocated blocks and free blocks are always disjoint and their
 * union covers the entire buffer [0, capacity_). Free blocks are always
 * maximal: no two free blocks are adjacent. This is maintained by:
 *   1. Allocate: removes one block, optionally inserts a smaller remainder
 *   2. Release: inserts a block, merging adjacent ones
 *
 *
 * --- Thread safety ---
 *
 * All public methods acquire lock_ before accessing any internal state.
 * The spinlock serializes all operations between allocator and reclaimer.
 * free_space_ is read under lock to avoid torn reads.
 *
 *
 * --- Best-fit correctness ---
 *
 * lower_bound(SpaceRange(0, aligned)) returns the first block where
 * !(block < SpaceRange(0, aligned)). With RangeSizeFirst comparator:
 *   - blocks with size < aligned are all "less" -> skip
 *   - first block with size >= aligned is "not less" -> found
 * Since offset=0 in the search key and comparator checks offset only
 * after size, the first result by size is always the smallest-fitting block.
 *
 *
 * --- Coalescing correctness ---
 *
 * After merging predecessor, the successor search uses the original
 * (offset + aligned), not the merged end. This is correct because:
 *   - Predecessor extends the region LEFT, not right — the right boundary
 *     (offset + aligned) does not change
 *   - If successor is exactly at the original right boundary, it is still
 *     at the original right boundary after predecessor merge
 *
 * Example: Free A:[0,64), Release R:[64,192), Free B:[192,256)
 *   pred search: lower_bound(64) -> B@192, -- -> A@0, A.end==64==R.start ✓
 *   succ search: lower_bound(64+128=192) -> B@192, B.start==192==R.end ✓
 *   Final: [0, 256) single free block
 *
 *
 * --- No double-free detection ---
 *
 * As with UrmaSockRingBuf, Release does not validate offset/size as this
 * is the caller's responsibility (enforced by credit/flow-control protocol).
 * Double-free or overlapping release may corrupt internal trees.
 *
 *
 * ============================================================================
 * Performance Characteristics
 * ============================================================================
 *
 * ┌──────────────────────────┬──────────────┬───────────────────────────────────────────┐
 * │ Metric                   │ Value        │ Notes                                     │
 * ├──────────────────────────┼──────────────┼───────────────────────────────────────────┤
 * │ Allocate latency         │ O(log n)     │ lower_bound + erase + optional insert     │
 * │ Release latency          │ O(log n)     │ 2x lower_bound + 2x erase + 1x insert     │
 * │ Available latency        │ O(1)         │ read free_space_ under spinlock           │
 * │ Lock contention          │ moderate     │ spinlock serializes all ops               │
 * │ Fragmentation            │ zero         │ coalescing guarantees maximal free blocks │
 * │ Memory overhead per node │ ~40B         │ RB-tree node (map + set)                  │
 * │ Capacity constraint      │ 64B-aligned  │ no power-of-2 requirement (unlike RingBuf)│
 * └──────────────────────────┴──────────────┴───────────────────────────────────────────┘
 *
 * Compared to UrmaSockRingBuf: RbTree accepts non-power-of-2 capacity, supports
 * immediate out-of-order reclaim, and has zero fragmentation. Tradeoff is higher
 * per-operation latency (log n tree ops vs constant-time atomics) and lock contention.
 *
 *
 * ============================================================================
 * Error Handling Contract
 * ============================================================================
 *
 * ┌──────────────────────────────┬────────────────────────────────────────────────┐
 * │ Scenario                     │ Behavior                                       │
 * ├──────────────────────────────┼────────────────────────────────────────────────┤
 * │ Invalid constructor args     │ throw std::invalid_argument                    │
 * │   - capacity == 0            │ throw (out of range)                           │
 * │   - capacity > kMaxCapacity  │ throw (out of range)                           │
 * │   - capacity % 64 != 0       │ throw (alignment)                              │
 * │   - non-power-of-2           │ OK (unlike RingBuf)                            │
 * │ Allocate insufficient space  │ return false                                   │
 * │ Release invalid offset/size  │ undefined behavior (caller ensures)            │
 * │ spinlock init failure        │ not handled (pthread_spin_init is infallible)  │
 * └──────────────────────────────┴────────────────────────────────────────────────┘
 */
UrmaSockRbTreeBuf::UrmaSockRbTreeBuf(uint64_t capacity)
    : capacity_(capacity),
      free_space_(capacity),
      addressTree_{&pool_res_},
      sizeTree_{&pool_res_}
{
    ValidateArgs(capacity);
    pthread_spin_init(&lock_, PTHREAD_PROCESS_PRIVATE);
    addressTree_.insert({0, capacity_});
    sizeTree_.insert(SpaceRange(0, capacity_));
}

UrmaSockRbTreeBuf::~UrmaSockRbTreeBuf()
{
    pthread_spin_destroy(&lock_);
}

void UrmaSockRbTreeBuf::ValidateArgs(uint64_t capacity) const
{
    if (capacity == 0 || capacity > UrmaSockBuf::kMaxCapacity) {
        throw std::invalid_argument(
            std::string("UrmaSockRbTreeBuf capacity out of range: ").append(std::to_string(capacity)));
    }
    if (capacity % UrmaSockBuf::kChunkSize != 0) {
        throw std::invalid_argument(
            std::string("UrmaSockRbTreeBuf capacity must be 64B aligned: ").append(std::to_string(capacity)));
    }
}

bool UrmaSockRbTreeBuf::RangeSizeFirst::operator()(const SpaceRange &sr1, const SpaceRange &sr2) const noexcept
{
    if (sr1.size != sr2.size) {
        return sr1.size < sr2.size;
    }
    return sr1.offset < sr2.offset;
}

bool UrmaSockRbTreeBuf::Allocate(uint64_t size, uint64_t &offset) noexcept
{
    uint64_t aligned = (size + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);

    pthread_spin_lock(&lock_);

    auto it = sizeTree_.lower_bound(SpaceRange(0, aligned));
    if (it == sizeTree_.end()) {
        pthread_spin_unlock(&lock_);
        return false;
    }

    uint64_t block_offset = it->offset;
    uint64_t block_size = it->size;

    addressTree_.erase(block_offset);
    sizeTree_.erase(it);
    free_space_ -= aligned;

    if (block_size > aligned) {
        uint64_t rem_offset = block_offset + aligned;
        uint64_t rem_size = block_size - aligned;
        addressTree_.insert({rem_offset, rem_size});
        sizeTree_.insert(SpaceRange(rem_offset, rem_size));
    }

    offset = block_offset;

    pthread_spin_unlock(&lock_);
    return true;
}

uint64_t UrmaSockRbTreeBuf::Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept
{
    uint64_t alignedMin = (minSize + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);
    uint64_t alignedMax = (maxSize + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);

    pthread_spin_lock(&lock_);

    // Step 1: try to serve exactly maxSize from the smallest block that can hold it
    auto it = sizeTree_.lower_bound(SpaceRange(0, alignedMax));
    if (it != sizeTree_.end()) {
        uint64_t blockOffset = it->offset;
        uint64_t blockSize = it->size;
        addressTree_.erase(blockOffset);
        sizeTree_.erase(it);
        free_space_ -= alignedMax;
        if (blockSize > alignedMax) {
            uint64_t remOffset = blockOffset + alignedMax;
            uint64_t remSize = blockSize - alignedMax;
            addressTree_.insert({remOffset, remSize});
            sizeTree_.insert(SpaceRange(remOffset, remSize));
        }
        offset = blockOffset;
        pthread_spin_unlock(&lock_);
        return alignedMax;
    }

    // Step 2: no block >= maxSize, take the largest block available
    if (sizeTree_.empty()) {
        pthread_spin_unlock(&lock_);
        return 0;
    }
    auto last = std::prev(sizeTree_.end());
    if (last->size < alignedMin) {
        pthread_spin_unlock(&lock_);
        return 0;
    }
    uint64_t blockOffset = last->offset;
    uint64_t blockSize = last->size;
    addressTree_.erase(blockOffset);
    sizeTree_.erase(last);
    free_space_ -= blockSize;
    offset = blockOffset;
    pthread_spin_unlock(&lock_);
    return blockSize;
}

void UrmaSockRbTreeBuf::Release(uint64_t offset, uint64_t size) noexcept
{
    uint64_t aligned = (size + UrmaSockBuf::kChunkSize - 1) & ~(UrmaSockBuf::kChunkSize - 1);

    pthread_spin_lock(&lock_);

    uint64_t new_offset = offset;
    uint64_t new_size = aligned;

    // Merge with predecessor: find free block whose end touches our start
    auto pred = addressTree_.lower_bound(offset);
    if (pred != addressTree_.begin()) {
        --pred;
        if (pred->first + pred->second == offset) {
            new_offset = pred->first;
            new_size += pred->second;
            sizeTree_.erase(SpaceRange(pred->first, pred->second));
            addressTree_.erase(pred->first);
        }
    }

    // Merge with successor: find free block starting exactly at our end
    auto succ = addressTree_.lower_bound(offset + aligned);
    if (succ != addressTree_.end() && succ->first == offset + aligned) {
        new_size += succ->second;
        sizeTree_.erase(SpaceRange(succ->first, succ->second));
        addressTree_.erase(succ->first);
    }

    addressTree_.insert({new_offset, new_size});
    sizeTree_.insert(SpaceRange(new_offset, new_size));
    free_space_ += aligned;

    pthread_spin_unlock(&lock_);
}

uint64_t UrmaSockRbTreeBuf::Available() const noexcept
{
    pthread_spin_lock(&lock_);
    uint64_t avail = free_space_;
    pthread_spin_unlock(&lock_);
    return avail;
}

uint64_t UrmaSockRbTreeBuf::Used() const noexcept
{
    return capacity_ - Available();
}

bool UrmaSockShiftBuf::Allocate(uint64_t size, uint64_t &offset) noexcept
{
    uint64_t base_offset = 0;
    auto success = base_->Allocate(size, base_offset);
    if (!success) {
        return false;
    }

    offset = (base_offset + shift_bytes) % capacity_;
    return true;
}

uint64_t UrmaSockShiftBuf::Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept
{
    uint64_t base_offset = 0;
    auto success_size = base_->Allocate(minSize, maxSize, base_offset);
    if (success_size == 0) {
        return 0;
    }

    offset = (base_offset + shift_bytes) % capacity_;
    return success_size;
}

uint64_t UrmaSockShiftBuf::Available() const noexcept
{
    return base_->Available();
}

uint64_t UrmaSockShiftBuf::Used() const noexcept
{
    return base_->Used();
}

void UrmaSockShiftBuf::Release(uint64_t offset, uint64_t size) noexcept
{
    uint64_t base_offset = (offset + capacity_ - shift_bytes) % capacity_;
    base_->Release(base_offset, size);
}

bool UrmaSockContinuousBuf::Allocate(uint64_t size, uint64_t &offset) noexcept
{
    uint64_t base_offset = 0;
    auto success = base_->Allocate(size, base_offset);
    if (UNLIKELY(!success)) {
        return false;
    }

    if (LIKELY(base_offset + size <= capacity_)) {
        offset = base_offset;
        return true;
    }

    auto end_size = capacity_ - base_offset;
    auto wrap_size = size - end_size;
    uint64_t append_offset = 0;
    success = base_->Allocate(end_size, append_offset);
    if (UNLIKELY(!success)) {
        AddLaterReleaseNode(base_offset, size);
        return false; // allocator crash
    }

    if (UNLIKELY(append_offset != wrap_size)) {
        AddLaterReleaseNode(base_offset, size, append_offset, end_size);
        return false; // allocator crash
    }

    offset = 0;
    wrap_end_offset_.store(base_offset, std::memory_order_relaxed);
    return true;
}

uint64_t UrmaSockContinuousBuf::Allocate(uint64_t min_size, uint64_t max_size, uint64_t &offset) noexcept
{
    uint64_t base_offset = 0;
    auto success_size = base_->Allocate(min_size, max_size, base_offset);
    if (UNLIKELY(success_size < min_size)) {
        return 0;
    }

    if (LIKELY(base_offset + success_size <= capacity_)) {
        offset = base_offset;
        return success_size;
    }

    auto end_size = capacity_ - base_offset;
    auto wrap_size = success_size - end_size;
    if (success_size < max_size) {
        if (wrap_size < min_size) {
            AddLaterReleaseNode(base_offset, success_size);
            return 0; // allocator crash
        }

        offset = 0;
        wrap_end_offset_.store(base_offset, std::memory_order_relaxed);
        return wrap_size;
    }

    uint64_t append_offset = 0;
    if (UNLIKELY(!base_->Allocate(end_size, append_offset))) {
        offset = 0;
        wrap_end_offset_.store(base_offset, std::memory_order_relaxed);
        return wrap_size;
    }

    if (UNLIKELY(append_offset != wrap_size)) {
        offset = 0;
        wrap_end_offset_.store(base_offset, std::memory_order_relaxed);
        return wrap_size; // allocator crash
    }

    offset = 0;
    wrap_end_offset_.store(base_offset, std::memory_order_relaxed);
    return success_size;
}

void UrmaSockContinuousBuf::Release(uint64_t offset, uint64_t size) noexcept
{
    if (later_release_node_count.load(std::memory_order_relaxed) > 0) {
        ReleaseAllLaterNodes();
    }

    uint64_t real_offset = offset;
    uint64_t real_size = size;
    if (UNLIKELY(offset == 0)) {
        real_offset = wrap_end_offset_.exchange(0U, std::memory_order_relaxed);
        if (real_offset != 0) {
            real_size += (capacity_ - real_offset);
        }
    }
    base_->Release(real_offset, real_size);
}

uint64_t UrmaSockContinuousBuf::Available() const noexcept
{
    return base_->Available();
}

uint64_t UrmaSockContinuousBuf::Used() const noexcept
{
    return base_->Used();
}

void UrmaSockContinuousBuf::AddLaterReleaseNode(uint64_t offset, uint64_t size) noexcept
{
    auto node = reinterpret_cast<LaterReleaseNode *>(start_addr_ + offset);
    node->offset = offset;
    node->size = size;

    pthread_spin_lock(&lock_);
    node->next = later_release_head_;
    later_release_head_ = node;
    pthread_spin_unlock(&lock_);
    later_release_node_count.fetch_add(1UL, std::memory_order_relaxed);
}

void UrmaSockContinuousBuf::AddLaterReleaseNode(uint64_t offset1, uint64_t size1, uint64_t offset2,
                                                uint64_t size2) noexcept
{
    auto node1 = reinterpret_cast<LaterReleaseNode *>(start_addr_ + offset1);
    node1->offset = offset1;
    node1->size = size1;

    auto node2 = reinterpret_cast<LaterReleaseNode *>(start_addr_ + offset2);
    node2->offset = offset2;
    node2->size = size2;
    node1->next = node2;

    pthread_spin_lock(&lock_);
    node2->next = later_release_head_;
    later_release_head_ = node1;
    pthread_spin_unlock(&lock_);
    later_release_node_count.fetch_add(2UL, std::memory_order_relaxed);
}

void UrmaSockContinuousBuf::ReleaseAllLaterNodes() noexcept
{
    pthread_spin_lock(&lock_);
    auto header = later_release_head_;
    later_release_head_ = nullptr;
    pthread_spin_unlock(&lock_);

    auto count = 0UL;
    while (header != nullptr) {
        auto next = header->next;
        base_->Release(header->offset, header->size);
        header = next;
        count++;
    }

    later_release_node_count.fetch_sub(count, std::memory_order_relaxed);
}

} // namespace urma
} // namespace ubs
} // namespace ock
