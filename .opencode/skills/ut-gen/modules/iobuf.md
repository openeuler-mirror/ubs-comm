# IO-BUF 模块测试附录 (csrc/iobuf/)

与 `ut-gen/SKILL.md` 配合使用。零拷贝内存管理: Block ABI 对齐 + UMQ qbuf pool 回调。

## 模块范围(2026-08-24 实测)

- `ubsocket_iobuf.h` — Block/BlockRef/BlockCache 结构体(与 brpc `IOBuf::Block` 手工对齐的 ABI 契约)
- `ubsocket_zcopy_adapter.{h,cpp}` — **`ubsocket_zcopy_adapter.cpp` 仅 19 行**,只剩全局变量 `g_zcopy_allocator` 定义(旧文档称 242 行/零拷贝实现——已被整体重构,当前实现语义见 `CONTEXT.md` §Block 零拷贝)
- `csrc/` 顶层另有 `ubsocket_iobuf.h` 关联的 UMQ qbuf pool 回调(`blockmem_allocate/deallocate` → `UmqZeroCopyAllocator` → `umq_buf_alloc/umq_buf_free`)

## 现状与空白点

- **当前没有任何 iobuf 测试**——`iobuf_zcopy_adapter_test` 已随 commit `61db74c0` 删除;本模块是全仓空白点,优先补
- 零拷贝链路涉及 ABI 对齐(reinterpret_cast 互转,无编译期校验)与 brpc 回调契约,`brpc/` 已从源码树移除——测试应聚焦: `ubsocket_iobuf.h` 的 struct 布局/大小、`UmqZeroCopyAllocator` 对 `umq_buf_alloc/free` 的调用、`g_zcopy_allocator` 生命周期

## 特有陷阱

1. **ABI 无编译期校验** — Block 结构体与 brpc 版本靠手工维护;测试可锁住 `sizeof`/字段偏移(若发现与上层库不符是产品 bug,不是测试问题)
2. **`RecordAndSetBrpcAllocator` 修改指针指向的值** — 若测试涉及 allocator 回调注册,断言前保存旧值(`alloc_addr_origin_` 语义),别直接比较已被改写的变量(旧文档陷阱,当前代码如已重构需先 grep 验证)
3. **qbuf 生命周期** — 测试 mock `umq_buf_alloc/umq_buf_free` 时注意 `buf_data` headroom 语义(placement-new Block 于 `buf_data - sizeof(Block)`),构造 mock buffer 时留出 headroom

## 边界清单

| 判定点 | 边界值(+相邻值) | 对应用例 |
|--------|----------------|----------|
| `Block::Full`/`LeftSpace` | `size = cap - 1`、`size = cap` | `Block_ConstructorAndSpaceState` |
| `Block::DecRef` | `nshared = 2`、`nshared = 1` | `Block_ReferenceAndLinkedListOperations` |
| `CutAndInsertAfter` 空输入 | 空 cache、`block == nullptr`，以及非空 cache + `nullptr` | `CutAndInsertAfter_EmptyOrNullInputReturnsZero` |
| partial block 截断 | `cut_size < partial.length`、`cut_size == partial.length`、`cut_size = 0` | `CutAndInsertAfter_PartialBlockCanBeConsumedInPieces`、`CutAndInsertAfter_ZeroCutKeepsPartialBlock` |
| 多 block 截断 | 首 block 后遇到 non-first block、cut 超过总 cache 长度 | `CutAndInsertAfter_NonFirstBlockStopsBeforeCut`、`CutAndInsertAfter_CutBeyondAllBlocksReturnsAvailableBytes` |
| output 链拼接 | output 已有 next block | `CutAndInsertAfter_PreservesExistingOutputChain` |
| Flush 生命周期 | 仅缓存链、partial block、partial + 剩余缓存链 | `InsertAndFlush_ReleasesAllCachedBlocks`、`Flush_WithPartialBlock_ReleasesPartialReference`、`Flush_WithPartialAndRemainingBlocks_ClearsBothLists` |
