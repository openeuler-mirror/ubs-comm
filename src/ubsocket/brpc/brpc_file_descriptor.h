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
#include <set>
#include <tuple>
#include <unordered_set>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <string>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <array>
#include "umq_pro_api.h"
#include "umq_errno.h"
#include "brpc_context.h"
#include "brpc_share_jfr.h"
#include "file_descriptor.h"
#include "configure_settings.h"
#include "qbuf_list.h"
#include "buffer_util.h"
#include "statistics.h"
#include "share_jfr_common.h"
#include "cli_message.h"
#include "umq_dfx_types.h"
#include "umq_dfx_api.h"
#include "utracer.h"
#include "net_common.h"
#include "ub_lock_ops.h"
#include "scope_exit.h"
#include "error.h"
#include "brpc_thread_pool.h"

#define UMQ_BIND_INFO_SIZE_MAX  (512)
#define DIVIDED_NUMBER          (2)
#define CACHE_LINE_ALIGNMENT    (64)
#define HANDLE_THRESHOLD        (2)
#define RETRIEVE_THRESHOLD      (1)
#define REPORT_THRESHOLD        (1)

#ifndef EID_FMT
#define EID_FMT "%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"
#endif

#ifndef EID_RAW_ARGS
#define EID_RAW_ARGS(eid) eid[0], eid[1], eid[2], eid[3], eid[4], eid[5], eid[6], eid[7], eid[8], eid[9], \
eid[10], eid[11], eid[12], eid[13], eid[14], eid[15]
#endif

#ifndef EID_ARGS
#define EID_ARGS(eid) EID_RAW_ARGS((eid).raw)
#endif

constexpr uint64_t SIZE_8K  = 8192;
constexpr uint64_t SIZE_16K = 16384;
constexpr uint64_t SIZE_32K = 32768;
constexpr uint64_t SIZE_64K = 65536;
constexpr uint64_t MASK_DIFF = 1;
constexpr uint64_t IOBUF_DIFF = 32;
constexpr uint16_t REFILL_THRESHOLD = 32;
constexpr int RETRY_NEEDED = 1;
// to improve the efficiency, do one ack event operation per GET_PER_ACK times get event operation(same as brpc)
constexpr uint32_t GET_PER_ACK = 32;
// currently, poll batch use 32 is for the balance of performance and efficiency
constexpr uint32_t POLL_BATCH_MAX = 32;

constexpr uint8_t MAX_PORT_COUNT = 8;

namespace Brpc {
using namespace Statistics;
// adapt to brpc, brpc IOBuf block use 8k as buffer slice with a 32 bytes head, thus, RX buffer size is 8160
inline uint32_t BrpcIOBufSize()
{
    umq_buf_block_size_t blockType = Context::GetContext()->GetIOBlockType();
    switch (blockType) {
        case BLOCK_SIZE_8K:  return SIZE_8K - IOBUF_DIFF;
        case BLOCK_SIZE_16K: return SIZE_16K - IOBUF_DIFF;
        case BLOCK_SIZE_32K: return SIZE_32K - IOBUF_DIFF;
        case BLOCK_SIZE_64K: return SIZE_64K - IOBUF_DIFF;
        default:
            return SIZE_8K - IOBUF_DIFF;
    }
}

// 描述在 Connect/Accept 间的握手状态
enum class UBHandshakeState : uint32_t {
    kOK = 0,
    // 初次握手
    kSTART = 1,
    // 初次握手失败，再次尝试
    kRETRY = 2,
    // 再次握手失败，用以通知客户端需要降级成 TCP
    kRETRY_FAILED_CHECK_OTHER_ROUTE = 3,
    // UB 握手失败，降级至 TCP
    kDEGRADE = 4,
    // UB 握手失败
    kFAILED = 6,
};

struct OtherRouteMessage {
    UBHandshakeState ub_handshake_state;
    umq_route_t other_route;
};

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

class EidRegistry {
public:
    static EidRegistry &Instance()
    {
        static EidRegistry inst;
        return inst;
    }
    bool RegisterEid(const umq_eid_t& eid) {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_registered_eids.insert(eid).second;
    }

    bool IsRegisteredEid(const umq_eid_t& eid) {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_registered_eids.count(eid) > 0;
    }

    bool UnregisterEid(const umq_eid_t& eid) {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_registered_eids.erase(eid) > 0;
    }

    // 控制建链轮询
    // 注册或者替换index值
    void RegisterOrReplaceEidIndex(const umq_eid_t& eid, uint32_t index) {
        ScopedUbExclusiveLocker sLock(m_mutex);
        m_eid_index_map[eid] = index;
    }

    // 仅检查eid是否存在（不获取值）
    bool IsRegisteredEidIndex(const umq_eid_t& eid) const {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_eid_index_map.find(eid) != m_eid_index_map.end();
    }

    // 获得index值
    bool GetEidIndex(const umq_eid_t& eid, uint32_t& index) const {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto it = m_eid_index_map.find(eid);
        if (it != m_eid_index_map.end()) {
            index = it->second;
            return true;
        }
        return false;
    }

    //删除 eid 及其值
    bool UnregisterEidIndex(const umq_eid_t& eid)
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_eid_index_map.erase(eid) > 0;
    }

private:
    EidRegistry()
    {
        m_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    ~EidRegistry()
    {
        g_external_lock_ops.destroy(m_mutex);
    }
    u_external_mutex_t* m_mutex;
    std::unordered_set<umq_eid_t, UmqEidHash> m_registered_eids;   // umq_dev_add eid
    std::unordered_map<umq_eid_t, uint32_t, UmqEidHash> m_eid_index_map; // bonding eidroute_list index
};

class RouteListRegistry {
public:
    static RouteListRegistry &Instance()
    {
        static RouteListRegistry inst;
        return inst;
    }
    // 注册或者替换routeList值
    void RegisterOrReplaceRouteList(const umq_eid_t &eid, const umq_route_list_t &routeList)
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        m_route_list_map[eid] = routeList;
    }

    // 仅检查eid对应的routeList是否存在（不获取值）
    bool IsRegisteredRouteList(const umq_eid_t &eid) const
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_route_list_map.find(eid) != m_route_list_map.end();
    }

    bool GetRouteList(const umq_eid_t &eid, umq_route_list_t &routeList) const
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto it = m_route_list_map.find(eid);
        if (it != m_route_list_map.end()) {
            routeList = it->second;
            return true;
        }
        return false;
    }

    //  RouteList
    bool UnregisterRouteList(const umq_eid_t &eid)
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_route_list_map.erase(eid) > 0;
    }

private:
    RouteListRegistry()
    {
        m_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    ~RouteListRegistry()
    {
        g_external_lock_ops.destroy(m_mutex);
    }
    u_external_mutex_t* m_mutex;
    std::unordered_map<umq_eid_t, umq_route_list_t, UmqEidHash> m_route_list_map; // route_list
};

class SocketFd : public ::SocketFd, public FallbackTcpMgr, public Statistics::StatsMgr {
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
        QBUF_LIST_INIT(&m_rx.m_umq_buf_cache_head);
        QBUF_LIST_INIT(&m_rx.m_umq_buf_cache_tail);
        m_async_accept_info.lock = g_external_lock_ops.create(LT_EXCLUSIVE);
        m_tx_window_capacity = Context::GetContext()->GetTxDepth();
        m_rx_window_capacity = Context::GetContext()->GetRxDepth();
        m_tx.m_window_size = m_tx_window_capacity;
        m_rx.m_refill_threshold = REFILL_THRESHOLD;
        m_tx.m_handle_threshold = HANDLE_THRESHOLD;
        m_tx.m_retrieve_threshold = RETRIEVE_THRESHOLD;
        m_tx.m_report_threshold = REPORT_THRESHOLD;
        m_rx.m_readv_unlimited = Context::GetContext()->GetReadvUnlimited();
        if (Context::GetContext()->GetTraceEnable()) {
            m_context_trace_enable = true;
        }
        m_event_fd = event_fd;
        m_rx.m_remaining_size = 0;
    }

    SocketFd(int fd, uint64_t protocol_negotiation, uint32_t protocol_negotiation_recv_size) :
        ::SocketFd(fd), StatsMgr(fd)
    {
        m_tx.m_protocol_negotiation = protocol_negotiation;
        m_tx.m_protocol_negotiation_recv_size = protocol_negotiation_recv_size;
        m_tx_use_tcp = true;
    }

    virtual ~SocketFd()
    {
        while (m_async_accept_info.asyncTaskNum.load() != 0) {
            usleep(100);
        }

        UnbindAndFlushRemoteUmq();
        DestroyLocalUmq();
        if (m_event_fd >= 0) {
            OsAPiMgr::GetOriginApi()->close(m_event_fd);
            m_event_fd = -1;
        }

        if (rxQueue != nullptr) {
            delete rxQueue;
            rxQueue = nullptr;
        }

        if (m_async_accept_info.lock != nullptr) {
            g_external_lock_ops.destroy(m_async_accept_info.lock);
        }

        if (share_jfr_rx_epoll_event != nullptr && !share_jfr_rx_epoll_event->IsAddEpollEvent()) {
            delete share_jfr_rx_epoll_event;
            share_jfr_rx_epoll_event = nullptr;
        }

        if (m_context_trace_enable) {
            SubMConnCount();
            if (m_peer_info.type_fd == 1) {
                SubMActiveConnCount();
            }
        }
    }

    ALWAYS_INLINE const std::string& GetPeerIp() const { return m_peer_info.peer_ip; }
    ALWAYS_INLINE const umq_eid_t& GetPeerEid() const { return m_peer_info.peer_eid; }
    ALWAYS_INLINE int GetPeerFd() const { return m_peer_info.peer_fd; }
    ALWAYS_INLINE int GetEventFd(void) { return m_event_fd; }
    ALWAYS_INLINE uint64_t GetLocalUmqHandle(void) { return m_local_umqh; }
    ALWAYS_INLINE uint64_t GetMainUmqHandle(void) { return m_main_umqh; }

    /**
     * @brief 从sockaddr结构体提取IP地址字符串
     * @param address 指向sockaddr结构体的指针
     * @return 成功返回IP地址字符串，失败返回空字符串
     */
    ALWAYS_INLINE std::string ExtractIpFromSockAddr(const struct sockaddr *address)
    {
        if (address == nullptr) {
            return "";
        }

        char ip_str[INET6_ADDRSTRLEN] = {0};
        const char* result = nullptr;

        if (address->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)address;
            result = inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN);
        } else if (address->sa_family == AF_INET6) {
            struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)address;
            result = inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
        }

        return (result != nullptr) ? std::string(ip_str) : "";
    }

    ALWAYS_INLINE void ProcessUBConnection(int fd, const std::string& peerIp)
    {
        bool is_blocking = IsBlocking(fd);
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(fd);
        }

        uint64_t protocol_negotiation = 0;
        ssize_t protocol_negotiation_recv_size = 0;
        int ret = ValidateProtocol(fd, protocol_negotiation, protocol_negotiation_recv_size);
        Context *context = Context::GetContext();
        if (ret > 0 && !context->AutoFallbackTCP()) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to accept as protocol dismatch,Peer IP:%s\n",
                GetPeerIp().c_str());
            OsAPiMgr::GetOriginApi()->close(fd);
            return;
        }
        if (ret > 0) {
            OsAPiMgr::GetOriginApi()->close(fd);
        } else if (ret == 0) {
            auto err = DoAccept(fd, peerIp);
            if (!IsOk(err)) {
                if (Degradable(err)) {
                    // 降级至 TCP，客户端可正确工作，不应清理数据.
                } else {
                    RPC_ADPT_VLOG_WARN("Fatal error occurred,Peer IP:%s, fd: %d fallback to TCP/IP\n",
                                       GetPeerIp().c_str(), fd);
                    // Clear messages that already exist on the TCP link to prevent
                    // dirty messages from affecting user data transmission
                    FlushSocketMsg(fd);
                }
            }
        }

        if (is_blocking) {
            // reset
            SetBlocking(fd);
        }
    }

    static bool IsTfoConnection(const int &fd)
    {
        tcp_info info{};
        socklen_t len = sizeof(info);
        bool is_tfo_connection = false;
        if (OsAPiMgr::GetOriginApi()->getsockopt_ptr(fd, SOL_TCP, TCP_INFO, &info, &len) == 0) {
            // check TCPI_OPT_SYN_DATA
            is_tfo_connection = (info.tcpi_options & TCPI_OPT_SYN_DATA) != 0;
        }
        RPC_ADPT_VLOG_INFO("Current tcpi_options: 0x%x, tfo connection: %s \n", info.tcpi_options,
            is_tfo_connection ? "true" : "false");
        return is_tfo_connection;
    }

    ALWAYS_INLINE int Accept(struct sockaddr *address, socklen_t *address_len)
    {
        int retCode = 0;
        int fd = -1;
        TRACE_DELAY_AUTO(BRPC_ACCEPT_CALL, retCode);
        if (Context::GetContext()->UseAsyncAccept()) {
            ScopedUbExclusiveLocker sLock(m_async_accept_info.lock);
            if (!m_async_accept_info.ready_queue.empty()) {
                auto tmp = m_async_accept_info.ready_queue.front();
                m_async_accept_info.ready_queue.pop();
                fd = std::get<0>(tmp);
                if (address != nullptr) {
                    *address = std::get<1>(tmp);
                    *address_len = std::get<2>(tmp);
                }
                RPC_ADPT_VLOG_DEBUG("found ready fd, return directly, fd %d\n", fd);
                return fd;
            }
        }

        struct sockaddr addr_tmp;
        socklen_t len_tmp;
        fd = OsAPiMgr::GetOriginApi()->accept(m_fd, &addr_tmp, &len_tmp);
        if (fd >= 0) {
            // 前置判断，如果不是TFO连接，作为普通TCP连接处理
            if (!IsTfoConnection(fd)) {
                return fd;
            }
            int tcpNoDelayRet = SetTcpNoDelay(fd);
            if (tcpNoDelayRet != 0) {
                RPC_ADPT_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", fd, tcpNoDelayRet, errno);
            }
        }

        std::string peerIp;
        if (fd >= 0 && address != nullptr) {
            *address = addr_tmp;
            *address_len = len_tmp;
            // 使用提取的接口获取IP地址
            peerIp = ExtractIpFromSockAddr(address);
            SocketFd* socket_fd_obj = static_cast<SocketFd*>(Fd<::SocketFd>::GetFdObj(fd));
            if (socket_fd_obj) {
                socket_fd_obj->m_peer_info.peer_ip = peerIp;
                // 对端fd就是accept返回的fd
                socket_fd_obj->m_peer_info.peer_fd = fd;
                socket_fd_obj->m_conn_info.create_time = std::chrono::system_clock::now();
            }
        }
        retCode = fd;
        if (m_tx_use_tcp || m_rx_use_tcp) {
            return fd;
        }

        if (fd < 0) {
            /*
            * 1. 若全连接队列不为空：
            * a. 正常情况下，返回非负整数的fd，tcp连接已完成，则执行DoAccept，且需要等待ub连接完成再返回，
            * b. 异常情况下，比如内存不足、文件描述符达到系统上限、客户端异常中止连接等，保持原错误码直接返回上层，由上层应用决定后续动作
            * 2. 若全连接队列为空：
            * a. fd为非阻塞，则返回-1，errno为EAGAIN/EWOULDBLOCK，保持原错误码直接返回上层
            * b. fd为阻塞，则等待直到有连接完成或者触发异常，比如被信号中断，返回-1，errno为EINTR，保持原错误码直接返回上层
            */
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {  // nonblocking
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "accept() failed, Peer IP:%s, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    GetPeerIp().c_str(), m_fd, fd, errno,
                    NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                return fd;
            }
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_DEBUG("tcp accept need try again, fd: %d, %d, %s\n",
                m_fd, errno, NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return fd;
        }

        if (Context::GetContext()->UseAsyncAccept()) {
            auto exec_ret = ExecutorService::GetExecutorService()->Execute([this, fd, addr_tmp, len_tmp]() {
                    RPC_ADPT_VLOG_DEBUG("async accept start. fd:%d\n", fd);
                    std::string ip = ExtractIpFromSockAddr(&addr_tmp);
                    ProcessUBConnection(fd, ip);
                    {
                        ScopedUbExclusiveLocker sLock(m_async_accept_info.lock);
                        m_async_accept_info.ready_queue.push(std::make_tuple(fd, addr_tmp, len_tmp));
                    }
                    
                    SocketEpollMapper* mapper = GetSocketEpollMapper(m_fd);
                    int epoll_fd = mapper->QueryFirst();
                    if (epoll_fd >= 0) {
                        EpollFd *obj = Fd<EpollFd>::GetFdObj(epoll_fd);
                        obj->WakeUpReadyEventFd(m_fd);
                    }
                    m_async_accept_info.asyncTaskNum.fetch_sub(1U);
                    RPC_ADPT_VLOG_DEBUG("async accept success. fd:%d\n", fd);
                });
            if (exec_ret == true) {
                m_async_accept_info.asyncTaskNum.fetch_add(1U);
            } else {
                RPC_ADPT_VLOG_WARN("submit async accept task failed, use sync accept. fd:%d\n", fd);
                ProcessUBConnection(fd, peerIp);
                return fd;
            }

            errno = EAGAIN;
            return -1;
        } else {
            ProcessUBConnection(fd, peerIp);
            return fd;
        }
    }

    int FillLocalSocketIdsForNegotiate(Context *context, uint32_t *socket_ids, uint32_t &socket_id_count)
    {
        std::vector<uint32_t> ids = context->GetAllSocketId();
        if (ids.empty() || ids.size() > NEGOTIATE_SOCKET_ID_MAX_NUM) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Invalid local socket ids, size %zu, Peer IP:%s, fd: %d\n",
                ids.size(), GetPeerIp().c_str(), m_fd);
            return -1;
        }
        socket_id_count = static_cast<uint32_t>(ids.size());
        for (uint32_t i = 0; i < socket_id_count; ++i) {
            socket_ids[i] = ids[i];
        }
        return 0;
    }

    int ConnectNegotiate(umq_eid_t *connEid, umq_route_t &connRoute, umq_eid_t &remoteEid)
    {
        Context *context = Context::GetContext();
        umq_eid_t localEid = context->GetDevSrcEid();
        dev_schedule_policy schedulePolicy = context->GetDevSchedulePolicy();
        NegotiateRsp rsp {};
        if (RecvSocketData(m_fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) != static_cast<int>(sizeof(rsp))) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Failed to receive negotiate response in connect,Peer IP:%s, fd: %d\n",
                GetPeerIp().c_str(), m_fd);
            return -1;
        }
        if (rsp.ret_code != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to negotiate in connect, peer ret %d, Peer IP:%s, fd: %d\n",
                rsp.ret_code, GetPeerIp().c_str(), m_fd);
            return -1;
        }
        ub_trans_mode local = context->GetUbTransMode();
        if (rsp.peer_trans_mode != local) {
            context->SetUbTransMode(rsp.peer_trans_mode < local ? rsp.peer_trans_mode : local);
        }

        bool useRoundRobin = true;
        if (schedulePolicy == dev_schedule_policy::CPU_AFFINITY || schedulePolicy ==
            dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
            RPC_ADPT_VLOG_WARN("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                static_cast<int>(schedulePolicy), m_fd);
            useRoundRobin = false;
        }

        if (context->IsBonding() == 1) {
            remoteEid = rsp.local_eid;
            if (DoRoute(&localEid, &remoteEid, &connRoute, useRoundRobin) != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get route list in connect, fd: %d\n", m_fd);
                return -1;
            }

            if (SendSocketData(
                m_fd, &connRoute, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "Failed to send connect eid message in connect, fd: %d\n",
                    m_fd);
                return -1;
            }

            m_peer_info.peer_eid = connRoute.dst_eid;
            *connEid = connRoute.src_eid;
        }
        return 0;
    }

    ubsocket::Error DoUbConnect(umq_eid_t &connEid)
    {
        CpMsg local_cp_msg;
        CpMsg remote_cp_msg;

        ubsocket::Error ret = CreateLocalUmq(&connEid);
        if (!IsOk(ret)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to create umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            return ret;
        }

        local_cp_msg.queue_bind_info_size =
            umq_bind_info_get(m_local_umqh, local_cp_msg.queue_bind_info, UMQ_BIND_INFO_SIZE_MAX);
        if (local_cp_msg.queue_bind_info_size == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_bind_info_get() failed, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ret: %u\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd, local_cp_msg.queue_bind_info_size);
            return ubsocket::Error::kUMQ_BIND_INFO_GET | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        }

        uint32_t len = sizeof(local_cp_msg) - sizeof(uint64_t);
        if (SendSocketData(m_fd, &local_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) != len) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Failed to send local control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            return ubsocket::FromRaw(errno);
        }
        RPC_ADPT_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %d, bind info len: %d\n",
            m_fd, sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

        if (RecvSocketData(
            m_fd, &remote_cp_msg, sizeof(remote_cp_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(remote_cp_msg)) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Failed to receive remote control message,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            return ubsocket::FromRaw(errno);
        }
        RPC_ADPT_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %d, bind info len: %d\n",
            m_fd, sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

        int umq_ret = umq_bind(m_local_umqh, remote_cp_msg.queue_bind_info, remote_cp_msg.queue_bind_info_size);
        if (umq_ret != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_bind() failed, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d, ret: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd, umq_ret);
            return ubsocket::Error::kUMQ_BIND | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        }
        m_bind_remote = true;

        if (Context::GetContext()->EnableShareJfr()) {
            // 强依赖当前实现，一个 eid 只对应一个主 umq. 如果后续逻辑有变更，需同步修改。
            auto main_umq = EidUmqTable::GetFirst(m_conn_info.conn_eid);
            if (main_umq == nullptr) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "The main umq is removed by other thread.\n");
                return ubsocket::Error::kUBSOCKET_NO_MAIN_UMQ;
            }

            return main_umq->EnsurePrefilled([this]() {
                if (PrefillRx() != 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                                      "Failed to fill rx buffer to main umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                                      EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                    return ubsocket::Error::kUBSOCKET_PREFILL_RX;
                }
                return ubsocket::Error::kOK;
            });
        }

        // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
        if (PrefillRx() != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to fill rx buffer to umq,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            return ubsocket::Error::kUBSOCKET_PREFILL_RX;
        }

        return ubsocket::Error::kOK;
    }

    int DoConnect(void)
    {
        umq_eid_t connEid;
        umq_route_t connRoute;
        umq_eid_t peerBondingEid;
        m_route_backup_src_eid = {};
        mTopoType = UMQ_TOPO_TYPE_FULLMESH_1D;
        if (ConnectNegotiate(&connEid, connRoute, peerBondingEid) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to negotiate in connect,Peer IP:%s, fd: %d\n",
                GetPeerIp().c_str(), m_fd);
            return -1;
        }
        m_conn_info.conn_eid = connEid;

        Context *context = Context::GetContext();
        umq_eid_t localEid = context->GetDevSrcEid();

        // 1. 用户直接指定普通设备建链，不重试、可降级
        // 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，不重试、可降级
        // 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误，优先重试，如果重试仍旧失败则降级
        bool ok = false;
        bool degradable = false;
        ubsocket::Error ackRet = ubsocket::Error::kOK;
        ubsocket::Error peerRet;
        umq_route_t otherConnRoute;
        OtherRouteMessage otherRouteMessage;
        UBHandshakeState state = UBHandshakeState::kSTART;
        dev_schedule_policy schedulePolicy = context->GetDevSchedulePolicy();
        while (!ok) {
            switch (state) {
                case UBHandshakeState::kOK:
                    ok = true;
                    break;

                case UBHandshakeState::kSTART:
                    // 作为客户端，它的 Degradable 属性对于是否降级不生效. Degradable 仅当角色为服务端时生效
                    if (context->IsBonding()) {
                        ackRet = CheckRouteDevAddForConnect(connEid);
                    }
                    
                    if (IsOk(ackRet)) {
                        ackRet = DoUbConnect(connEid);
                    }
                    if (!IsOk(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to finish ub bind in connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                    }

                    if (SendSocketData(m_fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        return -1;
                    }

                    if (RecvSocketData(m_fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) != sizeof(peerRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        return -1;
                    }

                    // 如果服务端支持降级则客户端需要配合
                    degradable = Degradable(peerRet);
                    if (IsOk(ackRet) && IsOk(peerRet)) {
                        state = UBHandshakeState::kOK;
                    } else if (context->IsBonding() && connEid != localEid &&
                               (Retryable(ackRet) || Retryable(peerRet))) {
                        state = UBHandshakeState::kRETRY;
                    } else if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kRETRY:
                    if (schedulePolicy == dev_schedule_policy::CPU_AFFINITY) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "CPU_AFFINITY:%d failed, connect no need to retry,Peer eid:" EID_FMT
                            ",Peer IP:%s, fd: %d\n",
                            static_cast<int>(schedulePolicy), EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);

                        if (degradable) {
                            state = UBHandshakeState::kDEGRADE;
                        } else {
                            state = UBHandshakeState::kFAILED;
                        }
                        break;
                    }
                    UnbindAndFlushRemoteUmq();
                    DestroyLocalUmq();

                    if (CheckOtherRoute(otherConnRoute, peerBondingEid, connRoute) != 0) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to get other route in retry,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        state = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
                        break;
                    }

                    otherRouteMessage.ub_handshake_state = UBHandshakeState::kRETRY;
                    otherRouteMessage.other_route = otherConnRoute;
                    if (SendSocketData(m_fd, &otherRouteMessage, sizeof(otherRouteMessage),
                                       CONTROL_PLANE_TIMEOUT_MS) != sizeof(otherRouteMessage)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send connect eid message in retry connect, Peer eid:" EID_FMT
                            ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        return -1;
                    }

                    connEid = otherConnRoute.src_eid;
                    ackRet = DoUbConnect(connEid);
                    if (!IsOk(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to finish ub bind in retry connect, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                    }

                    if (SendSocketData(m_fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send ack ret in retry connect, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        return -1;
                    }

                    if (RecvSocketData(m_fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                        return -1;
                    }

                    degradable = Degradable(peerRet);
                    if (IsOk(ackRet) && IsOk(peerRet)) {
                        state = UBHandshakeState::kOK;
                    } else if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE:
                    // 客户端在 kRETRY 错误时会进入 kRETRY_FAILED_CHECK_OTHER_ROUTE，但是服务端仍处于 kRETRY 阶段，
                    // 需要发送信令通知服务端，此种情况下 other_route 字段不可用
                    otherRouteMessage.ub_handshake_state = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
                    if (SendSocketData(m_fd, &otherRouteMessage, sizeof(otherRouteMessage),
                                       CONTROL_PLANE_TIMEOUT_MS) != sizeof(otherRouteMessage)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send connect eid message in retry connect,Peer eid:" EID_FMT
                            ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                    }

                    if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kDEGRADE:
                    Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
                    RPC_ADPT_VLOG_INFO("ubsocket is degraded to TCP.\n");
                    return 0;

                case UBHandshakeState::kFAILED:
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                        "Failed to get new connect in connect, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                        EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
                    return -1;
            }
        }

        if (m_context_trace_enable) {
            InitStatsMgr();
        }

        if (context && context->GetUsePolling()) {
            Socket *sock = NULL;
            if (PollingEpoll::GetInstance().SocketCreate(&sock, m_fd, SocketType::SOCKET_TYPE_TCP_CLIENT,
                                                         m_local_umqh) != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "SocketCreate failed,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                    EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            } else {
                PollingEpoll::GetInstance().AddSocket(m_fd, sock);
            }
        }

        m_conn_info.create_time = std::chrono::system_clock::now();
        RPC_ADPT_VLOG_INFO("UB connection has been successfully established new fd: %d\n", m_fd);

        PrintQbufPoolInfo();

        return 0;
    }

    ALWAYS_INLINE int Listen(int backlog)
    {
        RPC_ADPT_VLOG_INFO("Enable Server TFO, with QLen %d\n", backlog);
        // enable tfo
        if (SetSockOpt(m_fd, SOL_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog)) < 0) {
            RPC_ADPT_VLOG_WARN("Unable to enable server TFO");
        }
        return OsAPiMgr::GetOriginApi()->listen(m_fd, backlog);
    }

    ALWAYS_INLINE int Connect(const struct sockaddr *address, socklen_t address_len)
    {
        SetFdType(FdType::SOCKET_FD);
        int ret = 0;
        TRACE_DELAY_AUTO(BRPC_CONNECT_CALL, ret);
        // 判断TCPI_OPT_SYN_DATA，如果已置位则复用
        NegotiateReq req {};
        if (BuildNegotiateReq(&req) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to send negotiate request caused by building req failure\n");
            return -1;
        }
        bool is_blocking = IsBlocking(m_fd);
        if (!is_blocking) {
            SetBlocking(m_fd);
        }

        constexpr int fast_open = 1;
        OsAPiMgr::GetOriginApi()->setsockopt(m_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
        ssize_t sendto_ret = OsAPiMgr::GetOriginApi()->sendto(
            m_fd, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
        ret = sendto_ret < 0 ? -1 : 0;
        if (ret < 0 && errno != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "TFO sendto[1] failed, ret: %zd, errno %d, err msg: %s, fd %d\n",
                sendto_ret, errno, NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), m_fd);
        }
        if (!IsTfoConnection(m_fd)) {
            // 首次获取cookie，第二次发送
            RPC_ADPT_VLOG_INFO("TFO Cookie not found or not used. Retrying for immediate SYN+Data.\n");
            // 创建临时socket发送
            const int tmp_fd = OsAPiMgr::GetOriginApi()->socket(AF_INET, SOCK_STREAM, 0);
            OsAPiMgr::GetOriginApi()->setsockopt(tmp_fd, SOL_TCP, TCP_FASTOPEN, &fast_open, sizeof(fast_open));
            sendto_ret = OsAPiMgr::GetOriginApi()->sendto(
                tmp_fd, &req, sizeof(req), MSG_FASTOPEN, address, address_len);
            ret = sendto_ret < 0 ? -1 : 0;
            if (ret < 0 && errno != 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "TFO sendto[2] failed, ret: %zd, errno %d, err msg: %s, fd %d\n",
                    sendto_ret, errno, NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), tmp_fd);
            }

            int dup3_ret = dup3(tmp_fd, m_fd, O_CLOEXEC);
            OsAPiMgr::GetOriginApi()->close(tmp_fd);
            if (dup3_ret < 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "dup3 failed, ret: %d, errno %d, err msg: %s, tmp_fd %d, m_fd %d\n",
                    dup3_ret, errno, NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), tmp_fd, m_fd);
                return -1;
            }
        } else {
            // 已经是tfo连接，继续处理
            RPC_ADPT_VLOG_INFO("TFO Cookie exists, continue...\n");
        }

        if (!is_blocking) {
            SetNonBlocking(m_fd);
        }

        if (address != nullptr) {
            // 使用提取的接口获取IP地址
            std::string peer_ip = ExtractIpFromSockAddr(address);
            m_peer_info.peer_ip = peer_ip;
            // 对端fd就是accept返回的fd
            m_peer_info.peer_fd = m_fd;
            m_conn_info.create_time = std::chrono::system_clock::now();
        }

        if (m_tx_use_tcp || m_rx_use_tcp || !IsTfoConnection(m_fd)) {
            return ret;
        }

        if (ret == 0) {
            RPC_ADPT_VLOG_INFO("tcp connect succeed, fd %d\n", m_fd);
        } else {
            /* fd是非阻塞套接字
            * 1. 第一次调用connect返回-1，errno为EINPROGRESS，网络正在建连；
            * 2. 若未建连状态下，第n次对fd调用connect，n>=2，返回-1，errno为EALREADY；
            * 3. 若建连成功，且当前不是第二次调connect，返回-1，errno为EISCONN；否则返回0（非阻塞套接字）
            *
            * 若ret = 0 或者errno 是EISCONN，tcp连接已完成，则执行DoConnect，且需要等待连接完成再返回，
            * 若errno是EINPROGRESS/EALREADY，fd最终会变为连接状态，则执行DoConnect，且不需要等待ub连接完成
            * 若errno是EINTR/EADDRNOTAVAIL/EHOSTUNREACH等错误码，tcp连接失败，则不执行DoConnect，保持原错误码直接返回上层，由上层应用决定后续动作
            */
            if (errno == EINPROGRESS || errno == EALREADY) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_DEBUG("tcp connect inprogress:%s, fd %d\n",
                    NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), m_fd);
            } else if (errno != EISCONN) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "connect() failed, ret: %d, errno: %d, errmsg: %s, fd: %d\n", ret, errno,
                    NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE), m_fd);
                return ret;
            }
        }

        int tcpNoDelayRet = SetTcpNoDelay(m_fd);
        if (tcpNoDelayRet != 0) {
            RPC_ADPT_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", m_fd, tcpNoDelayRet, errno);
        }
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(m_fd);
        }

        ret = DoConnect();
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to establish UB connection,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), m_fd);
            Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
            /* Clear messages that already exist on the TCP link to prevent 
                 * dirty messages from affecting user data transmission*/
            FlushSocketMsg(m_fd);
        }

        if (is_blocking) {
            // reset
            SetBlocking(m_fd);
        }

        if (m_context_trace_enable) {
            m_peer_info.type_fd = 1;
            UpdateTraceStats(StatsMgr::CONN_COUNT, 1);
            UpdateTraceStats(StatsMgr::ACTIVE_OPEN_COUNT, 1);
        }

        return ret;
    }

    ubsocket::Error CheckRouteDevAddForConnect(const umq_eid_t &connEid)
    {
        if (CheckDevAdd(connEid) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to check main dev add in connect, target eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                EID_ARGS(connEid), GetPeerIp().c_str(), m_fd);
            return ubsocket::Error::kUMQ_DEV_ADD;
        }

        if (mTopoType == UMQ_TOPO_TYPE_FULLMESH_1D) {
            return ubsocket::Error::kOK;
        }

        if (CheckDevAdd(m_route_backup_src_eid) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to check backup dev add in connect, target eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                EID_ARGS(m_route_backup_src_eid), GetPeerIp().c_str(), m_fd);
            return ubsocket::Error::kUMQ_DEV_ADD;
        }

        return ubsocket::Error::kOK;
    }

    virtual void OutputStats(std::ostringstream &oss) 
    {
        StatsMgr::OutputStats(oss);
    }

    virtual void GetSocketFlowControlData(Statistics::CLIFlowControlData *data)
    {
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            m_conn_info.create_time.time_since_epoch());
        data->createTime = static_cast<uint64_t>(duration.count());

        if (umq_stats_flow_control_get(
            m_local_umqh, &(data->umqFlowControlStat)) != 0) {
            RPC_ADPT_VLOG_WARN("Failed to get umq flow control info\n");
        }
    }

    virtual void GetSocketCLIData(Statistics::CLISocketData *data)
    {
        StatsMgr::GetSocketCLIData(data);

        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            m_conn_info.create_time.time_since_epoch());
        data->createTime = static_cast<uint64_t>(duration.count());

        if (strcpy_s(data->remoteIp, sizeof(data->remoteIp), m_peer_info.peer_ip.c_str()) != 0) {
            RPC_ADPT_VLOG_WARN("Failed to strcpy remote ip\n");
        }

        if (memcpy_s(data->localEid, sizeof(data->localEid), m_conn_info.conn_eid.raw, UMQ_EID_SIZE) != 0) {
            RPC_ADPT_VLOG_WARN("Failed to memcpy local eid\n");
        }

        if (memcpy_s(data->remoteEid, sizeof(data->remoteEid), m_peer_info.peer_eid.raw, UMQ_EID_SIZE) != 0) {
            RPC_ADPT_VLOG_WARN("Failed to memcpy remote eid\n");
        }
    }

    uint64_t FloorMask()
    {
        umq_buf_block_size_t blockType = Context::GetContext()->GetIOBlockType();
        switch (blockType) {
            case BLOCK_SIZE_8K:  return SIZE_8K - MASK_DIFF;
            case BLOCK_SIZE_16K: return SIZE_16K - MASK_DIFF;
            case BLOCK_SIZE_32K: return SIZE_32K - MASK_DIFF;
            case BLOCK_SIZE_64K: return SIZE_64K - MASK_DIFF;
            default:
                return SIZE_8K - MASK_DIFF; 
        }
    }

    static const uint32_t SGE_MAX = 1;
    // currently, the upper limit of post batch for umq is 64
    static const uint32_t POST_BATCH_MAX = 64;
    /* unsolicited bytes use the same setting as brpc
     * accumulated bytes exceed UNSOLICITED_BYTES_MAX will generate a solicited interrupt event at remote */
    static const uint32_t UNSOLICITED_BYTES_MAX = 1048576;
    // defaultRX refill threshold. If RX depth less then REFILL_RX_THRESHOLD, then, threshold change to 1
    static const uint16_t REFILL_RX_THRESHOLD = 32;
    /* handle tx(poll tx cqe, set tx signaled, i.e.) when accumulated number equals to m_tx_window_capacity / 8 */
    static const uint16_t HANDLE_TX_THRESHOLD_RATIO_DIVISOR = 4;
    /* process up to m_tx_window capacity / 16 tx cqe each time */
    static const uint16_t RETRIEVE_TX_THRESHOLD_RATIO_DIVISOR = 8;
    // protocol negotiation is the 0xff + ASCII of "R" + "P" + "C" + "A" + "D" + "P" + "T"
    static const uint64_t CONTROL_PLANE_PROTOCOL_NEGOTIATION = 0xff52504341445054;
    static const uint32_t NEGOTIATE_TIMEOUT_MS = 10;
    // Current UB jetty handshake is synchronous that brpc acceptor can't yield from the point.
    // Ensure the connector has at most 5s to wait from server socket.
    static const uint32_t CONTROL_PLANE_TIMEOUT_MS = 20000;
    static const uint32_t NEGOTIATE_SOCKET_ID_MAX_NUM = 256;
    static const uint32_t DATA_PLANE_TIMEOUT_MS = 100;
    static const uint32_t POLL_TX_RETRY_MAX_CNT = 50;
    static const uint32_t FLUSH_TIMEOUT_MS = 200;
    static const uint32_t FALLBAKC_TCP_RESENT_TIMEOUT_MS = 800;
    static const uint32_t WAIT_UMQ_READY_TIMEOUT_US = 100;
    // max wait 1s for umq ready
    static const uint32_t WAIT_UMQ_READY_ROUND = 10000;

    void WakeUpTx()
    {
        bool need_fc_awake = m_tx.m_need_fc_awake.exchange(false, std::memory_order_relaxed);
        if (need_fc_awake && eventfd_write(m_event_fd, 1) == -1) {
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                              "eventfd_write() failed, event fd: %d, peer eid:" EID_FMT
                              ", peer ip: %s, errno: %d, errmsg: %s\n",
                              m_event_fd, EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), errno,
                              NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    ALWAYS_INLINE ssize_t ReadV(const struct iovec *iov, int iovcnt)
    {
        int retCode = -1;
        if (m_rx_use_tcp) {
            ssize_t size = OsAPiMgr::GetOriginApi()->readv(m_fd, iov, iovcnt);
            retCode = size < 0 ? -1 : 0;
            return size;
        }

        TRACE_DELAY_AUTO(BRPC_READV_CALL, retCode);
        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                               m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        /* if socket failed to pass protocol negotiation validation, then
         * (1) pass the received protocol negotiation as message to caller;
         * (2) when all the received message passed to caller, fallback to tcp/ip */
        ssize_t rx_total_len = OutputErrorMagicNumber(iov, iovcnt);
        if (rx_total_len > 0) {
            retCode = 0;
            return rx_total_len;
        }

        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr && m_rx.m_get_and_ack_event) {
            if (GetAndAckEvent(UMQ_IO_RX) < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "ReadV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
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
            poll_num = GetQbuf(buf, POLL_BATCH_MAX);
            if (poll_num < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "ReadV GetQbuf() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
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
                if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                    if (buf[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                        m_flow_control_failed = true;
                    }
                    HandleErrorRxCqe(buf[i]);
                } else {
                    m_rx.m_window_size += 1;
                    // try to wake up tx if necessary
                    bool need_fc_awake = m_tx.m_need_fc_awake.exchange(false, std::memory_order_relaxed);
                    if (need_fc_awake && eventfd_write(m_event_fd, 1) == -1) {
                        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "eventfd_write() failed, event fd: %d, peer eid:" EID_FMT
                            ", peer ip: %s, errno: %d, errmsg: %s\n",
                            m_event_fd, EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), errno,
                            NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                    }
                }

                QBUF_LIST_NEXT(buf[i]) = nullptr;
                umq_buf_free(buf[i]);
                continue;
            }
            if (m_context_trace_enable) {
                UpdateTraceStats(StatsMgr::RX_PACKET_COUNT, 1);
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
            bool closed = m_closed.load(std::memory_order_relaxed);
            if (closed == true) {
                retCode = 0;
                return 0;
            }

            if (m_flow_control_failed == true) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "ReadV flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            if (RearmRxInterrupt() < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "ReadV RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            /* return -1 and set errno to EAGAIN to notice user no more data to read */
            errno = EAGAIN;
            return -1;
        }

        /* Set the first block as used to prevent brpc from utilizing this block,
         * and only use it as the head of the block linked list. */
        out_first_block->size = out_first_block->cap;

        if (m_context_trace_enable) {
            UpdateTraceStats(StatsMgr::RX_BYTE_COUNT, rx_total_len);
        }
        retCode = 0;
        return rx_total_len;
    }

    ALWAYS_INLINE ssize_t WriteV(const struct iovec *iov, int iovcnt)
    {
        int retCode = -1;
        if (m_tx_use_tcp) {
            ssize_t size = OsAPiMgr::GetOriginApi()->writev(m_fd, iov, iovcnt);
            retCode = size < 0 ? -1 : 0;
            return size;
        }

        TRACE_DELAY_AUTO(BRPC_WRITEV_CALL, retCode);
        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "WriteV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "WriteV socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }
        
        if (m_tx.m_get_and_ack_event) {
            // handle tx epollin epoll event
            do {
                if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                    errno = EIO;
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                        "WriteV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
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
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                static_cast<unsigned long long>(m_local_umqh), tx_buf_list);
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
            if (m_context_trace_enable) {
                UpdateTraceStats(StatsMgr::TX_PACKET_COUNT, 1);
            }
        } else if (bad_qbuf != nullptr) {
            // Handle partial failure
            if (ret == -UMQ_ERR_EAGAIN) {
                // Operation would block, UMQ queue might be temporarily full despite window check
                errno = EAGAIN;
                m_tx.m_need_fc_awake.store(true, std::memory_order_relaxed);
            } else if (ret == -UMQ_ERR_EFLOWCTL) {
                errno = EIO;
                return -1;
            } else {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() failed for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), ret);
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
                tx_total_len = HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf,
                   _unsolicited_wr_num, _unsolicited_bytes, _unsignaled_wr_num);
            }
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_post() failed for TX without bad_qbuf, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), ret);
        }

        // After posting and before polling, the time for updating the count cna be concealed within the waiting period
        // for polling.

        if ((m_tx_window_capacity - m_tx.m_window_size) >= m_tx.m_handle_threshold) {
            PollTx(m_tx.m_retrieve_threshold);
        }

        if (m_context_trace_enable) {
            UpdateTraceStats(StatsMgr::TX_BYTE_COUNT, tx_total_len);
        }

        retCode = 0;
        return tx_total_len;
    }

    ALWAYS_INLINE ssize_t Send(const void *buf, size_t len, int flags)
    {
        if (m_tx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->send(m_fd, buf, len, flags);
        }

        if (buf == nullptr || len == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Send invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE; // Broken pipe if connection is closed
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Send socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_tx.m_get_and_ack_event) {
            do {
                if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                    errno = EIO;
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                        "Send GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                    return -1;
                }
                PollTx(m_tx.m_retrieve_threshold, true); // true indicates handling event
            } while (!m_tx.m_epoll_event_num.compare_exchange_strong(
                      m_tx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire));
            m_tx.m_get_and_ack_event = false;
        } else if (m_tx.m_window_size == 0) {
            PollTx(m_tx.m_retrieve_threshold);
            if (m_tx.m_window_size == 0) {
                return DpRearmTxInterrupt();
            }
        }
 
        uint32_t brpc_iobuf_size = BrpcIOBufSize();
        uint32_t num_bufs_needed = (len + brpc_iobuf_size - 1) / brpc_iobuf_size;
        uint32_t post_batch_max = (m_tx.m_window_size <= static_cast<uint32_t>(POST_BATCH_MAX)) ?
                                   m_tx.m_window_size : static_cast<uint32_t>(POST_BATCH_MAX);
        num_bufs_needed = (num_bufs_needed < post_batch_max) ? num_bufs_needed : post_batch_max;
        if (num_bufs_needed == 0) {
            errno = EAGAIN;
            return -1;
        }
 
        umq_buf_t *tx_buf_list = umq_buf_alloc(brpc_iobuf_size, num_bufs_needed, UMQ_INVALID_HANDLE, nullptr);
        if (tx_buf_list == nullptr) {
            RPC_ADPT_VLOG_WARN("umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                               static_cast<unsigned long long>(m_local_umqh), tx_buf_list);
            return DpRearmTxInterrupt();
        }
 
        umq_buf_t *current_buf = tx_buf_list;
        if (QBUF_LIST_EMPTY(&m_tx.m_head_buf)) {
            QBUF_LIST_FIRST(&m_tx.m_head_buf) = current_buf;
        } else {
            QBUF_LIST_NEXT(QBUF_LIST_FIRST(&m_tx.m_tail_buf)) = current_buf;
        }
 
        const char* src_ptr = static_cast<const char*>(buf);
        size_t remaining_len = len;
        size_t copied_total = 0;
        size_t bytes_per_buf = brpc_iobuf_size;
 
        uint16_t _unsolicited_wr_num = m_tx.m_unsolicited_wr_num;
        uint32_t _unsolicited_bytes = m_tx.m_unsolicited_bytes;
        uint16_t _unsignaled_wr_num = m_tx.m_unsignaled_wr_num;
        umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&m_tx.m_head_buf);
        umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&m_tx.m_tail_buf);
 
        for (uint32_t i = 0; i < num_bufs_needed && remaining_len > 0; ++i) {
            size_t copy_len = std::min(remaining_len, static_cast<size_t>(bytes_per_buf));
            if (current_buf->buf_data == nullptr) {
                umq_buf_free(tx_buf_list);
                errno = ENOMEM;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Send failed due to null TX buffer data, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
 
            memcpy_s(current_buf->buf_data, copy_len, src_ptr, copy_len); // Copy data into UMQ buffer
            current_buf->data_size = copy_len;
            remaining_len -= copy_len;
            src_ptr += copy_len;
            copied_total += copy_len;
 
            if (i == 0) {
                current_buf->total_data_size = len;
            }
 
            // Setup UMQ buffer properties (similar to WriteV)
            current_buf->io_direction = UMQ_IO_TX;
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)current_buf->qbuf_ext;
            if (buf_pro) {
                buf_pro->opcode = UMQ_OPC_SEND;
                buf_pro->flag.value = 0;
                buf_pro->remote_sge.addr = (uint64_t)current_buf->buf_data;
                buf_pro->remote_sge.length = current_buf->data_size;
                buf_pro->remote_sge.token_id = 0;
                buf_pro->remote_sge.token_value = 0;
                buf_pro->remote_sge.mempool_id = 0;
                buf_pro->remote_sge.rsvd0 = 0;
 
                if (m_tx.m_window_size == 1 || i + 1 == num_bufs_needed) {
                    buf_pro->flag.bs.solicited_enable = 1;
                } else {
                    if (m_tx.m_unsolicited_wr_num > m_tx.m_report_threshold ||
                        m_tx.m_unsolicited_bytes > UNSOLICITED_BYTES_MAX) {
                        buf_pro->flag.bs.solicited_enable = 1;
                    } else {
                        ++m_tx.m_unsolicited_wr_num;
                        m_tx.m_unsolicited_bytes += copy_len;
                    }
                }
 
                if (buf_pro->flag.bs.solicited_enable == 1) {
                    m_tx.m_unsolicited_wr_num = 0;
                    m_tx.m_unsolicited_bytes = 0;
                }
 
                if (++m_tx.m_unsignaled_wr_num >= m_tx.m_report_threshold) {
                    buf_pro->flag.bs.complete_enable = 1;
                    buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&m_tx.m_head_buf);
                    QBUF_LIST_FIRST(&m_tx.m_head_buf) = QBUF_LIST_NEXT(current_buf);
                    m_tx.m_unsignaled_wr_num = 0;
                }
            }
 
            if (i + 1 < num_bufs_needed) {
                current_buf = current_buf->qbuf_next;
            }
        }
 
        // Update tail pointer of the TX buffer list management
        QBUF_LIST_FIRST(&m_tx.m_tail_buf) = current_buf;
        umq_buf_t *bad_qbuf = nullptr;
 
        int post_result = umq_post(m_local_umqh, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
        if (post_result == UMQ_SUCCESS) {
            m_tx.m_window_size -= num_bufs_needed; // Consume window slots upon successful post
        } else if (bad_qbuf != nullptr) {
            // Handle partial failure
            if (post_result == -UMQ_ERR_EAGAIN) {
                // Operation would block, UMQ queue might be temporarily full despite window check
                errno = EAGAIN;
                m_tx.m_need_fc_awake.store(true, std::memory_order_relaxed);
            } else if (post_result == -UMQ_ERR_EFLOWCTL) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() returned EFLOWCTL for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
                return -1;
            } else {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() failed for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
            }
 
            umq_buf_list_t failed_list = { bad_qbuf }; // Create a temporary list head
            umq_buf_t *cur_failed = nullptr;
            QBUF_LIST_FOR_EACH(cur_failed, &failed_list) {
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_failed->buf_data))->DecRef();
            }
 
            if (bad_qbuf == tx_buf_list) {
                m_tx.m_unsolicited_wr_num = _unsolicited_wr_num; // Restore saved state from before loop
                m_tx.m_unsolicited_bytes = _unsolicited_bytes;
                m_tx.m_unsignaled_wr_num = _unsignaled_wr_num;
                QBUF_LIST_FIRST(&m_tx.m_head_buf) = head_qbuf;
                QBUF_LIST_FIRST(&m_tx.m_tail_buf) = tail_qbuf;
                umq_buf_free(bad_qbuf); // Free the failed list starting from the first bad buffer
                copied_total = 0; // Nothing was successfully sent
            } else {
                copied_total = HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf,
                                             _unsolicited_wr_num, _unsolicited_bytes, _unsignaled_wr_num);
            }
 
            return static_cast<ssize_t>(copied_total);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_post() failed for TX without bad_qbuf, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), post_result);
            errno = EIO;
            return -1;
        }
 
        if ((m_tx_window_capacity - m_tx.m_window_size) >= m_tx.m_handle_threshold) {
            PollTx(m_tx.m_retrieve_threshold);
        }
 
        return static_cast<ssize_t>(copied_total);
 
    }
 
    ALWAYS_INLINE ssize_t Recv(void *buf, size_t len, int flags)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->recv(m_fd, buf, len, flags);
        }
 
        if (buf == nullptr || len == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Recv invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }
 
        struct iovec single_iov = { .iov_base = buf, .iov_len = len };
        ssize_t rx_total_len = OutputErrorMagicNumber(&single_iov, 1);
        if (rx_total_len > 0) {
            return rx_total_len;
        }
 
        if (m_rx.m_get_and_ack_event) {
            if (GetAndAckEvent(UMQ_IO_RX) < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "Recv GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
            m_rx.m_get_and_ack_event = false;
        }

        size_t remaining_user_buf = len; // Remaining space in user buffer
        char* user_buf_ptr = static_cast<char*>(buf); // Use a pointer to track position in user buffer
        size_t copied_buf = 0;

        while (remaining_user_buf > 0 && m_rx.m_remaining_size > 0) {
            umq_buf_t *cur_buf = QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_head);
            if (cur_buf == nullptr) {
                break;
            }
            if (cur_buf->data_size <= remaining_user_buf) {
                memcpy_s(user_buf_ptr + copied_buf, remaining_user_buf, cur_buf->buf_data, cur_buf->data_size);
                m_rx.m_remaining_size -= cur_buf->data_size;
                remaining_user_buf -= cur_buf->data_size;
                copied_buf += cur_buf->data_size;
                umq_buf_t *next_buf = QBUF_LIST_NEXT(cur_buf);
                umq_buf_free(cur_buf);
                QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_head) = next_buf;
            } else {
                memcpy_s(user_buf_ptr + copied_buf, remaining_user_buf, cur_buf->buf_data, remaining_user_buf);
                m_rx.m_remaining_size -= remaining_user_buf;
                cur_buf->buf_data = static_cast<char*>(cur_buf->buf_data) + remaining_user_buf;
                cur_buf->data_size -= remaining_user_buf;
                remaining_user_buf = 0;
                break;
            }
        }

        if (remaining_user_buf == 0) {
            return len;
        }

        umq_buf_t *buf_array[POLL_BATCH_MAX];
        int poll_num = 0;
        while (poll_num == 0) {
            poll_num = UmqPollAndRefillRx(buf_array, POLL_BATCH_MAX);
            if (poll_num < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Recv UmqPollAndRefillRx() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            } else if (poll_num == 0) {
                m_rx.m_poll = false;
            }
        }

        if (poll_num != 0) {
            for (int i = 0; i < poll_num - 1; ++i) {
                QBUF_LIST_NEXT(buf_array[i]) = buf_array[i + 1];
            }
            if (QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_head) == nullptr) {
                QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_head) = buf_array[0];
                QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_tail) = buf_array[poll_num - 1];
            } else {
                QBUF_LIST_NEXT(QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_tail)) = buf_array[0];
                QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_tail) = buf_array[poll_num - 1];
            }
        }

        for (int i = 0; i < poll_num; ++i) {
            if (buf_array[i]->status != 0) {
                if (buf_array[i]->status != UMQ_BUF_FLOW_CONTROL_UPDATE) {
                    RPC_ADPT_VLOG_DEBUG("RX CQE is invalid, status: %d\n", buf_array[i]->status);
                    if (buf_array[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                        m_flow_control_failed = true;
                    }
                    QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                    umq_buf_free(buf_array[i]);
                    continue;
                } else {
                    // try to wake up tx if necessary
                    bool need_fc_awake = m_tx.m_need_fc_awake.exchange(false, std::memory_order_relaxed);
                    if (need_fc_awake && eventfd_write(m_event_fd, 1) == -1) {
                        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "eventfd_write() failed, event fd: %d, errno: %d, errmsg: %s\n",
                            m_event_fd, errno,
                            NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                    }

                    if (buf_array[i]->total_data_size == 0) {
                        QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                        umq_buf_free(buf_array[i]);
                        continue;
                    }
                }
            }

            if (remaining_user_buf > 0) {
                if (buf_array[i]->data_size <= remaining_user_buf) {
                    memcpy_s(user_buf_ptr + copied_buf, remaining_user_buf, buf_array[i]->buf_data,
                             buf_array[i]->data_size);
                    remaining_user_buf -= buf_array[i]->data_size;
                    copied_buf += buf_array[i]->data_size;
                    umq_buf_t *next_buf = QBUF_LIST_NEXT(buf_array[i]);
                    umq_buf_free(buf_array[i]);
                    QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_head) = next_buf;
                } else {
                    memcpy_s(user_buf_ptr + copied_buf, remaining_user_buf, buf_array[i]->buf_data,
                             remaining_user_buf);
                    m_rx.m_remaining_size += buf_array[i]->data_size - remaining_user_buf;
                    buf_array[i]->buf_data = static_cast<char*>(buf_array[i]->buf_data) + remaining_user_buf;
                    buf_array[i]->data_size -= remaining_user_buf;
                    remaining_user_buf = 0;
                }
            } else {
                m_rx.m_remaining_size += buf_array[i]->data_size;
            }
        }

        if (m_rx.m_remaining_size != 0) {
            QBUF_LIST_FIRST(&m_rx.m_umq_buf_cache_tail) = buf_array[poll_num - 1];
        }

        if (remaining_user_buf > 0) {
            // Check for epoll event num mismatch (similar to ReadV)
            m_rx.m_poll = true;
            if (!m_rx.m_epoll_event_num.compare_exchange_strong(
                        m_rx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire)) {
                m_rx.m_poll = true;
                errno = EINTR;
                return -1;
            }

            if (m_flow_control_failed == true) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Recv flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
 
            bool closed = m_closed.load(std::memory_order_relaxed);
            if (closed == true) {
                return 0;
            }
 
            if (RearmRxInterrupt() < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Recv RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            errno = EAGAIN;
            return -1;
        }

        return len;
    }

    ALWAYS_INLINE ssize_t Read(void *buf, size_t nbyte)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->read(m_fd, buf, nbyte);
        }

        if (buf == nullptr || nbyte == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Read invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE; // Broken pipe if connection is closed
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Read socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        struct iovec single_iov = { .iov_base = buf, .iov_len = nbyte };
        ssize_t rx_total_len = OutputErrorMagicNumber(&single_iov, 1);
        if (rx_total_len > 0) {
            return rx_total_len;
        }
        
        if (m_rx.m_get_and_ack_event) {
            if (GetAndAckEvent(UMQ_IO_RX) < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "Read GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
            m_rx.m_get_and_ack_event = false;
        }

        uint32_t max_buf_size;
        if (m_rx.m_readv_unlimited) {
            max_buf_size = UINT32_MAX;
        } else {
            max_buf_size = static_cast<uint32_t>(nbyte);
            if (max_buf_size != nbyte) {
                errno = EINVAL;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Read invalid max_buf_size conversion, fd: %d, nbyte: %zu, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, nbyte, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
        }

        bool is_blocking = IsBlocking(m_fd);
        umq_buf_t *buf_array[POLL_BATCH_MAX];
        int poll_num = 0;

        while (true) {
            poll_num = UmqPollAndRefillRx(buf_array, POLL_BATCH_MAX);
            if (poll_num < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Read UmqPollAndRefillRx() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            } else if (poll_num > 0) {
                break;
            } else { // poll_num == 0
                if (!is_blocking) {
                    errno = EAGAIN;
                    return -1;
                } else {
                    RPC_ADPT_VLOG_DEBUG("Read: Blocking mode, no data, polling again...\n");
                }
            }
        }

        // Process Polled Buffers and Copy Directly
        rx_total_len = 0;
        char* user_buf_ptr = static_cast<char*>(buf);
        size_t remaining_user_buf = nbyte;

        if (poll_num > 0) {
            for (int i = 0; i < poll_num; ++i) {
                if (remaining_user_buf == 0) {
                    RPC_ADPT_VLOG_DEBUG("Read: User buffer is full, stopping processing of further buffers\n");
                    QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                    umq_buf_free(buf_array[i]);
                    continue;
                }
                if (buf_array[i]->status != 0) {
                    if (buf_array[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                        RPC_ADPT_VLOG_DEBUG("RX CQE is invalid, status: %d\n", buf_array[i]->status);
                        if (buf_array[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                            m_flow_control_failed = true;
                        }
                        QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                        umq_buf_free(buf_array[i]);
                        continue;
                    } else {
                        m_rx.m_window_size += 1;
                        // Handle flow control update
                        bool need_fc_awake = m_tx.m_need_fc_awake.exchange(false, std::memory_order_relaxed);
                        if (need_fc_awake && eventfd_write(m_event_fd, 1) == -1) {
                            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                                "eventfd_write() failed, event fd: %d, errno: %d, errmsg: %s\n",
                                m_event_fd, errno,
                                NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                        }

                        QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                        umq_buf_free(buf_array[i]);
                        continue;
                    }
                }

                size_t data_size_to_copy = buf_array[i]->data_size;

                // Check if user buffer has enough space
                if (remaining_user_buf < data_size_to_copy) {
                    data_size_to_copy = remaining_user_buf;
                }

                if (data_size_to_copy > 0) {
                    memcpy_s(user_buf_ptr, data_size_to_copy, buf_array[i]->buf_data, data_size_to_copy);
                    rx_total_len += data_size_to_copy;
                    user_buf_ptr += data_size_to_copy;
                    remaining_user_buf -= data_size_to_copy;
                }

                QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                umq_buf_free(buf_array[i]);
            }
        }

        if (rx_total_len == 0) {
            if (!m_rx.m_epoll_event_num.compare_exchange_strong(
                 m_rx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire)) {
                m_rx.m_poll = true;
                errno = EINTR;
                return -1;
            }

            if (m_flow_control_failed == true) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Read flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            bool closed = m_closed.load(std::memory_order_relaxed);
            if (closed == true) {
                return 0;
            }

            if (RearmRxInterrupt() < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Read RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            errno = EAGAIN;
            return -1;
        }

        return rx_total_len;
    }

    ALWAYS_INLINE ssize_t Write(const void *buf, size_t nbyte)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->write(m_fd, buf, nbyte);
        }

        if (buf == nullptr || nbyte == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Write invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE; // Broken pipe if connection is closed
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Write socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_tx.m_get_and_ack_event) {
            do {
                if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                    errno = EIO;
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                        "Write GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                    return -1;
                }
                PollTx(m_tx.m_retrieve_threshold, true); // true indicates handling event
            } while (!m_tx.m_epoll_event_num.compare_exchange_strong(
                      m_tx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire));
            m_tx.m_get_and_ack_event = false;
        } else if (m_tx.m_window_size == 0) {
            PollTx(m_tx.m_retrieve_threshold);
            if (m_tx.m_window_size == 0) {
                return DpRearmTxInterrupt();
            }
        }

        uint32_t brpc_iobuf_size = BrpcIOBufSize();
        uint32_t num_bufs_needed = (nbyte + brpc_iobuf_size - 1) / brpc_iobuf_size;
        uint32_t post_batch_max = (m_tx.m_window_size <= static_cast<uint32_t>(POST_BATCH_MAX)) ?
                                   m_tx.m_window_size : static_cast<uint32_t>(POST_BATCH_MAX);
        num_bufs_needed = (num_bufs_needed < post_batch_max) ? num_bufs_needed : post_batch_max;
        if (num_bufs_needed == 0) {
            errno = EAGAIN;
            return -1;
        }

        umq_buf_t *tx_buf_list = umq_buf_alloc(brpc_iobuf_size, num_bufs_needed, UMQ_INVALID_HANDLE, nullptr);
        if (tx_buf_list == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                static_cast<unsigned long long>(m_local_umqh), tx_buf_list);
            return DpRearmTxInterrupt();
        }
        
        umq_buf_t *current_buf = tx_buf_list;

        const char* src_ptr = static_cast<const char*>(buf);
        size_t remaining_len = nbyte;
        size_t copied_total = 0;
        size_t bytes_per_buf = brpc_iobuf_size;

        uint16_t _unsolicited_wr_num = m_tx.m_unsolicited_wr_num;
        uint32_t _unsolicited_bytes = m_tx.m_unsolicited_bytes;
        uint16_t _unsignaled_wr_num = m_tx.m_unsignaled_wr_num;
        umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&m_tx.m_head_buf);
        umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&m_tx.m_tail_buf);

        for (uint32_t i = 0; i < num_bufs_needed && remaining_len > 0; ++i) {
            size_t copy_len = std::min(remaining_len, static_cast<size_t>(bytes_per_buf));
            if (current_buf->buf_data == nullptr) {
                umq_buf_free(tx_buf_list);
                errno = ENOMEM;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Write failed due to null TX buffer data, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
 
            memcpy_s(current_buf->buf_data, copy_len, src_ptr, copy_len); // Copy data into UMQ buffer
            current_buf->data_size = copy_len;
            remaining_len -= copy_len;
            src_ptr += copy_len;
            copied_total += copy_len;
 
            if (i == 0) {
                current_buf->total_data_size = nbyte;
            }

            // Setup UMQ buffer properties (similar to WriteV)
            current_buf->io_direction = UMQ_IO_TX;
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)current_buf->qbuf_ext;
            if (buf_pro) {
                buf_pro->opcode = UMQ_OPC_SEND;
                buf_pro->flag.value = 0;
                buf_pro->remote_sge.addr = (uint64_t)current_buf->buf_data;
                buf_pro->remote_sge.length = current_buf->data_size;
                buf_pro->remote_sge.token_id = 0;
                buf_pro->remote_sge.token_value = 0;
                buf_pro->remote_sge.mempool_id = 0;
                buf_pro->remote_sge.rsvd0 = 0;
 
                if (m_tx.m_window_size == 1 || i + 1 == num_bufs_needed) {
                    buf_pro->flag.bs.solicited_enable = 1;
                } else {
                    if (m_tx.m_unsolicited_wr_num > m_tx.m_report_threshold ||
                        m_tx.m_unsolicited_bytes > UNSOLICITED_BYTES_MAX) {
                        buf_pro->flag.bs.solicited_enable = 1;
                    } else {
                        ++m_tx.m_unsolicited_wr_num;
                        m_tx.m_unsolicited_bytes += copy_len;
                    }
                }
 
                if (buf_pro->flag.bs.solicited_enable == 1) {
                    m_tx.m_unsolicited_wr_num = 0;
                    m_tx.m_unsolicited_bytes = 0;
                }

                if (++m_tx.m_unsignaled_wr_num >= m_tx.m_report_threshold) {
                    buf_pro->flag.bs.complete_enable = 1;
                    buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&m_tx.m_head_buf);
                    QBUF_LIST_FIRST(&m_tx.m_head_buf) = QBUF_LIST_NEXT(current_buf);
                    m_tx.m_unsignaled_wr_num = 0;
                }
            }
 
            if (i + 1 < num_bufs_needed) {
                current_buf = current_buf->qbuf_next;
            }
        }

        // Update tail pointer of the TX buffer list management
        QBUF_LIST_FIRST(&m_tx.m_tail_buf) = current_buf;
        umq_buf_t *bad_qbuf = nullptr;
 
        int post_result = umq_post(m_local_umqh, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
        if (post_result == UMQ_SUCCESS) {
            m_tx.m_window_size -= num_bufs_needed; // Consume window slots upon successful post
        } else if (bad_qbuf != nullptr) {
            // Handle partial failure
            if (post_result == -UMQ_ERR_EAGAIN) {
                // Operation would block, UMQ queue might be temporarily full despite window check
                errno = EAGAIN;
                m_tx.m_need_fc_awake.store(true, std::memory_order_relaxed);
            } else if (post_result == -UMQ_ERR_EFLOWCTL) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() returned EFLOWCTL for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
                return -1;
            } else {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() failed for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
            }
 
            umq_buf_list_t failed_list = { bad_qbuf }; // Create a temporary list head
            umq_buf_t *cur_failed = nullptr;
            QBUF_LIST_FOR_EACH(cur_failed, &failed_list) {
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_failed->buf_data))->DecRef();
            }
 
            if (bad_qbuf == tx_buf_list) {
                m_tx.m_unsolicited_wr_num = _unsolicited_wr_num; // Restore saved state from before loop
                m_tx.m_unsolicited_bytes = _unsolicited_bytes;
                m_tx.m_unsignaled_wr_num = _unsignaled_wr_num;
                QBUF_LIST_FIRST(&m_tx.m_head_buf) = head_qbuf;
                QBUF_LIST_FIRST(&m_tx.m_tail_buf) = tail_qbuf;
 
                umq_buf_free(bad_qbuf); // Free the failed list starting from the first bad buffer
                copied_total = 0; // Nothing was successfully sent
            } else {
                copied_total = HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf,
                                             _unsolicited_wr_num, _unsolicited_bytes, _unsignaled_wr_num);
            }
 
            return static_cast<ssize_t>(copied_total);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_post() failed for TX without bad_qbuf, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), post_result);
            errno = EIO;
            return -1;
        }
 
        if ((m_tx_window_capacity - m_tx.m_window_size) >= m_tx.m_handle_threshold) {
            PollTx(m_tx.m_retrieve_threshold);
        }

        return static_cast<ssize_t>(copied_total);
    }

    ALWAYS_INLINE ssize_t SendTo(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                                 socklen_t addrlen)
    {
        if (m_tx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->sendto(m_fd, buf, len, flags, dest_addr, addrlen);
        }

        if (buf == nullptr || len == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "SendTo invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", m_fd, -1, errno,
                NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }

        if (m_closed.load(std::memory_order_relaxed)) {
            errno = EPIPE; // Broken pipe if connection is closed
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "SendTo socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", m_fd, -1, errno,
                NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }
 
        if (m_tx.m_get_and_ack_event) {
            do {
                if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                    errno = EIO;
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                        "SendTo GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                    return -1;
                }
                PollTx(m_tx.m_retrieve_threshold, true); // true indicates handling event
            } while (!m_tx.m_epoll_event_num.compare_exchange_strong(
                      m_tx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire));
            m_tx.m_get_and_ack_event = false;
        } else if (m_tx.m_window_size == 0) {
            PollTx(m_tx.m_retrieve_threshold);
            if (m_tx.m_window_size == 0) {
                return DpRearmTxInterrupt();
            }
        }
 
        uint32_t brpc_iobuf_size = BrpcIOBufSize();
        uint32_t num_bufs_needed = (len + brpc_iobuf_size - 1) / brpc_iobuf_size;
        uint32_t post_batch_max = (m_tx.m_window_size <= static_cast<uint32_t>(POST_BATCH_MAX)) ?
                                   m_tx.m_window_size : static_cast<uint32_t>(POST_BATCH_MAX);
        num_bufs_needed = (num_bufs_needed < post_batch_max) ? num_bufs_needed : post_batch_max;
        if (num_bufs_needed == 0) {
            errno = EAGAIN;
            return -1;
        }
 
        umq_buf_t *tx_buf_list = umq_buf_alloc(brpc_iobuf_size, num_bufs_needed, UMQ_INVALID_HANDLE, nullptr);
        if (tx_buf_list == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                static_cast<unsigned long long>(m_local_umqh), tx_buf_list);
            return DpRearmTxInterrupt();
        }
 
        umq_buf_t *current_buf = tx_buf_list;
        if (QBUF_LIST_EMPTY(&m_tx.m_head_buf)) {
            QBUF_LIST_FIRST(&m_tx.m_head_buf) = current_buf;
        } else {
            QBUF_LIST_NEXT(QBUF_LIST_FIRST(&m_tx.m_tail_buf)) = current_buf;
        }
 
        const char* src_ptr = static_cast<const char*>(buf);
        size_t remaining_len = len;
        size_t copied_total = 0;
        size_t bytes_per_buf = brpc_iobuf_size;
 
        uint16_t _unsolicited_wr_num = m_tx.m_unsolicited_wr_num;
        uint32_t _unsolicited_bytes = m_tx.m_unsolicited_bytes;
        uint16_t _unsignaled_wr_num = m_tx.m_unsignaled_wr_num;
        umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&m_tx.m_head_buf);
        umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&m_tx.m_tail_buf);
 
        for (uint32_t i = 0; i < num_bufs_needed && remaining_len > 0; ++i) {
            size_t copy_len = std::min(remaining_len, static_cast<size_t>(bytes_per_buf));
            if (current_buf->buf_data == nullptr) {
                umq_buf_free(tx_buf_list);
                errno = ENOMEM;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "SendTo failed due to null TX buffer data, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
 
            memcpy_s(current_buf->buf_data, copy_len, src_ptr, copy_len); // Copy data into UMQ buffer
            current_buf->data_size = copy_len;
            remaining_len -= copy_len;
            src_ptr += copy_len;
            copied_total += copy_len;
 
            if (i == 0) {
                current_buf->total_data_size = len;
            }
 
            // Setup UMQ buffer properties (similar to WriteV)
            current_buf->io_direction = UMQ_IO_TX;
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)current_buf->qbuf_ext;
            if (buf_pro) {
                buf_pro->opcode = UMQ_OPC_SEND;
                buf_pro->flag.value = 0;
                buf_pro->remote_sge.addr = (uint64_t)current_buf->buf_data;
                buf_pro->remote_sge.length = current_buf->data_size;
                buf_pro->remote_sge.token_id = 0;
                buf_pro->remote_sge.token_value = 0;
                buf_pro->remote_sge.mempool_id = 0;
                buf_pro->remote_sge.rsvd0 = 0;
 
                if (m_tx.m_window_size == 1 || i + 1 == num_bufs_needed) {
                    buf_pro->flag.bs.solicited_enable = 1;
                } else {
                    if (m_tx.m_unsolicited_wr_num > m_tx.m_report_threshold ||
                        m_tx.m_unsolicited_bytes > UNSOLICITED_BYTES_MAX) {
                        buf_pro->flag.bs.solicited_enable = 1;
                    } else {
                        ++m_tx.m_unsolicited_wr_num;
                        m_tx.m_unsolicited_bytes += copy_len;
                    }
                }
 
                if (buf_pro->flag.bs.solicited_enable == 1) {
                    m_tx.m_unsolicited_wr_num = 0;
                    m_tx.m_unsolicited_bytes = 0;
                }

                if (++m_tx.m_unsignaled_wr_num >= m_tx.m_report_threshold) {
                    buf_pro->flag.bs.complete_enable = 1;
                    buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&m_tx.m_head_buf);
                    QBUF_LIST_FIRST(&m_tx.m_head_buf) = QBUF_LIST_NEXT(current_buf);
                    m_tx.m_unsignaled_wr_num = 0;
                }
            }
 
            if (i + 1 < num_bufs_needed) {
                current_buf = current_buf->qbuf_next;
            }
        }
 
        // Update tail pointer of the TX buffer list management
        QBUF_LIST_FIRST(&m_tx.m_tail_buf) = current_buf;
        umq_buf_t *bad_qbuf = nullptr;
 
        int post_result = umq_post(m_local_umqh, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
        if (post_result == UMQ_SUCCESS) {
            m_tx.m_window_size -= num_bufs_needed; // Consume window slots upon successful post
        } else if (bad_qbuf != nullptr) {
            // Handle partial failure
            if (post_result == -UMQ_ERR_EAGAIN) {
                // Operation would block, UMQ queue might be temporarily full despite window check
                errno = EAGAIN;
                m_tx.m_need_fc_awake.store(true, std::memory_order_relaxed);
            } else if (post_result == -UMQ_ERR_EFLOWCTL) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() returned EFLOWCTL for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
                return -1;
            } else {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() failed for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), post_result);
                errno = EIO;
            }
 
            umq_buf_list_t failed_list = { bad_qbuf }; // Create a temporary list head
            umq_buf_t *cur_failed = nullptr;
            QBUF_LIST_FOR_EACH(cur_failed, &failed_list) {
                ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_failed->buf_data))->DecRef();
            }
 
            if (bad_qbuf == tx_buf_list) {
                m_tx.m_unsolicited_wr_num = _unsolicited_wr_num; // Restore saved state from before loop
                m_tx.m_unsolicited_bytes = _unsolicited_bytes;
                m_tx.m_unsignaled_wr_num = _unsignaled_wr_num;
                QBUF_LIST_FIRST(&m_tx.m_head_buf) = head_qbuf;
                QBUF_LIST_FIRST(&m_tx.m_tail_buf) = tail_qbuf;
 
                umq_buf_free(bad_qbuf); // Free the failed list starting from the first bad buffer
                copied_total = 0; // Nothing was successfully sent
            } else {
                copied_total = HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf,
                                             _unsolicited_wr_num, _unsolicited_bytes, _unsignaled_wr_num);
            }
 
            return static_cast<ssize_t>(copied_total);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_post() failed for TX without bad_qbuf, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), post_result);
            errno = EIO;
            return -1;
        }
 
        if ((m_tx_window_capacity - m_tx.m_window_size) >= m_tx.m_handle_threshold) {
            PollTx(m_tx.m_retrieve_threshold);
        }
 
        return static_cast<ssize_t>(copied_total);
    }
    
    ALWAYS_INLINE ssize_t RecvFrom(void *buf, size_t len, int flags, struct sockaddr *src_addr,
                                   socklen_t *addrlen)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->recvfrom(m_fd, buf, len, flags, src_addr, addrlen);
        }

        if (buf == nullptr || len == 0) {
            errno = EINVAL;
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "RecvFrom invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", m_fd, -1, errno,
                NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return -1;
        }
 
        struct iovec single_iov = { .iov_base = buf, .iov_len = len };
        ssize_t rx_total_len = OutputErrorMagicNumber(&single_iov, 1);
        if (rx_total_len > 0) {
            return rx_total_len;
        }
 
        if (m_rx.m_get_and_ack_event) {
            if (GetAndAckEvent(UMQ_IO_RX) < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "RecvFrom GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
            m_rx.m_get_and_ack_event = false;
        }
 
        uint32_t max_buf_size;
        if (m_rx.m_readv_unlimited) {
            max_buf_size = UINT32_MAX;
        } else {
            max_buf_size = static_cast<uint32_t>(len);
            if (max_buf_size != len) {
                errno = EINVAL;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "RecvFrom invalid max_buf_size conversion, fd: %d, len: %zu, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, len, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
        }
 
        bool is_blocking = IsBlocking(m_fd);
        umq_buf_t *buf_array[POLL_BATCH_MAX];
        int poll_num = 0;
 
        while (true) {
            poll_num = UmqPollAndRefillRx(buf_array, POLL_BATCH_MAX);
            if (poll_num < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "RecvFrom UmqPollAndRefillRx() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1; // Error occurred during polling
            } else if (poll_num > 0) {
                break;
            } else { // poll_num == 0
                if (!is_blocking) {
                    errno = EAGAIN;
                    return -1;
                } else {
                    RPC_ADPT_VLOG_DEBUG("Recv: Blocking mode, no data, polling again...\n");
                }
            }
        }
 
        // Process Polled Buffers and Copy Directly (only executed if poll_num > 0)
        rx_total_len = 0; // Total bytes copied to user buffer
        char* user_buf_ptr = static_cast<char*>(buf); // Use a pointer to track position in user buffer
        size_t remaining_user_buf = len; // Remaining space in user buffer
 
        if (poll_num > 0) {
            for (int i = 0; i < poll_num; ++i) {
                if (buf_array[i]->status != 0) {
                    if (buf_array[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                        RPC_ADPT_VLOG_DEBUG("RX CQE is invalid, status: %d\n", buf_array[i]->status);
                        if (buf_array[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                            m_flow_control_failed = true;
                        }
                        QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                        umq_buf_free(buf_array[i]);
                        continue; // Skip this invalid buffer
                    } else {
                        m_rx.m_window_size += 1;
                        // Handle flow control update
                        bool need_fc_awake = m_tx.m_need_fc_awake.exchange(false, std::memory_order_relaxed);
                        if (need_fc_awake && eventfd_write(m_event_fd, 1) == -1) {
                            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                                "eventfd_write() failed, event fd: %d, errno: %d, errmsg: %s\n",
                                m_event_fd, errno,
                                NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                        }

                        QBUF_LIST_NEXT(buf_array[i]) = nullptr;
                        umq_buf_free(buf_array[i]);
                        continue;  // No data in this FC update packet
                    }
                }
 
                size_t data_size_to_copy = buf_array[i]->data_size;
 
                // Check if user buffer has enough space for this chunk
                if (remaining_user_buf < data_size_to_copy) {
                    data_size_to_copy = remaining_user_buf; // Copy only what fits
                }
 
                if (data_size_to_copy > 0) {
                    // Copy data directly from UMQ buffer to user buffer
                    memcpy_s(user_buf_ptr, data_size_to_copy, buf_array[i]->buf_data, data_size_to_copy);
                    rx_total_len += data_size_to_copy;
                    user_buf_ptr += data_size_to_copy;
                    remaining_user_buf -= data_size_to_copy;
                }
 
                // Free the UMQ buffer after copying its data
                QBUF_LIST_NEXT(buf_array[i]) = nullptr; // Ensure list termination if needed by umq_buf_free
                umq_buf_free(buf_array[i]); // Free the UMQ buffer structure and its associated data area
 
                // If user buffer is full, stop processing more buffers in this call
                if (remaining_user_buf == 0) {
                    RPC_ADPT_VLOG_DEBUG("Recv: User buffer is full, stopping processing of further buffers\n");
                    break;
                }
            }
        }
 
        if (rx_total_len == 0) {
            // Check for epoll event num mismatch (similar to ReadV)
            if (!m_rx.m_epoll_event_num.compare_exchange_strong(
                 m_rx.m_expect_epoll_event_num, 0, std::memory_order_release, std::memory_order_acquire)) {
                m_rx.m_poll = true;
                errno = EINTR;
                return -1;
            }

            if (m_flow_control_failed == true) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "RecvFrom flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }
 
            bool closed = m_closed.load(std::memory_order_relaxed);
            if (closed == true) {
                return 0;
            }
 
            if (RearmRxInterrupt() < 0) {
                errno = EIO;
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "RecvFrom RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, -1, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                return -1;
            }

            errno = EAGAIN;
            return -1;
        }
 
        return rx_total_len;
    }

    ALWAYS_INLINE int Fcntl(int fd, int cmd, unsigned long int arg)
    {
        // arg can be either struct flock or int
        int ret { 0 };

        switch (cmd) {
            case F_SETFL: // Set file status flags (O_NONBLOCK, etc.)
                ret = OsAPiMgr::GetOriginApi()->fcntl(m_fd, cmd, arg);
                if (ret == 0) {
                    // Update m_isblocking based on the origin fd blocking state
                    m_isblocking = IsBlocking(m_fd);
                } else {
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                        "fcntl() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, ret, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                }
                break;
            case F_GETOWN: // Get process ID or process group ID receiving SIGIO/SIGURG signals
            case F_GETFL:  // Get file status flags
            case F_GETFD:  // Get file descriptor flags
                ret = OsAPiMgr::GetOriginApi()->fcntl(m_fd, cmd, arg);
                break;
            case F_DUPFD:          // Duplicate file descriptor using the lowest available fd >= arg
            case F_DUPFD_CLOEXEC:  // Duplicate file descriptor and set FD_CLOEXEC flag
            case F_SETOWN:         // Set process ID or process group ID for SIGIO/SIGURG signals
            case F_SETLK:          // Set file lock (non-blocking)
            case F_SETLKW:         // Set file lock (blocking)
            case F_GETLK:          // Get file lock information
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "fcntl fail, fd %d, cmd %d is not supported\n", m_fd, cmd);
                break;
            default:
                RPC_ADPT_VLOG_WARN("fcntl, fd %d, cmd %d may be not supported\n", m_fd, cmd);
                ret = OsAPiMgr::GetOriginApi()->fcntl(m_fd, cmd, arg);
                break;
        }
        return ret;
    }

    ALWAYS_INLINE int Fcntl64(int fd, int cmd, unsigned long int arg)
    {
        // arg can be either struct flock or int
        int ret { 0 };

        switch (cmd) {
            case F_SETFL:
                ret = OsAPiMgr::GetOriginApi()->fcntl64(m_fd, cmd, arg);
                if (ret == 0) {
                    // Update m_isblocking based on the origin fd blocking state
                    m_isblocking = IsBlocking(m_fd);
                } else {
                    char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                        "fcntl64() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                        m_fd, ret, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
                }
                break;
            case F_GETOWN: // Get process ID or process group ID receiving SIGIO/SIGURG signals
            case F_GETFL:  // Get file status flags
            case F_GETFD:  // Get file descriptor flags
                ret = OsAPiMgr::GetOriginApi()->fcntl64(m_fd, cmd, arg);
                break;
            case F_DUPFD:          // Duplicate file descriptor using the lowest available fd >= arg
            case F_DUPFD_CLOEXEC:  // Duplicate file descriptor and set FD_CLOEXEC flag
            case F_SETFD:          // Set file descriptor flags
            case F_SETOWN:         // Set process ID or process group ID for SIGIO/SIGURG signals
            case F_SETLK:          // Set file lock (non-blocking)
            case F_SETLKW:         // Set file lock (blocking)
            case F_GETLK:          // Get file lock information
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "fcntl64 fail, fd %d, cmd %d is not supported\n", m_fd, cmd);
                break;
            default:
                RPC_ADPT_VLOG_WARN("fcntl64, fd %d, cmd %d may be not supported\n", m_fd, cmd);
                ret = OsAPiMgr::GetOriginApi()->fcntl64(m_fd, cmd, arg);
                break;
        }
        return ret;
    }

    ALWAYS_INLINE int Ioctl(int fd, unsigned long request, unsigned long int arg)
    {
        int ret { 0 };

        if (request == FIONBIO) {
            ret = OsAPiMgr::GetOriginApi()->ioctl(m_fd, request, arg);
            if (ret == 0) {
                // set m_isblocking base on origin fd blocking state
                m_isblocking = IsBlocking(m_fd);
            } else {
                char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "ioctl() failed for FIONBIO, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
                    m_fd, ret, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            }
        } else {
            RPC_ADPT_VLOG_WARN("ioctl set fd %d, request %d may be not supported\n", m_fd, request);
            ret = OsAPiMgr::GetOriginApi()->ioctl(m_fd, request, arg);
        }
        return ret;
    }

    ALWAYS_INLINE int SetSockOpt(int fd, int level, int optname, const void *optval, socklen_t optlen)
    {
        RPC_ADPT_VLOG_INFO("SetSockOpt set fd %d, level %d, optname %d\n", m_fd, level, optname);
        return OsAPiMgr::GetOriginApi()->setsockopt(fd, level, optname, optval, optlen);
    }

    ALWAYS_INLINE int SetTcpNoDelay(int fd)
    {
        int on = 1;
        return SetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    }

    ALWAYS_INLINE void NewOriginEpollError()
    {
        m_closed.store(true, std::memory_order_relaxed);
    }

    ALWAYS_INLINE void NewOriginEpollIn(bool use_polling = false)
    {
        m_rx.m_epoll_in_msg_recv_size = 
            OsAPiMgr::GetOriginApi()->recv(m_fd, (void *)&m_rx.m_epoll_in_msg, sizeof(uint8_t), MSG_NOSIGNAL);
        if (m_rx.m_epoll_in_msg_recv_size == 0) {
            m_closed.store(true, std::memory_order_relaxed);
        } else if (!use_polling) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Unexpected EPOLLIN event through TCP\n");
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

    ALWAYS_INLINE int RearmShareJfrRxInterrupt()
    {
        umq_interrupt_option_t rx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
        int ret = umq_rearm_interrupt(m_main_umqh, false, &rx_option);
        if (ret < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_rearm_interrupt() failed for share jfr RX, main umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_main_umqh), ret);
        }
        return ret;
    }

    ALWAYS_INLINE int RearmRxInterrupt()
    {
        umq_interrupt_option_t rx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX };
        int ret = umq_rearm_interrupt(m_local_umqh, false, &rx_option);
        if (ret < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_rearm_interrupt() failed for RX, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), ret);
        }
        return ret;
    }

    ALWAYS_INLINE int RearmTxInterrupt()
    {
        umq_interrupt_option_t tx_option = { UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX };
        int ret = umq_rearm_interrupt(m_local_umqh, true, &tx_option);
        if (ret < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_rearm_interrupt() failed for TX, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), ret);
        }
        return ret;
    }

    uint32_t getLeftPostRxNum(uint64_t umq_handle)
    {
        uint32_t left_post_rx_num = 0;
        umq_cfg_get_t cfg = {0};
        int res = umq_cfg_get(umq_handle, &cfg);
        if (res != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_cfg_get() failed, umq handle: %llu, ret: %d\n",
                static_cast<unsigned long long>(umq_handle), res);
        } else {
            left_post_rx_num = cfg.rqe_post_factor * cfg.rx_depth;
            RPC_ADPT_VLOG_INFO("Successfully get umq cfg, left_post_rx_num = %u\n", left_post_rx_num);
        }
        return left_post_rx_num;
    }
    
    int PrefillRx()
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        uint64_t umq_handle = enable_share_jfr ? m_main_umqh : m_local_umqh;
        uint32_t left_post_rx_num = getLeftPostRxNum(umq_handle);
        if (left_post_rx_num == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Failed to set rx window capacity\n");
            return -1;
        }
        uint32_t cur_post_rx_num = 0;
        umq_alloc_option_t option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(IOBuf::Block) };
        do {
            cur_post_rx_num = left_post_rx_num > POST_BATCH_MAX ? POST_BATCH_MAX : left_post_rx_num;
            umq_buf_t *rx_buf_list = umq_buf_alloc(BrpcIOBufSize(), cur_post_rx_num, UMQ_INVALID_HANDLE, &option);
            if (rx_buf_list == nullptr) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_buf_alloc() failed, RX depth: %u, ret: %p\n",
                    m_rx_window_capacity, rx_buf_list);
                return -1;
            }

            umq_buf_t *bad_qbuf = nullptr;
            int umq_ret = umq_post(umq_handle, rx_buf_list, UMQ_IO_RX, &bad_qbuf);
            if (umq_ret != UMQ_SUCCESS) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_post() failed, RX depth: %u, ret: %d\n",
                    m_rx_window_capacity, umq_ret);
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

        int local_umq_state = QUEUE_STATE_IDLE;
        local_umq_state = umq_state_get(m_local_umqh);
        if (local_umq_state != QUEUE_STATE_READY) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_state_get() failed to reach ready, ret: %d\n", local_umq_state);
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

    // 逻辑关闭, 实际关闭得等到 read/readv 读取到 EOF.
    ALWAYS_INLINE void Close()
    {
        // 快速退出, 如果 brpc-adapter 正好在 readv/writev 中可以不经过一次 epoll_wait.
        m_closed.store(true, std::memory_order_relaxed);

        // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
        // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
        OsAPiMgr::GetOriginApi()->shutdown(m_fd, SHUT_RD);
        RPC_ADPT_VLOG_DEBUG("closing socket fd=%d\n", m_fd);
    }

    // 收端异常 CQE 错误处理.
    void HandleErrorRxCqe(umq_buf_t *buf);

    // 发端异常 CQE 错误处理.
    void HandleErrorTxCqe(umq_buf_t *buf);

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

    ALWAYS_INLINE int UmqPollAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size)
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool use_polling = context == nullptr ? false : context->GetUsePolling();
        int poll_num = use_polling ? PollingEpoll::GetInstance().GetAndPopQbuf(m_local_umqh, buf) :
                                     umq_poll(m_local_umqh, UMQ_IO_RX, buf, max_buf_size);
        if (poll_num < 0 || (poll_num == 0 && m_rx.m_window_size == 0)) {
            if (!use_polling && poll_num < 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_poll() failed, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), poll_num);
            }
            return -1;
        }

        m_rx.m_window_size -= static_cast<uint16_t>(poll_num);
        if (static_cast<uint16_t>(m_rx_window_capacity - m_rx.m_window_size) > m_rx.m_refill_threshold) {
            umq_alloc_option_t option = { UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(IOBuf::Block) };
            umq_buf_t *rx_buf_list = umq_buf_alloc(BrpcIOBufSize(), m_rx.m_refill_threshold, UMQ_INVALID_HANDLE,
                                                   &option);
            /* do nothing when failure occurs during refilling RX,
             * try to switch to tcp/ip until poll_num & m_rx.m_window_size both equal to zero */
            if (rx_buf_list != nullptr) {
                umq_buf_t *bad_qbuf = nullptr;
                int umq_ret = umq_post(m_local_umqh, rx_buf_list, UMQ_IO_RX, &bad_qbuf);
                if (umq_ret == UMQ_SUCCESS) {
                    m_rx.m_window_size += m_rx.m_refill_threshold;
                } else if ((m_rx.m_window_size += HandleBadQBuf(rx_buf_list, bad_qbuf)) == 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_post() failed in refill, ret: %d\n", umq_ret);
                    return -1;
                }
            }
        }

        return poll_num;
    }

    int AddQbuf(umq_buf_t *qbuf)
    {
        int enqueue_ret = 0;
        if (rxQueue == nullptr || (enqueue_ret = rxQueue->Enqueue(qbuf)) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "AddQbuf failed, fd: %d, ret: %d\n",
                m_fd, rxQueue == nullptr ? -1 : enqueue_ret);
            return -1;
        }

        return 0;
    }

    void SetShareJfrRxEpollEvent(ShareJfrRxEpollEvent *epoll_event)
    {
        share_jfr_rx_epoll_event = epoll_event;
    }

    ShareJfrRxEpollEvent *GetShareJfrRxEpollEvent() const
    {
        return share_jfr_rx_epoll_event;
    }

private:
    struct CpMsg {
        uint64_t protocol_negotiation = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
        uint64_t queue_bind_info_size;
        uint8_t queue_bind_info[UMQ_BIND_INFO_SIZE_MAX];
    };

    struct NegotiateReq {
        uint64_t magic_number = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
        ub_trans_mode trans_mode = RC_TP;
        uint8_t is_bonding = 0;
        uint8_t enable_share_jfr = 0;
        uint8_t schedule_policy = static_cast<uint8_t>(dev_schedule_policy::ROUND_ROBIN);
        uint8_t has_socket_id = 0;
        int32_t process_socket_id = -1;
        umq_eid_t local_eid = {};
        uint32_t socket_id_count = 0;
        uint32_t socket_ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    };

    struct NegotiateRsp {
        int32_t ret_code = 0;
        ub_trans_mode peer_trans_mode = RC_TP;
        uint8_t reserved[3] = {0};
        umq_eid_t local_eid = {};
    };

    int BuildNegotiateReq(NegotiateReq* req)
    {
        Context *context = Context::GetContext();
        umq_eid_t localEid = context->GetDevSrcEid();
        dev_schedule_policy schedulePolicy = context->GetDevSchedulePolicy();
        req->magic_number = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
        req->trans_mode = context->GetUbTransMode();
        req->is_bonding = context->IsBonding() ? 1 : 0;
        req->enable_share_jfr = context->EnableShareJfr() ? 1 : 0;
        req->schedule_policy = static_cast<uint8_t>(schedulePolicy);
        req->has_socket_id = ((schedulePolicy == dev_schedule_policy::CPU_AFFINITY) ||
            (schedulePolicy == dev_schedule_policy::CPU_AFFINITY_PRIORITY)) ? 1 : 0;
        req->process_socket_id = context->GetProcessSocketId();
        req->local_eid = localEid;
        if (req->is_bonding != 0 && (req->has_socket_id == 1) &&
            FillLocalSocketIdsForNegotiate(context, req->socket_ids, req->socket_id_count) != 0) {
            return -1;
        }
        return 0;
    }

    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size)
    {
        if (rxQueue == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "GetAndPopQbuf failed, rx queue is null, fd: %d, ret: %d\n",
                m_fd, -1);
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

    uint64_t GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid)
    {
        std::vector<std::shared_ptr<MainUmqState>> main_umqs;
        if (!EidUmqTable::Get(*localEid, main_umqs)) {
            umq_create_option_t cfg_main;
            memcpy_s(&cfg_main, sizeof(cfg_main), cfg, sizeof(*cfg));
            cfg_main.create_flag |= UMQ_CREATE_FLAG_MAIN_UMQ;

            return umq_create(&cfg_main);
        }

        if (main_umqs.empty()) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "Main umq list is empty, local eid:" EID_FMT ", ret: %llu\n",
                EID_ARGS(*localEid), static_cast<unsigned long long>(UMQ_INVALID_HANDLE));
            return UMQ_INVALID_HANDLE;
        }

        // 当前实现 eid 实际只对应 1 个主 umq, 故同一 eid 拿到的主 umq 都是相同的.
        return main_umqs.front()->GetUmqHandle();
    }

    uint64_t CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *localEid)
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr) {
            return umq_create(cfg);
        }

        ScopedUbExclusiveLocker sLock(EidUmqTable::GetMainMutex());
        uint64_t main_umq = GetOrCreateMainUmq(cfg, localEid);
        if (main_umq == UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "GetOrCreateMainUmq() failed, ret: %llu\n",
                static_cast<unsigned long long>(main_umq));
            return UMQ_INVALID_HANDLE;
        }

        cfg->create_flag |= UMQ_CREATE_FLAG_SHARE_RQ | UMQ_CREATE_FLAG_UMQ_CTX | UMQ_CREATE_FLAG_SUB_UMQ;
        cfg->share_rq_umqh = main_umq;
        cfg->umq_ctx = (uint64_t)m_fd;
        uint64_t sub_umq = umq_create(cfg);
        if (sub_umq == UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_create() failed for sub umq, ret: %llu\n",
                static_cast<unsigned long long>(sub_umq));
            return UMQ_INVALID_HANDLE;
        }

        EidUmqTable::Add(*localEid, main_umq);
        MainSubUmqTable::Add(main_umq, sub_umq);

        m_main_umqh = main_umq;
        return sub_umq;
    }

    int GetDevEid(char *dev_name, uint32_t eid_idx, umq_eid_t *eid)
    {
        umq_dev_info_t umq_dev_info = {};
        int ret = umq_dev_info_get(dev_name, UMQ_TRANS_MODE_UB, &umq_dev_info);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_dev_info_get() failed, ret: %d\n", ret);
            return -1;
        }

        for (uint32_t i = 0; i < umq_dev_info.ub.eid_cnt; ++i) {
            if (umq_dev_info.ub.eid_list[i].eid_index == eid_idx) {
                *eid = umq_dev_info.ub.eid_list[i].eid;
                return 0;
            }
        }

        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
            "Failed to find eid index in device info, eid_idx: %u, ret: %d\n",
            eid_idx, -1);
        return -1;
    }

    ubsocket::Error CreateLocalUmq(umq_eid_t *connEid)
    {
        if (m_local_umqh != UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Create umq on a created umq.\n");
            return ubsocket::FromRaw(EEXIST);
        }

        auto *context = Context::GetContext();

        umq_create_option_t queue_cfg;
        memset_s(&queue_cfg, sizeof(queue_cfg), 0, sizeof(queue_cfg));
        queue_cfg.trans_mode = context->GetTransMode();
        queue_cfg.create_flag = UMQ_CREATE_FLAG_TX_DEPTH | UMQ_CREATE_FLAG_RX_DEPTH |
                                UMQ_CREATE_FLAG_RX_BUF_SIZE | UMQ_CREATE_FLAG_TX_BUF_SIZE |
                                UMQ_CREATE_FLAG_QUEUE_MODE | UMQ_CREATE_FLAG_TP_MODE |
                                UMQ_CREATE_FLAG_TP_TYPE | UMQ_CREATE_FLAG_UMQ_CTX;
        queue_cfg.rx_depth = context->GetRxDepth();
        queue_cfg.tx_depth = context->GetTxDepth();
        queue_cfg.rx_buf_size = BrpcIOBufSize();
        queue_cfg.tx_buf_size = BrpcIOBufSize();
        queue_cfg.mode = UMQ_MODE_INTERRUPT;
        // 共享 JFR、AE 事件依赖 umq_ctx.
        queue_cfg.umq_ctx = m_fd;
        queue_cfg.used_ports = mUsedPorts;

        if (context->GetLinkPriority() != DEFAULT_LINK_PRIORITY) {
            queue_cfg.priority = context->GetLinkPriority();
            queue_cfg.create_flag |= UMQ_CREATE_FLAG_PRIORITY;
        } 

        int n = snprintf_s(queue_cfg.name, UMQ_NAME_MAX_LEN, UMQ_NAME_MAX_LEN - 1, "fd: %d", m_fd);
        if ((((int)UMQ_NAME_MAX_LEN - 1) < n) || (n < 0)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to set umq name\n");
            return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
        }

        umq_eid_t localEid;
        if (context->GetDevIpStr() != nullptr) {
            if (context->IsDevIpv6()) {
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
                if (strcpy_s(queue_cfg.dev_info.ipv6.ip_addr, UMQ_IPV6_SIZE, context->GetDevIpStr()) != EOK) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to strcpy_s device ipv6 address\n");
                    return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
                }
            } else {
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
                if (strcpy_s(queue_cfg.dev_info.ipv4.ip_addr, UMQ_IPV4_SIZE, context->GetDevIpStr()) != EOK) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to strcpy_s device ipv4 address\n");
                    return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
                }
            }
        } else if (context -> GetDevNameStr() != nullptr) {
            if (strcpy_s(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, context->GetDevNameStr()) != EOK) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to strcpy_s device name\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
            if(!context->IsBonding()){
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
                queue_cfg.dev_info.dev.eid_idx = context->GetEidIdx();

                if (GetDevEid(queue_cfg.dev_info.dev.dev_name, context->GetEidIdx(), &localEid) != 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                        "Failed to get eid by dev name:%s and eid index:%d \n",
                        context->GetDevNameStr(), context->GetEidIdx());
                }
                m_conn_info.conn_eid = localEid;
            } else {
                // init use bonding dev
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
                queue_cfg.dev_info.eid.eid = *connEid;
                localEid = *connEid;
            }
        } else {
            if (strcpy_s(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "bonding_dev_0") != EOK) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to strcpy_s device name\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
            if (context->IsBonding()) {
                queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
                queue_cfg.dev_info.eid.eid = *connEid;
            }
        }

        ub_trans_mode trans_mode = context->GetUbTransMode();
        static const char *trans_mode_str[RC_CTP + 1] = {
 	        "RC_TP",
 	        "RM_TP",
 	        "RM_CTP",
 	        "RC_CTP"
 	    };
        RPC_ADPT_VLOG_INFO("trans_mode result is: %s\n", trans_mode_str[trans_mode]);
        if (trans_mode == RC_TP) {
            queue_cfg.tp_mode = UMQ_TM_RC;
            queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
        } else if (trans_mode == RM_TP) {
            queue_cfg.tp_mode = UMQ_TM_RM;
            queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
        } else if (trans_mode == RM_CTP) {
            queue_cfg.tp_mode = UMQ_TM_RM;
            queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
        } else if (trans_mode == RC_CTP) {
            queue_cfg.tp_mode = UMQ_TM_RC;
            queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
        }

        m_local_umqh = CreateSubUmq(&queue_cfg, &localEid);
        if (m_local_umqh == UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "CreateSubUmq() failed, ret: %llu\n",
                static_cast<unsigned long long>(m_local_umqh));
            return ubsocket::Error::kUMQ_CREATE | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        }

        uint64_t share_jfr_rx_queue_depth = context == nullptr ? DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH :
                                                                 context->GetShareJfrRxQueueDepth();
        rxQueue = new (std::nothrow) QbufQueue<umq_buf_t *>(share_jfr_rx_queue_depth);
        if (rxQueue == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to init share jfr rx queue for fd: %d \n", m_fd);
            return ubsocket::Error::kUBSOCKET_INIT_SHARED_JFR_RX_QUEUE;
        }

        Context::FetchAdd();

        return ubsocket::Error::kOK;
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
        
        int ret = umq_unbind(m_local_umqh);
        if (ret != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_unbind() failed, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), ret);
        }
        FlushTx();

        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        enable_share_jfr ? FlushRxQueue() : FlushRx();
    }

    void FlushRxQueue()
    {
        if (rxQueue == nullptr) {
            return;
        }

        while (!rxQueue->IsEmpty()) {
            umq_buf_t *buf[1];
            if (rxQueue->Dequeue(buf) != 0) {
                return;
            }
            umq_buf_free(buf[0]);
        }
    }

    void DeleteSubUmq()
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr) {
            return;
        }

        MainSubUmqTable::RemoveSubUmq(m_main_umqh, m_local_umqh);
    }

    void DestroyLocalUmq(void)
    {
        if (m_local_umqh != UMQ_INVALID_HANDLE) {
            // need to flush
            int ret = umq_destroy(m_local_umqh);
            if (ret != UMQ_SUCCESS) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_destroy() failed, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), ret);
            }
            DeleteSubUmq();
            m_local_umqh = UMQ_INVALID_HANDLE;
            Context::FetchSub();
        }
    }
    static int ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size)
    {
        protocol_negotiation_recv_size =
           RecvSocketData(fd, &protocol_negotiation, sizeof(protocol_negotiation), NEGOTIATE_TIMEOUT_MS);
        if (protocol_negotiation_recv_size <= 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Validate protocol failed, fd: %d, ret: %zd\n",
                fd, protocol_negotiation_recv_size);
            return -1;
        }

        if (protocol_negotiation_recv_size != sizeof(protocol_negotiation) ||
            protocol_negotiation != CONTROL_PLANE_PROTOCOL_NEGOTIATION) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Validate protocol mismatch, fd: %d, ret: %zd\n",
                fd, protocol_negotiation_recv_size);
            return protocol_negotiation_recv_size;
        }

        return 0;
    }

    ALWAYS_INLINE ssize_t OutputErrorMagicNumber(const struct iovec *iov, int iovcnt)
    {
        if (m_tx.m_protocol_negotiation_recv_size == 0) {
            return 0;
        }

        ssize_t rx_total_len = 0;
        int iov_idx = 0;
        do {
            size_t copy_size = iov[iov_idx].iov_len < m_tx.m_protocol_negotiation_recv_size ?
                iov[iov_idx].iov_len : m_tx.m_protocol_negotiation_recv_size;
            (void)memcpy_s(iov[iov_idx++].iov_base, copy_size,
                (char *)&m_tx.m_protocol_negotiation + m_tx.m_protocol_negotiation_offset, copy_size);
            m_tx.m_protocol_negotiation_recv_size -= copy_size;
            m_tx.m_protocol_negotiation_offset += copy_size;
            rx_total_len += copy_size;        
        } while (m_tx.m_protocol_negotiation_recv_size > 0 && iov_idx < iovcnt);

        if (m_tx.m_protocol_negotiation_recv_size == 0) {
            m_rx_use_tcp = true;
        }

        return rx_total_len;
    }

    int AcceptNegotiate(int new_fd, umq_eid_t &connEid, umq_eid_t &dstEid, dev_schedule_policy &peerSchedulePolicy)
    {
        Context *context = Context::GetContext();
        dev_schedule_policy schedulePolicy = context->GetDevSchedulePolicy();
        bool has_socket_id = ((schedulePolicy == dev_schedule_policy::CPU_AFFINITY) ||
            (schedulePolicy == dev_schedule_policy::CPU_AFFINITY_PRIORITY)) ? 1 : 0;
        NegotiateReq req {};
        NegotiateRsp rsp {};
        char *req_buf = reinterpret_cast<char *>(&req) + sizeof(req.magic_number);
        size_t req_remain_size = sizeof(req) - sizeof(req.magic_number);
        if (RecvSocketData(new_fd, req_buf, req_remain_size, CONTROL_PLANE_TIMEOUT_MS) !=
            static_cast<int>(req_remain_size)) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Failed to receive negotiate request in accept, fd: %d\n",
                new_fd);
            return -1;
        }
        ub_trans_mode local_trans_mode = context->GetUbTransMode();
        rsp.peer_trans_mode = local_trans_mode;
        if (req.trans_mode != local_trans_mode) {
            context->SetUbTransMode(req.trans_mode < local_trans_mode ? req.trans_mode : local_trans_mode);
        }
        rsp.ret_code = (context->IsBonding() == (req.is_bonding != 0)) ? 0 : -1;
        connEid = context->GetDevSrcEid();
        rsp.local_eid = connEid;
        mPeerAllSocketIds.clear();
        if (rsp.ret_code == 0 && req.is_bonding != 0 && has_socket_id == 1 && req.has_socket_id == 1) {
            if (req.socket_id_count == 0 || req.socket_id_count > NEGOTIATE_SOCKET_ID_MAX_NUM) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "Invalid peer socket count %u in accept, fd: %d\n",
                    req.socket_id_count, new_fd);
                rsp.ret_code = -1;
            } else {
                mPeerAllSocketIds.assign(req.socket_ids, req.socket_ids + req.socket_id_count);
                std::ostringstream oss;
                const char *separator = "";
                for (uint32_t socket_id : mPeerAllSocketIds) {
                    oss << separator << socket_id;
                    separator = ", ";
                }
                RPC_ADPT_VLOG_INFO("Successfully get all socket ids in accept: %s\n", oss.str().c_str());
            }
        }
        if (rsp.ret_code == 0 && req.is_bonding != 0) {
            dstEid = req.local_eid;
            peerSchedulePolicy = static_cast<dev_schedule_policy>(req.schedule_policy);
            if ((peerSchedulePolicy == dev_schedule_policy::CPU_AFFINITY || peerSchedulePolicy ==
                dev_schedule_policy::CPU_AFFINITY_PRIORITY) && req.has_socket_id != 0) {
                RPC_ADPT_VLOG_WARN("Use consistent schedule policy CPU_AFFINITY: %d in connect, fd: %d\n",
                    static_cast<int>(peerSchedulePolicy), new_fd);
                mPeerSocketId = req.process_socket_id;
            }
            rsp.local_eid = connEid;
        }
        if (SendSocketData(new_fd, &rsp, sizeof(rsp), CONTROL_PLANE_TIMEOUT_MS) != static_cast<int>(sizeof(rsp))) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Failed to send negotiate response in accept, fd: %d\n",
                new_fd);
            return -1;
        }
        if (req.is_bonding == 1) {
            umq_route_t connRoute;
            if (RecvSocketData(
                new_fd, &connRoute, sizeof(umq_route_t), CONTROL_PLANE_TIMEOUT_MS) != sizeof(umq_route_t)) {
                RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                    "Failed to receive remote eid message in accept, Peer IP:%s, fd: %d\n",
                    GetPeerIp().c_str(), new_fd);
                return -1;
            }

            int checkResult = CheckDevAdd(connRoute.dst_eid);
            if (checkResult != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "CheckDevAdd() failed in accept, Peer IP:%s, fd: %d, ret: %d\n",
                    GetPeerIp().c_str(), new_fd, checkResult);
                return -1;
            }
        
            // 保存对端EID
            dstEid = connRoute.src_eid;
            connEid = connRoute.dst_eid;
        }
        return rsp.ret_code == 0 ? 0 : -1;
    }

    void PrintQbufPoolInfo()
    {
        umq_qbuf_pool_stats_t qbuf_pool_stats;
        int ret = umq_stats_qbuf_pool_get(m_main_umqh, &qbuf_pool_stats);
        if (ret != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_stats_qbuf_pool_get() failed, main umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_main_umqh), ret);
            return;
        }
        if (qbuf_pool_stats.num == 1) {
            uint64_t size_with_data = qbuf_pool_stats.qbuf_pool_info[0].available_mem.split.size_with_data;
            RPC_ADPT_VLOG_INFO("UMQ qbuf pool available buf size in data area: %lu \n", size_with_data);
        }
    }

    ubsocket::Error DoUbAccept(int new_fd, umq_eid_t &connEid, SocketFd *socket_fd_obj)
    {
        CpMsg local_cp_msg;
        CpMsg remote_cp_msg;

        ubsocket::Error ret = socket_fd_obj->CreateLocalUmq(&connEid);
        if (!IsOk(ret)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Failed to create umq\n");
            return ret;
        }

        local_cp_msg.queue_bind_info_size = umq_bind_info_get(
            socket_fd_obj->GetLocalUmqHandle(), local_cp_msg.queue_bind_info, sizeof(local_cp_msg.queue_bind_info));
        if (local_cp_msg.queue_bind_info_size == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_bind_info_get() failed, ret: %u\n",
                local_cp_msg.queue_bind_info_size);
            return ubsocket::Error::kUMQ_BIND_INFO_GET | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        }

        size_t len = sizeof(remote_cp_msg) - sizeof(uint64_t);
        if (RecvSocketData(
            new_fd, &remote_cp_msg.queue_bind_info_size, len, CONTROL_PLANE_TIMEOUT_MS) != (ssize_t)len) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET, "Failed to receive remote control message, fd: %d\n", new_fd);
            return ubsocket::FromRaw(errno);
        }
        RPC_ADPT_VLOG_DEBUG("recv remote control message, fd: %d, cp msg len: %d, bind info len: %d\n",
            new_fd, sizeof(remote_cp_msg), remote_cp_msg.queue_bind_info_size);

        if (SendSocketData(
            new_fd, &local_cp_msg, sizeof(local_cp_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local_cp_msg)) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET, "Failed to send local control message, fd: %d\n", new_fd);
            return ubsocket::FromRaw(errno);
        }
        RPC_ADPT_VLOG_DEBUG("send local control message, fd: %d, cp msg len: %d, bind info len: %d\n",
            new_fd, sizeof(local_cp_msg), local_cp_msg.queue_bind_info_size);

        int umq_ret = umq_bind(socket_fd_obj->GetLocalUmqHandle(), remote_cp_msg.queue_bind_info,
                               remote_cp_msg.queue_bind_info_size);
        if (umq_ret != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_bind() failed, ret: %d\n", umq_ret);
            return ubsocket::Error::kUMQ_BIND | ubsocket::Error::kRETRYABLE | ubsocket::Error::kDEGRADABLE;
        }
        socket_fd_obj->SetBindRemote(true);

        if (Context::GetContext()->EnableShareJfr()) {
            // 强依赖当前实现，一个 eid 只对应一个主 umq. 如果后续逻辑有变更，需同步修改。
            auto main_umq = EidUmqTable::GetFirst(socket_fd_obj->m_conn_info.conn_eid);
            if (main_umq == nullptr) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "The main umq state is removed by other thread.\n");
                return ubsocket::Error::kUBSOCKET_NO_MAIN_UMQ;
            }

            return main_umq->EnsurePrefilled([socket_fd_obj, new_fd]() {
                if (socket_fd_obj->PrefillRx() != 0) {
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to fill rx buffer to main umq, fd: %d\n", new_fd);
                    return ubsocket::Error::kUBSOCKET_PREFILL_RX;
                }
                return ubsocket::Error::kOK;
            });
        }

        // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
        if (socket_fd_obj->PrefillRx() != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to fill rx buffer to umq, fd: %d\n", new_fd);
            return ubsocket::Error::kUBSOCKET_PREFILL_RX;
        }

        return ubsocket::Error::kOK;
    }

    ubsocket::Error DoAccept(int new_fd, const std::string& peerIp)
    {
        SocketFd *socket_fd_obj = nullptr;
        int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (event_fd < 0) {
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "eventfd() failed, ret: %d, errno: %d, errmsg: %s\n",
                event_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return ubsocket::Error::kUBSOCKET_NEW_SOCKET_FD;
        }
        try {
            socket_fd_obj = new SocketFd(new_fd, event_fd);
            socket_fd_obj->m_peer_info.peer_ip = peerIp;
            socket_fd_obj->m_peer_info.peer_fd = new_fd;
            socket_fd_obj->m_peer_info.type_fd = 0;
        } catch (std::exception& e) {
            OsAPiMgr::GetOriginApi()->close(event_fd);
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
            return ubsocket::Error::kUBSOCKET_NEW_SOCKET_FD;
        }

        auto sockCleaner = ubsocket::MakeScopeExit([socket_fd_obj]() { delete socket_fd_obj; });

        umq_eid_t connEid;
        umq_eid_t dstEid;
        Context *context = Context::GetContext();
        umq_eid_t localEid = context->GetDevSrcEid();
        dev_schedule_policy peerSchedulePolicy = dev_schedule_policy::ROUND_ROBIN;
        if (AcceptNegotiate(new_fd, connEid, dstEid, peerSchedulePolicy) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Failed to negotiate in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
            return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
        }
        socket_fd_obj->m_peer_info.peer_eid = dstEid;
        socket_fd_obj->m_conn_info.conn_eid = connEid;

        // 1. 用户直接指定普通设备建链，失败不重试、可降级
        // 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，失败不重试、可降级
        // 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误
        //   - 优先重试，如果重试过程中失败则降级
        //   - 如果无法重试，则尝试降级
        //   - 如果无法降级，则返回失败
        bool ok = false;
        bool degradable = false;
        ubsocket::Error ackRet;
        ubsocket::Error peerRet;
        OtherRouteMessage otherRouteMessage;
        UBHandshakeState state = UBHandshakeState::kSTART;
        while (!ok) {
            switch (state) {
                case UBHandshakeState::kOK:
                    ok = true;
                    break;

                case UBHandshakeState::kSTART:
                    ackRet = DoUbAccept(new_fd, connEid, socket_fd_obj);
                    if (Degradable(ackRet) && !Context::GetContext()->Degradable()) {
                        ackRet = ackRet - ubsocket::Error::kDEGRADABLE;
                    }

                    if (!IsOk(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), m_fd);
                    }

                    if (SendSocketData(new_fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                        return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
                    }

                    if (RecvSocketData(new_fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                        return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
                    }

                    // 服务端判断是否可降级
                    degradable = Degradable(ackRet);
                    if (IsOk(ackRet) && IsOk(peerRet)) {
                        state = UBHandshakeState::kOK;
                    } else if (context->IsBonding() && connEid != localEid &&
                               (Retryable(ackRet) || Retryable(peerRet))) {
                        state = UBHandshakeState::kRETRY;
                    } else if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kRETRY:
                    if (peerSchedulePolicy == dev_schedule_policy::CPU_AFFINITY) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "CPU_AFFINITY: %d failed, accept no need to retry,Peer eid:" EID_FMT
                            ",Peer IP:%s, fd: %d\n",
                            static_cast<int>(peerSchedulePolicy), EID_ARGS(socket_fd_obj->GetPeerEid()),
                            socket_fd_obj->GetPeerIp().c_str(), m_fd);

                        if (degradable) {
                            state = UBHandshakeState::kDEGRADE;
                        } else {
                            state = UBHandshakeState::kFAILED;
                        }
                        break;
                    }
                    socket_fd_obj->UnbindAndFlushRemoteUmq();
                    socket_fd_obj->DestroyLocalUmq();

                    if (RecvSocketData(new_fd, &otherRouteMessage, sizeof(otherRouteMessage),
                        CONTROL_PLANE_TIMEOUT_MS) != sizeof(otherRouteMessage)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to receive remote eid message in retry accept, Peer eid:" EID_FMT
                            ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                        return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
                    }

                    // 客户端 CheckOtherRoute 失败
                    if (otherRouteMessage.ub_handshake_state != UBHandshakeState::kRETRY) {
                        RPC_ADPT_VLOG_INFO("Client CheckOtherRoute failed, try to degrade to TCP.\n");
                        state = UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE;
                        break;
                    }

                    connEid = otherRouteMessage.other_route.dst_eid;
                    if (CheckDevAdd(connEid) != 0) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to add dev in retry accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), m_fd);
                        ackRet = ubsocket::Error::kUMQ_DEV_ADD | ubsocket::Error::kDEGRADABLE;
                    } else {
                        ackRet = ubsocket::Error::kOK;
                    }

                    // 保留在 CheckDevAdd 阶段时的错误
                    ackRet = ackRet | DoUbAccept(new_fd, connEid, socket_fd_obj);
                    if (Degradable(ackRet) && !Context::GetContext()->Degradable()) {
                        ackRet = ackRet - ubsocket::Error::kDEGRADABLE;
                    }

                    if (!IsOk(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "Failed to finish ub bind in accept, Peer eid:" EID_FMT ", Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                    }

                    if (SendSocketData(new_fd, &ackRet, sizeof(ackRet), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ackRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to send ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                        return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
                    }

                    if (RecvSocketData(new_fd, &peerRet, sizeof(peerRet), CONTROL_PLANE_TIMEOUT_MS) !=
                        sizeof(peerRet)) {
                        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                            "Failed to receive peer ack ret, Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                            EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                        return ubsocket::Error::kUBSOCKET_TCP_EXCHANGE;
                    }

                    degradable = Degradable(ackRet);
                    if (IsOk(ackRet) && IsOk(peerRet)) {
                        state = UBHandshakeState::kOK;
                    } else if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE:
                    if (degradable) {
                        state = UBHandshakeState::kDEGRADE;
                    } else {
                        state = UBHandshakeState::kFAILED;
                    }
                    break;

                case UBHandshakeState::kDEGRADE:
                    // 不调用 OverrideFdObj，当此连接上有请求时直接使用裸 socket API.
                    RPC_ADPT_VLOG_INFO("ubsocket is degraded to TCP.\n");
                    return ubsocket::Error::kUBSOCKET_UB_ACCEPT | ubsocket::Error::kDEGRADABLE;

                case UBHandshakeState::kFAILED:
                    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                        "Failed to get new connect in accept,Peer eid:" EID_FMT ",Peer IP:%s, fd: %d\n",
                        EID_ARGS(socket_fd_obj->GetPeerEid()), socket_fd_obj->GetPeerIp().c_str(), new_fd);
                    return ubsocket::Error::kUBSOCKET_UB_ACCEPT;
            }
        }

        if (m_context_trace_enable) {
            socket_fd_obj->InitStatsMgr();
        }

        // Delete existing objects and record new objects in the list.
        sockCleaner.Deactivate();
        Fd<::SocketFd>::OverrideFdObj(new_fd, socket_fd_obj);

        if (context && context->GetUsePolling()) {
            Socket *sock = NULL;
            if (PollingEpoll::GetInstance().SocketCreate(&sock, new_fd, SocketType::SOCKET_TYPE_TCP_SERVER,
                                                         socket_fd_obj->GetLocalUmqHandle()) != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "SocketCreate failed \n");
            } else {
                PollingEpoll::GetInstance().AddSocket(new_fd, sock);
            }
        }

        socket_fd_obj->m_conn_info.create_time = std::chrono::system_clock::now();
        RPC_ADPT_VLOG_INFO("UB connection has been successfully established new fd: %d\n", new_fd);

        if (m_context_trace_enable) {
            socket_fd_obj->UpdateTraceStats(StatsMgr::CONN_COUNT, 1);
            m_peer_info.type_fd = 0;
        }

        PrintQbufPoolInfo();

        return ubsocket::Error::kOK;
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

        // 如果是一个 read OP, 那么它的 left_size=0.
        if (last_qbuf != nullptr) {
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }

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
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "TX umq buffer list is in error, TX user context does not contain the right list\n");
            return -1;
        }

        // 如果是一个 read OP, 那么它的 left_size=0.
        if (last_qbuf != nullptr) {
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }
        umq_buf_free(start_qbuf);

        return wr_cnt;
    }

    ALWAYS_INLINE int UmqPollTx(error_code &err_code)
    {
        umq_buf_t *buf[POLL_BATCH_MAX];
        int poll_num = umq_poll(m_local_umqh, UMQ_IO_TX, buf, POLL_BATCH_MAX);
        if (poll_num <= 0) {
            if (poll_num < 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_poll() failed for TX, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), poll_num);
            }
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
                    HandleErrorTxCqe(buf[i]);
                    ProcessErrorTxCqe(buf[i]);
                    wr_cnt++;
                    if (m_context_trace_enable) {
                        if (buf[i]->status == UMQ_BUF_ACK_TIMEOUT_ERR) {
                            UpdateTraceStats(StatsMgr::TX_LOST_PACKET_COUNT, 1);
                        } else {
                            UpdateTraceStats(StatsMgr::TX_ERROR_PACKET_COUNT, 1);
                        }
                    }
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
        int ret = umq_rearm_interrupt(m_local_umqh, false, &tx_option);
        if (ret == 0) {
            errno = EAGAIN;
            return -1;
        }

        // 1. try to switch to tcp/ip
        // 2. use EIO for now
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
            "umq_rearm_interrupt() failed for TX, local umq: %llu, ret: %d\n",
            static_cast<unsigned long long>(m_local_umqh), ret);
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
                    ((Brpc::IOBuf::Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
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
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_poll() failed for RX flush, local umq: %llu, ret: %d\n",
                    static_cast<unsigned long long>(m_local_umqh), poll_cnt);
                break;
            }

            for (int i = 0; i < poll_cnt; i++) {
                if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE) {
                    if (eventfd_write(m_event_fd, 1) == -1) {
                        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
                        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                            "eventfd_write() failed, event fd: %d, errno: %d, errmsg: %s\n",
                            m_event_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
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
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_get_cq_event() failed, local umq: %llu, ret: %d\n",
                static_cast<unsigned long long>(m_local_umqh), events);
            return -1;
        }

        AckEvent(dir == UMQ_IO_TX ? m_tx.m_event_num : m_rx.m_event_num, events, &option);

        return 0;
    }

    ALWAYS_INLINE int GetQbuf(umq_buf_t **buf, uint32_t max_buf_size)
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr) {
            return UmqPollAndRefillRx(buf, max_buf_size);
        }

        int poll_num = GetAndPopQbuf(buf, max_buf_size);
        if (poll_num < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GetQbuf failed, fd: %d, ret: %d\n", m_fd, poll_num);
            return -1;
        }

        return poll_num;
    }

    ALWAYS_INLINE uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf,
        uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num)
    {
        umq_buf_t *cur_qbuf = head_qbuf;
        umq_buf_t *last_qbuf = nullptr;
        umq_buf_t *head_qbuf_ = last_head_qbuf;
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

    void GetBondingEidMapIndex(const umq_eid_t &dstEid, uint32_t &index)
    {
        if (!EidRegistry::Instance().IsRegisteredEidIndex(dstEid)) {
            EidRegistry::Instance().RegisterOrReplaceEidIndex(dstEid, 0);
        }

        EidRegistry::Instance().GetEidIndex(dstEid, index);
    }

    // Round_Robin
    int GetRoundRobinConnEid(umq_route_list_t &route_list, const umq_eid_t *dstEid, umq_route_t *connRoute)
    {
        // 获取起始索引
        uint32_t startIndex = 0;
        GetBondingEidMapIndex(*dstEid, startIndex);
        
        // 确保索引在有效范围内
        startIndex = startIndex % route_list.route_num;

        // 从起始索引开始轮询查找
        bool found = false;
        for (uint32_t offset = 0; offset < route_list.route_num; ++offset) {
            uint32_t currentIndex = (startIndex + offset) % route_list.route_num;
            *connRoute = route_list.routes[currentIndex];
            found = true;
            startIndex = (currentIndex + 1) % route_list.route_num;  // 更新下次起始位置
            break;
        }

        // 更新下一个轮询位置
        EidRegistry::Instance().RegisterOrReplaceEidIndex(*dstEid, startIndex);

        if (!found) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find umq dev\n");
            return -1;
        }

        return 0;
    }

    uint32_t GetTargetChipId(const std::vector<uint32_t>& socketIds, const std::vector<uint32_t>& chipIdList,
        int processSocketId)
    {
        auto it = std::find(socketIds.begin(), socketIds.end(), processSocketId);
        if (it == socketIds.end()) {
            return UINT32_MAX;  // 错误标识
        }
        
        size_t index = std::distance(socketIds.begin(), it);
        if (index >= chipIdList.size()) {
            return UINT32_MAX;  // 索引越界
        }
        
        return chipIdList[index];
    }

    // 通过亲和性选择 Cpu_Affinity
    int GetCpuAffinityConnEid(umq_route_list_t &route_list, const umq_eid_t *dstEid, umq_route_t *connRoute,
        const std::vector<uint32_t>& socketIds, int processSocketId)
    {
        // 提取 chip_id 列表
        std::set<uint32_t> uniqueChipIds;
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            uniqueChipIds.insert(route_list.routes[i].src_port.bs.chip_id);
        }
        std::vector<uint32_t> chipIdList(uniqueChipIds.begin(), uniqueChipIds.end());

        // 获得对应的chip_id
        uint32_t targetChipId = GetTargetChipId(socketIds, chipIdList, processSocketId);
        if (targetChipId == UINT32_MAX) {
            return GetRoundRobinConnEid(route_list, dstEid, connRoute);
        }
   
        // 查找匹配的eid对
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            if (targetChipId == route_list.routes[i].src_port.bs.chip_id) {
                *connRoute = route_list.routes[i];
                return 0;
            }
        }

        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find umq dev\n");
        return -1;
    }

    int GetConnEid(umq_route_list_t &route_list, const umq_eid_t *dstEid, umq_route_t *connRoute, bool useRoundRobin)
    {
        Context *context = Context::GetContext();
        if (!useRoundRobin) {
            int processSocketId = context->GetProcessSocketId();
            std::vector<uint32_t> socketIds = context->GetAllSocketId();
            return GetCpuAffinityConnEid(route_list, dstEid, connRoute, socketIds, processSocketId);
        }
        return GetRoundRobinConnEid(route_list, dstEid, connRoute);
    }
    
    int CheckDevAdd(const umq_eid_t &connEid)
    {
        if (EidRegistry::Instance().IsRegisteredEid(connEid)) {
            return 0;
        }

        umq_trans_info_t trans_info;
        trans_info.trans_mode = UMQ_TRANS_MODE_UB;
        trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
        trans_info.dev_info.eid.eid = connEid;
        int ret = umq_dev_add(&trans_info);
        if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_dev_add() failed, ret: %d\n", ret);
            return -1;
        }

        ret = Context::GetContext()->RegisterAsyncEvent(trans_info);
        if (ret < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "RegisterAsyncEvent() failed, conn eid:" EID_FMT ", ret: %d\n",
                EID_ARGS(connEid), ret);
            return ret;
        }
        
        EidRegistry::Instance().RegisterEid(connEid);
        return 0;
    }

    int CheckOtherRoute(umq_route_t &otherConnRoute, umq_eid_t &dstEid, umq_route_t &connRoute)
    {
        if (!RouteListRegistry::Instance().IsRegisteredRouteList(dstEid)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to check other route to connect\n");
            return -1;
        }

        umq_route_list_t routeList = {};
        if (!RouteListRegistry::Instance().GetRouteList(dstEid, routeList)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get route list in map\n");
            return -1;
        }

        umq_route_list_t filteredList = {};
        uint32_t filterNum = 0;
        bool found = false;
        for (uint32_t i = 0; i< routeList.route_num; ++i) {
            if (routeList.routes[i].src_port.bs.chip_id != connRoute.src_port.bs.chip_id) {
                if (filterNum==0) {
                    otherConnRoute = routeList.routes[i];
                    found = true;
                }
                filteredList.routes[filterNum++] = routeList.routes[i];
            }
        }

        if (!found) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find other route in map\n");
            return -1;
        }

        filteredList.route_num = filterNum;
        RouteListRegistry::Instance().RegisterOrReplaceRouteList(dstEid, filteredList);

        if (CheckDevAdd(otherConnRoute.src_eid)!=0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "CheckDevAdd() failed in CheckOtherRoute, src eid:" EID_FMT ", ret: %d\n",
                EID_ARGS(otherConnRoute.src_eid), -1);
            return -1;
        }

        return 0;
    }

    int GetDevRouteList(const umq_eid_t *srcEid, const umq_eid_t *dstEid, umq_route_list_t &filteredList)
    {
        if (RouteListRegistry::Instance().GetRouteList(*dstEid, filteredList)) {
            if (filteredList.route_num > 0) {
                return 0;
            }
        }

        Context *context = Context::GetContext();
        ub_trans_mode trans_mode = context->GetUbTransMode();
        umq_tp_type_t tp_type;
        if (trans_mode == RC_TP) {
            tp_type = UMQ_TP_TYPE_RTP;
        } else if (trans_mode == RM_TP) {
            tp_type = UMQ_TP_TYPE_RTP;
        } else if (trans_mode == RM_CTP) {
            tp_type = UMQ_TP_TYPE_CTP;
        } else if (trans_mode == RC_CTP) {
            tp_type = UMQ_TP_TYPE_CTP;
        } else {
            tp_type = UMQ_TP_TYPE_RTP;
        }

        umq_route_key_t route;
        (void)memcpy_s(&route.src_bonding_eid, sizeof(umq_eid_t), srcEid, sizeof(umq_eid_t));
        (void)memcpy_s(&route.dst_bonding_eid, sizeof(umq_eid_t), dstEid, sizeof(umq_eid_t));
        route.tp_type = tp_type;

        umq_route_list_t route_list;
        int ret = umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
        if (ret!=0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_get_route_list() failed, ret: %d\n", ret);
            return -1;
        }

        uint32_t filterNum = 0;
        for (uint32_t i = 0;i< route_list.route_num; ++i) {
            filteredList.routes[filterNum++] = route_list.routes[i];
        }
        filteredList.route_num = filterNum;

        if (filteredList.route_num == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get urma topo is zero\n");
            return -1;
        }

        RouteListRegistry::Instance().RegisterOrReplaceRouteList(*dstEid, filteredList);
        return 0;
    }

    // CLOS组网 通过亲和性选择 Client端调用
    int GetCpuAffinityUmqRoute(umq_route_list_t &route_list, std::vector<umq_route_t>& mainRoutes)
    {
        mainRoutes.clear();

        Context *context = Context::GetContext();
        int processSocketId = context->GetProcessSocketId();
        // 本端
        std::vector<uint32_t> socketIds = context->GetAllSocketId();
        // 对端
        std::vector<uint32_t> peerSocketIds = mPeerAllSocketIds;

        uint32_t processChipId = 0;
        uint32_t peerChipId = 0;

        // 本端
        std::set<uint32_t> processChipIds;
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            processChipIds.insert(route_list.routes[i].src_port.bs.chip_id);
        }
        std::vector<uint32_t> processChipIdList(processChipIds.begin(), processChipIds.end());
        processChipId = GetTargetChipId(socketIds, processChipIdList, processSocketId);

        // 对端
        std::set<uint32_t> peerChipIds;
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            peerChipIds.insert(route_list.routes[i].dst_port.bs.chip_id);
        }
        std::vector<uint32_t> peerChipIdList(peerChipIds.begin(), peerChipIds.end());
        peerChipId = GetTargetChipId(peerSocketIds, peerChipIdList, mPeerSocketId);

        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            if (route_list.routes[i].src_port.bs.chip_id == processChipId
                && route_list.routes[i].dst_port.bs.chip_id == peerChipId) {
                mainRoutes.push_back(route_list.routes[i]);
            }
        }

        if (!mainRoutes.empty()) {
            RPC_ADPT_VLOG_INFO("Find umq route successfully\n");
            return 0;
        }

        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find umq route\n");
        return -1;
    }

    int RRChooseMainRoute(std::vector<umq_route_t>& mainRoutes,
        const umq_eid_t *dstEid, umq_route_t& connMainRoute, umq_route_t& connBackRoute)
    {
        // 获取起始索引
        uint32_t startIndex = 0;
        GetBondingEidMapIndex(*dstEid, startIndex);
        
        // 确保索引在有效范围内
        startIndex = startIndex % mainRoutes.size();

        // 从起始索引开始轮询查找
        bool found = false;
        for (uint32_t offset = 0; offset < mainRoutes.size(); ++offset) {
            uint32_t currentIndex = (startIndex + offset) % mainRoutes.size();
            connMainRoute = mainRoutes[currentIndex];
            startIndex = (currentIndex + 1) % mainRoutes.size();  // 更新下次起始位置
            break;
        }
        // 从下一个起始索引开始轮询查找
        for (uint32_t offset = 0; offset < mainRoutes.size(); ++offset) {
            uint32_t currentIndex = (startIndex + offset) % mainRoutes.size();
            connBackRoute = mainRoutes[currentIndex];
            found = true;
            startIndex = (currentIndex + 1) % mainRoutes.size();  // 更新下次起始位置
            break;
        }

        // 更新下一个轮询位置
        EidRegistry::Instance().RegisterOrReplaceEidIndex(*dstEid, startIndex);

        if (!found) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find umq dev\n");
            return -1;
        }

        return 0;
    }

    int DoRoute(const umq_eid_t *srcEid, const umq_eid_t *dstEid, umq_route_t *connRoute, bool useRoundRobin)
    {
        umq_route_list_t filteredList = {};
        if (GetDevRouteList(srcEid, dstEid, filteredList) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get dev route list\n");
            return -1;
        }
        mTopoType = filteredList.topo_type;
        RPC_ADPT_VLOG_INFO("Topo type's value is: %d\n", mTopoType);
        if (filteredList.topo_type == UMQ_TOPO_TYPE_FULLMESH_1D) {
            if (GetConnEid(filteredList, dstEid, connRoute, useRoundRobin)!=0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get connect eid\n");
                return -1;
            }
            // fm不走bond，所以mUsedPorts.num保持为0
            mUsedPorts.num = 0;
        } else {
            std::vector<umq_route_t> mainRoutes;
            umq_route_t connMainRoute;
            umq_route_t connBackRoute;
            int getAffinityRes = GetCpuAffinityUmqRoute(filteredList, mainRoutes);
            if (getAffinityRes != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get cpu affinity umq route\n");
                return -1;
            }
            int getRet = RRChooseMainRoute(mainRoutes, dstEid, connMainRoute, connBackRoute);
            if (getRet != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to get main and back route\n");
                return -1;
            }
            *connRoute = connMainRoute;
            std::vector<umq_port_id_t> ports = {
                connMainRoute.src_port,
                connBackRoute.src_port,
            };
            std::copy(ports.begin(), ports.end(), mUsedPortArray.begin());
            mUsedPorts.num = static_cast<uint8_t>(ports.size());
            m_route_backup_src_eid = connBackRoute.src_eid;
        }

        return 0;
    }
    struct PeerInfo {
        std::string peer_ip;      // 对端IP地址
        umq_eid_t peer_eid;      // 对端EID
        int peer_fd;             // 对端socket fd
        int type_fd;             // 0 server; 1 client
    } m_peer_info;

    struct AsyncAcceptInfo {
        std::queue<std::tuple<int, struct sockaddr, socklen_t> > ready_queue;
        std::atomic<int32_t> asyncTaskNum{0U};
        u_external_mutex_t* lock = nullptr;
    } m_async_accept_info;
    //common fields
    uint64_t m_local_umqh = UMQ_INVALID_HANDLE;
    uint64_t m_main_umqh = UMQ_INVALID_HANDLE;
    uint16_t m_tx_window_capacity = 0; // the capacity of TX window size 
    uint16_t m_rx_window_capacity = 0; // the capacity of RX window size
    bool m_bind_remote = false; // indicate whether to enable stats manager when umq is created and bound successfully
    bool m_context_trace_enable = false;
    bool m_flow_control_failed = false;
    std::atomic<bool> m_closed{false};
    int m_event_fd;
    int mPeerSocketId = -1;
    std::vector<uint32_t> mPeerAllSocketIds;
    bool m_isblocking = true;
    umq_topo_type_t mTopoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    umq_eid_t m_route_backup_src_eid; // 备
    static std::array<umq_port_id_t, MAX_PORT_COUNT> mUsedPortArray;
    umq_used_ports_t mUsedPorts = {
        .port = mUsedPortArray.data(),
        .num = 0
    };
    QbufQueue<umq_buf_t *> *rxQueue = nullptr;
    ShareJfrRxEpollEvent *share_jfr_rx_epoll_event = nullptr;
    struct ConnInfo {
        umq_eid_t conn_eid;
        std::chrono::system_clock::time_point create_time;
    } m_conn_info;

    // TX fields
    struct alignas(CACHE_LINE_ALIGNMENT) TxDataPlane {
        uint64_t m_protocol_negotiation = 0;
        uint32_t m_protocol_negotiation_recv_size = 0;
        uint32_t m_protocol_negotiation_offset = 0;
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
        std::atomic<bool> m_need_fc_awake{false};
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
        umq_buf_list_t m_umq_buf_cache_head = {0};
        umq_buf_list_t m_umq_buf_cache_tail = {0};
        size_t m_remaining_size = 0;
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

    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                int maxevents, bool use_polling = false) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewTxEpollIn();
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event, maxevents, use_polling);
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
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "Origin epoll control add event fd failed, epfd: %d, fd: %d, errno: %d, errmsg: %s\n",
                epoll_fd, m_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control add event fd successful, epfd: %d, fd: %d\n", epoll_fd, m_fd);
        m_add_epoll_event = true;

        return 0;
    }

    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                int maxevents, bool use_polling = false) override
    {
        uint64_t cnt;
        if (eventfd_read(m_fd, &cnt) == -1) {
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "eventfd_read() failed for event fd, fd: %d, errno: %d, errmsg: %s\n",
                m_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        }
        // notify tx epoll out
        input_event->events |= EPOLLOUT;

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewTxEpollInEventFd();
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event, maxevents, use_polling);
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

    int GetAndAckEvent()
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Get socket fd object failed when ack sub umq event, socket fd: %d\n",
                m_origin_fd);
            return -1;
        }
        umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX};
        int events = umq_get_cq_event(socket_fd_obj->GetLocalUmqHandle(), &option);
        if (events == 0) {
            return 0;
        } else if (events < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_get_cq_event() failed for sub umq, socket fd: %d, ret: %d\n",
                m_origin_fd, events);
            return -1;
        }

        if ((m_event_num += events) >= GET_PER_ACK) {
            umq_ack_interrupt(socket_fd_obj->GetLocalUmqHandle(), m_event_num, &option);
            m_event_num = 0;
        }

        return 0;
    }

    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                int maxevents, bool use_polling = false) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_origin_fd);
        if (socket_fd_obj != nullptr) {
            socket_fd_obj->NewRxEpollIn();
        }

        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (socket_fd_obj == nullptr || !enable_share_jfr) {
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event, maxevents, use_polling);
        }

        if (GetAndAckEvent() < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Get and ack sub umq failed, socket fd:%d \n", m_origin_fd);
            return -1;
        }

        umq_buf_t *buf[POLL_BATCH_MAX];
        int poll_num = umq_poll(socket_fd_obj->GetLocalUmqHandle(), UMQ_IO_RX, buf, POLL_BATCH_MAX);
        if (poll_num <= 0) {
            if (socket_fd_obj->RearmRxInterrupt() < 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "Rearm sub umq failed, socket fd:%d, ret: %d\n",
                    m_origin_fd, poll_num);
                return -1;
            }
            return 0;
        }

        for (int i = 0; i < poll_num; ++i) {
            if (buf[i]->status != 0) {
                if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                    socket_fd_obj->HandleErrorRxCqe(buf[i]);
                } else {
                    socket_fd_obj->WakeUpTx();
                }
                QBUF_LIST_NEXT(buf[i]) = nullptr;
                umq_buf_free(buf[i]);
            }
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event, maxevents, use_polling);
    }

private:
    int m_origin_fd = -1;
    uint16_t m_event_num = 0;
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

    virtual int AddEpollEvent(int epoll_fd, bool use_polling = false) override
    {
        if (::EpollEvent::AddEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "::EpollEvent::AddEpollEvent() failed, epfd: %d, fd: %d, ret: %d\n",
                epoll_fd, m_fd, -1);
            return -1;
        }

        // try best to add umq tx/rx epoll event
        if ((m_event.events & EPOLLOUT) && tx_epoll_event != nullptr) {
            if (tx_epoll_event->AddEpollEvent(epoll_fd, use_polling) < 0) {
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
            if (rx_epoll_event->AddEpollEvent(epoll_fd, use_polling) < 0) {
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

    virtual int ModEpollEvent(int epoll_fd, struct epoll_event *event, bool use_polling = false) override
    {
        if (::EpollEvent::ModEpollEvent(epoll_fd, event, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "::EpollEvent::ModEpollEvent() failed, epfd: %d, fd: %d, ret: %d\n",
                epoll_fd, m_fd, -1);
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return 0;
        }

        // try best to modify umq tx/rx epoll event
        if (tx_epoll_event != nullptr) {
            if (!tx_epoll_event->IsAddEpollEvent() && (event->events & EPOLLOUT)) {
                if (tx_epoll_event->AddEpollEvent(epoll_fd, use_polling) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(add) TX interrupt fd(%d) for umq, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(add) TX interrupt fd(%d) for umq successful, fd: %d\n",
                        tx_epoll_event->GetFd(), m_fd);
                }
            } else if (tx_epoll_event->IsAddEpollEvent() && !(event->events & EPOLLOUT)) {
                if (tx_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
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
                if (rx_epoll_event->AddEpollEvent(epoll_fd, use_polling) < 0) {
                    RPC_ADPT_VLOG_WARN("Failed to modify(add) RX interrupt fd(%d) for umq, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                } else {
                    RPC_ADPT_VLOG_DEBUG("Modify(add) RX interrupt fd(%d) for umq successful, fd: %d\n",
                        rx_epoll_event->GetFd(), m_fd);
                }
            } else if (rx_epoll_event->IsAddEpollEvent() && !(event->events & EPOLLIN)) {
                if (rx_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
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

    virtual int DelEpollEvent(int epoll_fd, bool use_polling = false) override
    {
        if (::EpollEvent::DelEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "::EpollEvent::DelEpollEvent() failed, epfd: %d, fd: %d, ret: %d\n",
                epoll_fd, m_fd, -1);
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return 0;
        }

        // try best to delete umq tx/rx epoll event
        if (tx_epoll_event != nullptr &&
            tx_epoll_event->IsAddEpollEvent() && tx_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete TX interrupt fd(%d) for umq, fd: %d\n",
                tx_epoll_event->GetFd(), m_fd);
        }

        if (event_fd_epoll_event != nullptr &&
            event_fd_epoll_event->IsAddEpollEvent() &&
            event_fd_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete TX event fd(%d) for umq, fd: %d\n",
                               tx_epoll_event->GetFd(), m_fd);
        }

        if (rx_epoll_event != nullptr &&
            rx_epoll_event->IsAddEpollEvent() && rx_epoll_event->DelEpollEvent(epoll_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to delete RX interrupt fd(%d) for umq, fd: %d\n",
                rx_epoll_event->GetFd(), m_fd);
        }

        return 0;
    }

    virtual int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event, int max_events,
                                  bool use_polling = false) override
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (input_event->events & EPOLLOUT) {
            if (socket_fd_obj != nullptr && !socket_fd_obj->UseTcp()) {
                if (!socket_fd_obj->GetBindRemote() && socket_fd_obj->DoConnect() != 0) {
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
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event, max_events, use_polling);
        }

        if ((input_event->events & EPOLLIN) &&
            socket_fd_obj != nullptr && !socket_fd_obj->RxUseTcp() && socket_fd_obj->GetBindRemote()) {
            socket_fd_obj->NewOriginEpollIn(use_polling);
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event, max_events, use_polling);
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event, max_events, use_polling);
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
    
    virtual ~EpollFd()
    {
        for (std::pair<int, ShareJfrRxEpollEvent *> kv : m_share_jfr_epoll_event_map) {
            delete kv.second;
        }
    }

    virtual ALWAYS_INLINE int EpollCtlAdd(int fd, struct epoll_event *event, bool use_polling = false) override
    {
        if (event == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr) {
            return ::EpollFd::EpollCtlAdd(fd, event, use_polling);
        }

        ScopedUbExclusiveLocker sLock(m_mutex);
        if (m_epoll_event_map.count(fd) > 0) {
            RPC_ADPT_VLOG_WARN("Origin epoll control add duplicated, epfd: %d, fd: %d\n", m_fd, fd);
            EpollEvent *epoll_event = static_cast<Brpc::EpollEvent*>(m_epoll_event_map[fd]);
            if (epoll_event->DelEpollEvent(m_fd) != 0)
            {
                RPC_ADPT_VLOG_WARN("Deleting duplicated fd: %d from epfd: %d is unsuccessful\n", fd, m_fd);
            }
            delete epoll_event;
            m_epoll_event_map.erase(fd);
        }

        EpollEvent *epoll_event = nullptr;
        try {
            epoll_event = new EpollEvent(fd, event);
            CreateAndAddShareJfrEpollEvent(fd, event);
        } catch (std::exception& e) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
            return -1;
        }

        if (epoll_event->AddEpollEvent(m_fd, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "AddEpollEvent() failed in EpollCtlAdd, epfd: %d, fd: %d, ret: %d\n",
                m_fd, fd, -1);
            delete epoll_event;
            return -1;
        }

        m_epoll_event_map[fd] = epoll_event;

        return 0;
    }

    virtual ALWAYS_INLINE int EpollCtlMod(int fd, struct epoll_event *event, bool use_polling = false) override
    {
        if (::EpollFd::EpollCtlMod(fd, event, use_polling) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "::EpollFd::EpollCtlMod() failed, epfd: %d, fd: %d, ret: %d\n",
                m_fd, fd, -1);
            return -1;
        }

        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr) {
            return 0;
        }

        int share_jfr_fd = GetShareJfrFd(fd);
        if (share_jfr_fd == 0) {
            return 0;
        }

        if (share_jfr_fd < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Get share jfr fd failed, epfd: %d, socket fd: %d\n", m_fd, fd);
            return -1;
        }

        if (m_share_jfr_epoll_event_map.count(share_jfr_fd) == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Share jfr rx epoll event not exist, epfd: %d, socket fd: %d, share jfr fd:%d \n",
                m_fd, fd, share_jfr_fd);
            return -1;
        }

        auto share_jfr_rx_epoll_event = m_share_jfr_epoll_event_map[share_jfr_fd];
        if (share_jfr_rx_epoll_event != nullptr &&
            share_jfr_rx_epoll_event->ModEpollEvent(m_fd, event, use_polling) != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                    "ShareJfrRxEpollEvent::ModEpollEvent() failed, epfd: %d, socket fd: %d, share jfr fd: %d, "
                    "ret: %d\n",
                    m_fd, fd, share_jfr_fd, -1);
            return -1;
        }

        return 0;
    }

private:
    std::unordered_map<int, ShareJfrRxEpollEvent *> m_share_jfr_epoll_event_map;

    int GetShareJfrFd(int fd)
    {
        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr || socket_fd_obj->UseTcp()) {
            return 0;
        }

        auto main_umq = socket_fd_obj->GetMainUmqHandle();
        if (main_umq == UMQ_INVALID_HANDLE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Get main umq handle failed, socket fd: %d\n", fd);
            return -1;
        }

        umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX};
        int share_jfr_fd = umq_interrupt_fd_get(main_umq, &rx_option);
        if (share_jfr_fd < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                "umq_interrupt_fd_get() failed, socket fd: %d, ret: %d\n",
                fd, share_jfr_fd);
            return -1;
        }

        return share_jfr_fd;
    }

    void CreateAndAddShareJfrEpollEvent(int fd, struct epoll_event *event)
    {
        Brpc::Context *context = Brpc::Context::GetContext();
        bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
        if (!enable_share_jfr) {
            return;
        }

        SocketFd *socket_fd_obj = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr || !socket_fd_obj->GetBindRemote() || socket_fd_obj->UseTcp()) {
            return;
        }

        int share_jfr_fd = GetShareJfrFd(fd);
        if (share_jfr_fd < 0) {
            errno = EINVAL;
            throw std::runtime_error("Failed to get RX interrupt fd for main umq\n");
        }

        if (m_share_jfr_epoll_event_map.count(share_jfr_fd) > 0) {
            RPC_ADPT_VLOG_DEBUG("share jfr fd:%d add duplicated, epoll fd:%d \n", share_jfr_fd, m_fd);
            return;
        }

        if (!(event->events & EPOLLIN)) {
            return;
        }

        ShareJfrRxEpollEvent *share_jfr_rx_epoll_event = new ShareJfrRxEpollEvent(fd, event, share_jfr_fd, m_fd,
                                                                                  socket_fd_obj->GetMainUmqHandle());

        if (share_jfr_rx_epoll_event == nullptr) {
            return;
        }
        socket_fd_obj->SetShareJfrRxEpollEvent(share_jfr_rx_epoll_event);

        if (share_jfr_rx_epoll_event->AddEpollEvent(m_fd) < 0) {
            RPC_ADPT_VLOG_WARN("Failed to add share jfr RX interrupt fd(%d) for main umq, fd: %d\n",
                               share_jfr_rx_epoll_event->GetFd(), fd);
        } else {
            RPC_ADPT_VLOG_DEBUG("Add share jfr RX interrupt fd(%d) for main umq successful, fd: %d\n",
                                share_jfr_rx_epoll_event->GetFd(), fd);
        }

        if (socket_fd_obj->GetBindRemote() && !socket_fd_obj->RxUseTcp()) {
            if (socket_fd_obj->RearmShareJfrRxInterrupt() < 0) {
                RPC_ADPT_VLOG_WARN("Failed to rearm share jfr RX interrupt fd(%d) for main umq, fd: %d\n",
                                   share_jfr_rx_epoll_event->GetFd(), fd);
            } else {
                RPC_ADPT_VLOG_DEBUG("Rearm share jfr RX interrupt fd(%d) for main umq successful, fd: %d\n",
                                    share_jfr_rx_epoll_event->GetFd(), fd);
            }
        }

        m_share_jfr_epoll_event_map[share_jfr_fd] = share_jfr_rx_epoll_event;
    }
};
}

#endif
