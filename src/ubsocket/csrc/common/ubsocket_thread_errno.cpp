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
#include "ubsocket_thread_errno.h"

#include <cerrno>

namespace ock {
namespace ubs {

/* noinline：即使 LTO 也不得内联进调用方——一旦内联，errno 宏就会共享调用方
 * 缓存的 __errno_location() 结果，防护失效（机理见头文件）。 */
__attribute__((noinline)) int ThreadErrno() noexcept
{
    return errno;
}

__attribute__((noinline)) void SetThreadErrno(int e) noexcept
{
    errno = e;
}

} // namespace ubs
} // namespace ock
