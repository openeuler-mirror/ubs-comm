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
#ifndef UBS_COMM_UBSOCKET_CONN_BUFFER_H
#define UBS_COMM_UBSOCKET_CONN_BUFFER_H

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "ubsocket_conn_types.h"

namespace ock {
namespace ubs {

/*
 * 每连接非阻塞增量读缓冲（控制面消息小，固定缓冲足够）。
 * recv 累积到 buf_ 直到 size_ >= expected；EAGAIN 时立即返回已读字节数，绝不阻塞。
 */
class AsyncConnBuffer {
public:
    AsyncConnBuffer() = default;

    /* 非阻塞增量读：recv 累积到 buf_，直到 size_ >= expected。
     * 返回当前累计字节数（>=0）；<0 表示出错（对端关闭 / recv 错误）。 */
    ssize_t Advance(int fd, size_t expected);

    bool Ready(size_t expected) const
    {
        return size_ >= expected;
    }

    uint8_t *Data()
    {
        return buf_;
    }

    /* 消费前 n 字节，剩余字节前移供下一阶段（保留一次 recv 多读的字节） */
    void Consume(size_t n);

    void Reset()
    {
        size_ = 0;
    }

private:
    uint8_t buf_[CONN_CTRL_BUF_SIZE];
    size_t size_ = 0;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONN_BUFFER_H
