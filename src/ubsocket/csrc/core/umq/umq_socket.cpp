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
#include <chrono>
#include <iostream>

#include "profiling/statistics/statistics.h"
#include "umq_conn_helper.h"
#include "umq_dfx_api.h"
#include "umq_eid_table.h"
#include "umq_epoll_runner_ops.h"
#include "umq_errno_converter.h"
#include "umq_socket.h"
#include "umq_socket_acceptor.h"
#include "umq_socket_connector.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqSocket::Initialize() noexcept
{
    return UBS_OK;
}

void UmqSocket::UnInitialize() noexcept {}

Result UmqSocket::CreateLocalUmq(umq_eid_t *conn_eid, umq_used_ports_t &used_ports, umq_eid_t *conn_eid_used,
                                 umq_topo_type_t &topo_type)
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("Create umq on a created umq.\n");
        return UBS_ERROR;
    }
    topo_type_ = topo_type;

    umq_create_option_t queue_cfg;
    memset(&queue_cfg, 0, sizeof(queue_cfg));
    UmqConnHelper::NewBaseUmqCreateOptions(queue_cfg, trans_mode_);

    // 共享 JFR、AE 事件依赖 umq_ctx.
    queue_cfg.umq_ctx = raw_socket_;
    // TODO: is_bonding 待确认如何设置到 socketbase
    UBS_VLOG_INFO("UmqSetting::UMQ_IS_BONDING %b topo_type_ %d", UmqSetting::UMQ_IS_BONDING, topo_type_);
    if (UmqSetting::UMQ_IS_BONDING && GlobalSetting::UBS_BACKUP_LINK_ENABLED) {
        queue_cfg.create_flag |= UMQ_CREATE_FLAG_USED_PORTS;
        queue_cfg.used_ports = used_ports;
    }

    int n = snprintf(queue_cfg.name, UMQ_NAME_MAX_LEN, "fd: %d", raw_socket_);
    if ((((int)UMQ_NAME_MAX_LEN - 1) < n) || (n < 0)) {
        UBS_VLOG_ERR("Failed to set umq name\n");
        return UBS_SET_DEV_INFO;
    }

    // TODO: 待补充指定 ip 和 bonging name 的情况
    umq_eid_t local_eid;
    if (!UmqSetting::UMQ_DEV_IP.empty()) {
        // TODO: 待补充指定 ip 情况
        UBS_VLOG_ERR("Unsupported to set umq ip address\n");
#ifdef ENABLED
        if (context->IsDevIpv6()) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
            if (strcpy(queue_cfg.dev_info.ipv6.ip_addr, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv6 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        } else {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
            if (strcpy(queue_cfg.dev_info.ipv4.ip_addr, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv4 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        }
#endif
    } else if (!UmqSetting::UMQ_DEV_NAME.empty()) {
        if (strcpy(queue_cfg.dev_info.dev.dev_name, UmqSetting::UMQ_DEV_NAME.c_str()) == nullptr) {
            UBS_VLOG_ERR("Failed to strcpy device name\n");
            return UBS_NEW_SOCKET_FD;
        }
        if (UmqSetting::UMQ_IS_BONDING && GlobalSetting::UBS_BACKUP_LINK_ENABLED) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            queue_cfg.dev_info.dev.eid_idx = UmqSetting::UMQ_EID_INDEX;

            if (UmqConnHelper::GetDevEid(queue_cfg.dev_info.dev.dev_name, UmqSetting::UMQ_EID_INDEX, &local_eid) != 0) {
                UBS_VLOG_ERR("Failed to get eid by dev name:%s and eid index:%d \n", UmqSetting::UMQ_DEV_NAME,
                             UmqSetting::UMQ_EID_INDEX);
            }
            *conn_eid_used = local_eid;
            UBS_VLOG_INFO("Use Bonding: " EID_FMT ".\n", EID_ARGS(*conn_eid_used));
        } else {
            // init use bonding dev
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
            local_eid = *conn_eid;
            UBS_VLOG_INFO("Use UDMA: " EID_FMT ".\n", EID_ARGS(*conn_eid));
        }
    } else {
        if (strcpy(queue_cfg.dev_info.dev.dev_name, "bonding_dev_0") == nullptr) {
            UBS_VLOG_ERR("Failed to strcpy device name, errno: %d\n", errno);
            return UBS_SET_DEV_INFO;
        }
        if (UmqSetting::UMQ_IS_BONDING) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
        }
    }

    umq_handle_ = CreateSubUmq(&queue_cfg, &local_eid);
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, savedErrno);
        UBS_VLOG_ERR("CreateSubUmq() failed, ret: %llu, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CREATE, UMQ_FAIL), savedErrno);
        return UBS_UMQ_CREATE | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }

    rxQueue = new (std::nothrow) UmqBufferReceiveQueue();
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
    UBS_VLOG_INFO("UBS_ENABLE_SHARE_JFR = true \n");
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
        UBS_VLOG_ERR("[UMQ_API] umq_create() failed for sub umq, ret: %llu\n",
                     static_cast<unsigned long long>(sub_umq));
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
        UBS_VLOG_ERR("Main umq list is empty, local eid:" EID_FMT ", ret: %llu\n", EID_ARGS(*localEid),
                     static_cast<unsigned long long>(UMQ_INVALID_HANDLE));
        return UMQ_INVALID_HANDLE;
    }
    // eid 对应多个不同 UB 传输模式的主 umq. 当前实现保证此时 main_umqs 长度为 1
    return main_umqs.front()->GetUmqHandle();
}

Result UmqSocket::UpdateRxQueueAvailNum()
{
    int local_umq_state = UmqApi::umq_state_get(umq_handle_);
    if (local_umq_state != QUEUE_STATE_READY) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::GET_STATE, local_umq_state, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_state_get() failed to reach ready, "
                     "state: %d, mapped errno: %d(%s), original errno: %d\n",
                     local_umq_state, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::GET_STATE, local_umq_state), savedErrno);
        return UBS_ERROR;
    }

    rx_.GetRxOps()->rx_queue_avail_num_ = GlobalSetting::UBS_RX_DEPTH;
    return 0;
}

void UmqSocket::UnbindAndFlushRemoteUmq(const SocketPtr &sock)
{
    if (!umq_is_bind_remote_) {
        return;
    }

    if (tx_.GetTxOps()->ack_event_num_ > 0) {
        umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
        UmqApi::umq_ack_interrupt(umq_handle_, tx_.GetTxOps()->ack_event_num_, &option);
        tx_.GetTxOps()->ack_event_num_ = 0;
    }

    if (rx_.GetRxOps()->ack_event_num_ > 0) {
        umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
        UmqApi::umq_ack_interrupt(umq_handle_, rx_.GetRxOps()->ack_event_num_, &option);
        rx_.GetRxOps()->ack_event_num_ = 0;
    }

    int ret = UmqApi::umq_unbind(umq_handle_);
    if (ret != UMQ_SUCCESS) {
        UBS_VLOG_ERR("[UMQ_API] umq_unbind() failed, local umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret);
    }
    tx_.GetTxOps()->FlushTx(sock);

    GlobalSetting::UBS_ENABLE_SHARE_JFR ? FlushRxQueue() : rx_.GetRxOps()->FlushRx(sock);
}

void UmqSocket::DestroyLocalUmq()
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        // need to flush
        int ret = UmqApi::umq_destroy(umq_handle_);
        if (ret != UMQ_SUCCESS) {
            UBS_VLOG_ERR("umq_destroy() failed, local umq: %llu, ret: %d\n",
                         static_cast<unsigned long long>(umq_handle_), ret);
        }
        /**
         * (1) 暂时无需 DeleteSubUmq(): umq_handle_ 会在此处释放, share_umq_handle_ 无需在此处删除, 
         *     在 share_umq_handle_ 做为 umq_handle_ 时会被删除
         * (2) 暂时无需 MainSubUmqTable: 记录了 share_umq_handle_ 和 umq_handle_ 和映射, 仅在 DeleteSubUmq 中使用
         * DeleteSubUmq();
         */
        umq_handle_ = UMQ_INVALID_HANDLE;
    }
}

Result UmqSocket::AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event)
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, tx_interrupt_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get TX interrupt fd, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), tx_interrupt_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, tx_interrupt_fd), savedErrno);
        return -1;
    }
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tx_interrupt_fd, event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d failed: %d : %s\n", sock->raw_socket_, errno,
                     strerror(errno));
        return -1;
    }

    // solicated : true 中断只返回给 solicited_enable 为 1 的 socket
    ret = ock::ubs::UmqApi::umq_rearm_interrupt(umq_handle_, true, &tx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return -1;
    }
    return 0;
}

Result UmqSocket::DelTxEvent(const SocketPtr &sock, int epoll_fd)
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, tx_interrupt_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get TX interrupt fd for DelTxEvent, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), tx_interrupt_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, tx_interrupt_fd), savedErrno);
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

Result UmqSocket::ProcessEpollEvent(struct epoll_event &event)
{
    auto event_data = (EpollEvent *)event.data.ptr;
    if (event_data->event_type == EPOLL_EVENT_UB_SOCKET_OUT) {
        UBS_VLOG_ERR("ProcessEpollEvent ProcessEpollEvent ProcessEpollEvent \n");
        NewTxEpollIn();
    }
    return UBS_OK;
}

Result UmqSocket::AddRxEventToRunner(uintptr_t event_poll, const SocketPtr &sock, int epoll_fd,
                                     struct epoll_event *event)
{
    if (UNLIKELY(epoll_fd < 0 || umq_handle_ <= 0)) {
        UBS_VLOG_ERR("epoll_fd or umq_handle invalid, epoll_fd: %d, umq_handle: %llu\n", epoll_fd,
                     static_cast<unsigned long long>(umq_handle_));
    }
    // 1. add share jfr main umq fd
    uint64_t main_umq = share_umq_handle_;
    umq_interrupt_option_t main_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto share_jfr_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(main_umq, &main_option);
    if (UNLIKELY(share_jfr_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, share_jfr_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get share jfr RX interrupt fd, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), share_jfr_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, share_jfr_fd), savedErrno);
        return -1;
    }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = share_jfr_fd;

    struct epoll_event shared_jfr_event {
    };
    shared_jfr_event.events = EPOLLIN | EPOLLET;
    shared_jfr_event.data.u64 = event_data.u64;

    UmqEpollRunnerOps *ops = (UmqEpollRunnerOps *)EpollRunnerFactory::GetInstance(this->Type()).GetOps();
    if (UNLIKELY(ops == nullptr || ops->InsertJfrMainUmq(share_jfr_fd, main_umq, epoll_fd, &shared_jfr_event) < 0)) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    // 3. add to socket
    SetAddedEpollFd((EventPoll *)event_poll, event->data);

    // 4. do rearm
    umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    int ret = ock::ubs::UmqApi::umq_rearm_interrupt(main_umq, false, &rx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for share jfr RX, "
                     "main umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return -1;
    }
    ret = ock::ubs::UmqApi::umq_rearm_interrupt(umq_handle_, false, &rx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for sub umq RX, "
                     "local umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return -1;
    }

    return 0;
}

int UmqSocket::GetTxFd()
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int tx_interrupt_fd = ock::ubs::UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (UNLIKELY(tx_interrupt_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, tx_interrupt_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get TX interrupt fd for GetTxFd, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), tx_interrupt_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, tx_interrupt_fd), savedErrno);
        return -1;
    }
    return tx_interrupt_fd;
}
int UmqSocket::AddQbuf(umq_buf_t *qbuf)
{
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("AddQbuf failed, fd: %d, reason: rxQueue is null\n", raw_socket_);
        return UBS_ERROR;
    }

    UmqBufferReceiveQueue::OpResult enqueue_ret = rxQueue->Enqueue(qbuf);
    if (enqueue_ret != UmqBufferReceiveQueue::OpResult::OK) {
        UBS_VLOG_ERR("AddQbuf failed, fd: %d, ret: %d\n", raw_socket_, static_cast<int>(enqueue_ret));
        return UBS_ERROR;
    }

    return UBS_OK;
}
int UmqSocket::GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size)
{
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("GetAndPopQbuf failed, rx queue is null, fd: %d, ret: %d\n", raw_socket_, -1);
        return -1;
    }

    uint32_t i = 0;
    while (i < max_buf_size) {
        UmqBufferReceiveQueue::OpResult ret = rxQueue->Dequeue(&buf[i]);
        if (ret == UmqBufferReceiveQueue::OpResult::OK) {
            i++;
        } else if (ret == UmqBufferReceiveQueue::OpResult::QUEUE_EMPTY) {
            break;
        } else {
            return i + 1;
        }
    }

    return i;
}
void UmqSocket::FlushRxQueue()
{
    if (rxQueue == nullptr) {
        return;
    }

    rxQueue->Shutdown();
    return;
}

Result UmqSocket::CheckDevAdd(const umq_eid_t &conn_eid)
{
    if (EidRegistry::Instance().IsRegisteredEid(conn_eid)) {
        return UBS_OK;
    }

    umq_trans_info_t trans_info;
    trans_info.trans_mode = UMQ_TRANS_MODE_UB;
    trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
    trans_info.dev_info.eid.eid = conn_eid;
    int ret = UmqApi::umq_dev_add(&trans_info);
    if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::ACCEPT, ret, savedErrno);
        UBS_VLOG_ERR("umq_dev_add() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::ACCEPT, ret), savedErrno);
        return UBS_UMQ_ERROR;
    }

    // TODO: AE 事件处理
#ifdef ENABLE
    ret = Context::GetContext()->RegisterAsyncEvent(trans_info);
    if (ret < 0) {
        UBS_VLOG_ERR("RegisterAsyncEvent() failed, conn eid:" EID_FMT ", ret: %d\n", EID_ARGS(conn_eid), ret);
        return ret;
    }
#endif

    EidRegistry::Instance().RegisterEid(conn_eid);
    return UBS_OK;
}

void UmqSocket::OutputStats(std::ostringstream &oss)
{
    stats_mgr_.OutputStats(raw_socket_, oss);
}

void UmqSocket::GetSocketFlowControlData(Statistics::CLIFlowControlData *data)
{
    UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
    data->createTime = static_cast<uint64_t>(duration.count());

    if (umq_stats_flow_control_get(umq_handle_, &(data->umqFlowControlStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq flow control info\n");
    }
}

void UmqSocket::GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data)
{
    UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
    data->createTime = static_cast<uint64_t>(duration.count());

    if (umq_stats_qbuf_pool_get(umq_handle_, &(data->umqQbufPoolStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq qbuf pool info\n");
    }
}

void UmqSocket::GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data)
{
    UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
    data->createTime = static_cast<uint64_t>(duration.count());

    if (umq_info_get(umq_handle_, &(data->umqInfo)) != 0) {
        UBS_VLOG_WARN("Failed to get umq info\n");
    }
}

void UmqSocket::GetSocketIoPacketData(Statistics::CLIIoPacketData *data)
{
    UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
    data->createTime = static_cast<uint64_t>(duration.count());

    if (umq_stats_io_get(umq_handle_, &(data->umqPacketStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq io packet stats\n");
    }
}

void UmqSocket::GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data)
{
    UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
    data->createTime = static_cast<uint64_t>(duration.count());

    if (UmqApi::umq_stats_perf_get(&(data->umqPerfStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq perf stats\n");
    }

    // urma 只统计 start 到 stop 之间的数据
    if (UmqApi::umq_stats_tp_perf_stop(UmqSetting::UMQ_TRANS_MODE) != 0) {
        UBS_VLOG_WARN("Failed to stop tp perf: umq_trans_mode=%d\n", UmqSetting::UMQ_TRANS_MODE);
        return;
    }

    memset(data->umqTpPerfBuf, 0, sizeof(data->umqTpPerfBuf));
    data->umqTpPerfLen = sizeof(data->umqTpPerfBuf);
    if (UmqApi::umq_stats_tp_perf_info_get(UmqSetting::UMQ_TRANS_MODE, data->umqTpPerfBuf, &(data->umqTpPerfLen)) !=
        0) {
        UBS_VLOG_WARN("Failed to get umq tp perf info\n");
        data->umqTpPerfLen = 0;
        data->umqTpPerfBuf[0] = '\0';
    }

    // 重新恢复 tp 统计
    if (UmqApi::umq_stats_tp_perf_start(UmqSetting::UMQ_TRANS_MODE) != 0) {
        UBS_VLOG_WARN("Failed to start tp perf: umq_trans_mode=%d\n", UmqSetting::UMQ_TRANS_MODE);
        return;
    }
}

void UmqSocket::GetSocketCLIData(Statistics::CLISocketData *data)
{
    stats_mgr_.GetSocketCLIData(data);

    if (IsClient()) {
        UmqConnectorOps *ops = (UmqConnectorOps *)(connector_->GetConnectorOps().Get());
        auto duration =
            std::chrono::duration_cast<std::chrono::seconds>(ops->umq_conn_info_.create_time.time_since_epoch());
        data->createTime = static_cast<uint64_t>(duration.count());

        strcpy(data->remoteIp, ops->umq_conn_info_.peer_ip.c_str());
        memcpy(data->localEid, ops->umq_conn_info_.conn_eid.raw, UMQ_EID_SIZE);
        memcpy(data->remoteEid, ops->umq_conn_info_.peer_eid.raw, UMQ_EID_SIZE);

    } else {
        UmqAcceptorOps *ops = (UmqAcceptorOps *)(acceptor_->GetAcceptorOps().Get());
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(ops->conn_info.create_time.time_since_epoch());
        data->createTime = static_cast<uint64_t>(duration.count());

        strcpy(data->remoteIp, ops->conn_info.peer_ip.c_str());
        memcpy(data->localEid, ops->umq_conn_info_.conn_eid.raw, UMQ_EID_SIZE);
        memcpy(data->remoteEid, ops->umq_conn_info_.peer_eid.raw, UMQ_EID_SIZE);
    }
}

} // namespace umq
} // namespace ubs
} // namespace ock