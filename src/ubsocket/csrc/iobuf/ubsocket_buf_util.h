/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for buffer, iov, etc
 * Author:
 * Create: 2025-08-12
 * Note:
 * History: 2025-08-12
*/

#ifndef UBS_COMM_IOV_CONVERTER_H
#define UBS_COMM_IOV_CONVERTER_H

#include <cstdint>
#include <sys/uio.h>

class IovConverter {
public:
    // The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
    IovConverter(const struct iovec *iov, int iovcnt) : iov_(iov), iovcnt_(iovcnt) {}

    uint32_t Cut(uint32_t len)
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

public:
    const struct iovec *iov_ = nullptr;
    int iovcnt_ = 0;
    uint32_t iov_offset_ = 0;
    int iov_idx_ = 0;
};

#endif