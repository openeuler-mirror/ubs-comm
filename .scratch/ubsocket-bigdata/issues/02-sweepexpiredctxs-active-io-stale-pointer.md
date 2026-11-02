# SweepExpiredCtxs pending 删除后 active_io 残留 stale 指针 → 重复计数 + UAF

**Status:** resolved
**Source:** chat(bigdata UT 批3 会话,2026-08-29 发现并修复)
**Reporter:** wangshun-1999(批3 认领人)
**Category:** bug(真实生产缺陷,已修复)

## Original report

SweepExpiredCtxs 的 pending_reads 超时分支删除 ctx 后未从 active_io 中移除该 ctx——paused ctx 在 enqueue 进 pending_reads 前已被插入 active_io(DoReadOffer 在 post 前插入,约 1613 行),缺失 erase 导致:
1. 同一次 sweep 中 active 分支对同一 ctx 重复计数 `rx_ctx_timeout`(delta=2 而非 1);
2. ctx delete 后 active_io 仍持有 stale 指针 → 后续 sweep / CleanupSocketState 解引用已 free 的 ctx(UAF,间歇 SEGFAULT)。

## Known leads

- 修复点: `csrc/core/ubsocket_bigdata.cpp` `SweepExpiredCtxs` pending 超时分支添加 `state->active_io.erase(ctx);`(约 2799 行),与 CleanupSocketState 既有模式(约 2933 行 `active.erase(ctx)`)一致。
- 测试侧: `ubsocket_bigdata_test.cpp` §5.2 `SweepExpiredForSocket_PendingCtxExpired_FreedAndCounterUp` 断言 `rx_ctx_timeout` delta=1 + free 计数,锁定该语义。
