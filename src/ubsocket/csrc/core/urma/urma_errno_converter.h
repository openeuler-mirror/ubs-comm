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
#ifndef UBS_COMM_URMA_ERRNO_CONVERTER_H
#define UBS_COMM_URMA_ERRNO_CONVERTER_H

#include <cerrno>

#include "common/ubsocket_defines.h"
#include "under_api/urma/urma_opcode.h"
#include "under_api/urma/urma_types.h"

namespace ock {
namespace ubs {
namespace urma {

enum UrmaOperation : uint8_t {
    URMA_OP_CONNECT = 0,
    URMA_OP_ACCEPT,
    URMA_OP_WRITEV,
    URMA_OP_READV,
    URMA_OP_CREATE_CTX,
    URMA_OP_CREATE_JETTY,
    URMA_OP_IMPORT_JETTY,
    URMA_OP_BIND,
    URMA_OP_COUNT
};

class UrmaErrnoConverter {
public:
    static int Convert(urma_status_t status, UrmaOperation op);
    static int ConvertCrStatus(urma_cr_status_t cr_status, UrmaOperation op);
};

ALWAYS_INLINE int UrmaErrnoConverter::Convert(urma_status_t status, UrmaOperation op)
{
    switch (status) {
        case URMA_SUCCESS:
            return 0;

        case URMA_EAGAIN:
            return EAGAIN;

        case URMA_ENOMEM:
            return ENOMEM;

        case URMA_EINVAL:
            return EINVAL;

        default:
            break;
    }

    switch (op) {
        case URMA_OP_CONNECT:
        case URMA_OP_ACCEPT:
            return ECONNREFUSED;

        case URMA_OP_WRITEV:
        case URMA_OP_READV:
            return EIO;

        case URMA_OP_CREATE_CTX:
        case URMA_OP_CREATE_JETTY:
            return ENODEV;

        case URMA_OP_IMPORT_JETTY:
        case URMA_OP_BIND:
            return ECONNREFUSED;

        default:
            return EIO;
    }
}

ALWAYS_INLINE int UrmaErrnoConverter::ConvertCrStatus(urma_cr_status_t cr_status, UrmaOperation op)
{
    if (cr_status == URMA_CR_SUCCESS) {
        return 0;
    }

    switch (cr_status) {
        case URMA_CR_RNR_RETRY_CNT_EXC_ERR:
            return EAGAIN;

        case URMA_CR_ACK_TIMEOUT_ERR:
            return ETIMEDOUT;

        case URMA_CR_LOC_ACCESS_ERR:
        case URMA_CR_REM_ACCESS_ABORT_ERR:
            return EACCES;

        case URMA_CR_LOC_LEN_ERR:
        case URMA_CR_REM_RESP_LEN_ERR:
            return EMSGSIZE;

        case URMA_CR_LOC_OPERATION_ERR:
        case URMA_CR_REM_OPERATION_ERR:
            return EIO;

        case URMA_CR_WR_FLUSH_ERR:
        case URMA_CR_WR_FLUSH_ERR_DONE:
            return ENETRESET;

        case URMA_CR_LOC_DATA_POISON:
        case URMA_CR_REM_DATA_POISON:
            return EIO;

        default:
            break;
    }

    return EIO;
}

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_ERRNO_CONVERTER_H
