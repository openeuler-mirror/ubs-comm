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
#ifndef UBS_COMM_UBSOCKET_THREAD_ERRNO_H
#define UBS_COMM_UBSOCKET_THREAD_ERRNO_H

namespace ock {
namespace ubs {

/* issue#33（753×753 现网 core）：glibc 把 __errno_location() 声明为 const 函数，
 * 高优化构建会在函数内把它一次取址、后续 errno 宏读写全走缓存指针；而含
 * bthread 停泊点（EventWait / poll 切片 yield / ProcessNewMessage）的函数在
 * 停泊后可能迁移到另一条 pthread——此后系统调用写的是新 pthread 的 TLS，
 * 函数经缓存指针读写的却是旧 pthread 的槽位（现网实测：真实 ECONNREFUSED
 * 被读成陈值 0，一路喂进 brpc Controller::SetFailed 的 CHECK(error_code != 0)
 * 致全进程 abort；b043 反汇编 SendSocketData 全函数仅 1 处 errno_location 调用）。
 *
 * 本单元独立编译且禁内联：每次调用都强制重新执行 __errno_location()，
 * 读写永远命中当前 pthread。凡"停泊点之后仍要访问 errno"的代码一律经此二函数。 */
int ThreadErrno() noexcept;
void SetThreadErrno(int e) noexcept;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_THREAD_ERRNO_H
