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
#ifndef UBS_COMM_UMQ_INTRUSIVE_BUF_QUEUE_H
#define UBS_COMM_UMQ_INTRUSIVE_BUF_QUEUE_H

#include <cstdint>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_umq_api.h"
#include "umq_pro_types.h"

namespace ock {
namespace ubs {
namespace umq {

/**
 * 侵入式 SPSC qbuf 队列（单生产者：Share-JFR poller 线程；单消费者：应用 readv 线程）。
 *
 * 与 SPSCRingQueue 不同，本队列不预留任何与容量成正比的数组：队列节点就是 umq_buf_t 本身，
 * 链接指针复用每个 qbuf 的 qbuf_ext 扩展区中 umq_buf_pro_t::rsvd1 保留字段（qbuf 在
 * ubsocket 持有期间该字段无任何使用者）。因此每条链路的队列固定开销为 O(1)（2 个
 * cache line：生产者行 + 消费者行），排队元素的存储完全由 qbuf 自身承载 —— 总内存受
 * "缓冲区数量"约束，与链路数量无关。
 *
 * 注意：绝不能复用 qbuf_next 作为链接字段 —— umq_buf_free() 会沿 qbuf_next 遍历并
 * 级联释放整条链，复用会导致排队中的其他 qbuf 被误释放。
 *
 * 算法：生产者以 CAS 将节点压入 inbox 单向链（新→旧）；消费者一次 exchange 摘走整条
 * inbox 链，就地反转为 FIFO 后放入消费者私有 pending 链逐个出队。pending 非空时不再
 * 合并 inbox（inbox 中元素必然更新），保证严格 FIFO。容量上限通过 enq/deq 提交计数
 * 检查（与 SPSCRingQueue 的 commit_write_/commit_read_ 语义一致），仅用于保留
 * QUEUE_FULL 背压语义，不再决定内存占用。
 */
class UmqIntrusiveBufQueue {
public:
    explicit UmqIntrusiveBufQueue(uint64_t capacity) : capacity_(capacity) {}

    ~UmqIntrusiveBufQueue() = default;

    UmqIntrusiveBufQueue(const UmqIntrusiveBufQueue &) = delete;
    UmqIntrusiveBufQueue &operator=(const UmqIntrusiveBufQueue &) = delete;

    /* 仅限单一生产者线程调用；队列达到容量上限时返回 false（不释放 buf，由调用者决策） */
    bool Push(umq_buf_t *buf) noexcept
    {
        if (buf == nullptr) {
            return false;
        }
        auto deq = __atomic_load_n(&commit_deq_, __ATOMIC_ACQUIRE);
        if (enq_local_ - deq >= capacity_) {
            return false;
        }

        /*
         * 先提交 enq 计数、后发布节点：消费者只能弹出已发布的节点，而发布动作
         * happens-after 计数提交，因此任何线程观察到 commit_deq_ == k 时必然
         * 也能观察到 commit_enq_ >= k，Size() 恒不回绕为负。
         */
        enq_local_++;
        __atomic_store_n(&commit_enq_, enq_local_, __ATOMIC_RELEASE);

        umq_buf_t *old = __atomic_load_n(&inbox_, __ATOMIC_RELAXED);
        do {
            LinkSlot(buf) = reinterpret_cast<uint64_t>(old);
            /* CAS 失败仅可能因消费者将 inbox 置空摘链，重试一次即成功 */
        } while (!__atomic_compare_exchange_n(&inbox_, &old, buf, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
        return true;
    }

    /* 仅限单一消费者线程调用；按 FIFO 顺序最多出队 maxCount 个 qbuf，返回实际个数 */
    uint64_t PopBatch(umq_buf_t **out, uint64_t maxCount) noexcept
    {
        uint64_t n = 0;
        while (n < maxCount) {
            if (pending_head_ == nullptr) {
                if (__atomic_load_n(&inbox_, __ATOMIC_RELAXED) == nullptr) {
                    break;
                }
                umq_buf_t *chain = __atomic_exchange_n(&inbox_, nullptr, __ATOMIC_ACQUIRE);
                /* inbox 链为新→旧，反转成旧→新的 FIFO 链 */
                umq_buf_t *rev = nullptr;
                while (chain != nullptr) {
                    umq_buf_t *next = FromSlot(LinkSlot(chain));
                    LinkSlot(chain) = reinterpret_cast<uint64_t>(rev);
                    rev = chain;
                    chain = next;
                }
                pending_head_ = rev;
            }

            umq_buf_t *buf = pending_head_;
            pending_head_ = FromSlot(LinkSlot(buf));
            LinkSlot(buf) = 0; /* 清理保留字段，避免向上层泄漏悬空指针 */
            out[n++] = buf;
        }

        if (n != 0) {
            deq_local_ += n;
            __atomic_store_n(&commit_deq_, deq_local_, __ATOMIC_RELEASE);
        }
        return n;
    }

    [[nodiscard]] uint64_t Size() const noexcept
    {
        /* 先读 deq 后读 enq：保证第三方线程并发观察时 enq >= deq，差值不会回绕为负 */
        auto deq = __atomic_load_n(&commit_deq_, __ATOMIC_ACQUIRE);
        auto enq = __atomic_load_n(&commit_enq_, __ATOMIC_ACQUIRE);
        return enq - deq;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return Size() == 0;
    }

    [[nodiscard]] uint64_t Capacity() const noexcept
    {
        return capacity_;
    }

private:
    static uint64_t &LinkSlot(umq_buf_t *buf) noexcept
    {
        return reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext)->rsvd1;
    }

    static umq_buf_t *FromSlot(uint64_t slot) noexcept
    {
        return reinterpret_cast<umq_buf_t *>(slot);
    }

private:
    /*
     * 两条 cache line（原为三条：inbox_ 单独一行）：
     *   生产者行：inbox_ + commit_enq_ + enq_local_ —— 生产者每次 Push 都要写这三者，
     *             合并到一行反而少碰一行；消费者只在 pending 耗尽时 exchange 一次 inbox_
     *             （每批一次而非每个元素一次），代价是生产者随后的一次写 miss；
     *   消费者行：commit_deq_ + deq_local_ + pending_head_ + capacity_ —— 生产者本就
     *             每次 Push 读 commit_deq_ 与 capacity_（容量检查），共处一行不新增抖动。
     * 每链路省 64 B（对象 256 → 128 B），几万条链路下是可观的常驻内存；
     * 生产者与消费者各自独占写入的字段仍分居两行，不引入新的伪共享。
     */
    /* 生产者行 */
    alignas(64) umq_buf_t *inbox_{nullptr};
    uint64_t commit_enq_{0};
    uint64_t enq_local_{0};
    /* 消费者行 */
    alignas(64) uint64_t commit_deq_{0};
    uint64_t deq_local_{0};
    umq_buf_t *pending_head_{nullptr};
    const uint64_t capacity_;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_INTRUSIVE_BUF_QUEUE_H
