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
    RunnerEventData event_data{};

    event_data.u64 = event.data.u64;
    if (event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR ||
        event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR_RETRY) {
        Locker slock(mutex_);
        auto pos = jfr_main_umq_.find(static_cast<int>(event_data.event_data.data));
        if (pos != jfr_main_umq_.end()) {
            main_umq = pos->second;
        }
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_SUB_UMQ_RX) {
        // 关闭共享 JFR：per-socket umq 的 RX 完成中断，event data 携带 socket fd
        return ProcessSubUmqRxEvent(static_cast<int>(event_data.event_data.data));
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
                     event_data.event_data.type);
    }

    if (main_umq != 0) {
        return ProcessShareJfrEvent(event, main_umq, event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR);
    }

    return 0;
}

int UmqShareJfrEpollRunnerOps::ProcessSubUmqRxEvent(int socket_fd)
{
    // 按 fd 从 ArraySet 查表取引用计数对象（与共享路径 Sift 相同做法）。
    // socket 已关闭/正在销毁时查表落空，直接忽略事件，规避悬垂指针。
    SocketPtr socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
    auto *umq_sock = dynamic_cast<UmqSocket *>(socket_ptr.Get());
    if (UNLIKELY(umq_sock == nullptr)) {
        UBS_VLOG_DEBUG("[Debug] async_epoll: sub umq rx event for closed socket fd: %d, skip.\n", socket_fd);
        return 0;
    }

    uint64_t sub_umq = umq_sock->UmqHandle();
    if (UNLIKELY(sub_umq == UMQ_INVALID_HANDLE)) {
        return 0;
    }

    // 消费并 ack 本次 CQ 中断事件，随后 rearm 请求下一次中断（EPOLLET 注册，必须 rearm，
    // 否则后续 RX 完成不再产生中断）。顺序与共享路径 ProcessMainUmqRearm 一致：
    // get_cq_event -> rearm -> ack；rearm 先于下方 umq_poll，poll 可兜住 rearm 前
    // 已入队的 CQE，不会丢事件。
    traceTime_.umq_rearm_start_timestamp_ = ubsocket_get_timeNs_compile();
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto events_cnt = UmqApi::umq_get_cq_event(sub_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, events_cnt, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed for sub umq RX, umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(sub_umq), events_cnt, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, events_cnt), savedErrno);
        return -1;
    }
    if (LIKELY(events_cnt > 0)) {
        int rearm_ret = UmqApi::umq_rearm_interrupt(sub_umq, false, &option);
        if (UNLIKELY(rearm_ret < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, rearm_ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for sub umq RX, umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(sub_umq), rearm_ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, rearm_ret), savedErrno);
        }
        // per-socket 事件不做 GET_PER_ACK 批量聚合，逐次 ack，保证连接关闭前事件全部归还
        UmqApi::umq_ack_interrupt(sub_umq, events_cnt, &option);
    }
    traceTime_.umq_rearm_end_timestamp_ = ubsocket_get_timeNs_compile();

    // runner 内直接收割（与共享 JFR 数据面一致，RTT 零劣化的关键）：
    //   umq_poll(sub_umq) -> SiftSocketEventsWithUmqBuffers -> replenish
    // FC_UPDATE 在 runner 线程内即译成 NotifyWritable()->EPOLLOUT，与 EPOLLIN 同批入队，
    // 应用一次 epoll_wait 可同时拿到 EPOLLIN+EPOLLOUT（1 次唤醒语义，与共享 JFR 等价）。
    // 若仍为"仅通知 EPOLLIN"模型，FC 信用在应用侧 PollRx 中是空操作，EPOLLOUT 永不触发，
    // 表现为"能建链、发完首包后无法继续发送"。
    // 应用侧取数相应改走 GetQbuf -> GetAndPopQbuf（pop rxQueue），runner 独占 drain 本 umq，
    // 不存在双端消费竞争。
    static thread_local std::unique_ptr<FlashDynamicBitSet> sub_event_reach_sockets;
    if (UNLIKELY(sub_event_reach_sockets.get() == nullptr)) {
        sub_event_reach_sockets.reset(
            new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        if (UNLIKELY(sub_event_reach_sockets.get() == nullptr)) {
            UBS_VLOG_ERR("allocate memory for FlashDynamicBitSet failed.\n");
            return -1;
        }
    }

    traced_socket_fds_.clear();
    do {
        traced_socket_fd_trace_map_.clear();
        umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
        traceTime_.umq_poll_start_timestamp_ = ubsocket_get_timeNs_compile();
        umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                       UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                       traceTime_.umq_poll_start_timestamp_};
        auto pollNum = UmqApi::umq_poll(sub_umq, &poll_option, buf, MAX_EPOLL_WAIT_COUNT);
        traceTime_.umq_poll_end_timestamp_ = ubsocket_get_timeNs_compile();
        if (UNLIKELY(pollNum < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for sub umq RX, umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(sub_umq), pollNum, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
            return -1;
        }
        if (pollNum == 0) {
            // 中断触发但 CQE 已被上一轮 poll 兜走（rearm 先于 poll 的正常竞态），静默返回
            return 0;
        }

        // 补投 RX 缓冲：排除流控 fake buffer，仅按真实消耗的 RX WQE 数量 1:1 回填到
        // per-socket umq（共享路径回填到 main_umq，此处回填到 sub_umq，其余逻辑一致）。
        int fcBufCnt = 0;
        for (int i = 0; i < pollNum; ++i) {
            if (buf[i]->status >= UMQ_FAKE_BUF_FC_UPDATE) {
                ++fcBufCnt;
            }
        }
        int ioPollNum = pollNum - fcBufCnt;
        if (ioPollNum != 0) {
            umq_alloc_option_t alloc_option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(ock::ubs::Block)};
            umq_buf_t *rx_buf_list =
                UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), ioPollNum, UMQ_INVALID_HANDLE, &alloc_option);
            if (LIKELY(rx_buf_list != nullptr)) {
                umq_buf_t *bad_qbuf = nullptr;
                traceTime_.umq_post_start_timestamp_ = ubsocket_get_timeNs_compile();
                umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TAG_TIMESTAMP,
                                                UMQ_IO_RX, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                                traceTime_.umq_post_start_timestamp_};
                if (UmqApi::umq_post(sub_umq, rx_buf_list, &io_rx_option, &bad_qbuf) != UMQ_SUCCESS) {
                    int savedErrno = errno;
                    errno = UmqErrnoConverter::Convert(UmqOperation::READV, UMQ_FAIL, savedErrno);
                    UBS_VLOG_ERR("[UMQ_API] umq_post() failed for sub umq RX refill, umq: %llu, "
                                 "mapped errno: %d(%s), original errno: %d\n",
                                 static_cast<unsigned long long>(sub_umq), errno,
                                 UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, UMQ_FAIL), savedErrno);
                    UmqApi::umq_buf_free(bad_qbuf);
                }
                traceTime_.umq_post_end_timestamp_ = ubsocket_get_timeNs_compile();
            }
        }

        sub_event_reach_sockets->ClearAll();

        // 复用共享路径的分拣逻辑：FC_UPDATE -> NotifyWritable、错误 CQE -> HandleErrorRxCqe、
        // 数据 -> AddQbuf(rxQueue)。sub umq 创建时已设 umq_ctx=socket fd，Sift 按 fd 路由成立。
        epoll_data_t event_data{};
        std::vector<SocketPtr> socket_ptrs;
        SiftSocketEventsWithUmqBuffers(buf, pollNum, *sub_event_reach_sockets, socket_ptrs);
        for (auto &obj : socket_ptrs) {
            auto socket_obj = obj.Get();
            ((UmqSocket *)socket_obj)->NewRxEpollIn();
            auto *epoll_fd_obj = (AsyncEventPoll *)(((SocketBase *)socket_obj)->GetAddedEpollFd(event_data));
            if (LIKELY(epoll_fd_obj != nullptr)) {
                epoll_fd_obj->AddReadableEvent(EPOLLIN, event_data);
                epoll_fd_obj->SetReadableEventFd();
            }
        }

        for (auto &kv : traced_socket_fd_trace_map_) {
            TRACE_ADD_EPOLL_FULL(kv.second, CORE_PROCESS_JRF_END, kv.first, 0, 0, pollNum,
                                 traceTime_.umq_post_start_timestamp_, traceTime_.umq_post_end_timestamp_);
        }
    } while (GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED);
    return 0;
}

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq,
                                                                  bool should_rearm_interrupt)
{
    traceTime_.umq_rearm_start_timestamp_ = ubsocket_get_timeNs_compile();
    if (should_rearm_interrupt && UNLIKELY(ProcessMainUmqRearm(main_umq) < 0)) {
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

    traced_socket_fds_.clear();
    do {
        traced_socket_fd_trace_map_.clear();
        umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
        traceTime_.umq_poll_start_timestamp_ = ubsocket_get_timeNs_compile();
        umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                       UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                       traceTime_.umq_poll_start_timestamp_};
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
            umq_buf_t *rx_buf_list =
                UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), ioPollNum, UMQ_INVALID_HANDLE, &alloc_option);
            if (LIKELY(rx_buf_list != nullptr)) {
                umq_buf_t *bad_qbuf = nullptr;
                traceTime_.umq_post_start_timestamp_ = ubsocket_get_timeNs_compile();
                umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TAG_TIMESTAMP,
                                                UMQ_IO_RX, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                                traceTime_.umq_post_start_timestamp_};
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
                epoll_fd_obj->AddReadableEvent(EPOLLIN, event_data);
                if (!event_reach_epoll_fds->Test(epoll_fd_obj->GetEpollFd())) {
                    readable_epoll_fds.emplace_back(epoll_fd_obj);
                    event_reach_epoll_fds->Set(epoll_fd_obj->GetEpollFd());
                }
            }
        }

        for (auto epoll_fd : readable_epoll_fds) {
            epoll_fd->SetReadableEventFd();
        }

        traceTime_.umq_post_end_timestamp_ = ubsocket_get_timeNs_compile();
        for (auto &kv : traced_socket_fd_trace_map_) {
            TRACE_ADD_EPOLL_FULL(kv.second, CORE_PROCESS_JRF_END, kv.first, 0, 0, pollNum,
                                 traceTime_.umq_post_start_timestamp_, traceTime_.umq_post_end_timestamp_);
        }
    } while (GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED);
    return 0;
}

void UmqShareJfrEpollRunnerOps::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count,
                                                               FlashDynamicBitSet &socket_fds,
                                                               std::vector<SocketPtr> &socket_ptrs)
{
    SocketPtr socket_ptr{nullptr};
    SocketBasePtr sk_base{nullptr};
    int last_socket_fd = -1;
    for (int i = 0; i < count; ++i) {
        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        if (socket_fd != last_socket_fd) {
            socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
            sk_base = RefStaticCast<SocketBase>(socket_ptr);
            last_socket_fd = socket_fd;
        }
        if (UNLIKELY(socket_ptr.Get() == nullptr)) {
            UBS_VLOG_DEBUG("[Debug] async_epoll: socket fd: %d object is null, skipping event processing. \n",
                           socket_fd);
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }

        if (buf[i]->status >= UMQ_FAKE_BUF_FC_UPDATE) {
            if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE) {
                sk_base->NotifyWritable();
            } else if (buf[i]->status != UMQ_FAKE_BUF_FC_MSG) {
                auto rx_ops = dynamic_cast<UmqRxOps *>(((UmqSocket *)socket_ptr.Get())->GetRx()->GetRxOps());
                rx_ops->HandleErrorRxCqe(buf[i]);
                socket_ptr->State(SOCK_STAT_CLOSE);
            }
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }

        auto *trace = socket_ptr->split_trace_;
        if (trace != nullptr) {
            if (traced_socket_fds_.find(socket_ptr->raw_socket_) == traced_socket_fds_.end()) {
                traced_socket_fds_.insert(socket_ptr->raw_socket_);
                TRACE_ADD_EPOLL_FULL(trace, CORE_EPOLL_REARM, socket_ptr->raw_socket_, buf_pro->imm.user_data,
                                     buf[i]->data_size, count, traceTime_.umq_rearm_start_timestamp_,
                                     traceTime_.umq_rearm_end_timestamp_);
            }
            if (traced_socket_fd_trace_map_.find(socket_ptr->raw_socket_) == traced_socket_fd_trace_map_.end()) {
                traced_socket_fd_trace_map_[socket_ptr->raw_socket_] = trace;
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
            UBS_VLOG_DEBUG("async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
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
    // 关闭共享 JFR：注册 per-socket umq 的 RX 中断 fd（SUB_UMQ_RX），事件 data 携带 socket fd
    SubUmqRxExtContext *sub_umq_rx_ctx = dynamic_cast<SubUmqRxExtContext *>(ctx);
    if (sub_umq_rx_ctx != nullptr) {
        RunnerEventData sub_event_data{};
        sub_event_data.event_data.type = RUNNER_EVENT_TYPE_SUB_UMQ_RX;
        sub_event_data.event_data.data = static_cast<uint64_t>(sub_umq_rx_ctx->socket_fd);

        struct epoll_event sub_event {};
        sub_event.events = EPOLLIN | EPOLLET;
        sub_event.data.u64 = sub_event_data.u64;
        if (LibcApi::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &sub_event) < 0) {
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) sub umq rx event failed, socket fd: %d, "
                         "interrupt fd: %d, errno: %d : %s\n",
                         sub_umq_rx_ctx->socket_fd, fd, errno, strerror(errno));
            return UBS_ERROR;
        }

        // 首次 arming：EPOLLET + 中断模式下必须先 rearm 一次，之后每次事件处理内再 rearm
        umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
        int ret = UmqApi::umq_rearm_interrupt(ctx->umq_handle, false, &rx_option);
        if (ret < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for sub umq RX, "
                         "umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(ctx->umq_handle), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            LibcApi::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            return UBS_ERROR;
        }
        return UBS_OK;
    }

    ShareJfrExtContext *share_jfr_ctx = dynamic_cast<ShareJfrExtContext *>(ctx);
    if (share_jfr_ctx == nullptr) {
        UBS_VLOG_ERR("Unsupported operation. Check context because context is null.\n");
        return UBS_ERROR;
    }
    if (InsertJfrMainUmq(fd, share_jfr_ctx->umq_handle, epoll_fd, event) < 0) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }

    if (share_jfr_ctx->should_rearm_interrupt) {
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
    }
    return UBS_OK;
}

int UmqShareJfrEpollRunnerOps::DelEpollEvent(int epoll_fd, int fd)
{
    // 用于关闭共享 JFR 时注销 per-socket umq 的 RX 中断 fd（SUB_UMQ_RX）。
    // 主 umq（SHARE_JFR/RETRY）为进程级常驻资源，不走此路径。
    auto ret = LibcApi::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del sub umq rx event fd: %d failed: %d : %s\n", fd, errno, strerror(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
