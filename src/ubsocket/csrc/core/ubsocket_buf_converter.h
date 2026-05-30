/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_IOV_CONVERTER_H
#define UBS_COMM_IOV_CONVERTER_H

#include <sys/uio.h>
#include <cstdint>

namespace ock {
namespace ubs {
class UbSocketBufConverter;
using ConverterPtr = Ref<UbSocketBufConverter>;

class UbSocketBufConverter {
public:
    virtual ~UbSocketBufConverter() = default;

    virtual uint32_t IndexMove(uint32_t len) = 0;

    virtual bool MemCopy(uint32_t len, uintptr_t buf) = 0;

    virtual void Reset() = 0;

    DEFINE_REF_OPERATION_FUNC;

public:
    DECLARE_REF_COUNT_VARIABLE;
};

class IovConverter : public UbSocketBufConverter {
public:
    // The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
    IovConverter(const struct iovec *iov, int iovcnt) : iov_(iov), iovcnt_(iovcnt) {}

    uint32_t IndexMove(uint32_t len) override
    {
        uint32_t moved_len = 0;
        if (iov_idx_ < iovcnt_) {
            if (iov_offset_ + len >= iov_[iov_idx_].iov_len) {
                while (iov_idx_ < iovcnt_ && iov_[iov_idx_].iov_len == 0) {
                    iov_idx_++;
                }
                if (iov_idx_ >= iovcnt_) {
                    return moved_len;
                }
                moved_len = iov_[iov_idx_].iov_len - iov_offset_;
                iov_offset_ = 0;
                /* Avoid core dump caused by brpc passing in memory with a length of 0,
                     * directly skip IOVs with a length of 0. */
                do {
                    iov_idx_++;
                } while (iov_idx_ < iovcnt_ && iov_[iov_idx_].iov_len == 0);
            } else {
                moved_len = len;
                iov_offset_ += moved_len;
            }
        }

        return moved_len;
    }

    void Reset()
    {
        iov_offset_ = 0;
        iov_idx_ = 0;
    }

protected:
    const struct iovec *iov_ = nullptr;
    int iovcnt_ = 0;
    uint32_t iov_offset_ = 0;
    int iov_idx_ = 0;
};

class BufferConverter : public UbSocketBufConverter {
public:
    BufferConverter(const void *buf, size_t size) : m_buf(static_cast<const char *>(buf)), m_size(size) {}

    uint32_t IndexMove(uint32_t len) override
    {
        if (m_offset + len >= m_size) {
            m_offset = m_size;
            return m_size - m_offset;
        }
        m_offset += len;
        return len;
    }

    void Reset() override
    {
        m_offset = 0;
    }

protected:
    const char *m_buf;
    size_t m_size;
    uint32_t m_offset = 0;
};
} // namespace ubs
} // namespace ock
#endif