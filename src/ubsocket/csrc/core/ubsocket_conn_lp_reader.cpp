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
#include "ubsocket_conn_lp_reader.h"

#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {

namespace {
constexpr uint32_t kMaxDiscardBytes = 64 * 1024; /* 对齐同步路径 RecvLengthPrefixed 的丢弃上限 */
} // namespace

LpReadResult LengthPrefixedReader::Read(int fd, AsyncConnBuffer &buf, const char *tag)
{
    for (;;) {
        switch (stage_) {
            case Stage::kPrefix: {
                ssize_t n = buf.Advance(fd, sizeof(body_len_));
                if (n < 0) {
                    return LpReadResult::kError;
                }
                if (!buf.Ready(sizeof(body_len_))) {
                    return LpReadResult::kPartial;
                }
                memcpy(&body_len_, buf.Data(), sizeof(body_len_));
                if (body_len_ == 0) {
                    UBS_VLOG_ERR("%s recv body_len=0, fd: %d\n", tag, fd);
                    return LpReadResult::kError;
                }
                read_body_ = std::min(body_len_, static_cast<uint32_t>(target_size_));
                extra_ = (body_len_ > target_size_) ? (body_len_ - static_cast<uint32_t>(target_size_)) : 0;
                if (extra_ > kMaxDiscardBytes) {
                    UBS_VLOG_ERR("%s body_len(%u) exceeds discard cap, fd: %d\n", tag, body_len_, fd);
                    return LpReadResult::kError;
                }
                buf.Reset(); /* 前缀已解析：body 直读 target_，buf_ 留给 discard 复用 */
                have_ = 0;
                stage_ = Stage::kBody;
                continue;
            }
            case Stage::kBody: {
                /* 直接把 body 读入 target_（省去中间缓冲的一次 memcpy） */
                while (have_ < read_body_) {
                    errno = 0;
                    ssize_t n = LibcApi::recv(fd, target_ + have_, read_body_ - have_, MSG_NOSIGNAL);
                    if (n > 0) {
                        have_ += static_cast<size_t>(n);
                        continue;
                    }
                    if (n == 0) {
                        UBS_VLOG_DEBUG("%s recv body peer closed, fd: %d\n", tag, fd);
                        return LpReadResult::kError;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return LpReadResult::kPartial; /* 半包：等下次可读续读 */
                    }
                    UBS_VLOG_ERR("%s recv body failed, fd: %d, errno: %d\n", tag, fd, errno);
                    return LpReadResult::kError;
                }
                if (read_body_ < target_size_) { /* 对端结构体较短：缺失字段补零（对齐跨版本语义） */
                    memset(target_ + read_body_, 0, target_size_ - read_body_);
                }
                stage_ = (extra_ > 0) ? Stage::kDiscard : Stage::kDone;
                continue;
            }
            case Stage::kDiscard: {
                while (extra_ > 0) {
                    uint32_t chunk = std::min(extra_, static_cast<uint32_t>(CONN_CTRL_BUF_SIZE));
                    ssize_t n = buf.Advance(fd, chunk);
                    if (n < 0) {
                        return LpReadResult::kError;
                    }
                    size_t avail = static_cast<size_t>(std::min(static_cast<ssize_t>(chunk), n));
                    if (avail == 0) {
                        return LpReadResult::kPartial; /* 超量字节尚未到齐 */
                    }
                    buf.Consume(avail);
                    extra_ -= static_cast<uint32_t>(avail);
                }
                stage_ = Stage::kDone;
                continue;
            }
            default:
                return LpReadResult::kComplete; /* kDone */
        }
    }
}

} // namespace ubs
} // namespace ock
