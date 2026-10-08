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
#ifndef UBS_COMM_UBSOCKET_FLASH_DYNAMIC_BITSET_H
#define UBS_COMM_UBSOCKET_FLASH_DYNAMIC_BITSET_H

#include <cstdint>
#include <ostream>
#include <vector>

#include "ubsocket_defines.h"
#include "ubsocket_errno.h"
#include "ubsocket_logger.h"

namespace ock::ubs {

constexpr int32_t DBS_CHUNK_SHIFT = 6;            // right shift bits
constexpr int32_t DBS_CHUNK_MASK = 63;            // mask for bit ops
#define DBS_CHUNK_POS(x) ((x) >> DBS_CHUNK_SHIFT) // chunk pos
#define DBS_BIT_POS(x) ((x)&DBS_CHUNK_MASK)       // bit pos

/**
 * @brief A bitset which dynamic bit count
 *
 * @note This is not thread safe
 */
class FlashDynamicBitSet final {
public:
    /**
     * @brief Construct the bitset with certain capacity
     *
     * @param capacity     [in] capacity
     */
    explicit FlashDynamicBitSet(uint32_t capacity) noexcept;

    /**
     * @brief Construct the bitset with external allocated memory and capacity,
     * the memory will NOT be allocated, use external allocated memory directly,
     * but other member variables will be initialized, this function can be used
     * for recovering from file
     *
     * @param memAddress   [in] address of external allocated memory
     * @param capacity     [in] how many bits stored
     * @param clearBits    [in] set all bites to 0, if not true, trueCount will be auto set by the state of memory
     */
    explicit FlashDynamicBitSet(uintptr_t memAddress, uint32_t capacity, bool clearBits) noexcept;
    ~FlashDynamicBitSet();

    FlashDynamicBitSet(const FlashDynamicBitSet &) = delete;
    FlashDynamicBitSet(FlashDynamicBitSet &&) = delete;
    FlashDynamicBitSet &operator=(const FlashDynamicBitSet &) = delete;
    FlashDynamicBitSet &operator=(FlashDynamicBitSet &&) = delete;

    /**
     * @brief Calculate memory size required based on capacity
     *
     * @param capacity     [in] how many bits can be supported
     * @return memory size in bytes
     */
    static uint64_t GetMemSize(uint32_t capacity) noexcept;

    /**
     * @brief Set the pos bit to true
     *
     * @param pos          [in] position of bit
     */
    void Set(uint32_t pos) noexcept;

    /**
     * @brief Set the pos bit to false
     *
     * @param pos          [in] position of bit
     */
    bool Clear(uint32_t pos) noexcept;

    /**
     * @brief Set all bits to false
     */
    void ClearAll() noexcept;

    /**
     * @brief Check the bit at position is true or not
     *
     * @param pos          [in] position of bit
     */
    bool Test(uint32_t pos) const noexcept;

    /**
     * @brief Count the number of bits of true
     */
    uint32_t Count() const noexcept;

    /**
     * @brief Check if all bits are true
     */
    bool Full() const noexcept;

    /**
     * @brief Capacity
     *
     * @return capacity of this bitset
     */
    uint32_t Capacity() const noexcept;

    /**
     * @brief Find the next bit pos which is not true
     *
     * @param startPos     [in] the start position to find
     * @param resultPos    [out] the position of result
     *
     * @return true if found
     */
    bool FindAndSet(uint32_t startPos, uint32_t &resultPos) noexcept;

    /**
     * @brief operator <<
     */
    friend std::ostream &operator<<(std::ostream &os, const FlashDynamicBitSet &bs);

private:
    /**
     * @brief Test bit without boundary check
     */
    bool TestInner(uint32_t pos) const noexcept;

    std::vector<uint64_t> data_;
    uint64_t *bitChunks_;                  /* real memory to store bits, one uint64_t stores 64 bits */
    uint32_t chunkCount_;                  /* how many uint64_t */
    uint32_t capacity_;                    /* how many bits in total */
    uint32_t trueCount_;                   /* how many bits already set to true */
    uint32_t minTouchedChunk_{UINT32_MAX}; /* min chunk touched since last ClearAll */
    uint32_t maxTouchedChunk_{0};          /* max chunk touched since last ClearAll */
};

ALWAYS_INLINE FlashDynamicBitSet::FlashDynamicBitSet(uint32_t capacity) noexcept
    : chunkCount_{(capacity + DBS_CHUNK_MASK) >> DBS_CHUNK_SHIFT},
      capacity_{capacity},
      trueCount_{0}
{
    UBS_SLOG_DEBUG("chunkCount: " << chunkCount_ << ", capacity: " << capacity_);
    data_.resize(chunkCount_);
    bitChunks_ = data_.data();
    UBS_SLOG_DEBUG("DynamicBitset initialized, this: " << std::hex << this << ", bitChunks: " << bitChunks_ << std::dec
                                                       << ", capacity: " << capacity_ << ", chunkCount: " << chunkCount_
                                                       << ", trueCount: " << trueCount_);
}

inline FlashDynamicBitSet::FlashDynamicBitSet(uintptr_t memAddress, uint32_t capacity, bool clearBits) noexcept
    : bitChunks_{reinterpret_cast<uint64_t *>(memAddress)},
      chunkCount_{(capacity + DBS_CHUNK_MASK) >> DBS_CHUNK_SHIFT},
      capacity_{capacity},
      trueCount_{0}
{
    if (clearBits) {
        /* set all bits to 0 */
        bzero(bitChunks_, chunkCount_ * sizeof(uint64_t));
    } else {
        /* loop all bits to get trueCount_ and touched chunk range */
        for (uint32_t i = 0; i < capacity_; i++) {
            if (TestInner(i)) {
                ++trueCount_;
                uint32_t chunkIdx = DBS_CHUNK_POS(i);
                if (chunkIdx < minTouchedChunk_) {
                    minTouchedChunk_ = chunkIdx;
                }
                if (chunkIdx > maxTouchedChunk_) {
                    maxTouchedChunk_ = chunkIdx;
                }
            }
        }
    }
}

ALWAYS_INLINE uint64_t FlashDynamicBitSet::GetMemSize(uint32_t capacity) noexcept
{
    if (capacity == 0) {
        UBS_SLOG_ERR("Invalid capacity " << capacity << ", should be not 0");
        return 0;
    }

    /* get chunk count by round up 64, i.e. how many uint64_t items */
    auto chunkCount = (static_cast<uint64_t>(capacity) + DBS_CHUNK_MASK) >> DBS_CHUNK_SHIFT;
    UBS_SLOG_DEBUG("chunkCount: " << chunkCount << ", capacity: " << capacity);

    /* multiple size of uint64_t */
    return chunkCount * sizeof(uint64_t);
}

ALWAYS_INLINE FlashDynamicBitSet::~FlashDynamicBitSet() {}

ALWAYS_INLINE void FlashDynamicBitSet::Set(uint32_t pos) noexcept
{
    if (UNLIKELY(pos >= capacity_)) {
        UBS_SLOG_WARN("Invalid pos " << pos << " exceed capacity " << capacity_);
        return;
    }

    /* if already set */
    if (TestInner(pos)) {
        return;
    }

    /* set the bit in chunks to true */
    bitChunks_[DBS_CHUNK_POS(pos)] |= (1UL) << DBS_BIT_POS(pos);
    ++trueCount_;

    uint32_t chunkIdx = DBS_CHUNK_POS(pos);
    if (chunkIdx < minTouchedChunk_) {
        minTouchedChunk_ = chunkIdx;
    }
    if (chunkIdx > maxTouchedChunk_) {
        maxTouchedChunk_ = chunkIdx;
    }
}

ALWAYS_INLINE bool FlashDynamicBitSet::Clear(uint32_t pos) noexcept
{
    if (UNLIKELY(pos >= capacity_)) {
        return false;
    }

    if (!TestInner(pos)) {
        return true;
    }

    /* set the bit in chunks to false i.e. 0 */
    bitChunks_[DBS_CHUNK_POS(pos)] &= ~((1UL) << DBS_BIT_POS(pos));
    --trueCount_;

    return true;
}

ALWAYS_INLINE void FlashDynamicBitSet::ClearAll() noexcept
{
    if (bitChunks_ == nullptr) {
        trueCount_ = 0;
        return;
    }

    if (minTouchedChunk_ <= maxTouchedChunk_) {
        bzero(bitChunks_ + minTouchedChunk_,
              (static_cast<uint64_t>(maxTouchedChunk_ - minTouchedChunk_) + 1) * sizeof(uint64_t));
    } else if (trueCount_ > 0) {
        /* bits set via external-memory ctor (not via Set/FindAndSet), full clear */
        bzero(bitChunks_, static_cast<uint64_t>(chunkCount_) * sizeof(uint64_t));
    }

    trueCount_ = 0;
    minTouchedChunk_ = UINT32_MAX;
    maxTouchedChunk_ = 0;
}

ALWAYS_INLINE bool FlashDynamicBitSet::TestInner(uint32_t pos) const noexcept
{
    /* test bit */
    return (bitChunks_[DBS_CHUNK_POS(pos)] & ((1UL) << DBS_BIT_POS(pos))) != 0;
}

ALWAYS_INLINE bool FlashDynamicBitSet::Test(uint32_t pos) const noexcept
{
    if (UNLIKELY(pos >= capacity_)) {
        return false;
    }

    return TestInner(pos);
}

ALWAYS_INLINE uint32_t FlashDynamicBitSet::Count() const noexcept
{
    return trueCount_;
}

ALWAYS_INLINE bool FlashDynamicBitSet::Full() const noexcept
{
    return trueCount_ == capacity_;
}

ALWAYS_INLINE uint32_t FlashDynamicBitSet::Capacity() const noexcept
{
    return capacity_;
}

ALWAYS_INLINE bool FlashDynamicBitSet::FindAndSet(uint32_t startPos, uint32_t &resultPos) noexcept
{
    if (UNLIKELY(startPos >= capacity_ || capacity_ == trueCount_)) {
        UBS_SLOG_ERR("Invalid params, may be start pos or true count exceed capacity.");
        return false;
    }

    for (uint32_t i = DBS_CHUNK_POS(startPos); i < chunkCount_; ++i) {
        /* if the whole uint64_t is all true bits */
        if (bitChunks_[i] == UINT64_MAX) {
            continue;
        }

        /* find available bit at or after startPos in this chunk */
        uint64_t value = bitChunks_[i];
        if (i == DBS_CHUNK_POS(startPos)) {
            value |= (1UL << DBS_BIT_POS(startPos)) - 1;
        }

        uint32_t bitIdx = static_cast<uint32_t>(__builtin_ffsll(~value));
        if (UNLIKELY(bitIdx == 0)) {
            continue;
        }

        /* calc result pos */
        resultPos = (i << DBS_CHUNK_SHIFT) + --bitIdx;
        if (UNLIKELY(resultPos >= capacity_)) {
            resultPos = 0;
            return false;
        }

        // set bit to true
        bitChunks_[i] |= ((1UL) << DBS_BIT_POS(bitIdx));
        ++trueCount_;
        if (i < minTouchedChunk_) {
            minTouchedChunk_ = i;
        }
        if (i > maxTouchedChunk_) {
            maxTouchedChunk_ = i;
        }

        return true;
    }

    return false;
}

ALWAYS_INLINE std::ostream &operator<<(std::ostream &os, const FlashDynamicBitSet &bs)
{
    os << "FlashDynamicBitSet initialized: " << (bs.bitChunks_ != nullptr) << ", chunksAddress: " << std::hex
       << static_cast<void *>(bs.bitChunks_) << std::dec << ", chunkCount: " << bs.chunkCount_
       << ", capacity: " << bs.capacity_ << ", bit allocated: " << bs.trueCount_;
    return os;
}

} // namespace ock::ubs

#endif // UBS_COMM_UBSOCKET_FLASH_DYNAMIC_BITSET_H
