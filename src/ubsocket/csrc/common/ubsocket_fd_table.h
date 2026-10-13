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
#ifndef UBS_COMM_UBSOCKET_FD_TABLE_H
#define UBS_COMM_UBSOCKET_FD_TABLE_H

#include <cstdint>

namespace ock {
namespace ubs {

/*
 * fd 表预扩容（规避 OS 扩容时延，brpc !47 的库内版本）
 *
 * 背景：Linux 进程的 fd 表按档位增长（64 → 128 → … → 32768 → 65536 → 131072），只增不减。每长一档走
 * expand_fdtable()，其中 `if (atomic_read(&files->count) > 1) synchronize_rcu();`——进程是多线程时，每次扩容
 * 都要等一个 RCU 宽限期（现网实测 40~70ms）；这段时间里本进程所有"需要超出当前表的新 fd"的调用
 *（socket / accept / eventfd …）都卡在 expand_files() 里。建链洪峰正是 fd 号一路往上走的时候。
 *
 * 做法：初始化时一次把表撑到目标档位。目标 N 会落到不小于 N+1 的下一档：20000 → 32768，45000 → 65536，
 * 70000 → 131072；每个表项 8 字节，65536 这一档约 0.5MB。
 *
 * 为什么不用 dup2(fd, N)：newfd 已经打开时 dup2 会把它静默关掉。ubsocket_init 的调用时机由宿主进程决定，
 * 被关掉的可能是宿主自己的 socket 或文件。fcntl(F_DUPFD_CLOEXEC, N) 返回"不小于 N 的最小空闲 fd"，
 * 同样触发扩容，但不会碰任何已打开的 fd。
 */

/* 目标钳到软上限以内：超过 RLIMIT_NOFILE 的 fd 本来也申请不到，而 F_DUPFD 的起点 ≥ 软上限会直接 EINVAL。
 * 返回实际使用的目标；0 = 不做。rlim_cur 传 0 表示无限制/未知。 */
uint32_t ClampFdTableTarget(uint32_t want, uint64_t rlim_cur) noexcept;

/* 把本进程的 fd 表预扩到能容纳 fd 号 want。want == 0 关闭。
 * 返回实际使用的目标（成功），0 = 未做或失败（失败只打 WARN，不影响初始化）。 */
uint32_t PreExpandFdTable(uint32_t want) noexcept;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_FD_TABLE_H
