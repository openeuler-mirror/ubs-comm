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

#include "common/ubsocket_link_trace.h"
#include "core/ubsocket_bigdata.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "profiling/statistics/statistics.h"
#include "umq_conn_helper.h"
#include "umq_dfx_api.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_share_jfr_epoll_runner_ops.h"
#include "umq_socket.h"
#include "umq_socket_acceptor.h"
#include "umq_socket_connector.h"
#include "umq_tp_tx_epoll_runner_ops.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqSocket::Initialize() noexcept
{
    return UBS_OK;
}

void UmqSocket::UnInitialize() noexcept
{
    // bigdata state 不依赖 umq handle，先释放（handle 可能已失效但 state 仍存活，
    // 此时也必须清理，否则 state/ctx 泄漏）
    UbsBigdata::CleanupSocketState(this);
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        return;
    }
    // FC TX 事件的 DelEpollEvent 由 UnbindAndFlushRemoteUmq 内部统一处理，
    // 覆盖所有 3 个调用点 (UnInitialize / DoUbAcceptRetry / DoUbConnectRetry)
    // 析构路径使用更长的 flush 超时，覆盖 RNR 重试周期 (rnr_retry=6, err_timeout=2s => 12s)
    //
    // 延迟拆链：若本 socket 已经过 TxCqePoller 的 retire 流程（RetireStep 已 unbind 并在 poller
    // 线程上完成/超时 drain），此处不再重复 unbind、也不再同步忙轮询——那正是原先在 brpc worker
    // 上阻塞最长 15s 的部分。未 retire 的路径（如握手重试、poller 未启动）保持原行为。
    if (!retiring_.load(std::memory_order_acquire)) {
        UnbindAndFlushRemoteUmq(this, UMQ_DESTROY_FLUSH_TIMEOUT_MS);
    } else if (!retire_drained_) {
        // deadline 到期仍未 drain 完：与原实现超时分支等价（原实现同样在超时后继续 destroy），
        // 只是等待发生在 poller 线程而非 worker 上。此处不再等待。
        UBS_VLOG_DEBUG("retire drain hit deadline, proceeding to destroy, fd: %d\n", raw_socket_);
    }
    DestroyLocalUmq();

    rxQueue.reset();
}

bool UmqSocket::RetireStep() noexcept
{
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        retiring_.store(true, std::memory_order_release);
        retire_drained_ = true;
        return true;
    }
    /* 防御：ops 仅在握手成功（GenerateSocketCommOps）后接线；虽然 bind 成功蕴含 ops 已接线，
     * 但 retire 路径运行在独立线程、面对所有历史状态的 socket，这里不依赖该不变量。 */
    DataTxOps *tx_ops = GetTxOps();
    DataRxOps *rx_ops = GetRxOps();
    if (tx_ops == nullptr || rx_ops == nullptr) {
        retiring_.store(true, std::memory_order_release);
        retire_drained_ = true; /* 无数据面，无在途 WR；析构走 destroy 即可 */
        return true;
    }
    if (!retiring_.load(std::memory_order_acquire)) {
        // 首次：与 UnbindAndFlushRemoteUmq 的前半段一致（ack 中断、注销 FC 事件、unbind），
        // 但不进入长时 flush 循环——drain 改为下面的有界切片，跨多个 reaper 轮次完成。
        // CleanupSocketState 必须在 umq_unbind 之前调用：排空 pending_reads 并设置
        // destroying 标志，防止 RetryPendingReads 在 unbind 后对已释放的 handle 调用 umq_post。
        retiring_.store(true, std::memory_order_release);
        UbsBigdata::CleanupSocketState(this);
        if (umq_is_bind_remote_) {
            if (tx_ops->ack_event_num_ > 0) {
                umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
                UmqApi::umq_ack_interrupt(umq_handle_, tx_ops->ack_event_num_, &option);
                tx_ops->ack_event_num_ = 0;
            }
            if (rx_ops->ack_event_num_ > 0) {
                umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
                UmqApi::umq_ack_interrupt(umq_handle_, rx_ops->ack_event_num_, &option);
                rx_ops->ack_event_num_ = 0;
            }
            if (UmqSetting::UMQ_FLOW_CONTROL_ENABLE) {
                UnregisterFcTxEvent();
            }
            int ret = UmqApi::umq_unbind(umq_handle_);
            if (ret != UMQ_SUCCESS) {
                UBS_VLOG_ERR("[UMQ_API] umq_unbind() failed (retire), local umq: %llu, ret: %d\n",
                             static_cast<unsigned long long>(umq_handle_), ret);
            }
            umq_is_bind_remote_ = false;
        } else {
            // 从未绑定远端：无在途 WR，无需 drain
            retire_drained_ = true;
            return true;
        }
    }
    if (UmqSetting::UMQ_TP_TYPE == POOL) {
        /* POOL 模式：本 socket 是逻辑队列，其 TX CQ 就是所借 jetty node 的 CQ，而 node 与其他
         * 存活 socket 共享，且 unbind 后可被新链路再借。在 reaper 线程上对它 poll（哪怕只收一轮）
         * 就是与 RX runner 双线程无锁 poll 同一个共享 CQ，并按 umq_ctx 把别人的完成分发给别的
         * socket——现场表现为 server 段错误。POOL 模式下正确的做法是根本不 drain：
         * umq_destroy 会强制归还所借 node（析构路径注释："destroy is the only remaining
         * opportunity to return it"），残余 CQE 由 runner 上既有的 orphan sweep（flag=ON 的
         * SweepOrphanPools / flag=OFF 的 TP_TX_TIMER 轮询）单线程回收。因此 unbind 完即视为
         * "已 drain"，直接放行到 destroy——node 以 ioctl 速度而非硬件 flush 速度回池，对小池子
         * （现场 100）的建链反而最友好。 */
        retire_drained_ = true;
        return true;
    }
    // SINGLE 模式：队列独占自己的 jetty/CQ，无共享，reaper 单线程 drain 是安全且必要的
    // （destroy 前收完自己的 CQE，避免 URMA 资源泄漏）。drain-to-empty，首空即返，单次有界。
    tx_ops->ForceDrainTx(this);
    if (tx_ops->tx_queue_avail_num_.load(std::memory_order_acquire) >= GlobalSetting::UBS_TX_DEPTH) {
        // TX 已 drain；RX 队列/缓存最后清一次（share-JFR 下为清空本 socket 接收队列，非阻塞）
        GlobalSetting::UBS_ENABLE_SHARE_JFR ? FlushRxQueue() : rx_ops->FlushRx(this, 1);
        retire_drained_ = true;
        return true;
    }
    return false;
}

Result UmqSocket::CreateLocalUmq(const umq_eid_t *conn_eid, umq_used_ports_t &used_ports, umq_topo_type_t &topo_type)
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("Create umq on a created umq.\n");
        return UBS_ERROR;
    }
    topo_type_ = static_cast<uint8_t>(topo_type);

    umq_create_option_t queue_cfg;
    memset(&queue_cfg, 0, sizeof(queue_cfg));
    UmqConnHelper::NewBaseUmqCreateOptions(queue_cfg, GetTransMode());

    // 共享 JFR、AE 事件依赖 umq_ctx.
    queue_cfg.umq_ctx = raw_socket_;
    // TODO: is_bonding 待确认如何设置到 socketbase
    UBS_VLOG_DEBUG("UmqSetting::UMQ_IS_BONDING %b topo_type_ %d", UmqSetting::UMQ_IS_BONDING, topo_type_);
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
        queue_cfg.create_flag |= UMQ_CREATE_FLAG_USED_PORTS;
        queue_cfg.used_ports = used_ports;
        // 日志：打印 used_ports 内容，验证一主三备是否传入
        UBS_VLOG_DEBUG("CreateLocalUmq: used_ports.num=%u (expect 1 main + up to 3 backup)\n", used_ports.num);
        for (uint32_t i = 0; i < used_ports.num; ++i) {
            UBS_VLOG_DEBUG("used_ports[%u]: src_port(chip=%u,die=%u,port=%u)\n", i, used_ports.port[i].bs.chip_id,
                           used_ports.port[i].bs.die_id, used_ports.port[i].bs.port_idx);
        }
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
        errno = ENOTSUP;
        return UBS_SET_DEV_INFO | UBS_DEGRADABLE_MASK; // 让上层走降级
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

        if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            queue_cfg.dev_info.dev.eid_idx = UmqSetting::UMQ_EID_INDEX;
            local_eid = UmqSetting::UMQ_LOCAL_EID;
            UBS_VLOG_DEBUG("Use Bonding: " EID_FMT ".\n", EID_ARGS(UmqSetting::UMQ_LOCAL_EID));
        } else {
            // init use bonding dev
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
            local_eid = *conn_eid;
            UBS_VLOG_DEBUG("Use UDMA: " EID_FMT ".\n", EID_ARGS(*conn_eid));
        }
    } else {
        if (strcpy(queue_cfg.dev_info.dev.dev_name, "bonding_dev_0") == nullptr) {
            UBS_VLOG_ERR("Failed to strcpy device name, errno: %d\n", errno);
            return UBS_SET_DEV_INFO;
        }
        if (UmqSetting::UMQ_IS_BONDING) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
            local_eid = *conn_eid;
        }
    }

    // 资源创建分界点:CreateSubUmq 内部调用 umq_create,从这里才开始分配真实的 UMQ 硬件资源
    // (queue/jetty/JFR/JFC)。此前的代码只是在填充 queue_cfg 配置 + 本地校验,无资源可清理,失败直接 return;
    // 此处成功后 umq_handle_ 变为有效 handle,后续任何失败路径都必须 DestroyLocalUmq() 释放,否则硬件 handle 泄露。
    
    umq_handle_ = CreateSubUmq(&queue_cfg, &local_eid);
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, savedErrno);
        UBS_VLOG_ERR("CreateSubUmq() failed, ret: %llu, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(umq_handle_), errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CREATE, UMQ_FAIL), savedErrno);
        return UBS_UMQ_CREATE | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    }
    Result fcRet = RegisterFcTxEvent();
    if (fcRet != UBS_OK) {
        UBS_VLOG_ERR("RegisterFcTxEvent() failed, ret: %llu, fd: %d\n", static_cast<unsigned long long>(fcRet),
                     raw_socket_);
        // umq_handle_ 已创建，销毁避免硬件 handle 泄露（UnregisterFcTxEvent 兜底清理半程注册的 FC 事件）
        DestroyLocalUmq();
        return UBS_ERROR | UBS_DEGRADABLE_MASK;
    }
    // 防御 rxQueue 指针覆盖泄露(泄漏点 D):DoUbConnectRetry 二次 CreateLocalUmq 时,
    // 旧 rxQueue 仍在(UnbindAndFlushRemoteUmq/DestroyLocalUmq 不删 rxQueue),此处先释放再 new
    rxQueue.reset(new (std::nothrow) UmqBufferReceiveQueue());
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("Failed to init share jfr rx queue for fd: %d \n", raw_socket_);
        // umq_handle_ 已创建，销毁避免硬件 handle 泄露
        DestroyLocalUmq();
        return UBS_INIT_SHARED_JFR_RX_QUEUE;
    }

    if (UmqSetting::UMQ_TP_TYPE == SINGLE) {
        // 总是使能 TX solicited，这会导致对端 JFR 只有接收到 solicited_enable=true 的包时才会产生中断。而在客
        // 户端本端，开启此功能后不会在 TX 上产生中断，无法通过 `epoll_wait` 唤醒，必须定期 poll cq
        umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
        PROF_START(UMQ_INTERRUPT_FD_GET);
        int tx_interrupt_fd = UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
        if (tx_interrupt_fd < 0) {
            PROF_END(UMQ_INTERRUPT_FD_GET, false);
            UBS_VLOG_ERR("[UMQ_API] Failed to get TX interrupt fd, local umq: %llu\n",
                         static_cast<unsigned long long>(umq_handle_));
            // umq_handle_ 已创建，销毁避免硬件 handle 泄露
            DestroyLocalUmq();
            return UBS_ERROR;
        }
        PROF_END(UMQ_INTERRUPT_FD_GET, true);

        PROF_START(UMQ_REARM_INTERRUPT);
        int ret = ock::ubs::UmqApi::umq_rearm_interrupt(umq_handle_, true, &tx_option);
        if (ret < 0) {
            PROF_END(UMQ_REARM_INTERRUPT, false);
            UBS_VLOG_ERR("[UMQ_API] Failed to enable solicited mode for umq: %llu\n",
                         static_cast<unsigned long long>(umq_handle_));
            // umq_handle_ 已创建，销毁避免硬件 handle 泄露
            DestroyLocalUmq();
            return UBS_ERROR;
        }
        PROF_END(UMQ_REARM_INTERRUPT, true);
    }

    // 保存 used_ports, 之后的遇到异常 CQE 时可以确定这些 port 都不可用（光组网）
    // num==0（非光组网常态）不再分配任何东西——原实现此处会做一次 0 长度堆分配
    if (used_ports.num > 0) {
        UmqSocketCold *cold = GetOrCreateCold();
        if (cold == nullptr) {
            UBS_VLOG_ERR("Failed to init used_ports for fd: %d\n", raw_socket_);
            return UBS_ERROR;
        }
        auto *ports = new (std::nothrow) umq_port_id_t[used_ports.num];
        if (ports == nullptr) {
            UBS_VLOG_ERR("Failed to init used_ports for fd: %d\n", raw_socket_);
            return UBS_ERROR;
        }
        std::copy_n(used_ports.port, used_ports.num, ports);
        cold->used_ports.reset(ports);
        cold->used_ports_num = used_ports.num;
    }
    return UBS_OK;
}

std::tuple<const umq_port_id_t *, std::size_t> UmqSocket::GetUsedPorts() const
{
    const UmqSocketCold *cold = GetCold();
    if (cold == nullptr) {
        return {nullptr, 0};
    }
    return {cold->used_ports.get(), cold->used_ports_num};
}

UmqSocketCold *UmqSocket::GetOrCreateCold() noexcept
{
    SocketExt *ext = EnsureExt();
    if (ext == nullptr) {
        return nullptr;
    }
    void *cold = ext->cold.load(std::memory_order_acquire);
    if (cold != nullptr) {
        return static_cast<UmqSocketCold *>(cold);
    }
    auto *candidate = new (std::nothrow) UmqSocketCold;
    if (candidate == nullptr) {
        return nullptr;
    }
    /* CAS 安装，败者回收自己的副本。不用 std::call_once（#10 结案）。 */
    if (!ext->cold.compare_exchange_strong(cold, candidate, std::memory_order_release, std::memory_order_acquire)) {
        delete candidate;
        return static_cast<UmqSocketCold *>(cold);
    }
    return candidate;
}

uint64_t UmqSocket::CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        PROF_START(UMQ_CREATE);
        uint64_t sub_umq = UmqApi::umq_create(cfg);
        PROF_END(UMQ_CREATE, sub_umq != UMQ_INVALID_HANDLE);
        return sub_umq;
    }
    UBS_VLOG_DEBUG("UBS_ENABLE_SHARE_JFR = true \n");
    uint64_t main_umq = GetOrCreateMainUmq(cfg, local_eid);
    if (main_umq == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("GetOrCreateMainUmq() failed, ret: %llu\n", static_cast<unsigned long long>(main_umq));
        return UMQ_INVALID_HANDLE;
    }

    if (UmqSetting::UMQ_TP_TYPE == POOL) {
        // 池化：创建逻辑umq
        cfg->create_flag |= UMQ_CREATE_FLAG_SHARE_RQ;
    } else {
        cfg->create_flag |= UMQ_CREATE_FLAG_SHARE_RQ | UMQ_CREATE_FLAG_SUB_UMQ;
    }
    cfg->share_rq_umqh = main_umq;
    cfg->umq_ctx = (uint64_t)raw_socket_;
    PROF_START(UMQ_CREATE);
    uint64_t sub_umq = UmqApi::umq_create(cfg);
    if (sub_umq == UMQ_INVALID_HANDLE) {
        PROF_END(UMQ_CREATE, false);
        UBS_VLOG_ERR("[UMQ_API] umq_create() failed for sub umq, ret: %llu\n",
                     static_cast<unsigned long long>(sub_umq));
        return UMQ_INVALID_HANDLE;
    }
    PROF_END(UMQ_CREATE, true);

    share_umq_handle_ = main_umq;
    return sub_umq;
}

uint64_t UmqSocket::GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid)
{
    std::vector<std::shared_ptr<MainUmqState>> main_umqs;
    if (UmqEidTable::Instance().Get(*localEid, GetTransMode(), main_umqs)) {
        if (main_umqs.empty()) {
            UBS_VLOG_ERR("Main umq list is empty, local eid:" EID_FMT ", ret: %llu\n", EID_ARGS(*localEid),
                         static_cast<unsigned long long>(UMQ_INVALID_HANDLE));
            return UMQ_INVALID_HANDLE;
        }
        return main_umqs.front()->GetUmqHandle();
    }

    umq_create_option_t cfg_main;
    memcpy(&cfg_main, cfg, sizeof(*cfg));
    cfg_main.create_flag |= UMQ_CREATE_FLAG_MAIN_UMQ;
    PROF_START(UMQ_CREATE);
    uint64_t new_umq = UmqApi::umq_create(&cfg_main);
    if (new_umq == UMQ_INVALID_HANDLE) {
        PROF_END(UMQ_CREATE, false);
        return UMQ_INVALID_HANDLE;
    }
    PROF_END(UMQ_CREATE, true);

    {
        Locker sLock(UmqEidTable::Instance().GetMainMutex());
        if (UmqEidTable::Instance().Get(*localEid, GetTransMode(), main_umqs)) {
            // Another thread won the race — use its handle, discard ours.
            UmqApi::umq_destroy(new_umq);
            return main_umqs.front()->GetUmqHandle();
        }
        UmqEidTable::Instance().Add(*localEid, GetTransMode(), new_umq);
        return new_umq;
    }
}

Result UmqSocket::UpdateRxQueueAvailNum()
{
    PROF_START(UMQ_STATE_GET);
    int local_umq_state = UmqApi::umq_state_get(umq_handle_);
    if (local_umq_state != QUEUE_STATE_READY) {
        PROF_END(UMQ_STATE_GET, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::GET_STATE, local_umq_state, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_state_get() failed to reach ready, "
                     "state: %d, mapped errno: %d(%s), original errno: %d\n",
                     local_umq_state, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::GET_STATE, local_umq_state), savedErrno);
        return UBS_ERROR;
    }
    PROF_END(UMQ_STATE_GET, true);

    /* 同上：条目可能已不归本 socket（fd 复用窗口），此处曾是 fork#21 第三份
     * core 的崩溃点（umq_socket.cpp:410 空指针成员写）。 */
    DataRxOps *rx_ops = GetRxOps();
    if (UNLIKELY(rx_ops == nullptr)) {
        UBS_VLOG_ERR("data-plane entry not owned by socket, fd: %d, skip rx avail init\n", raw_socket_);
        return UBS_ERROR;
    }
    rx_ops->rx_queue_avail_num_ = GlobalSetting::UBS_RX_DEPTH;
    return 0;
}

void UmqSocket::UnbindAndFlushRemoteUmq(Socket *sock, uint32_t flush_timeout_ms)
{
    if (!umq_is_bind_remote_) {
        return;
    }
    if (UNLIKELY(GlobalSetting::IsExiting())) {
        /* 退出期 umq poll 恒返回 0（umq_pro_ub.c 的 exiting 守卫），FlushTx/FlushRx
         * 的等待不可能推进，只会把超时全额烧完（每链最长 flush_timeout_ms，
         * 4 万链串行 = fork#28 的 StopReaper 卡住）。清零超时：unbind/ack/destroy
         * 全部照常执行，只去掉等待。 */
        flush_timeout_ms = 0;
    }

    // CleanupSocketState 必须在 umq_unbind 之前调用：排空 pending_reads 并设置
    // destroying 标志，防止 RetryPendingReads 在 unbind 后对已释放的 handle 调用 umq_post。
    UbsBigdata::CleanupSocketState(static_cast<UmqSocket *>(sock));

    /* 拆除路径必须容忍"数据面条目已不归本 socket"：GetTxOps/GetRxOps 在
     * owner != this（fd 复用窗口被接管）或条目从未构造时返回 nullptr。
     * 原实现在本函数里 8 次无保护解引用，现场 core 即 FlushTx(this=0x0)
     * 与 424 行的 ack_event_num_ 读取（fork#24，reaper 线程）。
     * 空 ops 只跳过依赖 ops 的清理，unbind 本身仍须完成。 */
    DataTxOps *tx_ops = GetTxOps();
    DataRxOps *rx_ops = GetRxOps();
    if (UNLIKELY(tx_ops == nullptr || rx_ops == nullptr)) {
        UBS_VLOG_WARN("data-plane entry not owned by socket during teardown, fd: %d, tx: %p, rx: %p; "
                     "unbinding without ops cleanup\n",
                     raw_socket_, static_cast<void *>(tx_ops), static_cast<void *>(rx_ops));
    }

    if (tx_ops != nullptr && tx_ops->ack_event_num_ > 0) {
        umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
        UmqApi::umq_ack_interrupt(umq_handle_, tx_ops->ack_event_num_, &option);
        tx_ops->ack_event_num_ = 0;
    }

    if (rx_ops != nullptr && rx_ops->ack_event_num_ > 0) {
        umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
        UmqApi::umq_ack_interrupt(umq_handle_, rx_ops->ack_event_num_, &option);
        rx_ops->ack_event_num_ = 0;
    }

    // 先从 TX poller 注销 FC TX 事件（标记 umq_handle 无效），防止 in-flight poll 与 umq_destroy 并发
    // 覆盖所有 3 个调用点: UnInitialize / DoUbAcceptRetry / DoUbConnectRetry
    if (UmqSetting::UMQ_FLOW_CONTROL_ENABLE) {
        UnregisterFcTxEvent();
    }
    int ret = UmqApi::umq_unbind(umq_handle_);
    if (ret != UMQ_SUCCESS) {
        UBS_VLOG_ERR("[UMQ_API] umq_unbind() failed, local umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(umq_handle_), ret);
    }
    umq_is_bind_remote_ = false;
    if (tx_ops != nullptr) {
        tx_ops->FlushTx(sock, flush_timeout_ms);
    }

    if (GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        FlushRxQueue();
    } else if (rx_ops != nullptr) {
        rx_ops->FlushRx(sock, flush_timeout_ms);
    }
}

void UmqSocket::DestroyLocalUmq()
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        // 先从 TX poller 注销 FC TX 事件（标记 umq_handle 无效），防止 in-flight poll 与 umq_destroy 并发
        // 覆盖所有 3 个调用点: UnInitialize / DoUbAcceptRetry / DoUbConnectRetry
        // bind 失败路径在此兜底
        if (UmqSetting::UMQ_FLOW_CONTROL_ENABLE) {
            UnregisterFcTxEvent();
        }
        // umq_destroy 在 jetty 仍 BUSY (in-flight WR 未完成) 时会失败。
        // 析构路径已通过 UnbindAndFlushRemoteUmq 用长超时 drain TX/RX，但
        // 硬件 RNR 重试可能仍在进行。此处重试给额外时间窗口让 jetty 转为 IDLE，
        // 避免 umq_destroy 失败导致 URMA 资源 (jetty/JFR/JFC/context) 泄漏 → 退出段错误。
        int ret = UMQ_FAIL;
        for (int i = 0; i < UMQ_DESTROY_MAX_RETRIES; ++i) {
            ret = UmqApi::umq_destroy(umq_handle_);
            if (ret == UMQ_SUCCESS) {
                break;
            }
            UBS_VLOG_ERR("umq_destroy() retry %d/%d failed, local umq: %llu, ret: %d\n", i + 1, UMQ_DESTROY_MAX_RETRIES,
                         static_cast<unsigned long long>(umq_handle_), ret);
            if (i < UMQ_DESTROY_MAX_RETRIES - 1) {
                usleep(UMQ_DESTROY_RETRY_INTERVAL_US);
            }
        }
        if (ret != UMQ_SUCCESS) {
            UBS_VLOG_ERR("umq_destroy() failed after %d retries, local umq: %llu, ret: %d, "
                         "URMA resources will leak\n",
                         UMQ_DESTROY_MAX_RETRIES, static_cast<unsigned long long>(umq_handle_), ret);
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
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        return 0;
    }
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
        ock::ubs::UmqApi::umq_uninit();
        return -1;
    }
    return 0;
}

bool UmqSocket::ShouldRegisterTxEvent()
{
    return UmqSetting::UMQ_TP_TYPE == SINGLE && umq_handle_ != UMQ_INVALID_HANDLE;
}

Result UmqSocket::ProcessEpollEvent(struct epoll_event &event)
{
    auto event_data = (EpollEvent *)event.data.ptr;
    if (event_data->event_type == EPOLL_EVENT_UB_SOCKET_OUT) {
        NewTxEpollIn();
    }
    return UBS_OK;
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

void UmqSocket::OnRnrEnter() noexcept
{
    /* 首次进入反压时惰性创建冷侧构；分配失败则本轮统计丢弃（反压功能本身不受影响，
     * rnr_blocked_ 仍在热侧），下次进入重试。 */
    UmqSocketCold *cold = GetOrCreateCold();
    if (cold == nullptr) {
        return;
    }
    cold->rnr_enter_cnt.fetch_add(1, std::memory_order_relaxed);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs(), std::memory_order_relaxed);
}

void UmqSocket::OnRnrRecover() noexcept
{
    UmqSocketCold *cold = GetCold();
    if (cold == nullptr) {
        /* OnRnrEnter 分配失败的对偶路径：无起点时间戳，无统计可记 */
        return;
    }
    cold->rnr_recover_cnt.fetch_add(1, std::memory_order_relaxed);
    const uint64_t start_ns = cold->rnr_block_start_ns.load(std::memory_order_relaxed);
    if (start_ns == 0) {
        return;
    }
    const uint64_t now_ns = ubsocket_get_timeNs();
    const uint64_t latency_us = (now_ns > start_ns) ? (now_ns - start_ns) / 1000 : 0;

    cold->rnr_latency_sum_us.fetch_add(latency_us, std::memory_order_relaxed);
    uint64_t min_us = cold->rnr_latency_min_us.load(std::memory_order_relaxed);
    while ((min_us == 0 || latency_us < min_us) &&
           !cold->rnr_latency_min_us.compare_exchange_weak(min_us, latency_us, std::memory_order_relaxed)) {}
    uint64_t max_us = cold->rnr_latency_max_us.load(std::memory_order_relaxed);
    while (latency_us > max_us &&
           !cold->rnr_latency_max_us.compare_exchange_weak(max_us, latency_us, std::memory_order_relaxed)) {}
    const uint64_t sample_cnt = cold->rnr_latency_cnt.fetch_add(1, std::memory_order_relaxed) + 1;
    if (sample_cnt >= UMQ_RNR_LATENCY_PRINT_CNT) {
        const uint64_t sum_us = cold->rnr_latency_sum_us.load(std::memory_order_relaxed);
        UBS_VLOG_WARN("RNR latency stats, socket fd: %d, enter: %llu, recover: %llu, timeout: %llu, "
                      "samples: %llu, avg_us: %llu, min_us: %llu, max_us: %llu\n",
                      raw_socket_, static_cast<unsigned long long>(cold->rnr_enter_cnt.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(cold->rnr_recover_cnt.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(cold->rnr_timeout_cnt.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(sample_cnt), static_cast<unsigned long long>(sum_us / sample_cnt),
                      static_cast<unsigned long long>(cold->rnr_latency_min_us.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(cold->rnr_latency_max_us.load(std::memory_order_relaxed)));
        cold->rnr_latency_sum_us.store(0, std::memory_order_relaxed);
        cold->rnr_latency_cnt.store(0, std::memory_order_relaxed);
        cold->rnr_latency_min_us.store(0, std::memory_order_relaxed);
        cold->rnr_latency_max_us.store(0, std::memory_order_relaxed);
        cold->rnr_block_start_ns.store(0, std::memory_order_relaxed);
    }
}

void UmqSocket::OnRnrTimeout() noexcept
{
    UmqSocketCold *cold = GetCold();
    if (cold == nullptr) {
        return;
    }
    cold->rnr_timeout_cnt.fetch_add(1, std::memory_order_relaxed);
}

bool UmqSocket::IsRnrBlockFatal() const noexcept
{
    if (!IsRnrBlocked()) {
        return false;
    }
    const uint32_t timeout_ms = GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS;
    if (timeout_ms == 0) {
        return false;
    }
    /* 时间戳 rnr_block_start_ns 仅在 first_entry 由 OnRnrEnter 设置，反复通知
     * 不刷新——保证持续反压期间 elapsed 只增不减，最终触发超时。
     * 冷侧构缺失（OnRnrEnter 分配失败）等价于无起点时间戳：不判超时。 */
    const UmqSocketCold *cold = GetCold();
    if (cold == nullptr) {
        return false;
    }
    const uint64_t start_ns = cold->rnr_block_start_ns.load(std::memory_order_relaxed);
    if (start_ns == 0) {
        return false;
    }
    const uint64_t now_ns = ubsocket_get_timeNs();
    const uint64_t elapsed_ms = (now_ns > start_ns) ? (now_ns - start_ns) / 1000000 : 0;
    return elapsed_ms >= timeout_ms;
}

bool UmqSocket::TryRnrBlockFatal() noexcept
{
    if (!IsRnrBlockFatal()) {
        return false;
    }
    bool expected = true;
    if (!rnr_blocked_.compare_exchange_strong(expected, false, std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return false;
    }
    OnRnrTimeout();
    return true;
}

u_mutex_t *UmqSocket::RxEnqueueLockFor(int fd) noexcept
{
    /* Created on first use (LockRegistry ops may be registered by the host
     * after static init), published once; the table lives for the process. */
    static std::atomic<u_mutex_t **> table{nullptr};
    u_mutex_t **t = table.load(std::memory_order_acquire);
    if (UNLIKELY(t == nullptr)) {
        auto *fresh = new u_mutex_t *[RX_ENQ_LOCK_STRIPES];
        for (uint32_t i = 0; i < RX_ENQ_LOCK_STRIPES; ++i) {
            fresh[i] = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        }
        u_mutex_t **expected = nullptr;
        if (table.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
            t = fresh;
        } else {
            for (uint32_t i = 0; i < RX_ENQ_LOCK_STRIPES; ++i) {
                LockRegistry::LOCK_OPS.destroy(fresh[i]);
            }
            delete[] fresh;
            t = expected;
        }
    }
    return t[static_cast<uint32_t>(fd) % RX_ENQ_LOCK_STRIPES];
}

int UmqSocket::AddQbuf(umq_buf_t *qbuf)
{
    if (rxQueue == nullptr) {
        UBS_VLOG_ERR("AddQbuf failed, fd: %d, reason: rxQueue is null\n", raw_socket_);
        if (qbuf != nullptr) {
            int savedErrno = errno;
            UmqApi::umq_buf_free(qbuf);
            errno = savedErrno;
        }
        return UBS_ERROR;
    }

    /* The RX queue is SPSC; the bigdata READ completion path can call AddQbuf
     * from both the share-JFR runner thread and the TxCqePoller thread, so
     * serialise the producer side with the registered lock (bthread::Mutex
     * under brpc). The lock exists only in share-JFR mode — with share JFR
     * off, AddQbuf has no callers (bigdata's receiver entry lives in the
     * share-JFR runner), so a null lock means single-producer by contract. */
    UmqBufferReceiveQueue::OpResult enqueue_ret;
    if (GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        Locker lk(RxEnqueueLockFor(raw_socket_));
        enqueue_ret = rxQueue->Enqueue(qbuf);
    } else {
        enqueue_ret = rxQueue->Enqueue(qbuf);
    }
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
        /* errno 契约：ubs_poll 对本函数的 <0 返回按"errno 已设置"透传给 brpc；
         * 裸 -1 会把线程残留 errno（可能为 0）一路送进 Controller::SetFailed 的
         * CHECK(error_code != 0)——0830 现网 753×753 全进程 abort 即此路（issue#33）。
         * 语义取 EAGAIN：已注册未就绪（建链完成前的暂态窗口）= 暂不可读，
         * 与既有契约测试 PollRegisteredSocketWithoutQueueReturnsEagain 一致——
         * 此前该语义靠入口 MSG_PEEK 的 EAGAIN 残留碰巧兑现，残留非 EAGAIN 即事故 */
        errno = EAGAIN;
        return -1;
    }

    uint32_t dequeued_count = 0;
    UmqBufferReceiveQueue::OpResult ret = rxQueue->DequeueBatch(buf, max_buf_size, &dequeued_count);
    if (ret == UmqBufferReceiveQueue::OpResult::OK) {
        return dequeued_count;
    }
    UBS_VLOG_ERR("GetAndPopQbuf failed, fd: %d, ret: %d\n", raw_socket_, static_cast<int>(ret));
    errno = std::abs(static_cast<int>(ret));
    if (errno == 0) { /* 同上契约：任何 <0 返回都必须带非零 errno */
        errno = EIO;
    }
    return UBS_ERROR;
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
    PROF_START(UMQ_DEV_ADD);
    int ret = UmqApi::umq_dev_add(&trans_info);
    if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
        int savedErrno = errno;
        PROF_END(UMQ_DEV_ADD, false);
        errno = UmqErrnoConverter::Convert(UmqOperation::ACCEPT, ret, savedErrno);
        UBS_VLOG_ERR("umq_dev_add() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::ACCEPT, ret), savedErrno);
        return UBS_UMQ_ERROR;
    }
    PROF_END(UMQ_DEV_ADD, true);

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
    if (GetStatsMgr() != nullptr) {
        GetStatsMgr()->OutputStats(raw_socket_, oss);
    }
}

void UmqSocket::GetSocketFlowControlData(Statistics::CLIFlowControlData *data)
{
    data->createTime = CreateTimeSec();

    if (umq_stats_flow_control_get(umq_handle_, &(data->umqFlowControlStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq flow control info\n");
    }
}

void UmqSocket::GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data)
{
    data->createTime = CreateTimeSec();

    if (umq_stats_qbuf_pool_get(umq_handle_, &(data->umqQbufPoolStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq qbuf pool info\n");
    }
}

void UmqSocket::GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data)
{
    data->createTime = CreateTimeSec();

    if (umq_info_get(umq_handle_, &(data->umqInfo)) != 0) {
        UBS_VLOG_WARN("Failed to get umq info\n");
    }
}

void UmqSocket::GetSocketIoPacketData(Statistics::CLIIoPacketData *data)
{
    data->createTime = CreateTimeSec();

    if (umq_stats_io_get(umq_handle_, &(data->umqPacketStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq io packet stats\n");
    }
}

void UmqSocket::GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data)
{
    data->createTime = CreateTimeSec();

    if (UmqApi::umq_stats_perf_get(&(data->umqPerfStat)) != 0) {
        UBS_VLOG_WARN("Failed to get umq perf stats\n");
    }

    // urma 只统计 start 到 stop 之间的数据
    {
        std::lock_guard<std::mutex> lock(Statistics::StatsMgr::gTpPerfSeqMutex);
        if (UmqApi::umq_stats_tp_perf_stop(UmqSetting::UMQ_TRANS_MODE) != 0) {
            UBS_VLOG_WARN("Failed to stop tp perf: umq_trans_mode=%d\n", UmqSetting::UMQ_TRANS_MODE);
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
        }
    }
}

void UmqSocket::GetSocketCLIData(Statistics::CLISocketData *data)
{
    if (GetStatsMgr() != nullptr) {
        GetStatsMgr()->GetSocketCLIData(data);
    }

    const bool use_bonding_eid = GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP;

    /* 身份信息统一读取 socket 本体（conn_info_ + PeerEidTable 共享快照），
     * 不再区分角色回查协商 ops——ops 已随建链完成释放 */
    data->createTime = CreateTimeSec();

    PeerIpStr(data->remoteIp, sizeof(data->remoteIp));
    const SocketExt *cli_ext = ExtOrNull();
    const PeerEidTable::Entry *eids =
        cli_ext != nullptr ? static_cast<PeerEidTable::Entry *>(cli_ext->eids_entry.load(std::memory_order_acquire))
                           : nullptr;
    if (eids == nullptr) {
        /* 建链未完成或条目分配失败：与旧实现零初始化快照的显示一致 */
        memset(data->localEid, 0, UMQ_EID_SIZE);
        memset(data->remoteEid, 0, UMQ_EID_SIZE);
    } else if (use_bonding_eid) {
        memcpy(data->localEid, eids->bonding_eid.raw, UMQ_EID_SIZE);
        memcpy(data->remoteEid, eids->peer_bonding_eid.raw, UMQ_EID_SIZE);
    } else {
        memcpy(data->localEid, eids->conn_eid.raw, UMQ_EID_SIZE);
        memcpy(data->remoteEid, eids->peer_eid.raw, UMQ_EID_SIZE);
    }
}

void UmqSocket::OnEstablished()
{
    /* 收拢 EID 快照并释放协商 ops。谁协商、从谁收拢：服务端走 acceptor ops，
     * 客户端走 connector ops；另一角色的 ops 建链后同样无人读取，一并释放。 */
    HandshakeCtx *hs = Hs();
    if (hs == nullptr) {
        /* 重复调用/异常路径防御：上下文已释放，无可收拢 */
        return;
    }
    if (IsClient()) {
        if (hs->connector != nullptr) {
            auto *cops = static_cast<UmqConnectorOps *>(hs->connector->GetConnectorOps().Get());
            if (cops != nullptr) {
                /* 客户端身份信息此前仅存于 connector ops，此处落到 conn_info_ */
                if (RawConnInfoV4 *ci = MutableConnInfo()) {
                    ci->SetPeerIp(cops->umq_conn_info_.peer_ip);
                    ci->peer_fd = cops->umq_conn_info_.peer_fd;
                    ci->create_time_sec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                                    cops->umq_conn_info_.create_time.time_since_epoch())
                                                                    .count());
                }
                if (SocketExt *sx = EnsureExt()) {
                    sx->eids_entry.store(PeerEidTable::Instance().Acquire(
                                             cops->umq_conn_info_.conn_eid, cops->umq_conn_info_.peer_eid,
                                             cops->umq_conn_info_.bonding_eid, cops->umq_conn_info_.peer_bonding_eid),
                                         std::memory_order_release);
                }
                hs->connector->ReleaseOps();
            }
        }
    } else {
        auto *aops = static_cast<UmqAcceptorOps *>(hs->acceptor.GetAcceptorOps().Get());
        if (aops != nullptr) {
            /* peer_ip/create_time 已由 DoAccept 直接写入 conn_info_，仅需收拢 EID */
            if (SocketExt *sx = EnsureExt()) {
                sx->eids_entry.store(PeerEidTable::Instance().Acquire(
                                         aops->umq_conn_info_.conn_eid, aops->umq_conn_info_.peer_eid,
                                         aops->umq_conn_info_.bonding_eid, aops->umq_conn_info_.peer_bonding_eid),
                                     std::memory_order_release);
            }
        }
    }
    /* 稳态链路不再保留握手期上下文：acceptor 壳、connector 及其 ops 一并释放。
     * 本钩子由 acceptor/connector 的建链收尾单线程调用一次；此后 Accept/Connect
     * 对本 fd 的误用由 hs_ 判空拦截（EINVAL，与旧语义等价）。 */
    if (SocketExt *sx = ExtOrNull()) {
        delete static_cast<HandshakeCtx *>(sx->hs);
        sx->hs = nullptr;
    }
}

DataPlaneTable &DataPlaneTable::Instance()
{
    /* 函数级静态（__cxa_guard），非 std::call_once——bthread 栈上安全（#10 结案） */
    static DataPlaneTable table;
    return table;
}

DataPlaneEntry *DataPlaneTable::SlotFor(int fd)
{
    if (fd < 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(fd);
    const std::size_t pi = idx >> PAGE_SHIFT;
    if (pi >= MAX_PAGES) {
        return nullptr;
    }
    Page *page = pages_[pi].load(std::memory_order_acquire);
    if (page == nullptr) {
        auto *candidate = new (std::nothrow) Page();
        if (candidate == nullptr) {
            return nullptr;
        }
        if (!pages_[pi].compare_exchange_strong(page, candidate, std::memory_order_release,
                                                std::memory_order_acquire)) {
            delete candidate; /* 败者回收；page 已被胜者填充 */
        } else {
            page = candidate;
        }
    }
    return reinterpret_cast<DataPlaneEntry *>(page->raw + (idx & (PAGE_SIZE - 1)) * sizeof(DataPlaneEntry));
}

DataPlaneEntry *DataPlaneTable::Peek(int fd) const
{
    if (fd < 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(fd);
    const std::size_t pi = idx >> PAGE_SHIFT;
    if (pi >= MAX_PAGES) {
        return nullptr;
    }
    Page *page = pages_[pi].load(std::memory_order_acquire);
    if (page == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<DataPlaneEntry *>(page->raw + (idx & (PAGE_SIZE - 1)) * sizeof(DataPlaneEntry));
}

void DataPlaneTable::DestroyEntry(DataPlaneEntry *e)
{
    e->~DataPlaneEntry();
    /* 原始存储约定：owner 槽清零 = "未构造"。析构后按字节写回，供下一次
     * SlotFor/Peek 的 owner 判定使用。 */
    memset(reinterpret_cast<unsigned char *>(e) + offsetof(DataPlaneEntry, owner), 0, sizeof(UmqSocket *));
}

PeerEidTable &PeerEidTable::Instance()
{
    /* 函数级静态：初始化用 __cxa_guard（futex），不经 std::call_once 的
     * TLS 传参机制，bthread 栈上安全（#10 结案的排除范围）——因此不能直接
     * 继承 LeakySingleton 模板（其 Instance 走 std::call_once）。
     * 故意堆分配且永不 delete（等效 LeakySingleton 语义）：TxCqePoller 的
     * reaper 线程在 main 返回后的 static destruction 阶段仍会经
     * ~UmqSocket → Release() 触达本表；Meyers 单例会随 __cxa_atexit 析构，
     * 分片 unordered_map 释放桶数组/节点后，reaper 的 find() 即
     * use-after-free（退出期 coredump，栈顶 _Hashtable::_M_find_before_node）。 */
    static PeerEidTable *table = new PeerEidTable();
    return *table;
}

PeerEidTable::Key PeerEidTable::MakeKey(const Entry &e)
{
    Key k;
    memcpy(k.raw, e.conn_eid.raw, UMQ_EID_SIZE);
    memcpy(k.raw + UMQ_EID_SIZE, e.peer_eid.raw, UMQ_EID_SIZE);
    memcpy(k.raw + UMQ_EID_SIZE * 2, e.bonding_eid.raw, UMQ_EID_SIZE);
    memcpy(k.raw + UMQ_EID_SIZE * 3, e.peer_bonding_eid.raw, UMQ_EID_SIZE);
    return k;
}

PeerEidTable::Entry *PeerEidTable::Acquire(const umq_eid_t &conn, const umq_eid_t &peer, const umq_eid_t &bonding,
                                           const umq_eid_t &peer_bonding)
{
    Entry probe;
    probe.conn_eid = conn;
    probe.peer_eid = peer;
    probe.bonding_eid = bonding;
    probe.peer_bonding_eid = peer_bonding;
    const Key key = MakeKey(probe);
    Stripe &stripe = stripes_[KeyHash{}(key) % STRIPE_NUM];

    std::lock_guard<std::mutex> guard(stripe.mtx);
    auto it = stripe.map.find(key);
    if (it == stripe.map.end()) {
        auto entry = std::unique_ptr<Entry>(new (std::nothrow) Entry(probe));
        if (entry == nullptr) {
            return nullptr;
        }
        it = stripe.map.emplace(key, std::move(entry)).first;
    }
    it->second->ref_cnt++;
    return it->second.get();
}

void PeerEidTable::Release(Entry *entry)
{
    if (entry == nullptr) {
        return;
    }
    const Key key = MakeKey(*entry);
    Stripe &stripe = stripes_[KeyHash{}(key) % STRIPE_NUM];

    std::lock_guard<std::mutex> guard(stripe.mtx);
    auto it = stripe.map.find(key);
    if (it == stripe.map.end() || it->second.get() != entry) {
        /* 不可达防御：条目只能经本表 Acquire 获得 */
        return;
    }
    if (--it->second->ref_cnt == 0) {
        stripe.map.erase(it);
    }
}

uint64_t UmqSocket::RegisterFcTxEvent()
{
    if (!UmqSetting::UMQ_FLOW_CONTROL_ENABLE || umq_handle_ == UMQ_INVALID_HANDLE) {
        return 0;
    }

    // 添加流控event事件（流控信令持有超时触发归还）
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_EVENT};
    int fc_event_fd = UmqApi::umq_interrupt_fd_get(umq_handle_, &tx_option);
    if (fc_event_fd < 0) {
        UBS_VLOG_ERR("[UMQ_API] Failed to get TX interrupt fd, local umq: %llu\n",
                     static_cast<unsigned long long>(umq_handle_));
        return UBS_ERROR;
    }

    auto *tx_epoll_event = new (std::nothrow) UmqTpTxEpollRunnerOps::TxEpollEvent{
        RUNNER_EVENT_TYPE_FC_TX, umq_handle_, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    if (tx_epoll_event == nullptr) {
        UBS_VLOG_ERR("Unable to alloc TxEpollEvent for FC TX event\n");
        return UBS_ERROR;
    }

    struct epoll_event umq_tx_event {
    };
    umq_tx_event.events = EPOLLIN | EPOLLET;
    umq_tx_event.data.u64 = reinterpret_cast<uintptr_t>(tx_epoll_event);

    UmqTpTxEpollRunnerOps::TpTxExtContext ctx;
    ctx.umq_handle = umq_handle_;

    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    if (UNLIKELY(epoll_runner.AddEpollEvent(fc_event_fd, &umq_tx_event, &ctx))) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) tp tx event failed: %d : %s\n", errno, strerror(errno));
        delete tx_epoll_event;
        tx_epoll_event = nullptr;
        return UBS_ERROR;
    }

    /* FC 打开的部署里每条链路都会走到这里：冷侧构在此处安装（FC 关闭的常规
     * 部署不受影响）。分配失败按注册失败处理，与上面的错误路径一致。 */
    UmqSocketCold *cold = GetOrCreateCold();
    if (cold == nullptr) {
        UBS_VLOG_ERR("Unable to alloc cold part for FC TX event, fd: %d\n", raw_socket_);
        EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER).DelEpollEvent(fc_event_fd);
        return UBS_ERROR;
    }
    cold->fc_event_fd = fc_event_fd;
    return 0;
}

void UmqSocket::UnregisterFcTxEvent()
{
    UmqSocketCold *cold = GetCold();
    if (cold == nullptr || cold->fc_event_fd < 0) {
        return;
    }

    int event_fd = cold->fc_event_fd;
    cold->fc_event_fd = -1;

    int ret = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER).DelEpollEvent(event_fd);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("Failed to delete FC TX event, fd: %d, ret: %d\n", event_fd, ret);
    }
}

void UmqSocket::SetAddedEpollFd(EventPoll *fd, const epoll_data_t &data)
{
    SocketBase::SetAddedEpollFd(fd, data);

    /* Handoff with the share-JFR RX runner (lock-free, two threads):
     *   runner:  enqueue(rxQueue); FENCE; if (added_epoll_fd_) notify
     *   here:    added_epoll_fd_ = fd;  FENCE; if (!rxQueue.Empty()) notify
     * Without the two seq_cst fences this is the classic store->load
     * (Dekker) race: on ARM (and x86 TSO) both threads may read the other's
     * pre-store value — the runner sees "no epoll yet" and skips, we see
     * "queue empty" and skip — and the data that arrived while brpc was
     * doing epoll_ctl(ADD) sits in the RX queue with nobody ever told. For a
     * request/response link that means the peer's first request is never
     * read and the peer times out, although both handshakes were perfect. */
    std::atomic_thread_fence(std::memory_order_seq_cst);

    const bool pending = rxQueue && !rxQueue->Empty();
    if (fd != nullptr) {
        UBS_LINK_TRACE(raw_socket_, "EPOLL_ADD", "rx_pending=%d", pending ? 1 : 0);
    }
    if (pending) {
        UBS_LINK_TRACE(raw_socket_, "RX_RESCUE", "");
        NotifyReadable();
    }
}

bool UmqSocket::RxQueueEmpty()
{
    return !rxQueue || rxQueue->Empty();
}

} // namespace umq
} // namespace ubs
} // namespace ock
