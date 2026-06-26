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

#include "umq_tp_tx_epoll_runner_ops.h"
#include "umq_errno_converter.h"
#include "umq_transport_pool.h"
#include "umq_tx_helper.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqTpTxEpollRunnerOps::ProcessOneEvent(const struct epoll_event &event)
{
    uint64_t main_umq = 0;
    TxEpollEvent *tx_epoll_event = reinterpret_cast<TxEpollEvent *>(static_cast<uintptr_t>(event.data.u64));
    if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX) {
        Locker slock(mutex_);
        umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TP_HANDLE_IDX, UMQ_IO_TX,
                                       tx_epoll_event->tp_idx};
        ops_error_code err_code = ops_error_code::OK;
        int poll_cnt = 0;
        do {
            poll_cnt = UmqTxHelper::PollUmqTx(
                tx_epoll_event->umq_handle, poll_option, err_code,
                [tx_epoll_event](umq_buf_t *qbuf) {
                    // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
                    // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
                    // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
                    auto buf_pro = (umq_buf_pro_t *)qbuf->qbuf_ext;
                    auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
                    auto socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd).Get();
                    if (socket_ptr) {
                        LibcApi::shutdown(socket_fd, SHUT_RD);
                        UBS_VLOG_DEBUG("closing socket fd=%d\n in TX CQE error", socket_fd);
                        socket_ptr->State(SOCK_STAT_CLOSE);
                    }

                    // 销毁重建对应的Tp
                    UmqTransportPool::Instance().RebuildTp(tx_epoll_event->umq_handle, tx_epoll_event->tp_idx);
                },
                SocketPtr());
        } while (poll_cnt > 0 && err_code == ops_error_code::OK);

        umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TP_HANDLE_IDX,
                                            UMQ_IO_TX, UMQ_FD_IO, tx_epoll_event->tp_idx};
        int ret = UmqApi::umq_rearm_interrupt(tx_epoll_event->umq_handle, false, &tx_option);
        if (ret < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(tx_epoll_event->umq_handle), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            return UBS_ERROR;
        }
        return UBS_OK;
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events, tx_epoll_event->type);
        return UBS_ERROR;
    }
}

int UmqTpTxEpollRunnerOps::AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx)
{
    TpTxExtContext *tp_tx_ctx = dynamic_cast<TpTxExtContext *>(ctx);
    if (tp_tx_ctx == nullptr) {
        UBS_VLOG_ERR("Unsupported operation. Check context because context is null.\n");
        return UBS_ERROR;
    }
    Locker sLock(mutex_);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event) < 0) {
        return UBS_ERROR;
    }

    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TP_HANDLE_IDX, UMQ_IO_TX,
                                        UMQ_FD_IO, tp_tx_ctx->tp_idx};
    // Jetty池化场景，开启TX中断
    int ret = UmqApi::umq_rearm_interrupt(ctx->umq_handle, false, &tx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(ctx->umq_handle), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock