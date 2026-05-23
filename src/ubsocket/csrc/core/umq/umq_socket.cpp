/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "umq_socket.h"
#include "umq_eid_table.h"
#include "umq_epoll_runner_ops.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqSocket::Initialize() noexcept
{
    return UBS_OK;
}

void UmqSocket::UnInitialize() noexcept {}

Result UmqSocket::CreateLocalUmq(umq_eid_t *conn_eid, umq_used_ports_t &used_ports)
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("Create umq on a created umq.\n");
        return UBS_ERROR;
    }

    umq_create_option_t queue_cfg;
    memset(&queue_cfg, 0, sizeof(queue_cfg));
    // TODO：增加环境变量
    queue_cfg.trans_mode = UMQ_TRANS_MODE_UB;
    queue_cfg.create_flag = UMQ_CREATE_FLAG_TX_DEPTH | UMQ_CREATE_FLAG_RX_DEPTH | UMQ_CREATE_FLAG_RX_BUF_SIZE |
                            UMQ_CREATE_FLAG_TX_BUF_SIZE | UMQ_CREATE_FLAG_QUEUE_MODE | UMQ_CREATE_FLAG_TP_MODE |
                            UMQ_CREATE_FLAG_TP_TYPE | UMQ_CREATE_FLAG_UMQ_CTX;

    queue_cfg.rx_depth = GlobalSetting::UBS_RX_DEPTH;
    queue_cfg.tx_depth = GlobalSetting::UBS_TX_DEPTH;
    queue_cfg.rx_buf_size = UmqSetting::GetIOBufSize();
    queue_cfg.tx_buf_size = UmqSetting::GetIOBufSize();
    queue_cfg.mode = UMQ_MODE_INTERRUPT;
    // 共享 JFR、AE 事件依赖 umq_ctx.
    queue_cfg.umq_ctx = raw_socket_;
    if (IsBonding()) {
        queue_cfg.create_flag |= UMQ_CREATE_FLAG_USED_PORTS;
        queue_cfg.used_ports = used_ports;
    }

    // TODO: 设置队列优先级
    // if (context->GetLinkPriority() != DEFAULT_LINK_PRIORITY) {
    //     queue_cfg.priority = context->GetLinkPriority();
    //     queue_cfg.create_flag |= UMQ_CREATE_FLAG_PRIORITY;
    // }

    int n = snprintf(queue_cfg.name, UMQ_NAME_MAX_LEN, "fd: %d", raw_socket_);
    if ((((int)UMQ_NAME_MAX_LEN - 1) < n) || (n < 0)) {
        UBS_VLOG_ERR("Failed to set umq name\n");
        return UBS_SET_DEV_INFO;
    }

    // TODO: 待补充指定 ip 和 bonging name 的情况
    umq_eid_t local_eid = *conn_eid;
#ifdef ENABLED
    if (context->GetDevIpStr() != nullptr) {
        if (context->IsDevIpv6()) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
            if (strcpy_s(queue_cfg.dev_info.ipv6.ip_addr, UMQ_IPV6_SIZE, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv6 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        } else {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
            if (strcpy_s(queue_cfg.dev_info.ipv4.ip_addr, UMQ_IPV4_SIZE, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv4 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        }
    } else if (context->GetDevNameStr() != nullptr) {
        if (strcpy_s(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, context->GetDevNameStr()) != EOK) {
            UBS_VLOG_ERR("Failed to strcpy_s device name\n");
            return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
        }
        if (!context->IsBonding()) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            queue_cfg.dev_info.dev.eid_idx = context->GetEidIdx();

            if (GetDevEid(queue_cfg.dev_info.dev.dev_name, context->GetEidIdx(), &local_eid) != 0) {
                UBS_VLOG_ERR("Failed to get eid by dev name:%s and eid index:%d \n", context->GetDevNameStr(),
                             context->GetEidIdx());
            }
            m_conn_info.conn_eid = local_eid;
        } else {
            // init use bonding dev
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
            local_eid = *conn_eid;
        }
    } else {
        if (strcpy(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "bonding_dev_0") != EOK) {
            UBS_VLOG_ERR("Failed to strcpy device name\n");
            return UBS_SET_DEV_INFO;
        }
        IsBonding if (context->IsBonding())
        {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
        }
    }
#endif

    if (strcpy(queue_cfg.dev_info.dev.dev_name, "bonding_dev_0") == nullptr) {
        UBS_VLOG_ERR("Failed to strcpy device name, errno: %d\n", errno);
        return UBS_SET_DEV_INFO;
    }
    queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
    queue_cfg.dev_info.eid.eid = *conn_eid;

    static const char *trans_mode_str[RC_CTP + 1] = {"RC_TP", "RM_TP", "RM_CTP", "RC_CTP"};
    UBS_VLOG_INFO("trans_mode result is: %s\n", trans_mode_str[trans_mode_]);
    if (trans_mode_ == RC_TP) {
        queue_cfg.tp_mode = UMQ_TM_RC;
        queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode_ == RM_TP) {
        queue_cfg.tp_mode = UMQ_TM_RM;
        queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode_ == RM_CTP) {
        queue_cfg.tp_mode = UMQ_TM_RM;
        queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
    } else if (trans_mode_ == RC_CTP) {
        queue_cfg.tp_mode = UMQ_TM_RC;
        queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
    }

    umq_handle_ = CreateSubUmq(&queue_cfg, &local_eid);
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("CreateSubUmq() failed, ret: %llu\n", static_cast<unsigned long long>(umq_handle_));
        return UBS_ERROR;
    }

    uint64_t share_jfr_rx_queue_depth = UmqSetting::UMQ_SHARE_JFR_RX_QUEUE_DEPTH;
    rxQueue = new (std::nothrow) QbufQueue<umq_buf_t *>(share_jfr_rx_queue_depth);
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("Failed to init share jfr rx queue for fd: %d \n", raw_socket_);
        return UBS_INIT_SHARED_JFR_RX_QUEUE;
    }
    return UBS_OK;
}

uint64_t UmqSocket::CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        return UmqApi::umq_create(cfg);
    }
    Locker sLock(UmqEidTable::Instance().GetMainMutex());
    uint64_t main_umq = GetOrCreateMainUmq(cfg, local_eid);
    if (main_umq == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("GetOrCreateMainUmq() failed, ret: %llu\n", static_cast<unsigned long long>(main_umq));
        return UMQ_INVALID_HANDLE;
    }

    cfg->create_flag |= UMQ_CREATE_FLAG_SHARE_RQ | UMQ_CREATE_FLAG_UMQ_CTX | UMQ_CREATE_FLAG_SUB_UMQ;
    cfg->share_rq_umqh = main_umq;
    cfg->umq_ctx = (uint64_t)raw_socket_;
    uint64_t sub_umq = UmqApi::umq_create(cfg);
    if (sub_umq == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("umq_create() failed for sub umq, ret: %llu\n", static_cast<unsigned long long>(sub_umq));
        return UMQ_INVALID_HANDLE;
    }

    UmqEidTable::Instance().Add(*local_eid, GetTransMode(), main_umq);

    share_umq_handle_ = main_umq;
    return sub_umq;
}

uint64_t UmqSocket::GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid)
{
    std::vector<std::shared_ptr<MainUmqState>> main_umqs;
    if (!UmqEidTable::Instance().Get(*localEid, GetTransMode(), main_umqs)) {
        umq_create_option_t cfg_main;
        memcpy(&cfg_main, cfg, sizeof(*cfg));
        cfg_main.create_flag |= UMQ_CREATE_FLAG_MAIN_UMQ;

        return UmqApi::umq_create(&cfg_main);
    }

    if (main_umqs.empty()) {
        UBS_VLOG_ERR("Main umq list is empty, local eid:" EID_FMT ", ret: %llu\n",
                     EID_ARGS(*localEid), static_cast<unsigned long long>(UMQ_INVALID_HANDLE));
        return UMQ_INVALID_HANDLE;
    }

    // eid 对应多个不同 UB 传输模式的主 umq. 当前实现保证此时 main_umqs 长度为 1
    return main_umqs.front()->GetUmqHandle();
}

Result UmqSocket::PrefillRx()
{
    uint64_t umq_handle = GlobalSetting::UBS_ENABLE_SHARE_JFR ? share_umq_handle_ : umq_handle_;
    uint32_t left_post_rx_num = getLeftPostRxNum(umq_handle);
    if (left_post_rx_num == 0) {
        UBS_VLOG_ERR("Failed to set rx window capacity\n");
        return UBS_ERROR;
    }
    uint32_t cur_post_rx_num = 0;
    umq_alloc_option_t option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(Block)};
    do {
        cur_post_rx_num = left_post_rx_num > UmqSetting::UMQ_POST_BATCH_MAX ? UmqSetting::UMQ_POST_BATCH_MAX :
                                                                              left_post_rx_num;
        umq_buf_t *rx_buf_list =
            UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), cur_post_rx_num, UMQ_INVALID_HANDLE, &option);
        if (rx_buf_list == nullptr) {
            int rx_window_capacity = 0;
            UBS_VLOG_ERR("umq_buf_alloc() failed, RX depth: %u, ret: %p\n", rx_window_capacity, rx_buf_list);
            return UBS_ERROR;
        }

        umq_buf_t *bad_qbuf = nullptr;
        int umq_ret = UmqApi::umq_post(umq_handle, rx_buf_list, UMQ_IO_RX, &bad_qbuf);
        if (umq_ret != UMQ_SUCCESS) {
            int rx_window_capacity = 0;
            UBS_VLOG_ERR("umq_post() failed, RX depth: %u, ret: %d\n", rx_window_capacity, umq_ret);
            // TODO：处理bad_qbuf
            // rx_.GetRxOps()->rx_queue_avail_num_ += HandleBadQBuf(rx_buf_list, bad_qbuf);
            return UBS_ERROR;
        }
        rx_.GetRxOps()->rx_queue_avail_num_ += cur_post_rx_num;
        UBS_VLOG_DEBUG("Post RX depth: %u\n", cur_post_rx_num);
    } while ((left_post_rx_num -= cur_post_rx_num) > 0);

    uint32_t poll_cnt = 0;
    do {
        // TODO tx_.GetTxOps()->PollUmqTx()
        tx_.GetTxOps()->PollTx(this);
        if (UmqApi::umq_state_get(umq_handle_) != QUEUE_STATE_IDLE) {
            break;
        }
        usleep(WAIT_READY_TIMEOUT_US);
    } while (poll_cnt++ < WAIT_READY_ROUND);

    int local_umq_state = UmqApi::umq_state_get(umq_handle_);
    if (local_umq_state != QUEUE_STATE_READY) {
        UBS_VLOG_ERR("umq_state_get() failed to reach ready, ret: %d\n", local_umq_state);
        return -1;
    }

    rx_.GetRxOps()->rx_queue_avail_num_ = GlobalSetting::UBS_RX_DEPTH;
    return 0;
}

Result UmqSocket::AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event)
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        UBS_VLOG_ERR("async_epoll Failed to get TX interrupt fd for umq, socket fd: %d\n", sock->raw_socket_);
        return -1;
    }
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tx_interrupt_fd, event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d failed: %d : %s\n", sock->raw_socket_, errno,
                     strerror(errno));
        return -1;
    }

    ret = ock::ubs::UmqApi::umq_rearm_interrupt(umq_handle_, true, &tx_option);
    if (ret < 0) {
        UBS_VLOG_ERR("umq_rearm_interrupt() failed for TX, local umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret);
        return -1;
    }
    return 0;
}

Result UmqSocket::DelTxEvent(const SocketPtr &sock, int epoll_fd)
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        UBS_VLOG_ERR("async_epoll Failed to get TX interrupt fd for umq, socket fd: %d\n", sock->raw_socket_);
        return -1;
    }
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, tx_interrupt_fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del out event for socket event fd: %d failed: %d : %s\n", tx_interrupt_fd, errno,
                     strerror(errno));
        return -1;
    }
    return 0;
}

Result UmqSocket::AddRxEventToRunner(uintptr_t event_poll, const SocketPtr &sock,
                                    int epoll_fd, struct epoll_event *event)
{
    if (UNLIKELY(epoll_fd < 0 || umq_handle_ <= 0)) {
        UBS_VLOG_ERR("epoll_fd or umq_handle invalid, epoll_fd: %d, umq_handle: %d\n", epoll_fd, umq_handle_);
    }
    // 1. add share jfr main umq fd
    uint64_t main_umq = share_umq_handle_;
    umq_interrupt_option_t main_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto share_jfr_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(main_umq, &main_option);
    if (UNLIKELY(share_jfr_fd < 0)) {
        UBS_VLOG_ERR("async_epoll umq_interrupt_fd_get() failed, socket fd: %d, ret: %d\n", sock->raw_socket_,
                     share_jfr_fd);
        return -1;
    }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = share_jfr_fd;

    struct epoll_event shared_jfr_event{};
    shared_jfr_event.events = EPOLLIN | EPOLLET;
    shared_jfr_event.data.u64 = event_data.u64;

    UmqEpollRunnerOps *ops = (UmqEpollRunnerOps *)EpollRunnerFactory::GetInstance(this->Type()).GetOps();
    if (UNLIKELY(ops == nullptr || ops->InsertJfrMainUmq(
        share_jfr_fd, main_umq, epoll_fd, &shared_jfr_event) < 0)) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    //2. add sub umq rx fd
    umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    int rx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &rx_option);
    if (UNLIKELY(rx_interrupt_fd < 0)) {
        UBS_VLOG_ERR("async_epoll Failed to get RX interrupt fd for umq, socket fd: %d, ret = %d\n",
                     sock->raw_socket_, rx_interrupt_fd);
        return -1;
    }
    struct epoll_event rx_event{};
    rx_event.events = EPOLLIN | EPOLLET;
    event_data.event_data.type = RUNNER_EVENT_TYPE_SUB_UMQ_RX;
    event_data.event_data.data = (ptrdiff_t)(void *)(sock.Get());
    rx_event.data.u64 = event_data.u64;
    if (UNLIKELY(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, rx_interrupt_fd, &rx_event) < 0)) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) rx event failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    // 3. add to socket
    SetAddedEpollFd((EventPoll *)event_poll, event->data);

    // 4. do rearm
    int ret = ock::ubs::UmqApi::umq_rearm_interrupt(main_umq, false, &rx_option);
    if (ret < 0) {
        UBS_VLOG_ERR("umq_rearm_interrupt() failed for share jfr RX, main umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(main_umq), ret);
        return -1;
    }
    ret = ock::ubs::UmqApi::umq_rearm_interrupt(umq_handle_, false, &rx_option);
    if (ret < 0) {
        UBS_VLOG_ERR("umq_rearm_interrupt() failed for sub umq RX, main umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret);
        return -1;
    }

    return 0;
}

Result UmqSocket::DelRxEventToRunner(const SocketPtr &sock, int epoll_fd)
{
    umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    int rx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &rx_option);
    if (UNLIKELY(rx_interrupt_fd < 0)) {
        UBS_VLOG_ERR("async_epoll Failed to get RX interrupt fd for umq, socket fd: %d\n", sock->raw_socket_);
        return -1;
    }

    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, rx_interrupt_fd, nullptr);
    SetAddedEpollFd(nullptr);
    return ret;
}

int UmqSocket::GetTxFd()
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        UBS_VLOG_ERR("async_epoll Failed to get TX interrupt fd for umq, umq handle: %d\n", umq_handle_);
        return -1;
    }
    return tx_interrupt_fd;
}
int UmqSocket::AddQbuf(umq_buf_t *qbuf)
{
    int enqueue_ret = 0;
    if (rxQueue == nullptr || (enqueue_ret = rxQueue->Enqueue(qbuf)) != 0) {
        UBS_VLOG_ERR("AddQbuf failed, fd: %d, ret: %d\n",
            raw_socket_, rxQueue == nullptr ? -1 : enqueue_ret);
        return -1;
    }

    return 0;
}
int UmqSocket::GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size)
{
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("GetAndPopQbuf failed, rx queue is null, fd: %d, ret: %d\n",
            raw_socket_, -1);
        return -1;
    }

    uint32_t i = 0;
    while (!rxQueue->IsEmpty() && i < max_buf_size) {
        if (rxQueue->Dequeue(&buf[i]) != 0) {
            return i + 1;
        }
        i++;
    }

    return i;
}
int UmqSocket::FlushRxQueue()
{
    if (rxQueue == nullptr) {
        return -1;
    }

    while (!rxQueue->IsEmpty()) {
        umq_buf_t *buf[1];
        if (rxQueue->Dequeue(buf) != 0) {
            return -1;
        }
        UmqApi::umq_buf_free(buf[0]);
    }

    return 0;
}
uint32_t UmqSocket::getLeftPostRxNum(uint64_t umq_handle)
{
    uint32_t left_post_rx_num = 0;
    umq_cfg_get_t cfg;
    memset(&cfg, 0, sizeof(umq_cfg_get_t));
    int res = UmqApi::umq_cfg_get(umq_handle, &cfg);
    if (res != 0) {
        UBS_VLOG_ERR("umq_cfg_get() failed, umq handle: %llu, ret: %d\n", static_cast<unsigned long long>(umq_handle),
                     res);
    } else {
        left_post_rx_num = cfg.rqe_post_factor * cfg.rx_depth;
        UBS_VLOG_INFO("Successfully get umq cfg, left_post_rx_num = %u\n", left_post_rx_num);
    }
    return left_post_rx_num;
}

} // namespace umq
} // namespace ubs
} // namespace ock