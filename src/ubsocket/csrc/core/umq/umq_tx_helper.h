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
#include "umq_errno_converter.h"
#include "umq_setting.h"
#include "umq_tp_wait_queue.h"
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

    struct PollArgs {
        uint64_t umq_handle;
        umq_io_option_t &poll_option;
        ops_error_code &err_code;
        Socket *sock = nullptr;
        bool silent_poll_err = false;

        PollArgs(uint64_t handle, umq_io_option_t &opt, ops_error_code &err, Socket *s = nullptr)
            : umq_handle(handle),
              poll_option(opt),
              err_code(err),
              sock(s)
        {
        }
    };

public:
    template <typename F>
    static int PollUmqTx(PollArgs &poll_args, const F &error_cb)
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
        return PollUmqTxInternal(poll_args, impl);
    }

    static int PollUmqTxForFcReturn(uint64_t umq_handle);

private:
    static int PollUmqTxInternal(PollArgs &poll_args, ICallback &error_cb);
    static int ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf, Socket *sock, bool is_first_cqe);
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
