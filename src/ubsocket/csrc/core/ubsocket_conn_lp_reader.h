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
#ifndef UBS_COMM_UBSOCKET_CONN_LP_READER_H
#define UBS_COMM_UBSOCKET_CONN_LP_READER_H

#include <cstddef>
#include <cstdint>

#include "ubsocket_conn_buffer.h"

namespace ock {
namespace ubs {

/*
 * 增量读结果（Read 返回）：
 * kError    — 帧错误（对端关闭 / recv 错误 / body_len=0 / 超丢弃上限），连接应终止；
 * kPartial  — 半包未收齐，调用方保留 fd 等下次可读重入续读；
 * kComplete — 一帧完整读出，target 已就绪可消费。
 */
enum class LpReadResult : int8_t
{
    kError = -1,
    kPartial = 0,
    kComplete = 1,
};

/*
 * 长度前缀控制面帧的增量读取器（Server/Client 共用）。
 * wire 为 [4B body_len][body]，body 长度可能大于本地结构体（跨版本）。语义对齐同步路径
 * RecvLengthPrefixed：body 按 min(body_len, target_size) 直接读入 target，不足补零、超出丢弃
 * （丢弃上限 kMaxDiscardBytes）。
 * 非阻塞：半包返回 kPartial（调用方保留 fd 等下次可读续读），出错返回 kError，完整返回 kComplete。
 */
class LengthPrefixedReader {
public:
    LengthPrefixedReader() = default;

    /* 开始一次读取：target/target_size 为本次要填充的结构体缓冲（读取期间须保持有效） */
    void Begin(void *target, size_t target_size)
    {
        target_ = static_cast<uint8_t *>(target);
        target_size_ = target_size;
        stage_ = Stage::kPrefix;
    }

    bool Done() const
    {
        return stage_ == Stage::kDone;
    }

    /* 驱动一次增量读 */
    LpReadResult Read(int fd, AsyncConnBuffer &buf, const char *tag);

private:
    enum class Stage : uint8_t
    {
        kPrefix = 0, /* 读 4B body_len */
        kBody,       /* 读 min(body_len, target_size) */
        kDiscard,    /* 丢弃超量 body 字节 */
        kDone,
    };

    Stage stage_{Stage::kPrefix};
    uint8_t *target_ = nullptr;
    size_t target_size_ = 0;
    uint32_t body_len_ = 0;
    uint32_t read_body_ = 0; /* min(body_len_, target_size_)：实际读入 target_ 的字节数 */
    size_t have_ = 0;        /* target_ 已读入字节数（增量进度） */
    uint32_t extra_ = 0;     /* 超量 body 字节数（需丢弃） */
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONN_LP_READER_H
