/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-08-12
 *Note:
 *History: 2025-08-12
*/

#ifndef BUFFER_UTIL_H
#define BUFFER_UTIL_H

#include <sys/uio.h>
#include "umq_types.h"

class IovConverter{
public:
    // The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
    IovConverter(const struct iovec *iov, int iovcnt) : m_iov(iov), m_iovcnt(iovcnt) {}

    uint32_t Cut(uint32_t len)
    {
        uint32_t moved_len = 0;
        if (m_iov_idx < m_iovcnt){
            if(m_iov_offset + len >= m_iov[m_iov_idx].iov_len){
                while (m_iov_idx < m_iovcnt && m_iov[m_iov_idx].iov_len == 0) {
                    m_iov_idx++;
                }
                if (m_iov_idx >= m_iovcnt) {
                    return moved_len;
                }
                moved_len = m_iov[m_iov_idx].iov_len - m_iov_offset;
                m_iov_offset = 0;
                /* Avoid core dump caused by brpc passing in memory with a length of 0,
                 * directly skip IOVs with a length of 0. */
                do {
                    m_iov_idx++;
                } while (m_iov_idx < m_iovcnt && m_iov[m_iov_idx].iov_len == 0);
            }else{
                moved_len = len;
                m_iov_offset += moved_len;
            }
        }

        return moved_len;
    }

    bool CutLast(uint32_t len, umq_buf_t *buf)
    { 
        uint32_t moved_len = 0;
        if(m_iov_idx<m_iovcnt){
            if(m_iov_offset + len >=m_iov[m_iov_idx].iov_len){
                while (m_iov_idx < m_iovcnt && m_iov[m_iov_idx].iov_len == 0) {
                    m_iov_idx++;
                }
                if (m_iov_idx >= m_iovcnt) {
                    return moved_len;
                }
                moved_len = m_iov[m_iov_idx].iov_len - m_iov_offset;
                buf->buf_data = (char *)m_iov[m_iov_idx].iov_base + m_iov_offset;
                buf->data_size = moved_len;

                m_iov_offset = 0;
                /* Avoid core dump caused by brpc passing in memory with a length of 0,
                 * directly skip IOVs with a length of 0. */
                do {
                    m_iov_idx++;
                } while (m_iov_idx < m_iovcnt && m_iov[m_iov_idx].iov_len == 0);
            }else{
                moved_len = len;
                buf->buf_data = (char *)m_iov[m_iov_idx].iov_base + m_iov_offset;
                buf->data_size = moved_len;

                m_iov_offset += moved_len;
            }
        }

        return m_iov_idx < m_iovcnt ? false : true;
    }

    void Reset()
    {
       m_iov_offset = 0;
       m_iov_idx = 0;
    }

private:
    const struct iovec *m_iov;
    int m_iovcnt;
    uint32_t m_iov_offset = 0;
    int m_iov_idx = 0;
};

#endif