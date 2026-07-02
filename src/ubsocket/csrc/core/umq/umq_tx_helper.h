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
#ifndef UBS_COMM_UMQ_TX_HELPER_H
#define UBS_COMM_UMQ_TX_HELPER_H

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_core_types.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
struct Block;

namespace umq {

class UmqTxHelper {
public:
    UmqTxHelper() = delete;

    // 定义一个极其轻量的纯虚接口，专门用来在内部传递指针
    struct ICallback {
        virtual void invoke(umq_buf_t *qbuf) = 0;
        virtual ~ICallback() = default;
    };

public:
    template <typename F>
    static int PollUmqTx(uint64_t umq_handle, umq_io_option_t &poll_option, ops_error_code &err_code, const F &error_cb,
                         const SocketPtr &sock)
    {
        struct CallbackImpl : public ICallback {
            const F &lambda;
            CallbackImpl(const F &l) : lambda(l) {}
            void invoke(umq_buf_t *qbuf) override
            {
                lambda(qbuf);
            }
        };

        CallbackImpl impl(error_cb);
        return PollUmqTxInternal(umq_handle, poll_option, err_code, impl, sock);
    }

private:
    static int PollUmqTxInternal(uint64_t umq_handle, umq_io_option_t &poll_option, ops_error_code &err_code,
                                 ICallback &error_cb, const SocketPtr &sock);
    static int ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf, const SocketPtr &sock, bool is_first_cqe);
    static void HandleTxCqeError(umq_buf_t *qbuf, int &wr_cnt);
    static bool HandleProbePacket(umq_buf_t *qbuf);
    static void LogTxCqeErrorMsg(umq_buf_t *buf);
    static void ProcessErrorTxCqe(umq_buf_t *first_qbuf);
    static Block *DataToBlock(void *data);
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_TX_HELPER_H
