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
#include "iobuf/ubsocket_iobuf.h"
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
    static std::vector<umq_port_id_t> GetUsedPorts() {
        return used_ports_;
    }
    static umq_topo_type_t GetTopoType() {
        return topo_type_;
    }

private:
    static Result AddUbDev(umq_trans_info_t &trans_info);
    static Result FindDevName();
    static Result FindDevEid(const char *dev, uint32_t eid_idx);
    static uint64_t CreateShareMainUmq(umq_eid_t &local_eid);
    static Result PrefillShareMainUmq(umq_eid_t &local_eid);
    static Result InitShareJfrMonitering(uint64_t main_umq_handle);
    static void DestroyShareMainUmq();
    static void UmqCleanup() noexcept;

private:
    static std::mutex UMQ_MUTEX;
    static bool UMQ_INITED;
    static std::vector<umq_port_id_t> used_ports_;
    static umq_topo_type_t topo_type_;
};

class UmqZeroCopyAllocator : public UbsZeroCopyAllocator {
public:
    void *allocate(size_t size, const ubs_iobuf_alloc_option_t *option) override
    {
        umq_alloc_option_t umq_option = {};
        umq_alloc_option_t *umq_option_ptr = nullptr;
        if (option != nullptr && (option->flag & UBS_IOBUF_ALLOC_FLAG_POOL_TYPE) != 0) {
            umq_option.flag = UMQ_ALLOC_FLAG_POOL_TYPE;
            switch (option->pool_type) {
                case UBS_IOBUF_POOL_TINY:
                    umq_option.pool_type = UMQ_ALLOC_POOL_TINY;
                    break;
                case UBS_IOBUF_POOL_NORMAL:
                    umq_option.pool_type = UMQ_ALLOC_POOL_NORMAL;
                    break;
                case UBS_IOBUF_POOL_ESCAPE:
                    umq_option.pool_type = UMQ_ALLOC_POOL_ESCAPE;
                    break;
                default:
                    return nullptr;
            }
            umq_option_ptr = &umq_option;
        }

        umq_alloc_option_t adjusted_option = {};
        umq_alloc_option_t *adjusted_option_ptr = umq_option_ptr;
        if (umq_option_ptr != nullptr) {
            adjusted_option = *umq_option_ptr;
        }
        adjusted_option.flag |= UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE;
        adjusted_option.headroom_size = 0;
        adjusted_option_ptr = &adjusted_option;

        umq_buf_t *buf =
            UmqApi::umq_buf_alloc(size, BRPC_ALLOC_DEFAULT_BUF_NUM, UMQ_INVALID_HANDLE, adjusted_option_ptr);
        if (buf == nullptr) {
            return nullptr;
        }
        return (void *)(buf->buf_data);
    }

    void deallocate(void *ptr) override
    {
        if (ptr == nullptr) {
            return;
        }
        /* ptr is a brpc IOBuf::Block*. Two creation paths exist:
         *   Path 1 (TX allocate): Block is placement-new'd at buf_data, so
         *     Block.data = buf_data + sizeof(Block). Here ptr == buf_data.
         *   Path 2 (RX zero-copy AdoptUbsPollSegment): Block is placement-new'd
         *     at buf_data - sizeof(Block), so Block.data = buf_data. Here
         *     ptr == buf_data - sizeof(Block).
         * We must recover the umq_buf_t in both cases. Reading block->data and
         * querying umq_data_to_head(data_ptr) covers both paths:
         *   Path 1: data_ptr = buf_data + 32, falls in the same 4K block as
         *     buf_data → umq_data_to_head finds the qbuf (block-boundary check).
         *   Path 2: data_ptr = buf_data → direct hit.
         * umq_data_to_head is now SEGV-safe (id guard + in_main_pool filter +
         * block-boundary validation), so this read is safe even for non-pool
         * pointers (returns NULL instead of crashing). */
        Block *block = reinterpret_cast<Block *>(ptr);
        char *data_ptr = block->data;
        if (data_ptr == nullptr) {
            umq_buf_t *buf = UmqApi::umq_data_to_head(ptr);
            if (buf != nullptr && buf->buf_data == ptr) {
                QBUF_LIST_NEXT(buf) = nullptr;
                UmqApi::umq_buf_free(buf);
            }
            return;
        }
        umq_buf_t *buf = UmqApi::umq_data_to_head(data_ptr);
        if (buf == nullptr) {
            return;
        }
        if (buf->buf_data != data_ptr) {
            buf = UmqApi::umq_data_to_head(buf->buf_data);
            if (buf == nullptr) {
                return;
            }
        }
        QBUF_LIST_NEXT(buf) = nullptr;
        UmqApi::umq_buf_free(buf);
    }
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_INIT_H
