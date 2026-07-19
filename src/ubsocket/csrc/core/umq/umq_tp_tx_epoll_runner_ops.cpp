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
#include "common/ubsocket_port_cooldown.h"
#include "umq_backend.h"
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
    TxEpollEvent *tx_epoll_event = reinterpret_cast<TxEpollEvent *>(static_cast<uintptr_t>(event.data.u64));
    if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX_TIMER) {
        uint64_t expirations = 0;
        [[maybe_unused]] ssize_t s = LibcApi::read(tx_epoll_event->timer_fd, &expirations, sizeof(expirations));

        umq_io_option_t poll_option = {
            UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TP_HANDLE_IDX,
            UMQ_IO_TX,
            tx_epoll_event->tp_idx,
        };

        ops_error_code err = ops_error_code::OK;
        UmqTxHelper::PollArgs args(tx_epoll_event->umq_handle, poll_option, err, nullptr);

        // 直到 poll tx 返回错误、或者 poll 空为止
        int poll_cnt = 0;
        do {
            poll_cnt = UmqTxHelper::PollUmqTx(args, [tx_epoll_event](umq_buf_t *qbuf) {
                auto buf_pro = (umq_buf_pro_t *)qbuf->qbuf_ext;
                auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
                auto socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd).Get();
                if (socket_ptr == nullptr) {
                    return;
                }

                // 异步关闭. 等待下次 EPOLLIN 事件时关闭.
                // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
                // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
                LibcApi::shutdown(socket_fd, SHUT_RD);
                UBS_VLOG_DEBUG("closing socket fd=%d\n in TX CQE error", socket_fd);
                socket_ptr->State(SOCK_STAT_CLOSE);

                // 光组网下，如果出现了异常 CQE 2/4/9 则说明底层 URMA 已将所有 port 都给重试了
                auto *umq_sock = static_cast<UmqSocket *>(socket_ptr);
                if (umq_sock->GetTopoType() == UMQ_TOPO_TYPE_CLOS) {
                    if (qbuf->status == UMQ_BUF_LOC_LEN_ERR || qbuf->status == UMQ_BUF_LOC_ACCESS_ERR ||
                        qbuf->status == UMQ_BUF_ACK_TIMEOUT_ERR) {
                        auto [ports, ports_num] = umq_sock->GetUsedPorts();
                        for (std::size_t i = 0; i < ports_num; ++i) {
                            UBS_VLOG_WARN("port is down, new UB connection will not use port(chip=%u,die=%u,port=%u)\n",
                                          ports[i].bs.chip_id, ports[i].bs.die_id, ports[i].bs.port_idx);
                            PortCooldownManager::MarkPortInCooldown(ports[i]);
                        }
                    }
                }
            });
        } while (poll_cnt > 0);

        return UBS_OK;
    } else if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX) {
        Locker slock(mutex_);
        umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TP_HANDLE_IDX,
                                            UMQ_IO_TX, UMQ_FD_IO, tx_epoll_event->tp_idx};
        auto events_cnt = UmqApi::umq_get_cq_event(tx_epoll_event->umq_handle, &tx_option);
        if (UNLIKELY(events_cnt < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, events_cnt, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed for share jfr TX, main umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(tx_epoll_event->umq_handle), events_cnt, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, events_cnt), savedErrno);
            return events_cnt;
        }
        umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TP_HANDLE_IDX, UMQ_IO_TX,
                                       tx_epoll_event->tp_idx};
        ops_error_code err_code = ops_error_code::OK;
        int poll_cnt = 0;
        do {
            UmqTxHelper::PollArgs poll_args(tx_epoll_event->umq_handle, poll_option, err_code, nullptr);
            poll_cnt = UmqTxHelper::PollUmqTx(poll_args, [tx_epoll_event](umq_buf_t *qbuf) {
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
            });
        } while (poll_cnt > 0 && err_code == ops_error_code::OK);
        int ret = UmqApi::umq_rearm_interrupt(tx_epoll_event->umq_handle, false, &tx_option);
        if (ret < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(tx_epoll_event->umq_handle), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
            return UBS_ERROR;
        }
        return UBS_OK;
    } else if (tx_epoll_event->type == RUNNER_EVENT_TYPE_FC_TX) {
        Locker slock(mutex_);
        // 校验 umq_handle 有效，防止 in-flight poll 与 umq_destroy 并发
        // DelEpollEvent -> RemoveSocketEventData 会标记 umq_handle = UMQ_INVALID_HANDLE
        if (tx_epoll_event->umq_handle == UMQ_INVALID_HANDLE) {
            return UBS_OK; // 已被注销，跳过
        }
        return UmqTxHelper::PollUmqTxForFcReturn(tx_epoll_event->umq_handle);
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

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event) < 0) {
        return UBS_ERROR;
    }

    TxEpollEvent *tx_epoll_event = reinterpret_cast<TxEpollEvent *>(static_cast<uintptr_t>(event->data.u64));
    if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX) {
        // 持锁保护 socket_data_：DelEpollEvent 在另一线程持锁 erase，并发 emplace/erase 是 UB
        {
            Locker slock(mutex_);
            InsertSocketEventData(fd, tx_epoll_event);
        }
        UBS_VLOG_DEBUG("[UMQ_API] Add Tx event, event type: %llu\n", static_cast<uint64_t>(tx_epoll_event->type));

        umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TP_HANDLE_IDX,
                                            UMQ_IO_TX, UMQ_FD_IO, tp_tx_ctx->tp_idx};
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
    } else if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX_TIMER) {
        // 定时器轮询 main_umq tx
        UBS_VLOG_DEBUG("Add poll tx timer event.\n");
    } else if (tx_epoll_event->type == RUNNER_EVENT_TYPE_FC_TX) {
        // FC TX 事件注册到 map，使 DelEpollEvent 能找到并标记 umq_handle 无效
        // 持锁保护 socket_data_：DelEpollEvent 在另一线程持锁 erase，并发 emplace/erase 是 UB
        Locker slock(mutex_);
        InsertSocketEventData(fd, tx_epoll_event);
    }
    return UBS_OK;
}

int UmqTpTxEpollRunnerOps::DelEpollEvent(int epoll_fd, int fd)
{
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del pure event for socket: %d failed: %d : %s\n", fd, errno, strerror(errno));
        return UBS_ERROR;
    }
    {
        Locker sLock(mutex_);
        RemoveSocketEventData(fd);
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
