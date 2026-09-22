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

#include "ubsocket_data_rx.h"
#include "common/ubsocket_common_includes.h"
#include "profiling/statistics/rx_stat_block.h"
#include "profiling/ubsocket_prof.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataRx::DataRx(const SocketPtr &sock, DataRxOps *ops) : rx_ops_(ops)
{
    /* caller must make sure ops is not null */
    (void)sock;
}

ssize_t DataRx::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    PROF_START(CORE_READ);
    PROF_START(CORE_READ_EAGAIN);
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        ssize_t size = LibcApi::readv(sock->raw_socket_, iov, iovcnt);
        PROF_END(CORE_READ, size >= 0);
        return size;
    }

    if (iov == nullptr || iovcnt == 0) {
        errno = EINVAL;
        UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", sock->raw_socket_, -1, errno,
                      Func::Error2Str(errno));
        PROF_END(CORE_READ, false);
        return UBS_ERROR;
    }

    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == nullptr) {
            errno = EINVAL;
            UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", sock->raw_socket_, -1, errno,
                          Func::Error2Str(errno));
            PROF_END(CORE_READ, false);
            return UBS_ERROR;
        }
    }

    /* 原 OutputErrorMagicNumber（协议嗅探字节回放）已删除：其状态从无写点
     * （recv_size 恒 0，函数恒返 0），协议失配路径直接 close fd，降级链路在
     * 上方 RAW_ESTABLISHED 分支短路——三个字段与函数均为死代码。 */
    ssize_t rx_total_len = 0;

    auto ts_entry_end = ubsocket_get_timeNs_compile();

    PROF_START(CORE_READ_POLL_RX);
    auto ts_pollrx_start = ubsocket_get_timeNs_compile();
    int ret = rx_ops_->PollRx(sock);
    auto ts_pollrx_end = ubsocket_get_timeNs_compile();
    PROF_END(CORE_READ_POLL_RX, ret >= 0);

    if (ret < 0) {
        PROF_END(CORE_READ, false);
        return ret;
    }

    uint32_t max_buf_size;
    if (GlobalSetting::UBS_READV_UNLIMITED) {
        max_buf_size = UINT32_MAX;
    } else {
        max_buf_size = 0;
        for (int i = 0; i < iovcnt; i++) {
            max_buf_size += iov[i].iov_len;
        }
    }

    auto ts_dataset_start = ubsocket_get_timeNs_compile();
    ret = rx_ops_->RxDataSet(iov[0].iov_base, max_buf_size);
    auto ts_dataset_end = ubsocket_get_timeNs_compile();

    if (ret < 0) {
        if (!((errno == EINTR) || (errno == EAGAIN))) {
            PROF_END(CORE_READ, false);
        } else {
            PROF_END(CORE_READ_EAGAIN, true);
        }
        return ret;
    }
    rx_total_len = ret;
    if (GlobalSetting::UBS_MONITOR_ENABLE) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
        /* mgr 为惰性指针（trace 开而 mgr 缺失 = 分配失败或非工厂构造的测试 socket） */
        if (auto *mgr = sockptr->GetStatsMgr()) {
            mgr->UpdateTraceStats(Statistics::StatsMgr::RX_BYTE_COUNT, rx_total_len);
        }
    }
    auto ts_exit = ubsocket_get_timeNs_compile();
    PROF_END(CORE_READ, rx_total_len != 0);

    /* SplitTrace: approach A (delayed write) — ReadV path uses seq_no from PollRx */
    uint32_t seq_no = rx_ops_->last_rx_seq_no_;
    bool do_trace = STRACE_TRY_SAMPLE(sock.Get(), sock->raw_socket_, seq_no, PATH_RX_READV);
    if (do_trace) {
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_ENTRY, seq_no, ts_entry_start, ts_entry_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_POLL_RX, seq_no, ts_pollrx_start, ts_pollrx_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_DATA_SET, seq_no, ts_dataset_start, ts_dataset_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_REARM, seq_no, ts_dataset_end, ts_exit);
        STRACE_END(sock.Get(), PATH_RX_READV, seq_no, static_cast<uint32_t>(rx_total_len), 0);
    }

    return rx_total_len;
}

ssize_t DataRxOps::RxDataSet(void *buf, uint32_t size)
{
    Block *out_first_block = DataToBlock(buf);
    if (out_first_block == nullptr) {
        RX_DATASET_ERR_ADD(this, rxstat::RX_DATASET_NO_BLOCK);
        errno = EINVAL;
        UBS_VLOG_ERR("ReadV failed to locate brpc block for data %p, fd: %d\n", buf, OwnerFd());
        return UBS_ERROR;
    }
    ssize_t rx_total_len = block_cache_.CutAndInsertAfter(size, out_first_block);
    if (rx_total_len == 0) {
        /*
         * m_rx.epoll_event_num_ not equals to m_rx.m_expect_epoll_event_num means another epoll event is reported
         * during readv processing procedure, set m_rx.m_poll to enable poll RX operation and set errno to EINTR
         * to let brpc retry and call readv()
         */
        if (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                      std::memory_order_acquire)) {
            poll_ = true;
            errno = EINTR;
            return UBS_ERROR;
        }
        auto trace_sock = ArraySet<Socket>::GetInstance().GetItem(OwnerFd());
        if (trace_sock != nullptr && trace_sock->State() == SOCK_STAT_CLOSE) {
            return 0;
        }

        if (flow_control_failed_) {
            RX_DATASET_ERR_ADD(this, rxstat::RX_FLOW_CTRL_FAILED);
            errno = EIO;
            UBS_VLOG_ERR("ReadV flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", OwnerFd(), -1, errno,
                         Func::Error2Str(errno));
            return UBS_ERROR;
        }

        if (RearmRxInterrupt() < 0) {
            RX_DATASET_ERR_ADD(this, rxstat::RX_REARM_FAIL);
            errno = EIO;
            UBS_VLOG_ERR("ReadV RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", OwnerFd(), -1, errno,
                         Func::Error2Str(errno));
            return UBS_ERROR;
        }

        // UB 链路上无数据，但还是触发了 EPOLLIN 事件，可能是对端 TCP 连接关闭了，此种场景下向 brpc
        // 返回 0 暗示读到 EOF, brpc 随后会主动关闭连接.
        char b[1];
        int n = LibcApi::recv(OwnerFd(), b, sizeof(b), MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) {
            RX_DATASET_ERR_ADD(this, rxstat::RX_PEER_CLOSED);
            UBS_VLOG_INFO("The TCP connection has been closed by peer.\n");
            return 0;
        }

        /* return UBS_ERROR and set errno to EAGAIN to notice user no more data to read */
        errno = EAGAIN;
        return UBS_ERROR;
    }

    /* Set the first block as used to prevent brpc from utilizing this block,
     * and only use it as the head of the block linked list. */
    out_first_block->size = out_first_block->cap;
    return rx_total_len;
}
} // namespace ubs
} // namespace ock
