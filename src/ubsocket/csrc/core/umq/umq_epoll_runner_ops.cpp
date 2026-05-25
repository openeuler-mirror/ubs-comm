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

#include "core/ubsocket_socket_set.h"
#include "umq_backend.h"
#include "umq_data_rx_ops.h"
#include "umq_errno_converter.h"
#include "umq_socket.h"
#include "umq_setting.h"
#include "umq_errno.h"
#include "umq_pro_types.h"
#include "umq_epoll_runner_ops.h"

namespace ock {
namespace ubs {
namespace umq {

ALWAYS_INLINE int UmqEpollRunnerOps::ProcessOneEvent(const struct epoll_event &event)
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
    int pollNum = UmqApi::umq_poll(umqSock->UmqHandle(), UMQ_IO_RX, buf, POLL_BATCH_MAX);
    if (UNLIKELY(pollNum <= 0)) {
        if (UNLIKELY(pollNum < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
            UBS_VLOG_ERR("umq_poll() failed for sub umq RX, local umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(umqSock->UmqHandle()),
                         pollNum, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum),
                         savedErrno);
        }
        if (umqSock->GetRx()->GetRxOps()->RearmRxInterrupt() < 0) {
            UBS_VLOG_ERR("Rearm sub umq failed, socket fd:%d\n", socket_object->raw_socket_);
        }
        return -1;
    }
    HandleSubUmqPollBuffers(socket_object, buf, pollNum);

    return 0;
}

void UmqEpollRunnerOps::HandleSubUmqPollBuffers(Socket *socketObject, umq_buf_t **buf, int pollNum)
{
    auto umqSock = dynamic_cast<UmqSocket *>(socketObject);
    for (int i = 0; i < pollNum; ++i) {
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                auto rxOps = dynamic_cast<UmqRxOps *>(umqSock->GetRx()->GetRxOps());
                rxOps->HandleErrorRxCqe(buf[i]);
            }
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
        }
    }
}

ALWAYS_INLINE int UmqEpollRunnerOps::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq)
{
    if (UNLIKELY(ProcessMainUmqRearm(main_umq)) < 0) {
        return -1;
    }

    umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
    auto pollNum = UmqApi::umq_poll(main_umq, UMQ_IO_RX, buf, MAX_EPOLL_WAIT_COUNT);
    if (UNLIKELY(pollNum < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
        UBS_VLOG_ERR("umq_poll() failed for share jfr RX, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), pollNum, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
        return -1;
    }
    if (UNLIKELY(pollNum == 0)) {
        return -1;
    }

    umq_alloc_option_t alloc_option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(ock::ubs::Block)};
    umq_buf_t *rx_buf_list =
        UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), pollNum, UMQ_INVALID_HANDLE, &alloc_option);
    if (LIKELY(rx_buf_list != nullptr)) {
        umq_buf_t *bad_qbuf = nullptr;
        if (UmqApi::umq_post(main_umq, rx_buf_list, UMQ_IO_RX, &bad_qbuf) != UMQ_SUCCESS) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, UMQ_FAIL, savedErrno);
            UBS_VLOG_ERR("umq_post() failed for share jfr RX refill, main umq: %llu, "
                         "mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(main_umq), errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, UMQ_FAIL), savedErrno);
            UmqApi::umq_buf_free(bad_qbuf);
        }
    }

    std::set<AsyncEventPoll *> readable_epoll_fds;
    epoll_data_t event_data{};
    auto event_reach_sockets = SiftSocketEventsWithUmqBuffers(buf, pollNum);
    for (auto obj : event_reach_sockets) {
        auto socket_obj = (Socket *)obj;
        ((UmqSocket *)socket_obj)->NewRxEpollIn();
        auto epoll_fd_obj = (AsyncEventPoll *)(((SocketBase *)socket_obj)->GetAddedEpollFd(event_data));
        if (UNLIKELY(epoll_fd_obj != nullptr)) {
            epoll_fd_obj->AddReadableEvent(event_data);
            readable_epoll_fds.emplace(epoll_fd_obj);
        }
    }

    for (auto epoll_fd : readable_epoll_fds) {
        epoll_fd->SetReadableEventFd();
    }

    return 0;
}

ALWAYS_INLINE std::unordered_set<Socket *>
UmqEpollRunnerOps::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count)
{
    std::unordered_set<Socket *> event_reach_sockets;
    for (int i = 0; i < count; ++i) {
        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        if (UNLIKELY(buf[i]->status == UMQ_FAKE_BUF_FC_ERR)) {
            UBS_VLOG_ERR("async_epoll Unreachable flow control.\n");
        }

        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        auto socket_ptr = SocketSet::Instance().GetSocket(socket_fd).Get();
        if (UNLIKELY(socket_ptr == nullptr)) {
            UBS_VLOG_WARN("async_epoll Get socket fd:%d object failed. \n", socket_fd);
            continue;
        }

        if (UNLIKELY((((UmqSocket *)socket_ptr)->AddQbuf(buf[i]) != 0))) {
            UBS_VLOG_ERR("async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
            continue;
        }

        event_reach_sockets.emplace(socket_ptr);
    }
    return event_reach_sockets;
}

ALWAYS_INLINE int UmqEpollRunnerOps::ProcessMainUmqRearm(uint64_t main_umq)
{
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto events_cnt = UmqApi::umq_get_cq_event(main_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, events_cnt, savedErrno);
        UBS_VLOG_ERR("umq_get_cq_event() failed for share jfr RX, main umq: %llu, "
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
            UBS_VLOG_ERR("umq_rearm_interrupt() failed for share jfr RX rearm, "
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

} // namespace umq
} // namespace ubs
} // namespace ock