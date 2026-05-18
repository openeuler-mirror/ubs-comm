//
// Created by l30030098 on 2026/5/15.
//

#ifndef HCOM_UMQ_BUF_CONVERTER_H
#define HCOM_UMQ_BUF_CONVERTER_H

#include <securec.h>

namespace ock {
namespace ubs {
class UmqIovConverter : public IovConverter {
public:
    UmqIovConverter(const iovec *iov, int iovcnt) : IovConverter(iov, iovcnt) {}
    bool MemCopy(uint32_t len, uintptr_t buf) override
    {
        umq_buf_t *umq_buf = reinterpret_cast<umq_buf_t *>(buf);
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
                umq_buf->buf_data = (char *)iov_[iov_idx_].iov_base + iov_offset_;
                umq_buf->data_size = moved_len;

                iov_offset_ = 0;
                /* Avoid core dump caused by brpc passing in memory with a length of 0,
                 * directly skip IOVs with a length of 0. */
                do {
                    iov_idx_++;
                } while (iov_idx_ < iovcnt_ && iov_[iov_idx_].iov_len == 0);
            } else {
                moved_len = len;
                umq_buf->buf_data = (char *)iov_[iov_idx_].iov_base + iov_offset_;
                umq_buf->data_size = moved_len;

                iov_offset_ += moved_len;
            }
        }
        return iov_idx_ >= iovcnt_;
    }
};

class UmqBufferConverter : public BufferConverter {
public:
    UmqBufferConverter(const void *buf, size_t size) : BufferConverter(buf, size) {}
    bool MemCopy(uint32_t len, uintptr_t buf) override
    {
        umq_buf_t *umq_buf = reinterpret_cast<umq_buf_t *>(buf);
        uint32_t buf_len = len;
        if (m_offset + len >= m_size) {
            buf_len = m_size - m_offset;
        }
        memcpy_s(umq_buf->buf_data, buf_len, m_buf + m_offset, buf_len);
        m_offset += buf_len;
        return m_offset >= m_size;
    }
};
}
}
#endif //HCOM_UMQ_BUF_CONVERTER_H
