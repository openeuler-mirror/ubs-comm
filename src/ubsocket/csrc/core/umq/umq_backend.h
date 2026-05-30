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
#ifndef UBS_COMM_UMQ_INIT_H
#define UBS_COMM_UMQ_INIT_H

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_socket_helper.h"
#include "iobuf/ubsocket_zcopy_adapter.h"
#include "umq_qbuf_list.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqBackend {
public:
    static Result Init() noexcept;
    static void UnInit() noexcept;

private:
    static Result AddUbDev(umq_trans_info_t &trans_info);
    static Result FindDevName();

private:
    static std::mutex UMQ_MUTEX;
    static bool UMQ_INITED;
};

class UmqZeroCopyAllocator : public UbsZeroCopyAllocator {
public:
    void *allocate(size_t size) override
    {
        umq_buf_t *buf = UmqApi::umq_buf_alloc(size, BRPC_ALLOC_DEFAULT_BUF_NUM, UMQ_INVALID_HANDLE, nullptr);
        if (buf == nullptr) {
            return nullptr;
        }
        return (void *)(buf->buf_data);
    }

    void deallocate(void *ptr) override
    {
        if (ptr == nullptr)
            return;

        umq_buf_t *buf = UmqApi::umq_data_to_head(ptr);
        if (buf == nullptr) {
            return;
        }

        QBUF_LIST_NEXT(buf) = nullptr;
        UmqApi::umq_buf_free(buf);
    }
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_INIT_H
