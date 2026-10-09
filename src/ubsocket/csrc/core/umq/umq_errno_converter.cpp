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
#include "umq_errno_converter.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqErrnoConverter::Convert(UmqOperation op, int umqRet, int savedErrno)
{
    if (umqRet == UMQ_SUCCESS) {
        return 0;
    }

    if (op == UmqOperation::GET_STATE) {
        if (umqRet == QUEUE_STATE_ERR || umqRet == QUEUE_STATE_MAX) {
            return EIO;
        }
        return 0;
    }

    int absUmqRet = umqRet < 0 ? -umqRet : umqRet;

    if (savedErrno > 0 && ShouldOverrideWithSavedErrno(absUmqRet, savedErrno)) {
        return savedErrno;
    }

    return FindErrno(kCommonErrnoMappings, absUmqRet, savedErrno);
}

int UmqErrnoConverter::ConvertBufStatus(UmqOperation op, umq_buf_status_t bufStatus, int savedErrno)
{
    switch (op) {
        case UmqOperation::CONNECT:
        case UmqOperation::ACCEPT:
            if (bufStatus == UMQ_BUF_SUCCESS) {
                return 0;
            }
            return FindBufStatusErrno(kCommonConnectAcceptBufStatusMappings, bufStatus, savedErrno);
        case UmqOperation::WRITEV:
            if (bufStatus == UMQ_BUF_SUCCESS) {
                return 0;
            }
            return FindBufStatusErrno(kWritevBufStatusMappings, bufStatus, savedErrno);
        case UmqOperation::READV:
            if (bufStatus == UMQ_BUF_SUCCESS) {
                return 0;
            }
            return FindBufStatusErrno(kReadvBufStatusMappings, bufStatus, savedErrno);
        default:
            if (savedErrno > 0) {
                return savedErrno;
            }
            return EIO;
    }
}

const char *UmqErrnoConverter::GetErrorDescription(UmqOperation op, int umqRet)
{
    if (umqRet == UMQ_SUCCESS) {
        return "Success";
    }

    if (op == UmqOperation::GET_STATE) {
        switch (umqRet) {
            case QUEUE_STATE_ERR:
                return "Queue error state";
            case QUEUE_STATE_MAX:
                return "Invalid queue handle or state";
            default:
                return "Unexpected queue state";
        }
    }

    int absUmqRet = umqRet < 0 ? -umqRet : umqRet;

    return FindDescription(kCommonErrnoMappings, absUmqRet);
}

const char *UmqErrnoConverter::GetBufStatusDescription(UmqOperation op, umq_buf_status_t bufStatus)
{
    if (bufStatus == UMQ_BUF_SUCCESS) {
        return "Buffer operation success";
    }

    switch (op) {
        case UmqOperation::CONNECT:
        case UmqOperation::ACCEPT:
            return FindBufStatusDescription(kCommonConnectAcceptBufStatusMappings, bufStatus);
        case UmqOperation::WRITEV:
            return FindBufStatusDescription(kWritevBufStatusMappings, bufStatus);
        case UmqOperation::READV:
            return FindBufStatusDescription(kReadvBufStatusMappings, bufStatus);
        default:
            return "Unknown operation";
    }
}

bool UmqErrnoConverter::ShouldOverrideWithSavedErrno(int absUmqRet, int savedErrno)
{
    if (absUmqRet == UMQ_ERR_EPERM) {
        return savedErrno == EINVAL || savedErrno == ENODEV || savedErrno == ENOMEM || savedErrno == ENOEXEC ||
               savedErrno == EIO;
    }
    if (absUmqRet == UMQ_ERR_ENODEV) {
        return savedErrno == EINVAL || savedErrno == EIO;
    }
    return false;
}

int UmqErrnoConverter::ConvertHandleResult(UmqOperation op, int savedErrno)
{
    switch (op) {
        case UmqOperation::CREATE:
            if (savedErrno == EINVAL || savedErrno == EPERM) {
                return savedErrno;
            }
            return EIO;
        case UmqOperation::BIND_INFO_GET:
            if (savedErrno == ENOMEM || savedErrno == EINVAL) {
                return savedErrno;
            }
            return EIO;
        default:
            return EIO;
    }
}

} // namespace umq
} // namespace ubs
} // namespace ock
