/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-08-12
 *Note:
 *History: 2025-08-12
*/

#include <unistd.h>

#include "brpc_iobuf_adapter.h"
#include "qbuf_list.h"
#include "umq_api.h"

#define BRPC_ALLOC_DEFAULT_BUF_NUM  (1)

namespace Brpc {
namespace IOBuf {

void *blockmem_allocate_zero_copy(size_t size)
{
    umq_buf_t *buf = umq_buf_alloc(size, BRPC_ALLOC_DEFAULT_BUF_NUM, UMQ_INVALID_HANDLE, nullptr);
    if (buf == nullptr) {
        return nullptr;
    }

    return (void *)(buf->buf_data);
} 

void blockmem_deallocate_zero_copy(void *addr)
{
    umq_buf_t *buf = umq_data_to_head(addr);
    if (buf == nullptr) {
        return;
    }

    QBUF_LIST_NEXT(buf) = nullptr;
    umq_buf_free(buf);
}

}    
}