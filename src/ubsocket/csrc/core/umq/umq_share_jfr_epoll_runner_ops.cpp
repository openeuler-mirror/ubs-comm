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

#include "umq_share_jfr_epoll_runner_ops.h"
#include "common/ubsocket_common_includes.h"
#include "umq_backend.h"
#include "umq_data_rx_ops.h"
#include "umq_data_tx_ops.h"
#include "umq_errno.h"
#include "umq_errno_converter.h"
#include "umq_pro_types.h"
#include "umq_setting.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessOneEvent(const struct epoll_event &event)
{
    uint64_t main_umq = 0;
    Socket *socket_object = nullptr;
    RunnerEventData event_data{};

    event_data.u64 = event.data.u64;
    if (event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR) {
        Locker slock(mutex_);
        auto pos = jfr_main_umq_.find(static_cast<int>(event_data.event_data.data));
        if (pos != jfr_main_umq_.end()) {
            main_umq = pos->second;
        }
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_SUB_UMQ_RX) {
        socket_object = reinterpret_cast<Socket *>(static_cast<ptrdiff_t>(event_data.event_data.data));
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
                     event_data.event_data.type);
    }

    if (main_umq != 0) {
        return ProcessShareJfrEvent(event, main_umq);
    }

    if (socket_object == nullptr) {
        return 0;
    }

    umq_buf_t *buf[POLL_BATCH_MAX];
    auto umqSock = dynamic_cast<UmqSocket *>(socket_object);
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int pollNum = UmqApi::umq_poll(umqSock->UmqHandle(), &poll_option, buf, POLL_BATCH_MAX);
    if (UNLIKELY(pollNum <= 0)) {
        if (UNLIKELY(pollNum < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for sub umq RX, local umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(umqSock->UmqHandle()), pollNum, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
        }
        if (umqSock->GetRx()->GetRxOps()->RearmRxInterrupt() < 0) {
            UBS_VLOG_ERR("Rearm sub umq failed, socket fd:%d\n", socket_object->raw_socket_);
        }
        return -1;
    }
    HandleSubUmqPollBuffers(socket_object, buf, pollNum);

    return 0;
}

void UmqShareJfrEpollRunnerOps::HandleSubUmqPollBuffers(Socket *socketObject, umq_buf_t **buf, int pollNum)
{
    auto umqSock = dynamic_cast<UmqSocket *>(socketObject);
    for (int i = 0; i < pollNum; ++i) {
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                auto rxOps = dynamic_cast<UmqRxOps *>(umqSock->GetRx()->GetRxOps());
                rxOps->HandleErrorRxCqe(buf[i]);
            } else {
                auto txOps = dynamic_cast<UmqTxOps *>(umqSock->GetTx()->GetTxOps());
                txOps->WakeUpTx(socketObject);
            }
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
        }
    }
}

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq)
{
    traceTime_.umq_rearm_start_timestamp_ = ubsocket_get_timeNs_compile();
    if (UNLIKELY(ProcessMainUmqRearm(main_umq) < 0)) {
        return -1;
    }
    traceTime_.umq_rearm_end_timestamp_ = ubsocket_get_timeNs_compile();

    static thread_local std::unique_ptr<FlashDynamicBitSet> event_reach_sockets;
    static thread_local std::unique_ptr<FlashDynamicBitSet> event_reach_epoll_fds;
    if (UNLIKELY(event_reach_sockets.get() == nullptr)) {
        event_reach_sockets.reset(new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        if (UNLIKELY(event_reach_sockets.get() == nullptr)) {
            UBS_VLOG_ERR("allocate memory for FlashDynamicBitSet failed.\n");
            return -1;
        }
    }
    if (UNLIKELY(event_reach_epoll_fds.get() == nullptr)) {
        event_reach_epoll_fds.reset(new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        if (UNLIKELY(event_reach_epoll_fds.get() == nullptr)) {
            UBS_VLOG_ERR("allocate memory for FlashDynamicBitSet failed.\n");
            return -1;
        }
    }

    umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
    traceTime_.umq_poll_start_timestamp_ = ubsocket_get_timeNs_compile();
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    auto pollNum = UmqApi::umq_poll(main_umq, &poll_option, buf, MAX_EPOLL_WAIT_COUNT);
    traceTime_.umq_poll_end_timestamp_ = ubsocket_get_timeNs_compile();
    if (UNLIKELY(pollNum < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for share jfr RX, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), pollNum, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
        return -1;
    }
    if (UNLIKELY(pollNum == 0)) {
        return -1;
    }
    // 计算时，排除流控的buffer
    int fcBufCnt = 0;
    for (int i = 0; i < pollNum; ++i) {
        if (buf[i]->status >= UMQ_FAKE_BUF_FC_UPDATE) {
            ++fcBufCnt;
        }
    }

    int ioPollNum = pollNum - fcBufCnt;
    if (ioPollNum != 0) {
        umq_alloc_option_t alloc_option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(ock::ubs::Block)};
        traceTime_.umq_alloc_start_timestamp_ = ubsocket_get_timeNs_compile();
        umq_buf_t *rx_buf_list =
            UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), ioPollNum, UMQ_INVALID_HANDLE, &alloc_option);
        traceTime_.umq_alloc_end_timestamp_ = ubsocket_get_timeNs_compile();
        if (LIKELY(rx_buf_list != nullptr)) {
            umq_buf_t *bad_qbuf = nullptr;
            traceTime_.umq_post_start_timestamp_ = ubsocket_get_timeNs_compile();
            umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                            UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
            if (UmqApi::umq_post(main_umq, rx_buf_list, &io_rx_option, &bad_qbuf) != UMQ_SUCCESS) {
                int savedErrno = errno;
                errno = UmqErrnoConverter::Convert(UmqOperation::READV, UMQ_FAIL, savedErrno);
                UBS_VLOG_ERR("[UMQ_API] umq_post() failed for share jfr RX refill, main umq: %llu, "
                             "mapped errno: %d(%s), original errno: %d\n",
                             static_cast<unsigned long long>(main_umq), errno,
                             UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, UMQ_FAIL), savedErrno);
                UmqApi::umq_buf_free(bad_qbuf);
            }
            traceTime_.umq_post_end_timestamp_ = ubsocket_get_timeNs_compile();
        }
    }

    event_reach_sockets->ClearAll();
    event_reach_epoll_fds->ClearAll();

    epoll_data_t event_data{};
    std::vector<SocketPtr> socket_ptrs;
    std::vector<AsyncEventPoll *> readable_epoll_fds;
    SiftSocketEventsWithUmqBuffers(buf, pollNum, *event_reach_sockets, socket_ptrs);
    for (auto &obj : socket_ptrs) {
        auto socket_obj = obj.Get();
        ((UmqSocket *)socket_obj)->NewRxEpollIn();
        auto epoll_fd_obj = (AsyncEventPoll *)(((SocketBase *)socket_obj)->GetAddedEpollFd(event_data));
        if (LIKELY(epoll_fd_obj != nullptr)) {
            epoll_fd_obj->AddReadableEvent(event_data);
            if (!event_reach_epoll_fds->Test(epoll_fd_obj->GetEpollFd())) {
                readable_epoll_fds.emplace_back(epoll_fd_obj);
                event_reach_epoll_fds->Set(epoll_fd_obj->GetEpollFd());
            }
        }
    }

    for (auto epoll_fd : readable_epoll_fds) {
        epoll_fd->SetReadableEventFd();
    }
    traceTime_.process_share_jfr_end_timestamp_ = ubsocket_get_timeNs_compile();
    return 0;
}

void UmqShareJfrEpollRunnerOps::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count,
                                                               FlashDynamicBitSet &socket_fds,
                                                               std::vector<SocketPtr> &socket_ptrs)
{
    SocketPtr socket_ptr{nullptr};
    int last_socket_fd = -1;
    for (int i = 0; i < count; ++i) {
        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        if (UNLIKELY(buf[i]->status == UMQ_FAKE_BUF_FC_ERR)) {
            UBS_VLOG_ERR("async_epoll Unreachable flow control.\n");
        }

        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        if (socket_fd != last_socket_fd) {
            socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
            last_socket_fd = socket_fd;
        }
        if (UNLIKELY(socket_ptr.Get() == nullptr)) {
            UBS_VLOG_WARN("async_epoll Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        auto *trace = socket_ptr->split_trace_;

        if (i == 0) {
            uint32_t last_seq = 0;
            if (buf_pro->imm.user_data > 0) {
                last_seq = buf_pro->imm.user_data - 1;
            }
            if (trace != nullptr) {
                TRACE_ADD_EPOLL_FULL(trace, CORE_PROCESS_JRF_END, socket_ptr->raw_socket_, last_seq, buf[i]->data_size,
                                     count, traceTime_.process_share_jfr_end_timestamp_, 0);
                TRACE_ADD_EPOLL_FULL(trace, CORE_EPOLL_REARM, socket_ptr->raw_socket_, buf_pro->imm.user_data,
                                     buf[i]->data_size, count, traceTime_.umq_rearm_start_timestamp_,
                                     traceTime_.umq_rearm_end_timestamp_);
                TRACE_ADD_EPOLL_FULL(trace, CORE_EPOLL_POST_RX, socket_ptr->raw_socket_, buf_pro->imm.user_data,
                                     buf[i]->data_size, count, traceTime_.umq_post_start_timestamp_,
                                     traceTime_.umq_post_end_timestamp_);
            }
        }
        // due to the flowcontrl buf message, not simple to record first and last
        TRACE_ADD_EPOLL_DETAIL(trace, CORE_EPOLL_ENQUEUE, socket_ptr->raw_socket_, buf_pro->imm.user_data,
                               buf[i]->data_size, 0);
        TRACE_TRY_SWAP_EPOLL(trace);

        if (UNLIKELY((((UmqSocket *)socket_ptr.Get())->AddQbuf(buf[i]) != 0))) {
            UBS_VLOG_ERR("async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
            continue;
        }

        if (!socket_fds.Test(socket_fd)) {
            socket_fds.Set(socket_fd);
            socket_ptrs.emplace_back(socket_ptr);
        }
    }
}

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessMainUmqRearm(uint64_t main_umq)
{
    umq_interrupt_option_t option = {
        .flag = UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TAG_TIMESTAMP,
        .direction = UMQ_IO_RX,
        .fd_type = UMQ_FD_IO,
        .tag_timestamp = traceTime_.umq_rearm_start_timestamp_,
    };
    auto events_cnt = UmqApi::umq_get_cq_event(main_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, events_cnt, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed for share jfr RX, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), events_cnt, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, events_cnt), savedErrno);
        return events_cnt;
    }

    if (LIKELY(events_cnt > 0)) {
        int rearmRet = UmqApi::umq_rearm_interrupt(main_umq, false, &option);
        if (rearmRet < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, rearmRet, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for share jfr RX rearm, "
                         "main umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(main_umq), rearmRet, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, rearmRet), savedErrno);
        }
        event_num_ += events_cnt;
        if (event_num_ >= GET_PER_ACK) {
            UmqApi::umq_ack_interrupt(main_umq, event_num_, &option);
            event_num_ = 0;
        }
    }

    return events_cnt;
}

int UmqShareJfrEpollRunnerOps::AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx)
{
    if (ctx == nullptr) {
        UBS_VLOG_ERR("Unsupported operation. Check context because context is null.\n");
        return UBS_ERROR;
    }
    if (InsertJfrMainUmq(fd, ctx->umq_handle, epoll_fd, event) < 0) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }

    umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    int ret = UmqApi::umq_rearm_interrupt(ctx->umq_handle, false, &rx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for share jfr RX, "
                     "main umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(ctx->umq_handle), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock