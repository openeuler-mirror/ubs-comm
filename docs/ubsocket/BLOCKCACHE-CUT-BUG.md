# BlockCache::CutAndInsertAfter 数据丢失 Bug

## 概述

**文件**: `src/ubsocket/csrc/iobuf/ubsocket_iobuf.h`  
**方法**: `BlockCache::CutAndInsertAfter()`  
**严重度**: 高 — 导致 Insert 到 BlockCache 的数据无法全部读取出来，丢失数据  
**触发条件**: `CutPartialBlock` 完全消耗 `partial_block_` 后使 `rx_total_len` 等于 `cut_size`，且 cache 中仍有后续 block

## 影响

BlockCache 的设计语义是 `Insert` 的数据全部可通过 `CutAndInsertAfter` 读出来。Bug 导致：当 `CutPartialBlock` 恰好满足 cut 需求后，cache 中当前 block 被错误地链接到输出链但不计入 `rx_total_len`，造成数据丢失且 `cache_len_` 与 `head_block_` 状态不一致。

## 复现步骤

```cpp
BlockCache cache;

// Step 1: Insert(data_size=8160)  → cache: [b1(cap=8160)], cache_len=8160
cache.Insert(d1, 8160);

// Step 2: Insert(data_size=68)    → cache: [b1(cap=8160)] → [b2(cap=68)], cache_len=8228
cache.Insert(d2, 68);

// Step 3: CutAndInsertAfter(32, out1)
//   b1 cap(8160) > cut(32), 首 block 创建 partial_block_:
//     partial_block_ = {block=b1, offset=32, length=8128, b1->cap=32}
//   b1 链接到 out1, head=cache的b2, cache_len=8228-32=8196
//   return 32  ✓
cache.CutAndInsertAfter(32, out1);

// Step 4: CutAndInsertAfter(8128, out2)
//   partial_block_(b1, length=8128) 存在:
//     CutPartialBlock(8128): 8128≥8128, 完全消耗 partial, b1->cap 恢复到 8160
//     partial_block_ 被 Reset, rx_total_len=8128
//   head_block_=b2(cap=68):
//     8128+68 < 8128?  FALSE  (不进入"未超cut继续累加"分支)
//     8128 < 8128?     FALSE  (不进入"创建partial"分支)
//     → 落入 BUG 路径: last_cache_block=b2; rx_total_len=8128; break
//     → b2 被链接到输出链, head_block_=nullptr
//   cache_len=8196-8128=68 (但 head 已空!)
//   return 8128  ✓ (返回值正确, 但 b2 已丢失)
cache.CutAndInsertAfter(8128, out2);

// Step 5: CutAndInsertAfter(136, out3)
//   cache_len=68 (!=0, 不进提前返回)
//   head_block_=nullptr → 跳过 do-while 循环
//   return 0  ✗ (期望 68, 数据丢失!)
cache.CutAndInsertAfter(136, out3);
```

## 根因分析

Bug 位于 `CutAndInsertAfter` 的 cache block 遍历循环 (`ubsocket_iobuf.h:136-162`):

```cpp
do {
    if (rx_total_len + cache_block->cap < cut_size) {
        // 分支A: 当前块加入后仍不足 cut → 累加, continue
        rx_total_len += cache_block->cap;
        last_cache_block = cache_block;
        continue;
    }

    if (rx_total_len < cut_size) {
        // 分支B: 需要当前块的部分数据
        if (rx_total_len != 0) {
            break;  // 非首 block, 禁止切割, 停止
        }
        // 首 block, 创建 partial_block_
        ...
    }
    // BUG: 当 rx_total_len >= cut_size 时,
    //      两个 if 都不进入, 直接落入此处的"消费当前 block"路径
    last_cache_block = cache_block;   // ← 错误: 标记当前 block 为已消费
    rx_total_len = cut_size;
    break;
} while (...);
```

**问题**: 三个分支覆盖了 `rx_total_len + cap < cut_size` 和 `rx_total_len < cut_size` 两种情况, 但没有覆盖 `rx_total_len >= cut_size`。当 cut 已满足时（例如 CutPartialBlock 已提供足够数据），代码理应停止循环、不消费当前 cache block。但实际走到了 `last_cache_block = cache_block` 分支，将当前 block 错误链接到输出链并清空 head。

**后果链**:
1. 当前 cache block 被 `out_block_tail->SetNext(head_block_)` 链接到输出链
2. `head_block_ = last_cache_block->GetNext()` 使 head 后移（或变 nullptr）
3. `cache_len_` 只减 `rx_total_len`（不含此 block 的 cap），造成 `cache_len_ > 0` 但 `head_block_ == nullptr` 的不一致状态
4. 后续 `CutAndInsertAfter` 因 head 为空返回 0，数据丢失

## 修复

在 `ubsocket_iobuf.h:158` 行后增加 `else { break; }`，当 `rx_total_len >= cut_size` 时立即停止循环，不消费当前 block:

```cpp
if (rx_total_len < cut_size) {
    if (rx_total_len != 0) {
        break;
    }
    partial_block_.offset = cut_size - rx_total_len;
    partial_block_.length = cache_block->cap - partial_block_.offset;
    partial_block_.block = cache_block;
    partial_block_.block->cap = partial_block_.offset;
    cache_block->IncRef();
} else {
    break;  // 修复: rx_total_len 已满足 cut, 停止, 不消费当前 block
}
last_cache_block = cache_block;
rx_total_len = cut_size;
break;
```

修复后 `last_cache_block` 保持为 `nullptr`（或在之前迭代中已指向一个已消费的 block），不进入 `if (last_cache_block != nullptr)` 的后处理逻辑，当前 cache block 留在 `head_block_` 链中供后续读取。

### 修复前后对比

| 状态 | 步骤4 后 cache_len_ | 步骤4 后 head_block_ | 步骤5 返回值 |
|------|---------------------|---------------------|-------------|
| 修复前 | 68 (但 head 为空, 不一致) | nullptr | 0 (数据丢失) |
| 修复后 | 68 | b2(cap=68) | 68 (正确) |

## 测试覆盖

`block_cache_test.cpp` 的 `CutAndInsertAfter_StepByStepScenario` 测试用例精确复现该场景并验证修复:

1. Insert(8160) → Insert(68)
2. CutAndInsertAfter(32) → expect 32
3. CutAndInsertAfter(8128) → expect 8128
4. CutAndInsertAfter(136) → expect 68

修复前步骤4返回0（失败），修复后全部5步通过。
