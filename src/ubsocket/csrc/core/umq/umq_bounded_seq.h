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
#ifndef UBS_COMM_UMQ_BOUNDED_SEQ_H
#define UBS_COMM_UMQ_BOUNDED_SEQ_H

#include "csrc/core/ubsocket_core_types.h"

namespace ock {
namespace ubs {
namespace umq {

// 默认情况下，MaxVal 为 0，表示使用标准的 (1 << Bits) - 1 作为最大值，可直接按位计算
template <size_t Bits, typename IntType = uint32_t, IntType MaxVal = 0>
class UmqBoundedSeqTraits {
    static_assert(Bits > 0 && Bits < sizeof(IntType) * 8, "Bits must be less than type bit-width.");

public:
    static constexpr IntType MASK = (Bits == sizeof(IntType) * 8) ? ~IntType(0) : (IntType(1) << Bits) - 1;
    static constexpr IntType MAX_VALID = (MaxVal == 0) ? MASK : MaxVal;

    // 整个环形空间的长度（模数）
    static constexpr IntType MODULUS = MAX_VALID + 1;

    // 动态计算当前序列号空间允许的最大前向滑动窗口（总空间的一半），用于判断：旧包 or 超出防环绕范围
    static constexpr IntType MAX_WINDOW = (MODULUS - 1) / 2;

    static inline IntType Mask(IntType key) noexcept
    {
        return key & MASK;
    }

    static inline IntType Normalize(IntType key) noexcept
    {
        // 如果开启了自定义 MaxVal，且传入的值正好是预留的最大值或超出了，进行回绕
        if constexpr (MaxVal != 0) {
            IntType masked = Mask(key);
            return (masked >= MODULUS) ? (masked - MODULUS) : masked;
        } else {
            return Mask(key);
        }
    }

    static IntType Distance(IntType base_key, IntType incoming_key) noexcept
    {
        IntType base = Normalize(base_key);
        IntType inc = Normalize(incoming_key);
        IntType diff_forward;
        if constexpr (MaxVal == 0) {
            // 利用掩码减法自然回绕
            return Mask(inc - base);
        } else {
            // 显式计算环形正向距离
            return (inc >= base) ? (inc - base) : (MODULUS - base + inc);
        }
    }

    static bool CompareLessInCircularOrder(IntType key_a, IntType key_b) noexcept
    {
        if constexpr (MaxVal == 0) {
            static constexpr size_t SHIFT_AMT = sizeof(IntType) * 8 - Bits;
            using SignedT = typename std::make_signed<IntType>::type;
            return static_cast<SignedT>((key_a - key_b) << SHIFT_AMT) < 0;
        } else {
            IntType dist = Distance(key_a, key_b);
            return (dist > 0) && (dist <= MAX_WINDOW);
        }
    }

    static IntType Add(IntType base, IntType val) noexcept
    {
        IntType b = Normalize(base);
        if constexpr (MaxVal == 0) {
            return Mask(b + val);
        } else {
            using SignedT = typename std::make_signed<IntType>::type;
            if (static_cast<SignedT>(val) < 0) {
                // 负数回退逻辑：转为正向加法
                IntType abs_val = static_cast<IntType>(-static_cast<SignedT>(val));
                if (abs_val >= MODULUS) {
                    abs_val %= MODULUS;
                }
                return (b >= abs_val) ? (b - abs_val) : (b + MODULUS - abs_val);
            }

            IntType step = val;
            if (step >= MODULUS) {
                step %= MODULUS; // 仅在增量大于模数时才取模
            }

            IntType res = b + step;
            return (res >= MODULUS) ? (res - MODULUS) : res;
        }
    }

    static IntType Next(IntType base)
    {
        if constexpr (MaxVal == 0) {
            return Mask(base + 1);
        } else {
            IntType b = Normalize(base);
            IntType next = b + 1;
            return (next == MODULUS) ? 0 : next;
        }
    }
};

template <size_t Bits, typename IntType = uint32_t, IntType MaxVal = 0>
class UmqSocketBoundedSequence : public UmqBoundedSeqTraits<Bits, IntType, MaxVal> {
    using Traits = UmqBoundedSeqTraits<Bits, IntType, MaxVal>;

public:
    explicit UmqSocketBoundedSequence(IntType val = 0) : m_seq_num(Traits::Normalize(val)) {}

    UmqSocketBoundedSequence(const UmqSocketBoundedSequence &) = delete;
    UmqSocketBoundedSequence &operator=(const UmqSocketBoundedSequence &) = delete;

    IntType FetchAddSeqNum(IntType val, std::memory_order order = std::memory_order_relaxed)
    {
        IntType old_val = m_seq_num.load(order);
        IntType new_val;
        do {
            new_val = Traits::Add(old_val, val);
        } while (!m_seq_num.compare_exchange_weak(old_val, new_val, order));
        return old_val;
    }

    IntType FetchSubSeqNum(IntType val, std::memory_order order = std::memory_order_relaxed)
    {
        IntType old_val = m_seq_num.load(order);
        IntType new_val;
        do {
            new_val = Traits::Add(old_val, -val);
        } while (!m_seq_num.compare_exchange_weak(old_val, new_val, order));
        return old_val;
    }

    IntType LoadSeqNum(std::memory_order order = std::memory_order_relaxed) const
    {
        return m_seq_num.load(order);
    }

    void StoreSeqNum(IntType val, std::memory_order order = std::memory_order_relaxed)
    {
        m_seq_num.store(Traits::Normalize(val), order);
    }

private:
    std::atomic<IntType> m_seq_num;
};

} // namespace umq
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UMQ_BOUNDED_SEQ_H
