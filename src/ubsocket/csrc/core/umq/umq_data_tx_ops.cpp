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
#include "umq_data_tx_ops.h"

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_port_cooldown.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "profiling/statistics/tx_stat_block.h" // TX-STAT: UMQ_POST_* 宏, g_tx_stat_on
#include "umq_buf_converter.h"
#include "umq_errno_converter.h"
#include "umq_socket.h"
#include "umq_tp_wait_queue.h"
#include "umq_tx_helper.h"

namespace ock {
namespace ubs {
/* 去虚化合并：类本体已下沉到 ock::ubs；umq 命名空间符号经 using 引入 */
using namespace umq;

/* 生产实例（DataPlaneEntry 内）owner_ 非空：fd/句柄恒读 socket 本体；
 * 独立实例（UT 栈对象等）回退构造参数。 */
int DataTxOps::OwnerFd() const
{
    return owner_ != nullptr ? owner_->Fd() : fd_;
}

uint64_t DataTxOps::OwnerUmqh() const
{
    return owner_ != nullptr ? owner_->UmqHandle() : fallback_umqh_;
}


using namespace txstat; // TX-STAT: PostErr/POST_ERR_* 位于 txstat 命名空间

uintptr_t DataTxOps::AllocTxBuf(uint32_t size, uint32_t count)
{
    PROF_START(UMQ_BUF_ALLOC);
    umq_buf_t *tx_buf_list = UmqApi::umq_buf_alloc(size, count, UMQ_INVALID_HANDLE, nullptr);
    if (tx_buf_list == nullptr) {
        PROF_END(UMQ_BUF_ALLOC, false);
        UBS_VLOG_ERR("[UMQ_API] umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                     static_cast<unsigned long long>(OwnerUmqh()), tx_buf_list);
        DpRearmTxInterrupt();
    } else {
        PROF_END(UMQ_BUF_ALLOC, true);
    }

    return reinterpret_cast<uintptr_t>(tx_buf_list);
}

int DataTxOps::PostSend(const SocketPtr &sock, uintptr_t buf, uint32_t batch, const ConverterPtr &cvt)
{
    umq_buf_t *tx_buf_list = reinterpret_cast<umq_buf_t *>(buf);
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    int flagEIO = -1;
    umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&head_buf_);
    umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&tail_buf_);
    uint16_t _unsolicited_wr_num = unsolicited_wr_num_;
    uint32_t _unsolicited_bytes = unsolicited_bytes_;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num_;

    PROF_START(CORE_WRITE_MEM_COPY);
    umq_buf_t *cur_buf = tx_buf_list;
    umq_buf_t *next_buf = cur_buf;
    if (QBUF_LIST_EMPTY(&head_buf_)) {
        QBUF_LIST_FIRST(&head_buf_) = cur_buf;
    } else {
        QBUF_LIST_NEXT(QBUF_LIST_FIRST(&tail_buf_)) = cur_buf;
    }
    uint32_t tx_total_len = 0;
    uint32_t sn_allocated = 0;
    cvt->Reset();

    const uint32_t io_buf_size = UmqSetting::GetIOBufSize();
    for (uint32_t i = 0; i < batch; ++i) {
        umq_buf_t *cur_wr_first = next_buf;
        uint32_t moved_total_len = 0;
        uint32_t wr_left_len = io_buf_size;
        uint32_t sge_idx = 0;
        bool last = false;
        for (cur_buf = cur_wr_first; cur_buf && (next_buf = cur_buf->qbuf_next, 1); cur_buf = next_buf) {
            last = cvt->MemCopy(wr_left_len, reinterpret_cast<uintptr_t>(cur_buf));
            cur_buf->io_direction = UMQ_IO_TX;
            Block *block = DataToBlock(cur_buf->buf_data);
            if (block == nullptr) {
                errno = EINVAL;
                UBS_VLOG_ERR("failed to locate brpc block for TX data %p, fd: %d\n", cur_buf->buf_data, OwnerFd());
                for (umq_buf_t *b = cur_wr_first; b != cur_buf; b = QBUF_LIST_NEXT(b)) {
                    Block *prev = DataToBlock(b->buf_data);
                    if (prev != nullptr) {
                        prev->DecRef();
                    }
                }
                return -1;
            }
            block->IncRef();
            wr_left_len -= cur_buf->data_size;
            moved_total_len += cur_buf->data_size;

            if (last || ++sge_idx >= TX_SGE_MAX || moved_total_len >= io_buf_size) {
                break;
            }
        }

        if (moved_total_len == 0) {
            UBS_VLOG_ERR("PostSend: moved_total_len=0, skip empty WR, fd: %d, batch: %u, i: %u\n", OwnerFd(), batch, i);
            next_buf = cur_wr_first->qbuf_next;
            continue;
        }

        tx_total_len += moved_total_len;
        cur_wr_first->total_data_size = moved_total_len;
        umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)cur_wr_first->qbuf_ext;
        buf_pro->opcode = UMQ_OPC_SEND_IMM;
        buf_pro->flag.value = 0;
        buf_pro->user_ctx = 0;
        auto seq_no = umq_socket->FetchAddSeqNum(1);
        buf_pro->imm.user_data = seq_no;
        ++sn_allocated;

        if (tx_queue_avail_num_.load(std::memory_order_acquire) == 1 || i + 1 == batch) {
            buf_pro->flag.bs.solicited_enable = 1;
        } else {
            if (unsolicited_wr_num_ > TX_REPORT_THRESHOLD || unsolicited_bytes_ > TX_UNSOLICITED_BYTES_MAX) {
                buf_pro->flag.bs.solicited_enable = 1;
            } else {
                ++unsolicited_wr_num_;
                unsolicited_bytes_ += moved_total_len;
            }
        }

        if (buf_pro->flag.bs.solicited_enable == 1) {
            unsolicited_wr_num_ = 0;
            unsolicited_bytes_ = 0;
        }

        // bonding 下, complete_enable 必须置为 1, 原判断和阈值失效
        if (++unsignaled_wr_num_ >= TX_REPORT_THRESHOLD) {
            buf_pro->flag.bs.complete_enable = 1;
            buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&head_buf_);
            QBUF_LIST_FIRST(&head_buf_) = QBUF_LIST_NEXT(cur_buf);
            unsignaled_wr_num_ = 0;
        }
    }

    QBUF_LIST_FIRST(&tail_buf_) = cur_buf;
    PROF_END(CORE_WRITE_MEM_COPY, true);

    const uint32_t pre = umq_socket->GetVersionedWritableReady(std::memory_order_acquire);

    // update last seqno type to CORE_WRITE_UMQ_POST, and add the timestamp
    PROF_START(CORE_WRITE_UMQ_POST);
    umq_buf_t *bad_qbuf = nullptr;
    umq_io_option_t option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int ret = UmqApi::umq_post(OwnerUmqh(), tx_buf_list, &option, &bad_qbuf);

    if (ret == UMQ_SUCCESS) {
        tx_queue_avail_num_.fetch_sub(batch, std::memory_order_acq_rel);
        TxCqePoller::Instance().MarkActive(sock);
        PROF_END(CORE_WRITE_UMQ_POST, true);
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
            if (auto *mgr = sockptr->GetStatsMgr()) {
                mgr->UpdateTraceStats(Statistics::StatsMgr::TX_PACKET_COUNT, batch);
            }
        }
        /* Cross-thread wake: PostSend runs on the app thread, so we must
         * both mark inflight and wake the poller via eventfd. flag=OFF:
         * NotifyInflight/NotifyPosted are no-ops (poller not started). */
        if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().NotifyInflight();
            TxCqePoller::Instance().NotifyPosted();
        }
    } else if (bad_qbuf != nullptr) {
        const bool all_failed = bad_qbuf == tx_buf_list;
        PROF_END(CORE_WRITE_UMQ_POST, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
        if (errno == EAGAIN) {
            // 无流控 credit, 底层 umq 会发送流控 credit 报文给对端，对端会回复。此种情况下发包必定会全部失败.
            // 接下来此函数会返回 -1, errno=EAGAIN. brpc 会主动监控 EPOLLIN | EPOLLOUT 事件.
            // @See EpollCtlMod
            //
            // 但是流控报文通过 UB 链路传输，它可能会先到达本端的共享 JFR 中。如果在 brpc epoll_ctl MOD 之前到
            // 达就直接通知，brpc 可能会不认这个通知？因为 brpc 还没有注册 EPOLLOUT 事件了，但是底层却上报了一
            // 个 EPOLLOUT 通知事件。
            // @See SiftSocketEventsWithUmqBuffers
            // @See NotifyWritable
            //
            // 因此在 EpollCtlMod 与 NotifyWritable 中使用 versioned_writable_ready_ 原子变量决策，由谁来真正
            // 地向上通知 EPOLLOUT 事件。
            if (all_failed) {
                // 目前版本号仅会在通知可写时增加，如果此时 CAS 失败，说明一个可写通知已经送达，这个时候不会改
                // 写它的 writable 属性。在之后 EpollCtlMod 时可以检测到它是可写的，会补发一个可写通知
                umq_socket->SetNotWritableReadyIfUnchanged(pre);
                UMQ_POST_ERR_ADD(POST_ERR_EAGAIN_ALL); // TX-STAT: 全部 post 失败（信用耗尽）
                need_fc_awake_.store(true, std::memory_order_relaxed);
            } else {
                UMQ_POST_ERR_ADD(POST_ERR_EAGAIN_PART); // TX-STAT: 部分 post 成功、部分失败
                // 在部分数据写入成功时出现 EAGAIN, 说明 umq 的流控 credit 不足，它会发起另一个流控 credit 请
                // 求。ubsocket 对此不关注。
                //
                // 通常会出现两种情况：
                // - 下次 ubsocket writev 时选项其他 jetty node, 任由它可能会被共享 JFR 线程的
                //   `NotifyWritable()` 被标记为 `writable_ready_=true`. 下次如果它真正出现 EAGAIN, 它的
                //   `writable_ready_=false` 仍会被重新设置
                // - 下次 ubsocket writev 时仍选择此 jetty node.
                //   - 共享 JFR 的 `NotifyWritable()` 可能会先运行，它拥有足够的流控 credit, 可继续发送消息
                //   - 迟迟收不到对端的流控 credit 回复，在时间范围内返回 EAGAIN. 如果超时会报 ETIMEOUT
                //   - 不过已经是在等待 EPOLLOUT, 为什么还会在 EPOLLOUT 到达前调用 writev 呢？
            }
        } else if (errno == ETIMEDOUT) {
            UMQ_POST_ERR_ADD(POST_ERR_ETIMEDOUT); // TX-STAT: 接收对端流控 credit 回复超时 (默认1s)
            // 接收对端流控 credit 回复超时 (默认1s)
            errno = EIO;
            flagEIO = 1;
        } else if (ret == -UMQ_ERR_EFLOWCTL || ret == -UMQ_ERR_EFLOWCTL_FATAL || ret == -UMQ_ERR_EFLOWCTL_EAGAIN) {
            UMQ_POST_ERR_ADD(POST_ERR_EFLOWCTL); // TX-STAT: 流控失败（含 FATAL/EAGAIN）
            errno = EIO;
            flagEIO = 1;
        } else if (errno == EMLINK) {
            UMQ_POST_ERR_ADD(POST_ERR_EMLINK); // TX-STAT: jetty pool 繁忙，无法分配 jetty node
            // jetty pool 繁忙，无法分配 jetty node
            UBS_VLOG_DEBUG(
                "[Debug] umq_post() suspended: no available jetty. Queued for automatic retry. socket fd: %d\n",
                sock->raw_socket_);
            // 期间没有可写通知到达，需要真正排队等待。如果失败了，说明存在一个可写通知，此时 writable 仍旧为
            // true. 后续在 EpollCtlMod 时会补发一个可写通知
            if (umq_socket->SetNotWritableReadyIfUnchanged(pre)) {
                UmqTpWaitQueue::Instance().Enqueue(sock);
            }
            errno = EAGAIN;
        } else if (errno == ENOBUFS) {
            // jetty pool 整体被占用，当前 jetty node 不允许发送数据.
            //
            // umq 在部分写成功时，仍旧会返回 ENOBUFS 表示 jetty pool 没有足够的 qbuf. 不过存在一种可能，如果
            // 正好被 PollTx 释放了资源，那么此 jetty node 还是可写的，尝试一下
            if (all_failed) {
                UMQ_POST_ERR_ADD(POST_ERR_ENOBUFS_ALL); // TX-STAT: 全部 post 失败（jetty/qbuf 池耗尽）
                UBS_VLOG_DEBUG(
                    "[Debug] umq_post() suspended: no enough buffers. Queued for automatic retry. socket fd: %d\n",
                    sock->raw_socket_);
                // 目前版本号仅会在通知可写时增加，如果此时 CAS 失败，说明一个可写通知已经送达，这个时候不会改
                // 写它的 writable 属性。在之后 EpollCtlMod 时可以检测到它是可写的，会补发一个可写通知
                umq_socket->SetNotWritableReadyIfUnchanged(pre);
                errno = EAGAIN;
            } else {
                UMQ_POST_ERR_ADD(POST_ERR_ENOBUFS_PART); // TX-STAT: 部分 post 成功、部分失败
            }
        } else {
            UBS_VLOG_ERR("[UMQ_API] umq_post() failed for TX, local umq: %llu, ret: %d, "
                         "mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(OwnerUmqh()), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
            UMQ_POST_ERR_ADD(POST_ERR_OTHER); // TX-STAT
            flagEIO = 1;
        }
        umq_buf_list_t head = {bad_qbuf};
        umq_buf_t *cur = nullptr;
        QBUF_LIST_FOR_EACH(cur, &head)
        {
            Block *block = DataToBlock(cur->buf_data);
            if (block != nullptr) {
                block->DecRef();
            } else {
                UBS_VLOG_ERR("failed to locate brpc block for bad TX data %p, fd: %d\n", cur->buf_data, OwnerFd());
            }
        }
        if (bad_qbuf == tx_buf_list) {
            // 全部 post 失败, 恢复状态
            unsolicited_wr_num_ = _unsolicited_wr_num;
            unsolicited_bytes_ = _unsolicited_bytes;
            unsignaled_wr_num_ = _unsignaled_wr_num;
            QBUF_LIST_FIRST(&head_buf_) = head_qbuf;
            QBUF_LIST_FIRST(&tail_buf_) = tail_qbuf;
            PROF_START(UMQ_BUF_FREE);
            UmqApi::umq_buf_free(bad_qbuf);
            PROF_END(UMQ_BUF_FREE, true);
            tx_total_len = -1;
            umq_socket->FetchSubSeqNum(sn_allocated);
        } else {
            uint32_t buf_num = 0;
            tx_total_len -= HandleBadQBuf(sock, tx_buf_list, bad_qbuf, head_qbuf, batch, _unsolicited_wr_num,
                                          _unsolicited_bytes, _unsignaled_wr_num, &buf_num);
            if (GlobalSetting::UBS_MONITOR_ENABLE && buf_num > 0) {
                SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
                if (auto *mgr = sockptr->GetStatsMgr()) {
                    mgr->UpdateTraceStats(Statistics::StatsMgr::TX_PACKET_COUNT, buf_num);
                }
            }

            umq_socket->FetchSubSeqNum(sn_allocated - buf_num);
        }
        /* Partial success: some WRs were posted, so mark inflight and
         * wake the poller. Same cross-thread path as full success. */
        if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().NotifyInflight();
            TxCqePoller::Instance().NotifyPosted();
        }
        if (flagEIO == 1) {
            UBS_VLOG_ERR("write failed, destroy UB\n");
            return -1;
        }
    } else {
        PROF_END(CORE_WRITE_UMQ_POST, false);
        int savedErrno = errno;
        UMQ_POST_ERR_ADD(POST_ERR_NO_BADQBUF); // TX-STAT: umq_post 返回无 bad_qbuf 的异常分支
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_post() failed for TX without bad_qbuf, "
                     "local umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(OwnerUmqh()), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
    }

    return tx_total_len;
}

int DataTxOps::PollTx(Socket *sock)
{
    if (get_and_ack_event_) {
        // handle tx epollin epoll event
        do {
            PROF_START(CORE_WRITE_REARM);
            if (GetAndAckEvent() < 0) {
                PROF_END(CORE_WRITE_REARM, false);
                UBS_VLOG_ERR("WriteV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", OwnerFd(), -1, errno,
                             Func::Error2Str(errno));
                return -1;
            }
            PROF_END(CORE_WRITE_REARM, true);
            PROF_START(CORE_WRITE_POLL_TX_FIRST);
            // set poll_to_empty, means poll at least m_tx.m_retrieve_threshold TX CQE
            PollUmqTx(sock, true);
            /* m_tx.epoll_event_num_ not equals to m_tx.m_expect_epoll_event_num means
             * another epoll event is reportedduring readv processing procedure */
            PROF_END(CORE_WRITE_POLL_TX_FIRST, true);
        } while (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                           std::memory_order_acquire));

        get_and_ack_event_ = false;
    } else if (tx_queue_avail_num_.load(std::memory_order_acquire) == 0) {
        PROF_START(CORE_WRITE_POLL_TX_SECOND);
        PollUmqTx(sock, false);
        if (tx_queue_avail_num_.load(std::memory_order_acquire) == 0) {
            // 暂时禁用，否则会关闭 solicited mode.
            // return DpRearmTxInterrupt();
        }
        PROF_END(CORE_WRITE_POLL_TX_SECOND, true);
    } else {
        // Tx CQE poller 大概率会在这里运行
        PROF_START(CORE_WRITE_POLL_TX_THIRD);
        PollUmqTxOnce(sock);
        PROF_END(CORE_WRITE_POLL_TX_THIRD, true);
    }

    return 0;
}

ConverterPtr DataTxOps::BuildIovConverter(const struct iovec *iov, int iovcnt)
{
    auto umqConverter = MakeRef<UmqIovConverter>(iov, iovcnt);
    return RefConvert<UmqIovConverter, UbSocketBufConverter>(umqConverter);
}

ConverterPtr DataTxOps::BuildBufferConverter(const void *buf, size_t size)
{
    auto umqConverter = MakeRef<UmqBufferConverter>(buf, size);
    return RefConvert<UmqBufferConverter, UbSocketBufConverter>(umqConverter);
}

int DataTxOps::GetAndAckEvent()
{
    int ret = 0;
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    PROF_START(UMQ_GET_CQ_EVENT);
    int events = UmqApi::umq_get_cq_event(OwnerUmqh(), &option);
    if (events == 0) {
        PROF_END(UMQ_GET_CQ_EVENT, true);
        return 0;
    } else if (events < 0) {
        PROF_END(UMQ_GET_CQ_EVENT, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, events, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed, local umq: %llu, ret: %d, mapped: %d(%s), original: %d\n",
                     static_cast<unsigned long long>(OwnerUmqh()), events, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, events), savedErrno);
        return -1;
    }
    PROF_END(UMQ_GET_CQ_EVENT, true);
    if ((ack_event_num_ += events) >= 1) {
        PROF_START(UMQ_ACK_INTERRUPT);
        UmqApi::umq_ack_interrupt(OwnerUmqh(), ack_event_num_, &option);
        PROF_END(UMQ_ACK_INTERRUPT, true);
        ack_event_num_ = 0;
        // TODO: 返回值判断
        PROF_START(UMQ_REARM_INTERRUPT);
        ret = UmqApi::umq_rearm_interrupt(OwnerUmqh(), false, &option);
        PROF_END(UMQ_REARM_INTERRUPT, ret == 0);
    }
    return 0;
}

int DataTxOps::PollUmqTx(Socket *sock, bool poll_to_empty)
{
    uint32_t poll_total_cnt = 0;
    int poll_cnt = 0;
    ops_error_code err_code = ops_error_code::OK;
    do {
        PROF_START(CORE_WRITE_DO_TX_POLL);
        poll_cnt = DoUmqTxPoll(sock, err_code);
        PROF_END(CORE_WRITE_DO_TX_POLL, poll_cnt >= 0);
        if (poll_cnt <= 0) {
            break;
        }
        poll_total_cnt += (uint32_t)poll_cnt;
    } while ((poll_total_cnt < TX_RETRIEVE_THRESHOLD || poll_to_empty) && err_code == ops_error_code::OK);
    return 0;
}

int DataTxOps::PollUmqTxOnce(Socket *sock)
{
    PROF_START(CORE_WRITE_DO_TX_POLL);
    ops_error_code err_code = OK;
    int poll_cnt = DoUmqTxPoll(sock, err_code);
    PROF_END(CORE_WRITE_DO_TX_POLL, poll_cnt >= 0);
    return poll_cnt > 0 ? poll_cnt : 0;
}

void DataTxOps::ForceDrainTx(Socket *sock)
{
    /* poll_to_empty=true: drain all pending TX CQEs until umq_poll returns
     * 0 (first-empty-exit), reclaiming SQ slots. Used by the background
     * TxCqePoller to prevent status:12 (SQ full) under high throughput. */
    (void)PollUmqTx(sock, true);
}

void DataTxOps::QuickPollTx(Socket *sock)
{
    /* Single-shot poll: reclaim at most one batch of CQEs (POLL_BATCH_MAX=256)
     * without drain-to-empty. Bounds per-socket TX poll latency in the RX
     * active loop; remaining CQEs are reclaimed by the TX-only active loop
     * (TX_CQE_TIMER / TX_WAKE events) which calls ForceDrainTx. */
    (void)PollUmqTxOnce(sock);
}

void DataTxOps::WakeUpTx(Socket *sock)
{
    bool need_fc_awake = need_fc_awake_.exchange(false, std::memory_order_acq_rel);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (need_fc_awake && sockBase->NotifyWritable() == -1) {
        UBS_VLOG_ERR("NotifyWritable() failed, raw sock fd %d: errno: %d, errmsg: %s\n", sockBase->raw_socket_, errno,
                     Func::Error2Str(errno));
    }
}

bool DataTxOps::Writable(const SocketPtr &sock)
{
    // writev 热路径：调用方 DataTx::WriteV 持有 SocketPtr(sock) 保证本次调用内存活，
    // 无需自持引用；static_cast 省去 RefConvert 的 RTTI(dynamic_cast) + 两次原子 refcount。
    auto *umqSock = static_cast<UmqSocket *>(sock.Get());

    // RNR 反压期间不可写，writev 返回 EAGAIN
    if (umqSock->IsRnrBlocked()) {
        return false;
    }

    if (UmqSetting::UMQ_TP_TYPE != POOL) {
        return true;
    }

    if (umqSock->GetJettyAllocState() == JettyAllocState::WAITING) {
        return false;
    } else {
        return true;
    }
}

int DataTxOps::DoUmqTxPoll(Socket *sock, ops_error_code &err_code)
{
    // RNR 反压 fatal 超时检查（周期触发点：active loop / TX timer / writev 尾部
    // QuickPollTx 均经此进入）。放在批处理前，纯 RNR 通知（无 wr_cnt 贡献）的
    // socket 也能被覆盖，比批末 socket_wr_cnt_map 循环更可靠。sock 可能为 null
    // （FC-return 轮询场景），仅对非 null socket 做检查。
    if (sock != nullptr) {
        auto *umq_sock = static_cast<UmqSocket *>(sock);
        if (umq_sock->TryRnrBlockFatal()) {
            UBS_VLOG_WARN("RNR backpressure fatal timeout, fd: %d, disconnecting (poll path)\n", sock->raw_socket_);
            LibcApi::shutdown(sock->raw_socket_, SHUT_RD);
            sock->State(SOCK_STAT_CLOSE);
            return 0;
        }
    }
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    UmqTxHelper::PollArgs poll_args(OwnerUmqh(), poll_option, err_code, sock);
    return UmqTxHelper::PollUmqTx(poll_args, [this, sock](umq_buf_t *qbuf) {
        // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
        // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
        // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
        LibcApi::shutdown(OwnerFd(), SHUT_RD);
        // TX CQE 异常断链：记录实际 status，便于区分 status=99(RNR 重试耗尽) 等 fatal 码。
        UBS_VLOG_WARN("closing socket fd=%d on TX CQE error, status: %lu\n", OwnerFd(),
                      static_cast<unsigned long>(qbuf->status));
        sock->State(SOCK_STAT_CLOSE);

        // 光组网下，如果出现了异常 CQE 2/4/9 则说明底层 URMA 已将所有 port 都给重试了
        auto *umq_sock = static_cast<UmqSocket *>(sock);
        if (umq_sock->GetTopoType() == UMQ_TOPO_TYPE_CLOS) {
            if (qbuf->status == UMQ_BUF_LOC_LEN_ERR || qbuf->status == UMQ_BUF_LOC_ACCESS_ERR ||
                qbuf->status == UMQ_BUF_ACK_TIMEOUT_ERR || qbuf->status == UMQ_FAKE_BUF_FC_ERR ||
                qbuf->status == UMQ_FAKE_BUF_FC_ERR_FATAL) {
                auto [ports, ports_num] = umq_sock->GetUsedPorts();
                for (std::size_t i = 0; i < ports_num; ++i) {
                    UBS_VLOG_WARN("port is down, new UB connection will not use port(chip=%u,die=%u,port=%u)\n",
                                  ports[i].bs.chip_id, ports[i].bs.die_id, ports[i].bs.port_idx);
                    PortCooldownManager::MarkPortInCooldown(ports[i]);
                }
            }
        }
    });
}

int DataTxOps::DpRearmTxInterrupt()
{
    PROF_START(CORE_WRITE_REARM);
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int ret = UmqApi::umq_rearm_interrupt(OwnerUmqh(), false, &tx_option);
    if (ret == 0) {
        PROF_END(CORE_WRITE_REARM, true);
        errno = EAGAIN;
        return -1;
    }

    int savedErrno = errno;
    errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
    UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                 "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                 static_cast<unsigned long long>(OwnerUmqh()), ret, errno,
                 UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
    PROF_END(CORE_WRITE_REARM, false);
    return -1;
}

Block *DataTxOps::DataToBlock(void *data)
{
    umq_buf_t *qbuf = UmqApi::umq_data_to_head(data);
    if (qbuf == nullptr || qbuf->buf_data == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Block *>(qbuf->buf_data);
}

/* 去虚化后本方法被跨 TU 直接调用（原先经 vtable 间接、符号必然发射）：
 * cpp 内 inline 定义在其它 TU 只留下未定义引用（ld: undefined reference，
 * 见 issue #12）。去掉 inline，按普通外部符号发射。 */
uint32_t DataTxOps::IOBufSize()
{
    return UmqSetting::GetIOBufSize();
}

uint32_t DataTxOps::HandleBadQBuf(const SocketPtr &sock, umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf,
                                 umq_buf_t *last_head_qbuf, uint32_t batch, uint16_t unsolicited_wr_num,
                                 uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num, uint32_t *buf_num)
{
    umq_buf_t *cur_qbuf = head_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *head_qbuf_ = last_head_qbuf;
    umq_buf_t *bad_qbuf_ori = bad_qbuf;
    uint32_t wr_cnt = 0;
    uint16_t _unsolicited_wr_num = unsolicited_wr_num;
    uint32_t _unsolicited_bytes = unsolicited_bytes;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num;
    uint32_t total_size = 0;

    while (bad_qbuf != nullptr) {
        cur_qbuf = bad_qbuf;
        total_size += cur_qbuf->data_size;
        bad_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        last_qbuf = cur_qbuf;
        wr_cnt++;
    }
    wr_cnt = batch - wr_cnt;

    unsolicited_wr_num_ = _unsolicited_wr_num;
    unsolicited_bytes_ = _unsolicited_bytes;
    unsignaled_wr_num_ = _unsignaled_wr_num;
    tx_queue_avail_num_.fetch_sub(wr_cnt, std::memory_order_acq_rel);
    if (wr_cnt > 0) {
        TxCqePoller::Instance().MarkActive(sock);
    }
    *buf_num = wr_cnt;

    QBUF_LIST_FIRST(&head_buf_) = head_qbuf_;
    if (last_qbuf != nullptr) {
        /* If head set to nullptr, it means no need to cache the posted qbuf list anymore, reset head
             * to nullptr as well */
        QBUF_LIST_FIRST(&tail_buf_) = (head_qbuf_ == nullptr) ? nullptr : last_qbuf;
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }

    PROF_START(UMQ_BUF_FREE);
    UmqApi::umq_buf_free(bad_qbuf_ori);
    PROF_END(UMQ_BUF_FREE, true);
    return total_size;
}

void DataTxOps::FlushTx(Socket *sock, uint32_t timeout_ms)
{
    uint16_t threshold = GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acquire);
    if (threshold <= 0) {
        return;
    }

    uint32_t poll_total_cnt = 0;
    int poll_cnt = 0;
    ops_error_code err_code = ops_error_code::OK;
    auto start_ms = SocketConnHelper::GetTimeMs();
    uint64_t next_yield_ms = start_ms + 1;
    do {
        if (SocketConnHelper::IsTimeout(start_ms, timeout_ms)) {
            /* If a timeout is triggered here, it would indicate a memory leak.
                 * In this case, processing of unsignaled wr should not continue. */
            UBS_VLOG_DEBUG("Flush TX operation exceeded timeout period(%u ms)\n", timeout_ms);
            break;
        }

        poll_cnt = DoUmqTxPoll(sock, err_code);
        if (poll_cnt < 0) {
            break;
        }

        poll_total_cnt += static_cast<uint32_t>(poll_cnt);
        /* Fallback synchronous teardown may wait for the full destroy timeout.
         * Yield periodically while no CQE arrives so it cannot monopolize a
         * host worker for that entire interval. */
        const uint64_t now_ms = SocketConnHelper::GetTimeMs();
        if (poll_cnt == 0 && now_ms >= next_yield_ms && sock->Type() != SocketType::SOCK_TYPE_COUNT &&
            poll_total_cnt < threshold && err_code != ops_error_code::FATAL_ERROR) {
            PollerYield();
            next_yield_ms = SocketConnHelper::GetTimeMs() + 1;
        } else if (poll_cnt > 0) {
            next_yield_ms = now_ms + 1;
        }
    } while (sock->Type() != SocketType::SOCK_TYPE_COUNT && poll_total_cnt < threshold &&
             err_code != ops_error_code::FATAL_ERROR);

    if (err_code != ops_error_code::FATAL_ERROR &&
        tx_queue_avail_num_.load(std::memory_order_relaxed) < GlobalSetting::UBS_TX_DEPTH && unsignaled_wr_num_ > 0) {
        uint32_t left_wr_num = GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acquire);
        umq_buf_t *cur_qbuf = QBUF_LIST_FIRST(&head_buf_);
        umq_buf_t *last_qbuf = nullptr;
        uint32_t cached_wr_cnt = 0;
        while (cached_wr_cnt < left_wr_num && cur_qbuf != nullptr) {
            /* unsignaled wr list:
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * |  0  |  1  |  2  |  3  |  4  |  5  |  6  | wr idx
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * |  S  |  S  |  S  |  F  |  F  |  F  |  F  | wr status: (1) S:successful; (2) F:Failed
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * Since the successful wr(0~2) did not set the signaled flag, it will not generate a cqe
                 * Therefore, it is necessary to perform a release operation through the cache list.
                 * The unsuccessful(3 ~ 6) wrs have already been released and retried via(by tcp) an
                 * exceptional cqe within the DoUmqTxPoll() operation, so there is no need to handle these wrs
                 * again here. Consequently, only 0 ~ 2 wrs need to be processed. */
            int64_t rest_size = cur_qbuf->total_data_size;
            /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
                * the situation that rest_size would not reduced to zero */
            while (cur_qbuf && rest_size > 0) {
                rest_size -= (int64_t)cur_qbuf->data_size;
                last_qbuf = cur_qbuf;
                Block *block = DataToBlock(cur_qbuf->buf_data);
                if (block != nullptr) {
                    block->DecRef();
                } else {
                    UBS_VLOG_ERR("failed to locate brpc block for cached TX data %p, fd: %d\n", cur_qbuf->buf_data,
                                 OwnerFd());
                }
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            }

            cached_wr_cnt++;
        }

        if (last_qbuf != nullptr) {
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }

        PROF_START(UMQ_BUF_FREE);
        UmqApi::umq_buf_free(QBUF_LIST_FIRST(&head_buf_));
        PROF_END(UMQ_BUF_FREE, true);
        tx_queue_avail_num_.fetch_add(cached_wr_cnt, std::memory_order_acq_rel);
    }

    if (tx_queue_avail_num_.load(std::memory_order_acquire) < GlobalSetting::UBS_TX_DEPTH) {
        UBS_VLOG_DEBUG("Failed to flush umq(TX), leak %u wr(s) of buffer\n",
                       GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acquire));
    }
}


} // namespace ubs
} // namespace ock
