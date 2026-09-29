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
#include "ubsocket_conn_buffer.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {

ssize_t AsyncConnBuffer::Advance(int fd, size_t expected)
{
    if (expected > CONN_CTRL_BUF_SIZE) {
        UBS_VLOG_ERR("AsyncConnBuffer::Advance expected(%zu) exceed buffer size(%u), fd: %d\n", expected,
                     CONN_CTRL_BUF_SIZE, fd);
        errno = EINVAL;
        return -1;
    }
    if (size_ >= expected) {
        return static_cast<ssize_t>(size_);
    }

    errno = 0;
    for (;;) {
        if (size_ >= expected) {
            return static_cast<ssize_t>(size_);
        }
        ssize_t n = LibcApi::recv(fd, buf_ + size_, expected - size_, MSG_NOSIGNAL);
        if (n > 0) {
            size_ += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) { /* 对端关闭 */
            UBS_VLOG_DEBUG("AsyncConnBuffer::Advance peer closed, fd: %d\n", fd);
            errno = ECONNRESET;
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { /* 半包：非阻塞返回已读字节数 */
            errno = 0;
            return static_cast<ssize_t>(size_);
        }
        UBS_VLOG_ERR("AsyncConnBuffer::Advance recv failed, fd: %d, errno: %d, errmsg: %s\n", fd, errno,
                     Func::Error2Str(errno));
        return -1;
    }
}

void AsyncConnBuffer::Consume(size_t n)
{
    if (n >= size_) {
        size_ = 0;
        return;
    }
    memmove(buf_, buf_ + n, size_ - n);
    size_ -= n;
}

} // namespace ubs
} // namespace ock
