/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2025-07-31
 * Note:
 * History: 2025-07-31
*/

#ifndef BRPC_FILE_DESCRIPTOR_H
#define BRPC_FILE_DESCRIPTOR_H

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_set>
#include "umq_pro_api.h"
#include "umq_errno.h"
#include "brpc_context.h"
#include "file_descriptor.h"
#include "qbuf_list.h"
#include "buffer_util.h"
#include "statistics.h"

#define UMQ_BIND_INFO_SIZE_MAX  (512)
#define UMQ_BIND_SYNC_MSG       "SYNC_DONE"
#define DIVIDED_NUMBER          (2)
#define CACHE_LINE_ALIGNMENT    (64)

inline bool operator==(const umq_eid_t& a, const umq_eid_t& b) {
    return ::memcmp(a.raw, b.raw, sizeof(a.raw)) == 0;
}

inline bool operator!=(const umq_eid_t& a, const umq_eid_t& b) {
    return !(a==b);
}

namespace Brpc {

class FallbackTcpMgr {
public:
    ALWAYS_INLINE bool TxUseTcp(void)
    {
        return m_tx_use_tcp;
    }
    
    ALWAYS_INLINE bool RxUseTcp(void)
    {
        return m_rx_use_tcp;
    } 

    ALWAYS_INLINE bool UseTcp(void)
    {
        return m_rx_use_tcp || m_tx_use_tcp;
    }
    
protected:
    
    bool m_tx_use_tcp = false;
    bool m_rx_use_tcp = false;
}; 

class StatsMgr {
public:
    enum stats_type {
        TX_INTER_CNT,
        RX_INTER_CNT,
        WRITEV_BATCH_CNT,
        WRITEV_BUF_CNT,
        WRITEV_BYTES,
        READV_OUTPUT_BUF_CNT,
        READV_OUTPUT_BYTES,
        READV_CACHE_BYTES,
        
        STATE_TYPE_MAX
    };

    StatsMgr(int fd) : m_output_fd(fd) {}

    bool InitStatsMgr()
    {
        for (int i = 0; i < STATE_TYPE_MAX; ++i) {
            try {
                m_recorder_vec.emplace_back(GetStatsStr((enum stats_type)i));
            } catch (std::exception& e) {
                RPC_ADPT_VLOG_ERR("Failed to construct statistics manager, %s\n", e.what());
                return false;
            }
        }

        m_stats_enable = true;

        return true;
    }

    // data plane interface, caller ensure input validation
    ALWAYS_INLINE void UpdateStats(enum stats_type type, uint32_t value)
    {
        m_recorder_vec[type].Update(value);
    }

protected:
    const char *GetStatsStr(enum stats_type type)
    {
        const static char *state_type_str[STATE_TYPE_MAX] = {
            "tx interrupt count",
            "rx interrupt count",
            "writev batch count",
            "writev buffer count",
            "writev bytes",
            "readv output buffer count",
            "readv output bytes",
            "readv cache bytes",
        };

        return state_type_str[type];
    }
    
    void OutputStats(std::ostringstream &oss)
    {
        if (!m_stats_enable) {
            return;
        }

        for (int i = 0; i < STATE_TYPE_MAX; ++i){
            m_recorder_vec[i].GetInfo(m_output_fd, oss);
        }
    }

    std::vector<Statistics::Recorder> m_recorder_vec;
    int m_output_fd = -1;
    bool m_stats_enable = false;
};

struct UmqEidHash {
    std::size_t operator()(const umq_eid_t& eid) const noexcept {
        uint64_t h = *reinterpret_cast<const uint64_t*>(eid.raw);
        uint64_t l = *reinterpret_cast<const uint64_t*>(eid.raw + 8);
        return std::hash<uint64_t>{}(h) ^ (std::hash<uint64_t>{}(l) << 1);
    }
};

class EidRegistry {
public:
    bool RegisterEid(const umq_eid_t& eid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_registered_eids.insert(eid).second;
    }

    bool IsRegisteredEid(const umq_eid_t& eid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_registered_eids.count(eid) > 0;
    }

    bool UnregisterEid(const umq_eid_t& eid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_registered_eids.erase(eid) > 0;
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_set<umq_eid_t, UmqEidHash> m_registered_eids;
};

class SocketFd : public ::SocketFd, public FallbackTcpMgr, public StatsMgr {
public:
    enum error_code {
        OK,
        UMQ_ERROR,
        FATAL_ERROR
    };

    SocketFd(int fd, int event_fd) : ::SocketFd(fd), StatsMgr(fd)
    {
        QBUF_LIST_INIT(&m_tx.m_head_buf);
        QBUF_LIST_INIT(&m_tx.m_tail_buf);
        m_tx_window_capacity = Context::GetContext()->GetTxDepth();
        m_rx_window_capacity = Context::GetContext()->GetRxDepth();
        m_tx.m_window_size = m_tx_window_capacity;
        m_rx.m_refill_threshold = m_rx_window_capacity <= REFILL_RX_THRESHOLD ? 1 : REFILL_RX_THRESHOLD;
        m_tx.m_retrieve_threshold = m_tx_window_capacity <= RETRIEVE_TX_THRESHOLD_RATIO_DIVISOR ?
            1 : m_tx_window_capacity / RETRIEVE_TX_THRESHOLD_RATIO_DIVISOR;
        m_tx.m_handle_threshold = m_tx_window_capacity <= HANDLE_TX_THRESHOLD_RATIO_DIVISOR ?
            1 : m_tx_window_capacity / HANDLE_TX_THRESHOLD_RATIO_DIVISOR;   
        m_tx.m_report_threshold = m_tx.m_handle_threshold == 1 ?
            1 : m_tx_window_capacity / HANDLE_TX_THRESHOLD_RATIO_DIVISOR / DIVIDED_NUMBER;
        m_rx.m_readv_unlimited = Context::GetContext()->GetReadvUnlimited();
        if (Context::GetContext()->GetStatsEnable()) {
            m_context_stats_enable = true;
        }
        m_event_fd = event_fd;
    }

    SocketFd(int fd, uint64_t magic_number, uint32_t magic_number_recv_size) : ::SocketFd(fd), StatsMgr(fd)
    {
        m_tx.m_magic_number = magic_number;
        m_tx.m_magic_number_recv_size = magic_number_recv_size;
        m_tx_use_tcp = true;
    }

    virtual ~SocketFd()
    {
        UnbindAndFlushRemoteUmq();
        DestroyLocalUmq();
        if (m_event_fd >= 0) {
            OsAPiMgr::GetOriginApi()->close(m_event_fd);
            m_event_fd = -1;
        }
    }

    ALWAYS_INLINE int GetEventFd(void)
    {
        return m_event_fd;
    }

    ALWAYS_INLINE uint64_t GetLocalUmqHandle(void)
    {
        return m_local_umqh;
    }

    ALWAYS_INLINE int Accept(struct sockaddr *address, socklen_t *address_len)
    {
        int fd = OsAPiMgr::GetOriginApi()->accept(m_fd, address, address_len);
        if (fd < 0 || m_tx_use_tcp || m_rx_use_tcp) {
            return fd;
        }

        bool is_blocking = IsBlocking(fd);
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(fd);
        }

        uint64_t magic_number = 0;
        ssize_t magic_number_recv_size = 0;
        int ret = ValidateMagicNumber(fd, magic_number, magic_number_recv_size);
        if (ret > 0) {
            /* IF the magic number verification fails, it is still necessary to create a socket fd object
             * to store the magic number information, so that the received information can be reported to 
             * the user when readv is called. */
            SocketFd *socket_fd_obj = nullptr;
            try {
                socket_fd_obj = new SocketFd(fd, magic_number, (uint32_t)magic_number_recv_size);
                Fd<::SocketFd>::OverrideFdObj(fd, socket_fd_obj);
            } catch (std::exception& e) {
                RPC_ADPT_VLOG_ERR("%s\n", e.what());
                OsAPiMgr::GetOriginApi()->close(fd);
                return -1;
            }
        } else if (ret == 0) {
            if (DoAccept(fd) != 0) {
                RPC_ADPT_VLOG_WARN("Fatal error occurred, fd: %d fallback to TCP/IP", fd);
                /* Clear messages that already exist on the TCP link to prevent 
                 * dirty messages from affecting user data transmission*/
                 FlushSocketMsg(fd);
            }
        }

        if (is_blocking) {
            // reset
            SetBlocking(fd);
        }

        return fd;
    }

    int ConnectExchangeEid(umq_eid_t *connEid){
        Context *context = Context::GetContext();
        if(!context->IsBonding()){
            return 0;
        }
        umq_eid_t localEid = context->GetDevSrcEid();
        if (SendSocketData(m_fd, &localEid, sizeof(umq_eid_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_eid_t)) {
            RPC_ADPT_VLOG_ERR("Failed to send local eid message in connect, fd: %d\n", m_fd);
            return -1;
        }

        umq_eid_t remoteEid;
        if (RecvSocketData(
            m_fd, &remoteEid, sizeof(umq_eid_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_eid_t)) {
            RPC_ADPT_VLOG_ERR("Failed to receive remote eid message in connect, fd: %d\n", m_fd);
            return -1;
        }

        *connEid = context->GetDevSrcEid();
        if (localEid != remoteEid) {
            umq_route_t connRoute;
            if (GetDevRouteList(&localEid, &remoteEid, &connRoute) != 0) {
                RPC_ADPT_VLOG_ERR("Failed to get route list in connect, fd: %d\n", m_fd);
                return -1;
            }

            if (RecvSocketData(
                m_fd, &connRoute, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
                RPC_ADPT_VLOG_ERR("Failed to receive remote eid message in connect, fd: %d\n", m_fd);
                return -1;
            }

            *connEid = connRoute.dst;
        }

        return 0;
    }

    int DoConnect(void)
    {
        CpMsg local_cp_msg;
        CpMsg remote_cp_msg;
        char send_sync_msg[] = UMQ_BIND_SYNC_MSG;
        char recv_sync_msg[] = UMQ_BIND_SYNC_MSG;
        if (SendSocketData(
            m_fd, &local_cp_msg.magic_number, sizeof(uint64_t), NEGOTIATE_TIMEOUT_MS) != sizeof(uint64_t)) {
            RPC_ADPT_VLOG_ERR("Failed to send magic number, fd: %d\n", m_fd);
            return -1;
        }

        umq_eid_t connEid;
        if (ConnectExchangeEid(&connEid) < 0) {
            RPC_ADPT_VLOG_ERR("Failed to exchange eid in connect\n");
            return -1;
        }

        if (CreateLocalUmq(&connEid) < 0) {
            RPC_ADPT_VLOG_ERR("Failed to create umq\n");
            return -1;
        }

        local_cp_msg.queue_bind_info_size = 
           umq_bind_info_get(m_local_umqh, local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
        if (local_cp_msg.queue_bind_info_size == 0) {
            RPC_ADPT_VLOG_ERR("Failed to execute umq_bind_info_get\n");
            return -1;
        }
        
        uint32_t len = sizeof(local_cp_msg) - sizeof(uint64_t);
        if (SendSocketData(m_fd, &local_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) != len) {
            RPC_ADPT_VLOG_ERR("Failed to send local control message, fd: %d\n", m_fd);
            return -1;
        }
        RPC_ADPT_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %d, bind info len: %d\n", 
            m_fd, sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);
        
        if (RecvSocketData(
            m_fd, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(remote_cp_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to receive remote control message, fd: %d\n", m_fd);
            return -1;
        }
        RPC_ADPT_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %d, bind info len: %d\n", 
            m_fd, sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

        if (umq_bind(m_local_umqh, remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size) != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR("Failed to execute umq_bind\n");
            return -1;
        }  
        m_bind_remote = true;  

        // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
        if (PrefillRx() != 0) {
            RPC_ADPT_VLOG_ERR("Failed to fill rx buffer to umq, fd: %d\n", m_fd);
            return -1;
        }

        if (SendSocketData(
            m_fd, send_sync_msg, sizeof(send_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(send_sync_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to send sync done message, fd: %d\n", m_fd);
            return -1;
        }

        if (RecvSocketData(
            m_fd, recv_sync_msg, sizeof(recv_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(recv_sync_msg) ||
            strcmp(recv_sync_msg, UMQ_BIND_SYNC_MSG) != 0) {
            RPC_ADPT_VLOG_ERR("Failed to receive sync done message, fd: %d\n", m_fd);
            return -1;
        }

        if (m_context_stats_enable) {
            InitStatsMgr();
        }

        RPC_ADPT_VLOG_DEBUG("Connect to remote with UMQ successful, fd: %d\n", m_fd);

        return 0;
    }

    ALWAYS_INLINE int Connect (const struct sockaddr *address, socklen_t address_len)
    {
        int ret = OsAPiMgr::GetOriginApi()->connect(m_fd, address, address_len);
        if (ret < 0 || m_tx_use_tcp || m_rx_use_tcp) {
            return ret;
        }

        bool is_blocking = IsBlocking(m_fd);
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(m_fd);
        }

        ret = DoConnect();
        if (ret != 0) {
            RPC_ADPT_VLOG_WARN("Fatal error occurred, fd: %d fallback to TCP/IP", m_fd);
            Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
            /* Clear messages that already exist on the TCP link to prevent 
                 * dirty messages from affecting user data transmission*/
            FlushSocketMsg(m_fd);
        }

        if (is_blocking) {
            // reset
            SetBlocking(m_fd);
        }

        return ret;
    }

    virtual void OutputStats(std::ostringstream &oss) 
    {
        StatsMgr::OutputStats(oss);
    }

    // adapt to brpc, brpc IOBuf block use 8k as buffer slice with a 32 bytes head, thus, RX buffer size is 8160
    uint32_t BrpcIOBufSize()
    {
        if (Context::GetContext()->GetIOBlockType() == BLOCK_SIZE_8K) {
            return 8160;
        }
        return 65504;
    }

    uint64_t FloorMask()
    {
        if (Context::GetContext()->GetIOBlockType() == BLOCK_SIZE_8K) {
            return 8191;
        }
        return 65535;
    }

    static const uint32_t SGE_MAX = 6;
    // currently, the upper limit of post batch for umq is 64
    static const uint32_t POST_BATCH_MAX = 64;
    // currently, poll batch use 32 is for the balance of performance and efficiency  
    static const uint32_t POLL_BATCH_MAX = 32;
    /* unsolicited bytes use the same setting as brpc
     * accumulated bytes exceed UNSOLICITED_BYTES_MAX will generate a solicited interrupt event at remote */
    static const uint32_t UNSOLICITED_BYTES_MAX = 1048576;
    // to improve the efficiency, do one ack event operation per GET_PER_ACK times get event operation(same as brpc)
    static const uint32_t GET_PER_ACK = 32;
    // defaultRX refill threshold. If RX depth less then REFILL_RX_THRESHOLD, then, threshold change to 1
    static const uint16_t REFILL_RX_THRESHOLD = 32;
    /* handle tx(poll tx cqe, set tx signaled, i.e.) when accumulated number equals to m_tx_window_capacity / 8 */
    static const uint16_t HANDLE_TX_THRESHOLD_RATIO_DIVISOR = 4;
    /* process up to m_tx_window capacity / 16 tx cqe each time */
    static const uint16_t RETRIEVE_TX_THRESHOLD_RATIO_DIVISOR = 8;
    // magic number is the 0xff + ASCII of "R" + "P" + "C" + "A" + "D" + "P" + "T"
    static const uint64_t CONTROL_PLANE_MAGIC_NUMBER = 0xff52504341445054;
    static const uint8_t CONTROL_PLANE_MAGIC_NUMBER_PREFIX = 0xff;
    static const uint64_t CONTROL_PLANE_MAGIC_NUMBER_BODY = 0x52504341445054;
    static const uint32_t NEGOTIATE_TIMEOUT_MS = 10;
    static const uint32_t CONTROL_PLANE_TIMEOUT_MS = 500;
    static const uint32_t DATA_PLANE_TIMEOUT_MS = 100;
    static const uint32_t POLL_TX_RETRY_MAX_CNT = 50;
    static const uint32_t FLUSH_TIMEOUT_MS = 200;
    static const uint32_t FALLBAKC_TCP_RESENT_TIMEOUT_MS = 800;
    static const uint32_t WAIT_UMQ_READY_TIMEOUT_US = 100;
    // max wait 1s for umq ready
    static const uint32_t WAIT_UMQ_READY_ROUND = 10000;

    ALWAYS_INLINE ssize_t ReadV(const struct iovec *iov, int iovcnt)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->readv(m_fd, iov, iovcnt);
        }

        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            return -1;
        }

        /* if socket failed to pass magic number validation, then
         * (1) pass the received magic number as message to caller;
         * (2) when all the received message passed to caller, fallback to tcp/ip */
        ssize_t rx_total_len = OutputErrorMagicNumber(iov, iovcnt);
        if (rx_total_len > 0) {
            return rx_total_len;
        }
        
        if (m_rx.m_get_and_ack_event) {
            if (GetAndAckEvent(UMQ_IO_RX) < 0) {
                errno = EIO;
                return -1;
            }
            m_rx.m_get_and_ack_event = false;
        }

        uint32_t max_buf_size;
        if (m_rx.m_readv_unlimited) {
            max_buf_size = UINT32_MAX;
        } else {
            max_buf_size = 0;
            for (int i = 0; i < iovcnt; i++) {
                max_buf_size += iov[i].iov_len;
            }
        }

        umq_buf_t *buf[POLL_BATCH_MAX];
        int poll_num = 0;
        if (m_rx.m_poll) {
            poll_num = UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
            if (poll_num < 0) {
                errno = EIO;
                return -1;
            } else if (poll_num == 0) {
                /* might be useful for qps performance by
                 * (1) avoid redundant poll operations when handing cache;
                 * (2) aggregating RX requests; */
                m_rx.m_poll = false; 
            }
        }

        uint32_t polled_size = 0;
        for (int i = 0; i < poll_num; ++i) {
            // currently, umq over IB return IB cr status directly, successful = 0
            if (buf[i]->status != 0) {
                if (buf[i]->status != UMQ_BUF_FLOW_CONTROL_UPDATE) {
                    RPC_ADPT_VLOG_DEBUG("RX CQE is invalid, status: %d\n", buf[i]->status);
                    QBUF_LIST_NEXT(buf[i]) = nullptr;
                    umq_buf_free(buf[i]);
                    continue;
                } else if (buf[i]->total_data_size == 0) {
                    QBUF_LIST_NEXT(buf[i]) = nullptr;
                    umq_buf_free(buf[i]);
                    continue;
                }
            }

            m_rx.m_block_cache.Insert((char *)(buf[i]->buf_data), buf[i]->data_size);
            polled_size += buf[i]->data_size;
        }

        /* rpc adapter has replace brpc butil::iobuf::blockmem_allocate() & butil::iof::blockmem_deallocate()
         * and ensures that the starting address of the Block is aligned to an 8k boundary. */
        IOBuf::Block *out_first_block = (Brpc::IOBuf::Block *)PtrFloorToBoundary(iov[0].iov_base);
        rx_total_len = m_rx.m_block_cache.CutAndInsertAfter(max_buf_size, out_first_block);
        if (rx_total_len == 0) {
            /* m_rx.m_epoll_event_num not equals to m_rx.m_expect_epoll_event_num means another epoll event is reported
             * during readv processing procedure, set m_rx.m_poll to enable poll RX operation and set errno to EINTR
             * to let brpc retry and call readv() */
            if (!m_rx.m_epoll_event_num.compare_exchange_strong(
                m_rx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire)) {
                m_rx.m_poll = true;
                errno = EINTR;
                return -1;
            }
            
            if (m_stats_enable) {
                UpdateStats(StatsMgr::RX_INTER_CNT, 1);
            }

            bool closed = m_closed.load(std::memory_order_relaxed);
            if (closed == true) {
                return 0;
            }

            if (RearmRxInterrupt() < 0) {
                errno = EIO;
                return -1;
            }

            /* return -1 and set errno to EAGAIN to notice user no more data to read */
            errno = EAGAIN;
            return -1;
        }
        
        if (m_stats_enable) {
            UpdateStats(StatsMgr::READV_OUTPUT_BUF_CNT, poll_num);
            UpdateStats(StatsMgr::READV_OUTPUT_BYTES, rx_total_len);
            UpdateStats(StatsMgr::READV_CACHE_BYTES, m_rx.m_block_cache.GetCacheLen());
        }

        /* Set the first block as used to prevent brpc from utilizing this block,
         * and only use it as the head of the block linked list. */
        out_first_block->size = out_first_block->cap;
        
        return rx_total_len;
    }

    ALWAYS_INLINE ssize_t WriteV(const struct iovec *iov, int iovcnt)
    {
        if (m_tx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->writev(m_fd, iov, iovcnt);
        }

        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE;
            return -1;
        }
        
        if (m_tx.m_get_and_ack_event) {
            // handle tx epollin epoll event
            do {
                if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                    errno = EIO;
                    return -1;
                }
                
                // set poll_to_empty, means poll at least m_tx.m_retrieve_threshold TX CQE
                PollTx(m_tx.m_retrieve_threshold, true);
                /* m_tx.m_epoll_event_num not equals to m_tx.m_expect_epoll_event_num means
                 * another epoll event is reportedduring readv processing procedure */
            } while (!m_tx.m_epoll_event_num.compare_exchange_strong(
                m_tx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire));

            m_tx.m_get_and_ack_event = false;    
        } else if (m_tx.m_window_size == 0) {
            PollTx(m_tx.m_retrieve_threshold);
            if (m_tx.m_window_size == 0) {
                if (m_stats_enable) {
                    UpdateStats(StatsMgr::TX_INTER_CNT, 1);
                }
                return DpRearmTxInterrupt();
            }
        }

        IovConverter iov_converter(iov, iovcnt);
        uint32_t input_total_len = 0;
        uint32_t batch = 0;
        uint32_t post_batch_max = m_tx.m_window_size > POST_BATCH_MAX ? POST_BATCH_MAX : m_tx.m_window_size;
        uint32_t umq_buf_cnt = 0;
        uint32_t cut_total_len = 0;
        uint16_t _unsolicited_wr_num = m_tx.m_unsolicited_wr_num;
        uint32_t _unsolicited_bytes = m_tx.m_unsolicited_bytes;
        uint16_t _unsignaled_wr_num = m_tx.m_unsignaled_wr_num;
        umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&m_tx.m_head_buf);
        umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&m_tx.m_tail_buf);
        do {
            cut_total_len = 0;
            uint32_t cut_len = 0;
            uint32_t wr_left_len = BrpcIOBufSize();
            uint32_t sge_idx = 0;
            while (sge_idx++ < SGE_MAX && cut_total_len < BrpcIOBufSize() &&
                   ((cut_len = iov_converter.Cut(wr_left_len)) != 0)) {
                ++umq_buf_cnt;
                wr_left_len -= cut_len;
                cut_total_len += cut_len;
            }
            input_total_len += cut_total_len;
        } while (cut_total_len != 0 && ++batch < post_batch_max);

        umq_buf_t *tx_buf_list = umq_buf_alloc(0, umq_buf_cnt, UMQ_INVALID_HANDLE, nullptr);
        if (tx_buf_list == nullptr) {
            if (m_stats_enable) {
                UpdateStats(StatsMgr::TX_INTER_CNT, 1);
            }
            return DpRearmTxInterrupt();
        }

        umq_buf_t *cur_buf = tx_buf_list;
        umq_buf_t *next_buf = cur_buf;
        if (QBUF_LIST_EMPTY(&m_tx.m_head_buf)) {
            QBUF_LIST_FIRST(&m_tx.m_head_buf) = cur_buf;
        } else {
            QBUF_LIST_NEXT(QBUF_LIST_FIRST(&m_tx.m_tail_buf)) = cur_buf;
        }
        uint32_t tx_total_len = 0;
        iov_converter.Reset();
        for (uint32_t i = 0; i < batch; ++i) {
            umq_buf_t *cur_wr_first = next_buf;
            uint32_t moved_total_len = 0;
            uint32_t wr_left_len = BrpcIOBufSize();
            uint32_t sge_idx = 0;
            bool last = false;
            for (cur_buf = cur_wr_first; cur_buf && (next_buf = cur_buf->qbuf_next, 1); cur_buf = next_buf) {
                last = iov_converter.CutLast(wr_left_len, cur_buf);
                cur_buf->io_direction = UMQ_IO_TX;
                /* rpc adapter has replace brpc butil::iobuf::blockmem_allocate() & butil::iof::blockmem_deallocate()
                 * and ensures that the starting address of the Block is aligned to an 8k boundary. */
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_buf->buf_data))->IncRef();
                wr_left_len -= cur_buf->data_size;
                moved_total_len += cur_buf->data_size;
                if (last || ++sge_idx >= SGE_MAX || moved_total_len >= BrpcIOBufSize()) {
                    break;
                }
            }

            tx_total_len += moved_total_len;
            cur_wr_first->total_data_size = moved_total_len;
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)cur_wr_first->qbuf_ext;
            buf_pro->opcode = UMQ_OPC_SEND;
            buf_pro->flag.value = 0;
            buf_pro->user_ctx = 0;
            if (m_tx.m_window_size == 1 || i + 1 == batch) {
                buf_pro->flag.bs.solicited_enable = 1;
            } else {
                if (m_tx.m_unsolicited_wr_num > m_tx.m_report_threshold ||
                    m_tx.m_unsolicited_bytes > UNSOLICITED_BYTES_MAX) {
                    buf_pro->flag.bs.solicited_enable = 1;
                } else {
                    ++m_tx.m_unsolicited_wr_num;
                    m_tx.m_unsolicited_bytes += moved_total_len;
                }
            }

            if (buf_pro->flag.bs.solicited_enable == 1) {
                m_tx.m_unsolicited_wr_num = 0;
                m_tx.m_unsolicited_bytes = 0;
            }

            if (++m_tx.m_unsignaled_wr_num >= m_tx.m_report_threshold) {
                buf_pro->flag.bs.complete_enable = 1;
                buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&m_tx.m_head_buf);
                QBUF_LIST_FIRST(&m_tx.m_head_buf) = QBUF_LIST_NEXT(cur_buf);
                m_tx.m_unsignaled_wr_num = 0;
            }
        }

        QBUF_LIST_FIRST(&m_tx.m_tail_buf) = cur_buf;
        
        umq_buf_t *bad_qbuf = nullptr;
        int ret = umq_post(m_local_umqh, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
        if (ret == UMQ_SUCCESS) {
            m_tx.m_window_size -= batch;
        } else if (bad_qbuf != nullptr) {
            if (ret == -UMQ_ERR_EAGAIN) {
                errno = EAGAIN;
            } else {
                errno = EIO;
            }
            umq_buf_list_t head = { bad_qbuf };
            umq_buf_t *cur = nullptr;
            QBUF_LIST_FOR_EACH(cur, &head) {
                /* rpc adapter has replace brpc butil::iobuf::blockmem_allocate() &
                 * butil::iof::blockmem_deallocate()
                 * and ensures that the starting address of the Block is aligned to an 8k boundary. */
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur->buf_data))->DecRef();
            }

            if (bad_qbuf == tx_buf_list) {
                m_tx.m_unsolicited_wr_num = _unsolicited_wr_num;
                m_tx.m_unsolicited_bytes = _unsolicited_bytes;
                m_tx.m_unsignaled_wr_num = _unsignaled_wr_num;
                QBUF_LIST_FIRST(&m_tx.m_head_buf) =  head_qbuf;          
                QBUF_LIST_FIRST(&m_tx.m_tail_buf) = tail_qbuf;
                umq_buf_free(bad_qbuf);
                tx_total_len = 0;
            } else {
                tx_total_len = HandleBadQBuf(tx_buf_list,bad_qbuf,
                   _unsolicited_wr_num, _unsolicited_bytes, _unsignaled_wr_num);
            }
        }

        // After posting and before polling, the time for updating the count cna be concealed within the waiting period
        // for polling.
        if (m_stats_enable) {
            UpdateStats(StatsMgr::WRITEV_BATCH_CNT, batch);
            UpdateStats(StatsMgr::WRITEV_BUF_CNT, umq_buf_cnt);
            UpdateStats(StatsMgr::WRITEV_BYTES, tx_total_len);
        }

        if ((m_tx_window_capacity - m_tx.m_window_size) >= m_tx.m_handle_threshold) {
            PollTx(m_tx.m_retrieve_threshold);
        }

        return tx_total_len;
    }

    ALWAYS_INLINE void NewOriginEpollError()
    {
        m_closed.store(true, std::memory_order_relaxed);
    }

    ALWAYS_INLINE void NewOriginEpollIn()
    {
        m_rx.m_epoll_in_msg_recv_size = 
            OsAPiMgr::GetOriginApi()->recv(m_fd, (void *)&m_rx.m_epoll_in_msg, sizeof(uint8_t), MSG_NOSIGNAL);
        if (m_rx.m_epoll_in_msg_recv_size == 0) {
            m_closed.store(true, std::memory_order_relaxed);
        } else {
            RPC_ADPT_VLOG_ERR("Unexpected EPOLLIN event through TCP\n");
        }    
    }

    ALWAYS_INLINE void NewRxEpollIn()
    {
        if (m_rx.m_epoll_event_num.fetch_add(1, std::memory_order_acq_rel) == 0) {
            m_rx.m_get_and_ack_event = true;
            m_rx.m_poll = true;
            m_rx.m_expect_epoll_event_num = 1;
        }
    }

    ALWAYS_INLINE void NewTxEpollIn()
    {
        if (m_tx.m_epoll_event_num.fetch_add(1, std::memory_order_acq_rel) == 0) {
            m_tx.m_get_and_ack_event = true;
            m_tx.m_expect_epoll_event_num = 1;
        }
    }

    ALWAYS_INLINE void NewTxEpollInEventFd()
    {
        if (m_tx.m_epoll_event_num.fetch_add(1, std::memory_order_acq_rel) == 0) {
            m_tx.m_expect_epoll_event_num = 1;
        }
    }

    ALWAYS_INLINE int RearmRxInterrupt()
    {
        umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
        return umq_rearm_interrupt(m_local_umqh, false, &tx_option);
    }

    ALWAYS_INLINE int RearmTxInterrupt()
    {
        umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
        return umq_rearm_interrupt(m_local_umqh, true, &tx_option);
    }

    int PrefillRx()
    {
        uint32_t left_post_rx_num = m_rx_window_capacity;
        uint32_t cur_post_rx_num = 0;
        umq_alloc_option_t option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(IOBuf::Block) };
        do {
            cur_post_rx_num = left_post_rx_num > POST_BATCH_MAX ? POST_BATCH_MAX : left_post_rx_num;
            umq_buf_t *rx_buf_list = umq_buf_alloc(BrpcIOBufSize(), cur_post_rx_num, UMQ_INVALID_HANDLE, &option);
            if (rx_buf_list == nullptr) {
                RPC_ADPT_VLOG_ERR("Failed to allocate RX buffer, RX depth: %u\n", m_rx_window_capacity);
                return -1;
            }

            umq_buf_t *bad_qbuf = nullptr;
            if (umq_post(m_local_umqh, rx_buf_list, UMQ_IO_RX, &bad_qbuf) != UMQ_SUCCESS) {
                RPC_ADPT_VLOG_ERR("Failed to post RX buffer, RX depth: %u\n", m_rx_window_capacity);
                m_rx.m_window_size += HandleBadQBuf(rx_buf_list, bad_qbuf);
                return -1;
            }
            m_rx.m_window_size += cur_post_rx_num;
            RPC_ADPT_VLOG_DEBUG("Post RX depth: %u\n", cur_post_rx_num);
        } while ((left_post_rx_num -= cur_post_rx_num) > 0);

        uint32_t poll_cnt = 0;
        do {
            PollTx(m_tx.m_retrieve_threshold);
            if (umq_state_get(m_local_umqh) != QUEUE_STATE_IDLE) {
                break;
            }
            usleep(WAIT_UMQ_READY_TIMEOUT_US);
        } while (poll_cnt++ < WAIT_UMQ_READY_ROUND);

        if (umq_state_get(m_local_umqh) != QUEUE_STATE_READY) {
            RPC_ADPT_VLOG_ERR("Wait umq to be ready failed, umq state %d\n", umq_state_get(m_local_umqh));
            return -1;
        }

        return 0;
    }

    ALWAYS_INLINE bool GetBindRemote(void)
    {
        return m_bind_remote;
    }

    ALWAYS_INLINE void SetBindRemote(bool input)
    {
        m_bind_remote = input;
    }

    void *operator new(std::size_t size)
    {
        void *ptr = nullptr;
        if (posix_memalign(&ptr, CACHE_LINE_ALIGNMENT, size) != 0){
            throw std::bad_alloc();
        }

        return ptr;
    }

    void operator delete(void* ptr) noexcept
    {
        free(ptr);
    }

private:
    struct CpMsg {
        uint64_t magic_number = CONTROL_PLANE_MAGIC_NUMBER;
        uint64_t queue_bind_info_size;
        uint8_t queue_bind_info[UMQ_BIND_INFO_SIZE_MAX];
    };
    
    int CreateLocalUmq(umq_eid_t *connEid)
    {
        if (m_local_umqh != UMQ_INVALID_HANDLE) {
            return EEXIST;
        }

        Context *context = Context::GetContext();
        if (context->IsBonding() && connEid == nullptr){
            RPC_ADPT_VLOG_ERR("Failed to use eid, because eid is null\n");
            return -1;
        }
        umq_create_option_t queue_cfg;
        memset_s(&queue_cfg, sizeof(queue_cfg), 0, sizeof(queue_cfg));
        queue_cfg.trans_mode = context->GetTransMode();
        queue_cfg.create_flag = UMQ_CREATE_FLAG_TX_DEPTH | UMQ_CREATE_FLAG_RX_DEPTH |
            UMQ_CREATE_FLAG_RX_BUF_SIZE | UMQ_CREATE_FLAG_TX_BUF_SIZE | UMQ_CREATE_FLAG_QUEUE_MODE;
        queue_cfg.rx_depth = context->GetRxDepth();
        queue_cfg.tx_depth = context->GetTxDepth();
        queue_cfg.rx_buf_size = BrpcIOBufSize();
        queue_cfg.tx_buf_size = BrpcIOBufSize();
        queue_cfg.mode = UMQ_MODE_INTERRUPT;  
        
        int n = snprintf_s(queue_cfg.name, UMQ_NAME_MAX_LEN, UMQ_NAME_MAX_LEN - 1, "fd: %d", m_fd);
        if ((((int)UMQ_NAME_MAX_LEN - 1) < n) || (n < 0)) {
            RPC_ADPT_VLOG_ERR("Failed to set umq name\n");
            return -1;
        }

        if (context->GetDevIpStr() != nullptr) {
            if (context->IsDevIpv6()) {
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
                if (strcpy_s(queue_cfg.dev_info.ipv6.ip_addr, UMQ_IPV6_SIZE, context->GetDevIpStr()) != EOK) {
                    RPC_ADPT_VLOG_ERR("Failed to strcpy_s device ipv6 address\n");
                    return -1;
                } 
            } else {
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
                if (strcpy_s(queue_cfg.dev_info.ipv4.ip_addr, UMQ_IPV4_SIZE, context->GetDevIpStr()) != EOK) {
                    RPC_ADPT_VLOG_ERR("Failed to strcpy_s device ipv4 address\n");
                    return -1;
                }     
            }
        } else if (context -> GetDevNameStr() != nullptr) {
            if (strcpy_s(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, context->GetDevNameStr()) != EOK) {
                RPC_ADPT_VLOG_ERR("Failed to strcpy_s device name\n");
                return -1;
            }
            if(!context->IsBonding()){
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
                queue_cfg.dev_info.dev.eid_idx = context->GetEidIdx();
            } else {
                // init use bonding dev
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
                queue_cfg.dev_info.eid.eid = *connEid;
            }
        }

        m_local_umqh = umq_create(&queue_cfg);
        if (m_local_umqh == UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR("Failed to execute umq_create failed\n");
            return -1;
        }

        Context::FetchAdd();

        return 0;
    }

    void UnbindAndFlushRemoteUmq()
    {
        if (!m_bind_remote) {
            return;
        }

        if (m_tx.m_event_num > 0) {
            umq_interrupt_option_t option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX};
            umq_ack_interrupt(m_local_umqh, m_tx.m_event_num, &option);
            m_tx.m_event_num = 0;
        }

        if (m_rx.m_event_num > 0) {
            umq_interrupt_option_t option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX};
            umq_ack_interrupt(m_local_umqh, m_rx.m_event_num, &option);
            m_rx.m_event_num = 0;
        }
        
        (void)umq_unbind(m_local_umqh);
        FlushTx();
        FlushRx();
    }

    void DestroyLocalUmq(void)
    {
        if (m_local_umqh != UMQ_INVALID_HANDLE) {
            // need to flush
            (void)umq_destroy(m_local_umqh);
            m_local_umqh = UMQ_INVALID_HANDLE;
            Context::FetchSub();
        }
    }

    static int ValidateMagicNumber(int fd, uint64_t &magic_number, ssize_t &magic_number_recv_size)
    {
        magic_number_recv_size = 
           RecvSocketData(fd, &magic_number, sizeof(magic_number), NEGOTIATE_TIMEOUT_MS);
        if (magic_number_recv_size <= 0) {
            return -1;
        }
        
        if (magic_number_recv_size != sizeof(magic_number) || magic_number != CONTROL_PLANE_MAGIC_NUMBER) {
            return magic_number_recv_size;
        }

        return 0;
    }

    ALWAYS_INLINE ssize_t OutputErrorMagicNumber(const struct iovec *iov, int iovcnt)
    {
        if (m_tx.m_magic_number_recv_size == 0) {
            return 0;
        }

        ssize_t rx_total_len = 0;
        int iov_idx = 0;
        do {
            size_t copy_size = iov[iov_idx].iov_len < m_tx.m_magic_number_recv_size ?
                iov[iov_idx].iov_len : m_tx.m_magic_number_recv_size;
            (void)memcpy_s(iov[iov_idx++].iov_base, copy_size,
                (char *)&m_tx.m_magic_number + m_tx.m_magic_number_offset, copy_size);
            m_tx.m_magic_number_recv_size -= copy_size;
            m_tx.m_magic_number_offset += copy_size;
            rx_total_len += copy_size;        
        } while (m_tx.m_magic_number_recv_size > 0 && iov_idx < iovcnt);

        if (m_tx.m_magic_number_recv_size == 0) {
            m_rx_use_tcp = true;
        }

        return rx_total_len;
    }

    int AcceptExchangeEid(int new_fd, umq_eid_t *connEid){
        Context *context = Context::GetContext();
        if(!context->IsBonding()){
            return 0;
        }

        umq_eid_t remoteEid;
        if (RecvSocketData(
            new_fd, &remoteEid, sizeof(umq_eid_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_eid_t)) {
            RPC_ADPT_VLOG_ERR("Failed to receive remote eid message in accept, fd: %d\n", new_fd);
            return -1;
        }

        umq_eid_t localEid = context->GetDevSrcEid();
        if (SendSocketData(new_fd, &localEid, sizeof(umq_eid_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_eid_t)) {
            RPC_ADPT_VLOG_ERR("Failed to send local eid message in accept, fd: %d\n", new_fd);
            return -1;
        }

        *connEid = context->GetDevSrcEid();
        if (localEid != remoteEid) {
            umq_route_t connRoute;
            if (GetDevRouteList(&localEid, &remoteEid, &connRoute) != 0) {
                RPC_ADPT_VLOG_ERR("Failed to get route list in accept, fd: %d\n", new_fd);
                return -1;
            }

            if (SendSocketData(
                new_fd, &connRoute, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
                RPC_ADPT_VLOG_ERR("Failed to send connect eid message in accept, fd: %d\n", new_fd);
                return -1;
            }

            *connEid = connRoute.src;
        }

        return 0;
    }

    int DoAccept(int new_fd)
    {
        CpMsg local_cp_msg;
        CpMsg remote_cp_msg;
        char send_sync_msg[] = UMQ_BIND_SYNC_MSG;
        char recv_sync_msg[] = UMQ_BIND_SYNC_MSG;
        SocketFd *socket_fd_obj = nullptr;
        int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        try {
            socket_fd_obj = new SocketFd(new_fd, event_fd);
        } catch (std::exception& e) {
            OsAPiMgr::GetOriginApi()->close(event_fd);
            RPC_ADPT_VLOG_ERR("%s\n", e.what());
            return -1;
        }

        umq_eid_t connEid;
        if (AcceptExchangeEid(new_fd, &connEid) < 0) {
            RPC_ADPT_VLOG_ERR("Failed to exchange eid in accept\n");
            delete socket_fd_obj;
            return -1;
        }

        if (socket_fd_obj->CreateLocalUmq(&connEid) < 0) {
            RPC_ADPT_VLOG_ERR("Failed to create umq\n");
            delete socket_fd_obj;
            return -1;
        }

        local_cp_msg.queue_bind_info_size = umq_bind_info_get(
            socket_fd_obj->GetLocalUmqHandle(), local_cp_msg.queue_bind_info, sizeof(local_cp_msg.queue_bind_info));
        if (local_cp_msg.queue_bind_info_size == 0) {
            RPC_ADPT_VLOG_ERR("Failed to execute umq_bind_info_get\n");
            delete socket_fd_obj;
            return -1;
        }
        
        size_t len = sizeof(remote_cp_msg) - sizeof(uint64_t);
        if (RecvSocketData(
            new_fd, &remote_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) != (ssize_t)len) {
            RPC_ADPT_VLOG_ERR("Failed to receive remote control message, fd: %d\n", new_fd);
            delete socket_fd_obj;
            return -1;
        }
        RPC_ADPT_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %d, bind info len: %d\n", 
            new_fd, sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

        if (SendSocketData(
            new_fd, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local_cp_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to send local control message, fd: %d\n", new_fd);
            delete socket_fd_obj;
            return -1;
        }
        RPC_ADPT_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %d, bind info len: %d\n", 
            new_fd, sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);
        
        if (umq_bind(socket_fd_obj->GetLocalUmqHandle(), remote_cp_msg.queue_bind_info, 
            remote_cp_msg.queue_bind_info_size) != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR("Failed to execute umq_bind\n");
            delete socket_fd_obj;
            return -1;
        }
        socket_fd_obj->SetBindRemote(true);

        // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
        if (socket_fd_obj->PrefillRx() != 0) {
            RPC_ADPT_VLOG_ERR("Failed to fill rx buffer to umq, fd: %d\n", new_fd);
            delete socket_fd_obj;
            return -1;
        }

        if (SendSocketData(
            new_fd, send_sync_msg, sizeof(send_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(send_sync_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to send sync done message, fd: %d\n", new_fd);
            delete socket_fd_obj;
            return -1;
        }

        if (RecvSocketData(
            new_fd, recv_sync_msg, sizeof(recv_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(recv_sync_msg) ||
            strcmp(recv_sync_msg, UMQ_BIND_SYNC_MSG) != 0) {
            RPC_ADPT_VLOG_ERR("Failed to receive sync done message, fd: %d\n", new_fd);
            delete socket_fd_obj;
            return -1;
        }

        if (m_context_stats_enable) {
            socket_fd_obj->InitStatsMgr();
        }

        // Delete existing objects and record new objects in the list.
        Fd<::SocketFd>::OverrideFdObj(new_fd, socket_fd_obj);

        RPC_ADPT_VLOG_DEBUG("Accept to remote with UMQ successful, listen fd: %d, new fd: %d\n", m_fd, new_fd);

        return 0;  
    }

    ALWAYS_INLINE void *PtrFloorToBoundary(void *ptr)
    {
        return (void *)((uint64_t)ptr & ~FloorMask());
    }

    ALWAYS_INLINE void ProcessErrorTxCqe(umq_buf_t *first_qbuf)
    {
        umq_buf_t *cur_qbuf = first_qbuf;
        umq_buf_t *last_qbuf = nullptr;
        int64_t left_size = (int64_t)cur_qbuf->total_data_size;
        while (cur_qbuf != nullptr && left_size > 0) {
            left_size -= cur_qbuf->data_size;
            /* rpc adapter has replace brpc butil::iobuf::blockmeme_allocate() &
            * butil::iof::blockmem_deallocate() and ensures that the starting address
            * of the Block is aligned to an 8k boundary. */
            ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        }

        QBUF_LIST_NEXT(last_qbuf) = nullptr;
        umq_buf_free(first_qbuf);
    }

    ALWAYS_INLINE int ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf)
    {
        int wr_cnt = 0;
        umq_buf_t *cur_qbuf = start_qbuf;
        umq_buf_t *last_qbuf = nullptr;
        umq_buf_t *wr_first_buf;
        do {
            wr_first_buf = cur_qbuf;
            int64_t left_size = (int64_t)wr_first_buf->total_data_size;
            while (cur_qbuf != nullptr && left_size > 0) {
                left_size -= cur_qbuf->data_size;
                /* rpc adapter has replace brpc butil::iobuf::blockmeme_allocate() &
                * butil::iof::blockmem_deallocate() and ensures that the starting address
                * of the Block is aligned to an 8k boundary. */
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
                last_qbuf = cur_qbuf;
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            }
            wr_cnt++;
        } while (cur_qbuf != nullptr && wr_first_buf != end_qbuf);

        if (wr_first_buf == nullptr) {
            RPC_ADPT_VLOG_ERR("TX umq buffer list is in error, TX user context does not contain the right list\n");
            return -1;
        }

        QBUF_LIST_NEXT(last_qbuf) = nullptr;
        umq_buf_free(start_qbuf);

        return wr_cnt;
    }

    ALWAYS_INLINE int UmqPollTx(error_code &err_code)
    {
        umq_buf_t *buf[POLL_BATCH_MAX];
        int poll_num = umq_poll(m_local_umqh, UMQ_IO_TX, buf, POLL_BATCH_MAX);
        if (poll_num <= 0) {
            return poll_num;
        }

        int wr_cnt = 0;
        int cur_wr_cnt;
        umq_buf_t *first_qbuf = nullptr;
        for (int i = 0; i < poll_num; ++i) {
            if (buf[i] == nullptr || buf[i]->status != 0 ||
                (first_qbuf = (umq_buf_t *)((umq_buf_pro_t *)(buf[i]->qbuf_ext))->user_ctx) == nullptr) {
                
                // set err_code to true to force a quick exit from current function.
                err_code = SocketFd::UMQ_ERROR;
                
                if (buf[i] == nullptr) {
                    RPC_ADPT_VLOG_DEBUG("TX CQE is invalid, umq buffer is empty\n");
                    continue;
                }

                if (buf[i]->status != 0) {
                    ProcessErrorTxCqe(buf[i]);
                    wr_cnt++;
                    continue;
                }

                RPC_ADPT_VLOG_DEBUG("TX CQE is invalid, status: %d%s\n", buf[i]->status,
                    first_qbuf == nullptr ? ", and umq buffer list is empty" : "");
                continue;
            }

            cur_wr_cnt = ProcessTxCqe(first_qbuf, buf[i]);
            if (cur_wr_cnt < 0) {
                // set err_code to true to force a quick exit from current function.
                err_code = SocketFd::FATAL_ERROR;
                return wr_cnt;
            }

            wr_cnt += cur_wr_cnt;
        }

        return wr_cnt;
    }

    ALWAYS_INLINE int DpRearmTxInterrupt()
    {
        umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
        if (umq_rearm_interrupt(m_local_umqh, false, &tx_option) == 0) {
            errno = EAGAIN;
            return -1;
        }

        // 1. try to switch to tcp/ip
        // 2. use EIO for now
        errno = EIO;
        return -1;
    }

    ALWAYS_INLINE void PollTx(
        uint32_t threshold, bool poll_to_empty = false, uint32_t max_retry_cnt = POLL_TX_RETRY_MAX_CNT)
    {
        uint32_t poll_total_cnt = 0;
        int poll_cnt  = 0;
        uint32_t poll_zero_cnt = 0;
        error_code err_code = SocketFd::OK;
        do {
            poll_cnt = UmqPollTx(err_code);
            if (poll_cnt < 0) {
                break;
            } else if (poll_cnt == 0) {
                poll_zero_cnt++;
            } else if (poll_zero_cnt != 0) {
                // reset poll_zero_cnt when actually get tx cqe(s)
                poll_zero_cnt = 0;
            }
            poll_total_cnt += (uint32_t)poll_cnt;
        } while ((poll_total_cnt < threshold || (poll_to_empty && poll_cnt > 0)) &&
            poll_zero_cnt < max_retry_cnt && err_code == SocketFd::OK);
        m_tx.m_window_size += poll_total_cnt;    
    } 
    
    ALWAYS_INLINE void FlushTx(uint32_t timeout_ms = FLUSH_TIMEOUT_MS)
    {
        uint16_t threshold = m_tx_window_capacity - m_tx.m_window_size;
        if (threshold <= 0) {
            return;
        }

        uint32_t poll_total_cnt = 0;
        int poll_cnt = 0;
        error_code err_code = SocketFd::OK;
        auto start = std::chrono::high_resolution_clock::now();
        do {
            if (IsTimeout(start, timeout_ms)) {
                /* If a timeout is triggered here, it would indicate a memory leak.
                 * In this case, processing of unsignaled wr should not continue. */
                err_code = SocketFd::FATAL_ERROR;
                RPC_ADPT_VLOG_DEBUG("Flush TX operation exceeded timeout period(%u ms)\n", timeout_ms);
                break; 
            }

            poll_cnt = UmqPollTx(err_code);
            if (poll_cnt < 0) {
                break;
            }

            poll_total_cnt += (uint32_t)poll_cnt;
        } while (::ConfigSettings::GetSocketFdTransMode() != ::ConfigSettings::SOCKET_FD_TRANS_MODE_UNSET &&
            poll_total_cnt < threshold && err_code != SocketFd::FATAL_ERROR);
        m_tx.m_window_size += poll_total_cnt;
        
        if (err_code != SocketFd::FATAL_ERROR &&
            m_tx.m_window_size < m_tx_window_capacity && m_tx.m_unsignaled_wr_num > 0) {
            uint32_t left_wr_num = m_tx_window_capacity - m_tx.m_window_size;
            umq_buf_t *cur_qbuf = QBUF_LIST_FIRST(&m_tx.m_head_buf);
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
                 * exceptional cqe within the UmqPollTx() operation, so there is no need to handle these wrs
                 * again here. Consequently, only 0 ~ 2 wrs need to be processed. */
                int64_t rest_size = cur_qbuf->total_data_size;
                /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
                * the situation that rest_size would not reduced to zero */
                while (cur_qbuf && rest_size > 0) {
                     rest_size -= (int64_t)cur_qbuf->data_size;
                     last_qbuf = cur_qbuf;
                     cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
                }
                
                cached_wr_cnt++;
            }

            if (last_qbuf != nullptr) {
                QBUF_LIST_NEXT(last_qbuf) = nullptr;
            }

            umq_buf_free(QBUF_LIST_FIRST(&m_tx.m_head_buf));
            m_tx.m_window_size += cached_wr_cnt;
        }

        if (m_tx.m_window_size < m_tx_window_capacity) {
            RPC_ADPT_VLOG_DEBUG("Failed to flush umq(TX), leak %u wr(s) of buffer\n",
                m_tx_window_capacity - m_tx.m_window_size);
        }
    }

    ALWAYS_INLINE void FlushRx(uint32_t timeout_ms = FLUSH_TIMEOUT_MS)
    {
        m_rx.m_block_cache.Flush();

        if (m_rx.m_window_size <= 0) {
            return;
        }

        umq_buf_t *buf[POLL_BATCH_MAX];
        uint32_t poll_total_cnt = 0;
        int poll_cnt = 0;
        auto start = std::chrono::high_resolution_clock::now();
        do {
            if (IsTimeout(start, timeout_ms)) {
                RPC_ADPT_VLOG_DEBUG("Flush RX operation exceeded timeout period(%u ms)\n", timeout_ms);
                break; 
            }

            poll_cnt = umq_poll(m_local_umqh, UMQ_IO_RX, buf, POLL_BATCH_MAX);
            if (poll_cnt < 0) {
                break;
            }

            for (int i = 0; i < poll_cnt; i++) {
                if (buf[i]->status == UMQ_BUF_FLOW_CONTROL_UPDATE) {
                    if (eventfd_write(m_event_fd, 1) == -1) {
                        RPC_ADPT_VLOG_ERR("write event fd %d failed, errno %d\n", m_event_fd, errno);
                    }
                }
                umq_buf_free(buf[i]);
            }

            poll_total_cnt += (uint32_t)poll_cnt;
        } while (::ConfigSettings::GetSocketFdTransMode() != ::ConfigSettings::SOCKET_FD_TRANS_MODE_UNSET &&
            poll_total_cnt < m_rx.m_window_size);

        if ((m_rx.m_window_size -= poll_total_cnt) > 0) {
            RPC_ADPT_VLOG_DEBUG("Failed to flush umq(RX), leak %u piece(s) of buffer\n", m_rx.m_window_size);
        }
    }

    ALWAYS_INLINE void AckEvent(uint16_t &event_num, uint16_t add_event_num, umq_interrupt_option_t *option)
    {
        if ((event_num += add_event_num) >= GET_PER_ACK) {
            umq_ack_interrupt(m_local_umqh, event_num, option);
            event_num = 0;
        }
    }

    ALWAYS_INLINE int GetAndAckEvent(umq_io_direction_t dir)
    {
        umq_interrupt_option_t option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, dir };
        int events = umq_get_cq_event(m_local_umqh, &option);
        if (events == 0) {
            return 0;
        } else if (events < 0) {
            return -1;
        }

        AckEvent(dir == UMQ_IO_TX ? m_tx.m_event_num : m_rx.m_event_num, events, &option);

        return 0;
    }

    ALWAYS_INLINE int UmqPollAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size)
    {
        int poll_num = umq_poll(m_local_umqh, UMQ_IO_RX, buf, max_buf_size);
        if (poll_num < 0 || (poll_num == 0 && m_rx.m_window_size == 0)) {
            return -1;
        }

        m_rx.m_window_size -= poll_num;
        if (m_rx_window_capacity - m_rx.m_window_size > m_rx.m_refill_threshold) {
            umq_alloc_option_t option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(IOBuf::Block) };
            umq_buf_t *rx_buf_list = umq_buf_alloc(BrpcIOBufSize(), m_rx.m_refill_threshold, UMQ_INVALID_HANDLE,
                                                   &option);
            /* do nothing when failure occurs during refilling RX,
             * try to switch to tcp/ip until poll_num & m_rx.m_window_size both equal to zero */
            if (rx_buf_list != nullptr) {
                umq_buf_t *bad_qbuf = nullptr;
                if (umq_post(m_local_umqh, rx_buf_list, UMQ_IO_RX, &bad_qbuf) == UMQ_SUCCESS) {
                    m_rx.m_window_size += m_rx.m_refill_threshold;
                } else if ((m_rx.m_window_size += HandleBadQBuf(rx_buf_list, bad_qbuf)) == 0) {
                    return -1;
                }
            }
        }

        return poll_num;
    }

    ALWAYS_INLINE uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, uint16_t unsolicited_wr_num,
        uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num)
    {
        umq_buf_t *cur_qbuf = head_qbuf;
        umq_buf_t *last_qbuf = nullptr;
        umq_buf_t *head_qbuf_ = head_qbuf;
        uint32_t wr_cnt = 0;
        uint16_t _unsolicited_wr_num = unsolicited_wr_num;
        uint32_t _unsolicited_bytes = unsolicited_bytes;
        uint16_t _unsignaled_wr_num = unsignaled_wr_num;
        uint32_t total_size = 0;

        while (cur_qbuf != bad_qbuf) {
            int64_t rest_size = cur_qbuf->total_data_size;
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)cur_qbuf->qbuf_ext;
            if (buf_pro->flag.bs.solicited_enable == 1) {
                _unsolicited_wr_num = 0;
                _unsolicited_bytes = 0;
            } else {
                _unsolicited_wr_num++;
                _unsolicited_bytes += cur_qbuf->total_data_size;
            }

            if (buf_pro->flag.bs.complete_enable == 1) {
                _unsignaled_wr_num = 0;
            } else {
                _unsignaled_wr_num++;
            }

            total_size += cur_qbuf->total_data_size;

            /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
             * the situation that rest_size would not reduced to zero */
            while (cur_qbuf && rest_size > 0) {
                rest_size -= (int64_t)cur_qbuf->data_size;
                last_qbuf = cur_qbuf;
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            }

            if (buf_pro->flag.bs.complete_enable == 1) {
                /* If the last successfully posted wr has 'complete_enable' set, it means no need to cache
                 * the posted qbuf list anymore, then reset head to nullptr */
                head_qbuf_ = (cur_qbuf != bad_qbuf) ? cur_qbuf : nullptr;
            }
                
            wr_cnt++;
        }
        
        m_tx.m_unsolicited_wr_num = _unsolicited_wr_num;
        m_tx.m_unsolicited_bytes = _unsolicited_bytes;
        m_tx.m_unsignaled_wr_num = _unsignaled_wr_num;
        m_tx.m_window_size -= wr_cnt;

        QBUF_LIST_FIRST(&m_tx.m_head_buf) = head_qbuf_;
        if (last_qbuf != nullptr) {
            /* If head set to nullptr, it means no need to cache the posted qbuf list anymore, reset head
             * to nullptr as well */
            QBUF_LIST_FIRST(&m_tx.m_tail_buf) = (head_qbuf_ == nullptr) ? nullptr : last_qbuf;
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }

        umq_buf_free(bad_qbuf);
        return total_size;
    }
    
    ALWAYS_INLINE uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf)
    {
        umq_buf_t *cur_qbuf = head_qbuf;
        umq_buf_t *last_qbuf = nullptr;
        uint32_t wr_cnt = 0;
        while (cur_qbuf != bad_qbuf) {
            int64_t rest_size = cur_qbuf->total_data_size;
            /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
             * the situation that rest_size would not reduced to zero */
            while (cur_qbuf && rest_size > 0) {
                rest_size -= (int64_t)cur_qbuf->data_size;
                last_qbuf = cur_qbuf;
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            }

            wr_cnt++;
        }
        
        if (last_qbuf != nullptr) {
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }

        umq_buf_free(bad_qbuf);
        return wr_cnt;
    }

    int GetDevRouteList(const umq_eid_t *srcEid, const umq_eid_t *dstEid, umq_route_t *connRoute)
    {
        umq_route_t route;
        (void)memcpy_s(&route.src, sizeof(umq_eid_t), srcEid, sizeof(umq_eid_t));
        (void)memcpy_s(&route.dst, sizeof(umq_eid_t), dstEid, sizeof(umq_eid_t));

        umq_route_list_t route_list;
        int ret = umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
        if (ret!=0) {
            RPC_ADPT_VLOG_ERR("Failed to get urma topo\n");
            return -1;
        }

        if (route_list.len == 0) {
            RPC_ADPT_VLOG_ERR("Failed to get urma topo is zero\n");
            return -1;
        }

        for(uint32_t i = 0;i< route_list.len; ++i){
            if(route_list.buf[i].flag.bs.rtp == 1){
                *connRoute = route_list.buf[i];
                break;
            }         
        }

        if(connRoute == nullptr){
            RPC_ADPT_VLOG_ERR("Failed to find umq dev\n");
            return -1;
        }
        
        if(mEidRegistry.IsRegisteredEid(*srcEid)) {
            return 0;
        }
        
        for(uint32_t i = 0;i< route_list.len; ++i){
            umq_trans_info_t trans_info;
            trans_info.trans_mode = UMQ_TRANS_MODE_UB;
            trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            trans_info.dev_info.eid.eid = route_list.buf[i].src;
            ret = umq_dev_add(&trans_info);
            if(ret != 0){
                RPC_ADPT_VLOG_ERR("Failed to add umq dev\n");
                return -1;
            }
        }

        mEidRegistry.RegisterEid(*srcEid);
        return 0;
    }

    //common fields
    uint64_t m_local_umqh = UMQ_INVALID_HANDLE;
    uint16_t m_tx_window_capacity = 0; // the capacity of TX window size 
    uint16_t m_rx_window_capacity = 0; // the capacity of RX window size
    bool m_bind_remote = false; // indicate whether to enable stats manager when umq is created and bound successfully
    bool m_context_stats_enable = false;
    std::atomic<bool> m_closed{false};
    int m_event_fd;
    EidRegistry mEidRegistry;

    // TX fields
    struct alignas(CACHE_LINE_ALIGNMENT) TxDataPlane {
        uint64_t m_magic_number = 0;
        uint32_t m_magic_number_recv_size = 0;
        uint32_t m_magic_number_offset = 0;
        /* m_tx.m_head_buf -> |umq_buf 0| -> |umq_buf 1| -> ... -> |umq_buf n| <- m_tx.m_tailbuf */
        umq_buf_list_t m_head_buf = {0};
        umq_buf_list_t m_tail_buf = {0};
        uint32_t m_unsolicited_bytes = 0; //length of accumulated work request without setting solicited
        uint16_t m_unsolicited_wr_num = 0; // number of accumulated work request without setting solicited
        uint16_t m_unsignaled_wr_num = 0; // number of accumulated work request without setting signaled
        uint16_t m_window_size = 0; // current window size for TX
        uint16_t m_event_num = 0;
        uint16_t m_retrieve_threshold = 0; // when the polled WRs exceeds m_tx.m_retrieve_threshold, then, stop do poll
        uint16_t m_handle_threshold = 0; // when the polled WRs exceeds m_tx.m_handle_threshold, then, stop do poll TX
        /* when the polled WRs exceeds m_tx.m_report_threshold, set signaled/solicited to report and reset counter */
        uint16_t m_report_threshold = 0;
        std::atomic<int> m_epoll_event_num{0};
        int m_expect_epoll_event_num = 0;
        bool m_get_and_ack_event = false;
    } m_tx;

    // RX fields
    struct alignas(CACHE_LINE_ALIGNMENT) RxDataPlane {
        uint8_t m_epoll_in_msg = 0;
        uint8_t m_epoll_in_msg_recv_size = 0;
        uint16_t m_window_size = 0; // current window size for RX
        uint16_t m_event_num = 0;
        uint16_t m_refill_threshold = 0;
        std::atomic<int> m_epoll_event_num{0};
        int m_expect_epoll_event_num = 0;
        bool m_get_and_ack_event = false;
        bool m_poll = false;
        bool m_readv_unlimited = false;
        Brpc::BlockCache m_block_cache; 
    } m_rx;
};

class UmqTxEpollEvent : public ::EpollEvent {
public:
    UmqTxEpollEvent(int fd, struct epoll_event *event, int tx_interrupt_fd) :
        EpollEvent(tx_interrupt_fd, event), m_origin_fd(fd)
    {
        m_event.events = EPOLLOUT | EPOLLET;
    } 
    
    virtual ~UmqTxEpollEvent() {}

    virtual ALWAYS_INLINE
    int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewTxEpollIn();
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
    }

private:
    int m_origin_fd = -1;
};

class UmqEventFdEpollEvent : public ::EpollEvent {
public:
    UmqEventFdEpollEvent(int fd, struct epoll_event *event, int tx_interrupt_fd) :
        EpollEvent(tx_interrupt_fd, event), m_origin_fd(fd)
    {
        m_event.events = EPOLLOUT | EPOLLET;
    }

    virtual ~UmqEventFdEpollEvent() {}

    virtual int AddEpollEvent(int epoll_fd)
    {
        struct epoll_event tmp_event;
        tmp_event.events = EPOLLIN;
        tmp_event.data.ptr = (void *)(uintptr_t)this;

        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_fd, &tmp_event);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Origin epoll control add event fd failed, epfd: %d, fd: %d\n", epoll_fd, m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control add event fd successful, epfd: %d, fd: %d\n", epoll_fd, m_fd);
        m_add_epoll_event = true;

        return 0;
    }

    virtual ALWAYS_INLINE
    int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event) override
    {
        uint64_t cnt;
        if (eventfd_read(m_fd, &cnt) == -1) {
            RPC_ADPT_VLOG_WARN("read event fd %d failed, errno %d\n", m_fd, errno);
        }
        // notify tx epoll out
        input_event->events |= EPOLLOUT;

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewTxEpollInEventFd();
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
    }

private:
    int m_origin_fd = -1;
};

class UmqRxEpollEvent : public ::EpollEvent {
public:
    UmqRxEpollEvent(int fd, struct epoll_event *event, int rx_interrupt_fd) :
        EpollEvent(rx_interrupt_fd, event), m_origin_fd(fd)
    {
        m_event.events = EPOLLIN | EPOLLET;
    } 
    
    virtual ~UmqRxEpollEvent() {}

    virtual ALWAYS_INLINE
    int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewRxEpollIn();
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
    }

private:
    int m_origin_fd = -1;
};

class EpollEvent : public ::EpollEvent {
public:
    void CleanUp()
    {
        if (tx_epoll_event != nullptr) {
            delete tx_epoll_event;
            tx_epoll_event = nullptr;
        }

        if (event_fd_epoll_event != nullptr) {
            delete event_fd_epoll_event;
            event_fd_epoll_event = nullptr;
        }

        if (rx_epoll_event != nullptr) {
            delete rx_epoll_event;
            rx_epoll_event = nullptr;
        }
    }
    
    EpollEvent(int fd, struct epoll_event *event): ::EpollEvent(fd, event)
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return;
        }

        umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
        int tx_interrupt_fd = umq_interrupt_fd_get(socket_fd_obj->GetLocalUmqHandle(), &tx_option);
        if (tx_interrupt_fd < 0) {
            errno = EINVAL;
            throw std::runtime_error("Failed to get TX interrupt fd for umq\n");
        }

        umq_interrupt_option_t rx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
        int rx_interrupt_fd = umq_interrupt_fd_get(socket_fd_obj->GetLocalUmqHandle(), &rx_option);
        if (rx_interrupt_fd < 0) {
            errno = EINVAL;
            throw std::runtime_error("Failed to get RX interrupt fd for umq\n");
        }

        try {
            tx_epoll_event = new UmqTxEpollEvent(fd, event, tx_interrupt_fd);
            event_fd_epoll_event = new UmqEventFdEpollEvent(fd, event, socket_fd_obj->GetEventFd());
            rx_epoll_event = new UmqRxEpollEvent(fd, event, rx_interrupt_fd);
        } catch (std::exception& e) {
            CleanUp();
            throw std::runtime_error(e.what());
        }
    }

    virtual ~EpollEvent()
    {
        CleanUp();
    }

    virtual int AddEpollEvent(int epoll_fd) override
    {
        if (::EpollEvent::AddEpollEvent(epoll_fd) < 0) {
            return -1;
        }

        // try best to add umq tx/rx epoll event
        if ((m_event.events & EPOLLOUT) && tx_epoll_event != nullptr) {
            if (tx_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                RPC_ADPT_VLOG_WARN("Failed to add TX interrupt fd(%d) for umq, fd: %d\n",
                    tx_epoll_event->GetFd(), m_fd);
            } else {
                RPC_ADPT_VLOG_DEBUG("Add TX interrupt fd(%d) for umq successful, fd: %d\n",
                    tx_epoll_event->GetFd(), m_fd);
            }

            SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
            if (socket_fd_obj != nullptr && socket_fd_obj->GetBindRemote() && !socket_fd_obj->TxUseTcp()) {
                if (socket_fd_obj->RearmTxInterrupt() < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to rearm TX interrupt fd(%d) for umq, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Rearm TX interrupt fd(%d) for umq successful, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                }
            }

            if (event_fd_epoll_event != nullptr) {
                if (event_fd_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to add TX event fd(%d) for umq, fd: %d\n",
                                       event_fd_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Add TX event fd(%d) for umq successful, fd: %d\n",
                                        event_fd_epoll_event->GetFd(), m_fd);
                }
            }
        }

        if ((m_event.events & EPOLLIN) && rx_epoll_event != nullptr) {
            if (rx_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                RPC_ADPT_VLOG_WARN("Failed to add RX interrupt fd(%d) for umq, fd: %d\n",
                    rx_epoll_event->GetFd(), m_fd);
            } else {
                RPC_ADPT_VLOG_DEBUG("Add RX interrupt fd(%d) for umq successful, fd: %d\n",
                    rx_epoll_event->GetFd(), m_fd);
            }

            SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
            if (socket_fd_obj != nullptr && socket_fd_obj->GetBindRemote() && !socket_fd_obj->RxUseTcp()) {
                if (socket_fd_obj->RearmRxInterrupt() < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to rearm RX interrupt fd(%d) for umq, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Rearm RX interrupt fd(%d) for umq successful, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                }
            }
        }

        return 0;
    }

    virtual int ModEpollEvent(int epoll_fd, struct epoll_event *event) override
    {
        if (::EpollEvent::ModEpollEvent(epoll_fd, event) < 0) {
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return 0;
        }

        // try best to modify umq tx/rx epoll event
        if (tx_epoll_event != nullptr) {
            if (!tx_epoll_event->IsAddEpollEvent() && (event->events & EPOLLOUT)) {
                if (tx_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(add) TX interrupt fd(%d) for umq, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(add) TX interrupt fd(%d) for umq successful, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                }
            } else if (tx_epoll_event->IsAddEpollEvent() && !(event->events & EPOLLOUT)) {
                if (tx_epoll_event->DelEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(delete) TX interrupt fd(%d) for umq, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(delete) TX interrupt fd(%d) for umq successful, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                }
            }

            if (!event_fd_epoll_event->IsAddEpollEvent() && (event->events & EPOLLOUT)) {
                if (event_fd_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(add) TX event fd(%d) for umq, fd: %d\n",
                                       event_fd_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(add) TX event fd(%d) for umq successful, fd: %d\n",
                                        event_fd_epoll_event->GetFd(), m_fd);
                }
            }
        }

        if (rx_epoll_event != nullptr) {
            if (!rx_epoll_event->IsAddEpollEvent() && (event->events & EPOLLIN)) {
                if (rx_epoll_event->AddEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(add) RX interrupt fd(%d) for umq, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(add) RX interrupt fd(%d) for umq successful, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                }
            } else if (rx_epoll_event->IsAddEpollEvent() && !(event->events & EPOLLIN)) {
                if (rx_epoll_event->DelEpollEvent(epoll_fd) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(delete) RX interrupt fd(%d) for umq, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(delete) RX interrupt fd(%d) for umq successful, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                }
            }
        }

        return 0;
    }

    virtual int DelEpollEvent(int epoll_fd) override
    {
        if (::EpollEvent::DelEpollEvent(epoll_fd) < 0) {
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return 0;
        }

        // try best to delete umq tx/rx epoll event
        if (tx_epoll_event != nullptr &&
            tx_epoll_event->IsAddEpollEvent() && tx_epoll_event->DelEpollEvent(epoll_fd) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete TX interrupt fd(%d) for umq, fd: %d\n",
                tx_epoll_event->GetFd(), m_fd);
        }

        if (event_fd_epoll_event != nullptr &&
            event_fd_epoll_event->IsAddEpollEvent() && event_fd_epoll_event->DelEpollEvent(epoll_fd) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete TX event fd(%d) for umq, fd: %d\n",
                               tx_epoll_event->GetFd(), m_fd);
        }

        if (rx_epoll_event != nullptr &&
            rx_epoll_event->IsAddEpollEvent() && rx_epoll_event->DelEpollEvent(epoll_fd) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete RX interrupt fd(%d) for umq, fd: %d\n",
                rx_epoll_event->GetFd(), m_fd);
        }

        return 0;
    }

    virtual int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (input_event->events & EPOLLOUT) {
            if (socket_fd_obj != nullptr && !socket_fd_obj->UseTcp() && !socket_fd_obj->GetBindRemote()) {
                if (socket_fd_obj->DoConnect() != 0) {
                    RPC_ADPT_VLOG_WARN("Fatal error occurred, fd: %d fallback to TCP/IP", m_fd);
                    Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
                    /* Clear messages that already exist on the TCP link to prevent
                     * dirty messages from affecting user data transmission. */
                    (void)SocketFd::FlushSocketMsg(m_fd); 
                }
            } else {
                // EPOLLOUT events from original socket fd will be ignored
                return 0;
            }
        }

        if ((input_event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) &&
            socket_fd_obj != nullptr && !socket_fd_obj->RxUseTcp() && socket_fd_obj->GetBindRemote()) {
            socket_fd_obj->NewOriginEpollError();
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
        }

        if ((input_event->events & EPOLLIN) &&
            socket_fd_obj != nullptr && !socket_fd_obj->RxUseTcp() && socket_fd_obj->GetBindRemote()) {
            socket_fd_obj->NewOriginEpollIn();
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event);
    }

    UmqTxEpollEvent *GetUmqTxEpollEvent()
    {
        return tx_epoll_event;
    }

    UmqRxEpollEvent *GetUmqRxEpollEvent()
    {
        return rx_epoll_event;
    }

private:
    UmqTxEpollEvent *tx_epoll_event = nullptr;
    UmqEventFdEpollEvent *event_fd_epoll_event = nullptr;
    UmqRxEpollEvent *rx_epoll_event = nullptr;
};

class EpollFd : public ::EpollFd {
public:
    EpollFd(int epoll_fd) : ::EpollFd(epoll_fd) {}
    
    virtual ~EpollFd() {}

    virtual ALWAYS_INLINE int EpollCtlAdd(int fd, struct epoll_event *event) override
    {
        if (event == nullptr) {
            RPC_ADPT_VLOG_ERR("Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr) {
            return ::EpollFd::EpollCtlAdd(fd, event);
        }

        if (m_epoll_event_map.count(fd) > 0) {
            RPC_ADPT_VLOG_ERR("Origin epoll control add duplicated, epfd: %d, fd: %d\n", m_fd, fd);
            errno = EEXIST;
            return -1;
        }

        EpollEvent *epoll_event = nullptr;
        try {
            epoll_event = new EpollEvent(fd, event);
        } catch (std::exception& e) {
            RPC_ADPT_VLOG_ERR("%s\n", e.what());
            return -1;
        }

        if (epoll_event->AddEpollEvent(m_fd) < 0) {
            delete epoll_event;
            return -1;
        }

        m_epoll_event_map[fd] = epoll_event;

        return 0;
    }
};

}

#endif