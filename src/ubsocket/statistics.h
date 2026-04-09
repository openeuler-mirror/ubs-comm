/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-09-20
 *Note:
 *History: 2025-09-20
*/
#ifndef STATISTICS_H
#define STATISTICS_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <ctime>
#include <cmath>
#include <thread>
#include <limits>
#include <sstream>
#include <iostream>
#include <mutex>
#include <string>
#include <iomanip>
#include "file_descriptor.h"
#include "configure_settings.h"
#include "cli_message.h"
#include "utracer.h"
#include "utracer_manager.h"
#include "net_common.h"

#define RPC_VAR         (2)

namespace Statistics {

class Recorder {
    public:
    const static uint32_t NAME_WIDTH_MAX = 30;
    const static uint32_t FIELD_WIDTH_MAX = 21;
    const static uint32_t FD_WIDTH_MAX = 10;
    const static uint32_t DEFAULT_PRECISION = 3;
    const static uint32_t TOTAL_PRECISION = 7;

    Recorder(const char *name)
    {
        if(name == nullptr){
            throw std::runtime_error("Input invalid name");
        }

        m_name = name;
        if(m_name.length() > NAME_WIDTH_MAX){
            throw std::runtime_error("Input name length(" + std::to_string(m_name.length()) +
                ") exceeds upper limit(" + std::to_string(NAME_WIDTH_MAX) + ")");
        }
    }

    ALWAYS_INLINE void Update(uint32_t input)
    {
        /* Here use Welford algorithm to calculate mean and variance
         * (1) mean update equation: M(new) = M(old) + (x(new) - M(old)) / n
         * (2) variance update equation: M2(new) = M2(old) + (x(new) - M(old)) * (x(new) - M(new))
         * (3) sample variance: s^2 = M2 / (n - 1)
         * M: mean of inputsamples
         * x: new input
         * n: number of total input
         * M2: Intermediate quantilties used for calculating sample variance,
         * the sum of squares of differences from the current mean */
        m_cnt += input;
    }

    uint64_t GetCnt()
    {
        return m_cnt;
    }

    double GetMean()
    {
        return m_mean;
    }

    double GetVar()
    {
        return (m_cnt < RPC_VAR) ? 0 : m_m2 / (m_cnt -1);
    }

    double GetStd()
    {
        return std::sqrt(GetVar());
    }

    double GetCV()
    {
        /* CV < 1: Indicates it has relatively low dispersion. The volatility is below the average level.
         * CV = 1: Indicates it has dispersion comparable to the average level.
         * CV > 1: Indicates it has relatively high dispersion. The volatility is above the average level.
         * CV >1.5 or higher: Typically suggests it has very high volatility, possibly containing extreme
         * values or multipe distinct groups. */
         return (m_cnt == 0 || IsZero(m_mean)) ? 0 : GetStd() / m_mean;
    }

    void Reset()
    {
        m_cnt = 0;
        m_mean = 0.0;
        m_m2 = 0.0;
        m_max = 0;
        m_min = UINT32_MAX;
    }

    void GetInfo(int fd, std::ostringstream &oss)
    {
        if(m_min == UINT32_MAX && m_max == 0){
            /* When both the maximum and minimum values remain unchanged, it is considered that no statistical
             * information for this variable has been recorded, and a '-' is directly output. */
            oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd)
                << std::setw(NAME_WIDTH_MAX) << m_name
                << std::setw(FIELD_WIDTH_MAX) << "-"
                << std::endl;
            return;    
        }

        oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd)
            << std::setw(NAME_WIDTH_MAX) << m_name
            << std::setw(FIELD_WIDTH_MAX) << m_cnt
            << std::endl;
    }

    static void GetTitle(std::ostringstream &oss)
    {
        oss << std::left << std::setw(FD_WIDTH_MAX) << "fd"
            << std::setw(NAME_WIDTH_MAX) << "type"
            << std::setw(FIELD_WIDTH_MAX) << "total"
            << std::endl;
    }

    static void FillEmptyForm(std::ostringstream &oss)
    {
        static std::once_flag once_flag;
        std::call_once(once_flag, [](){
            std::ostringstream title_oss;
            GetTitle(title_oss);
            m_title_len = title_oss.str().length();
        });

        /* Here, the use if length rather than content comparison is to enhance the efficiency of the comparsion,
         * with the caller ensuring that the content does not deviate from expectations. */
        if (oss.str().length() != m_title_len){
            return;
        }
        
        oss << std::left << std::setw(FD_WIDTH_MAX) << "-"
            << std::setw(NAME_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-"
            << std::endl;
    }

    private:
    bool IsZero(double a)
    {
        return std::fabs(a) < std::numeric_limits<double>::epsilon();
    }

    uint64_t m_cnt = 0;
    double m_mean = 0;
    double m_m2 = 0;
    uint32_t m_max = 0;
    uint32_t m_min = UINT32_MAX;
    std::string m_name;
    static uint32_t m_title_len;
};

class StatsMgr {
public:
    enum trace_stats_type {
        CONN_COUNT,
        ACTIVE_OPEN_COUNT,
        RX_PACKET_COUNT,
        TX_PACKET_COUNT,
        RX_BYTE_COUNT,
        TX_BYTE_COUNT,
        TX_ERROR_PACKET_COUNT,
        TX_LOST_PACKET_COUNT,
        
        TRACE_STATE_TYPE_MAX
    };

    StatsMgr(int fd) : m_output_fd(fd) {}

    bool InitStatsMgr()
    {
        for (int i = 0; i < TRACE_STATE_TYPE_MAX; ++i) {
            try {
                m_recorder_vec.emplace_back(GetStatsStr((enum trace_stats_type)i));
            } catch (std::exception& e) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to construct statistics manager, %s\n", e.what());
                return false;
            }
        }

        m_stats_enable = true;

        return true;
    }

    inline static std::atomic<uint64_t> mConnCount{0};
    inline static std::atomic<uint64_t> mActiveConnCount{0};
    inline static std::atomic<uint64_t> mRxPacketCount{0};
    inline static std::atomic<uint64_t> mTxPacketCount{0};
    inline static std::atomic<uint64_t> mRxByteCount{0};
    inline static std::atomic<uint64_t> mTxByteCount{0};
    inline static std::atomic<uint64_t> mTxErrorPacketCount{0};
    inline static std::atomic<uint64_t> mTxLostPacketCount{0};

    static uint64_t GetConnCount()
    {
        return mConnCount.load(std::memory_order_relaxed);
    }

    static uint64_t GetActiveConnCount()
    {
        return mActiveConnCount.load(std::memory_order_relaxed);
    }

    static ALWAYS_INLINE void OutputAllStats(std::ostringstream &oss, uint32_t pid) {
        constexpr int timeBufSize = 32;
        time_t now = time(nullptr);
        char timeBuf[timeBufSize];
        struct tm timeInfo;
        if (localtime_r(&now, &timeInfo) != nullptr) {
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
        } else {
            timeBuf[0] = '\0';
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create timeStamp.\n");
        }

        oss << "{"
            << "\"timeStamp\":\"" << timeBuf << "\","
            << "\"pid\":\"" << pid << "\","
            << "\"trafficRecords\":{";

        oss << "\"" << "totalConnections" << "\":" << mConnCount.load() << ",";
        oss << "\"" << "activeConnections" << "\":" << mActiveConnCount.load() << ",";
        oss << "\"" << "sendPackets" << "\":" << mRxPacketCount.load() << ",";
        oss << "\"" << "receivePackets" << "\":" << mTxPacketCount.load() << ",";
        oss << "\"" << "sendBytes" << "\":" << mRxByteCount.load() << ",";
        oss << "\"" << "receiveBytes" << "\":" << mTxByteCount.load() << ",";
        oss << "\"" << "errorPackets" << "\":" << mTxErrorPacketCount.load() << ",";
        oss << "\"" << "lostPackets" << "\":" << mTxLostPacketCount.load() << "";

        oss << "}" << "}";
    }

    // data plane interface, caller ensure input validation
    ALWAYS_INLINE void UpdateTraceStats(enum trace_stats_type type, uint32_t value)
    {
        switch (type) {
            case CONN_COUNT:
                mConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case ACTIVE_OPEN_COUNT:
                mActiveConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case RX_PACKET_COUNT:
                mRxPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_PACKET_COUNT:
                mTxPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case RX_BYTE_COUNT:
                mRxByteCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_BYTE_COUNT:
                mTxByteCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_ERROR_PACKET_COUNT:
                mTxErrorPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_LOST_PACKET_COUNT:
                mTxLostPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            default:
                break;
        }
    }

    static ALWAYS_INLINE void SubMConnCount()
    {
        if (mConnCount.load() >= 1) {
            mConnCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    static ALWAYS_INLINE void SubMActiveConnCount()
    {
        if (mActiveConnCount.load() >= 1) {
            mActiveConnCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

protected:
    const char *GetStatsStr(enum trace_stats_type type)
    {
        const static char *state_type_str[TRACE_STATE_TYPE_MAX] = {
            "totalConnections",
            "activeConnections",
            "sendPackets",
            "receivePackets",
            "sendBytes",
            "receiveBytes",
            "errorPackets",
            "lostPackets",
        };

        return state_type_str[type];
    }
    
    void OutputStats(std::ostringstream &oss)
    {
        if (!m_stats_enable) {
            return;
        }

        for (int i = 0; i < TRACE_STATE_TYPE_MAX; ++i) {
            m_recorder_vec[i].GetInfo(m_output_fd, oss);
        }
    }

    void GetSocketCLIData(Statistics::CLISocketData *data)
    {
        if (!m_stats_enable || data == nullptr) {
            return;
        }
        data->sendPackets = m_recorder_vec[TX_PACKET_COUNT].GetCnt();
        data->recvPackets = m_recorder_vec[RX_PACKET_COUNT].GetCnt();
        data->sendBytes = m_recorder_vec[TX_BYTE_COUNT].GetCnt();
        data->recvBytes = m_recorder_vec[RX_BYTE_COUNT].GetCnt();
        data->errorPackets = m_recorder_vec[TX_ERROR_PACKET_COUNT].GetCnt();
        data->lostPackets = m_recorder_vec[TX_LOST_PACKET_COUNT].GetCnt();
    }

    std::vector<Statistics::Recorder> m_recorder_vec;
    int m_output_fd = -1;
    bool m_stats_enable = false;
};

class Listener {
    public:
    struct __attribute__((packed)) CtrlHead {
        uint16_t m_module_id;
        uint16_t m_cmd_id;
        uint32_t m_error_code;
        uint32_t m_data_size;
    };

    enum RpcAdptCmdType {
        RPC_ADPT_CMD_STATS,
        RPC_ADPT_CMD_MAX
    };

    const static uint32_t LISTENER_SEND_RECV_TIMEOUT_MS = 8000;
    const static uint32_t MAX_EPOLL_EVENT_NUM = 32;
    const static uint32_t MAX_EPOLL_FD_NUM = 16;
    const static uint32_t CACHE_BUFFER_LEN = 8192;
    const static uint32_t UDS_SUN_PATH_NAME_MAX = 32;
    const static uint32_t UDS_SEND_RECV_TIMEOUT_S = 8;

    class fd_guard {
    public:
        fd_guard() : mfd(-1) {}
        explicit fd_guard(int fd) : mfd(fd) {}

        ~fd_guard()
        {
            if (mfd >= 0) {
                OsAPiMgr::GetOriginApi()->close(mfd);
                mfd = -1;
            }
        }

        fd_guard(const fd_guard&) = delete;
        void operator=(const fd_guard&) = delete;
    private:
        int mfd;
    };

    Listener()
    {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        // Create an abstract namespace socket name, which is also convenient to calculate string length
        char name[UDS_SUN_PATH_NAME_MAX] = {0};
        int ret = snprintf_s(name, sizeof(name), sizeof(name) - 1, "ubscli-%u", (uint32_t)getpid());
        if(ret<0 || ret >=(int)sizeof(name)){
            throw std::runtime_error(
                std::string("Failed to copy unix domain socket name, error ") + std::to_string(ret));
        }

        //Set the first character to an empty character.
        addr.sun_path[0] = '\0';
        // Copy the name to the ramaining part
        ret = strncpy_s(addr.sun_path + 1, sizeof(addr.sun_path) - 1, name, UDS_SUN_PATH_NAME_MAX);
        if (ret != EOK) {
            throw std::runtime_error(
                std::string("Failed to construct unix domain socket name, error ") + std::to_string(ret));
        }

        m_uds_fd = OsAPiMgr::GetOriginApi()->socket(AF_UNIX, SOCK_STREAM, 0);
        if(m_uds_fd < 0){
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to create unix domain socket, ") +
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }

        if (OsAPiMgr::GetOriginApi()->bind(m_uds_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            (void)OsAPiMgr::GetOriginApi()->close(m_uds_fd);
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to bind unix domain socket, ") +
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }

        if (OsAPiMgr::GetOriginApi()->listen(m_uds_fd, 1) < 0) {
            (void)OsAPiMgr::GetOriginApi()->close(m_uds_fd);
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to listen unix domain socket, ") +
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    virtual ~Listener()
    {
        if (m_internal_epoll_enable){
            if (m_wakeup_fd >= 0){
                (void)OsAPiMgr::GetOriginApi()->close(m_wakeup_fd);
            }

            if (m_epoll_fd >= 0){
                (void)OsAPiMgr::GetOriginApi()->close(m_epoll_fd);
            }
        }

        if (m_uds_fd >= 0){
            (void)OsAPiMgr::GetOriginApi()->close(m_uds_fd);
        }
    }

    int InternalEpollEnable()
    {
        m_epoll_fd = OsAPiMgr::GetOriginApi()->epoll_create(MAX_EPOLL_FD_NUM);
        if(m_epoll_fd < 0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create epoll file descriptor\n");
            return -1;
        }

        m_wakeup_fd = eventfd(0, EFD_NONBLOCK);
        if(m_wakeup_fd == -1){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create wakeup event file descriptor\n");
            goto CLEAN_EPOLL;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = m_wakeup_fd;
        if(OsAPiMgr::GetOriginApi()->epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_wakeup_fd, &ev) == -1){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to add epoll event for wakeup event file descriptor\n");
            goto CLEAN_ALL_RESOURCE;
        }

        ev.data.fd = m_uds_fd;
        if (OsAPiMgr::GetOriginApi()->epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_uds_fd, &ev) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to add epoll control event, %s\n",
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            goto CLEAN_ALL_RESOURCE;
        }

        m_internal_epoll_enable = true;

        return 0;

    CLEAN_ALL_RESOURCE:
        OsAPiMgr::GetOriginApi()->close(m_wakeup_fd);
        m_wakeup_fd = -1;

    CLEAN_EPOLL:
        OsAPiMgr::GetOriginApi()->close(m_epoll_fd);
        m_epoll_fd = -1; 
        
        return -1;
    }

    uint32_t GetSockNum()
    {
        uint32_t sockNum = 0;
        ScopedUbReadLocker lock(Fd<SocketFd>::GetRWLock());
        SocketFd **socketMap = Fd<SocketFd>::GetFdObjMap();
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i) {
            if (socketMap[i] == nullptr) {
                continue;
            }
            sockNum++;
        }
        return sockNum;
    }

    void GetAllSocketData(CLISocketData *data, uint32_t sockNum)
    {
        ScopedUbReadLocker lock(Fd<SocketFd>::GetRWLock());
        SocketFd **socketMap = Fd<SocketFd>::GetFdObjMap();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i] == nullptr) {
                continue;
            }
            data->socketId = i;
            socketMap[i]->GetSocketCLIData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void Poll(void)
    {
        struct epoll_event events[MAX_EPOLL_EVENT_NUM];
        // Do not set a timeout to reduce the core usage of the listening thread.
        int ev_num = OsAPiMgr::GetOriginApi()->epoll_wait(m_epoll_fd, events, MAX_EPOLL_EVENT_NUM, -1);
        if (ev_num == -1){
            return;
        }

        for (int i = 0; i < ev_num; i++){
            if (events[i].data.fd == m_wakeup_fd){
                /* The current epoll event reported from the wakeup fd only indicates that
                 * the program needs to exit as soon as possible, so it directly returns. */
                 AckWakeupEpoll();
                 return;
            }else if(events[i].data.fd == m_uds_fd) {
                Process(events[i].events);
            }
        }
    }

    void ProcessStatRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLISocketData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            RPC_ADPT_VLOG_INFO("Failed to alloc reponsese memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            RPC_ADPT_VLOG_INFO("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllSocketData(reinterpret_cast<CLISocketData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketFd::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            RPC_ADPT_VLOG_INFO("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketFd::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            RPC_ADPT_VLOG_INFO("Failed to send CLIControlHeader\n");
            return;
        }
    }

    void ProcessTopoRequest(int fd, CLIControlHeader &header)
    {
        umq_route_key_t route{};
        route.src_bonding_eid = header.srcEid;
        route.dst_bonding_eid = header.dstEid;
        route.tp_type = UMQ_TP_TYPE_RTP;

        umq_route_list_t route_list;
        int ret = umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
        if (ret != 0) {
            RPC_ADPT_VLOG_INFO("Failed to get urma topo\n");
            return;
        }
        umq_route_list_t filteredList{};
        uint32_t filterNum = 0;
        for (uint32_t i = 0;i< route_list.route_num; ++i) {
            filteredList.routes[filterNum++] = route_list.routes[i];
        }

        filteredList.route_num = filterNum;
        if (filteredList.route_num == 0) {
            RPC_ADPT_VLOG_INFO("Failed to get urma topo is zero\n");
            return;
        }

        if (SocketFd::SendSocketData(fd, &filteredList, sizeof(umq_route_list_t), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(umq_route_list_t)) {
            RPC_ADPT_VLOG_INFO("Failed to send umq route list\n");
        }

        return;
    }

    void DealDelayOperation(CLIDelayHeader &delayHeader,
                            std::vector<TranTraceInfo> &tranTraceInfos, CLIControlHeader header)
    {
        switch (header.mType) {
            case CLITypeParam::TRACE_OP_QUERY:
                tranTraceInfos = GetTraceInfos(
                    INVALID_TRACE_POINT_ID, header.mValue, UTracerManager::IsLatencyQuantileEnable());
                delayHeader.tracePointNum = tranTraceInfos.size();
                break;
            case CLITypeParam::TRACE_OP_RESET:
                ResetTraceInfos();
                break;
            case CLITypeParam::TRACE_OP_ENABLE_TRACE:
                UTracerManager::SetEnable(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
                UTracerManager::SetLatencyQuantileEnable(
                    header.GetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE));
                UTracerManager::SetEnableLog(header.GetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE), "");
                break;
            case CLITypeParam::INVALID:
            default:
                delayHeader.retCode = -1;
                RPC_ADPT_VLOG_WARN("OpType error for trace operation.");
                break;
        }
    }

    void ProcessDelayRequest(int fd, CLIMessage &msg, CLIControlHeader header)
    {
        CLIDelayHeader delayHeader{};
        std::vector<TranTraceInfo> tranTraceInfos{};
        DealDelayOperation(delayHeader, tranTraceInfos, header);
        uint32_t headerSize = sizeof(CLIDelayHeader);
        uint32_t traceDataSize = delayHeader.tracePointNum * sizeof(TranTraceInfo);
        uint32_t totalSize = headerSize + traceDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            RPC_ADPT_VLOG_WARN("Failed to alloc reponsese memory\n");
            return;
        }
        msg.ResetBuf();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &delayHeader, headerSize) != 0) {
            RPC_ADPT_VLOG_WARN("Failed to memcpy cli header\n");
            return;
        }
        if (delayHeader.tracePointNum > 0) {
            uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDelayHeader);
            if (memcpy_s(data, traceDataSize, tranTraceInfos.data(), traceDataSize) != 0) {
                RPC_ADPT_VLOG_WARN("Failed to memcpy trace data\n");
                return;
            }
        }
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketFd::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            RPC_ADPT_VLOG_WARN("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketFd::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            RPC_ADPT_VLOG_WARN("Failed to send CLIControlHeader\n");
            return;
        }
    }

    void Process(uint32_t events)
    {
        CLIMessage msg{};
        if ((events & ((uint32_t)EPOLLERR | EPOLLHUP)) != 0){
            return;
        }

        int fd = OsAPiMgr::GetOriginApi()->accept(m_uds_fd, NULL, NULL);
        if(fd<0){
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to accept connection, %s\n",
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return;
        }
        fd_guard tmpFd(fd);
        struct timeval tv = {0};
        tv.tv_sec = UDS_SEND_RECV_TIMEOUT_S;
        if(OsAPiMgr::GetOriginApi()->setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to set socket send timeout option\n");
            // fd_guard 会自动 close
            return;
        }

        if(OsAPiMgr::GetOriginApi()->setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to set socket recv timeout option\n");
            // fd_guard 会自动 close
            return;
        }

        CLIControlHeader header{};
        if (SocketFd::RecvSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            RPC_ADPT_VLOG_INFO("Failed to recv CLIControlHeader\n");
            return;
        }

        if (header.mCmdId == CLICommand::STAT) {
            ProcessStatRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::TOPO) {
            ProcessTopoRequest(fd, header);
        } else if (header.mCmdId == CLICommand::DELAY) {
            ProcessDelayRequest(fd, msg, header);
        }
        return;
    }

    void WakeupEpoll()
    {
        uint64_t value = 1;
        ssize_t n = OsAPiMgr::GetOriginApi()->write(m_wakeup_fd, &value, sizeof(value));
        if(n!=sizeof(value)){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to wakeup listen thread\n");
        }
    }

    void AckWakeupEpoll()
    {
        uint64_t value;
        ssize_t n = OsAPiMgr::GetOriginApi()->read(m_wakeup_fd, &value, sizeof(value));
        if(n!=sizeof(value)){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to acknowledge wakeup listen thread\n");
        }
    }

    int GetFd()
    {
        return m_uds_fd;
    }

    protected:
    int RecvCmd(int fd, CtrlHead &ipc_ctl)
    {
        if(SocketFd::RecvSocketData(
            fd, &ipc_ctl, sizeof(CtrlHead), LISTENER_SEND_RECV_TIMEOUT_MS) != sizeof(CtrlHead)){
            return -1;
        }

        if(ipc_ctl.m_data_size > CACHE_BUFFER_LEN){
            // Currently, using 8KB of memory is more than sufficient.
            return -1;
        }

        if(ipc_ctl.m_data_size == 0){
            return 0;
        }

        if(SocketFd::RecvSocketData(
            fd, m_cache_buffer, ipc_ctl.m_data_size, LISTENER_SEND_RECV_TIMEOUT_MS) != ipc_ctl.m_data_size){
            return -1;
        }

        return 0;
    }

    int SendCmd(int fd, CtrlHead &ipc_ctl, const char *in_data)
    {
        if(SocketFd::SendSocketData(
            fd, &ipc_ctl, sizeof(CtrlHead), LISTENER_SEND_RECV_TIMEOUT_MS) != sizeof(CtrlHead)){
            return -1;
        }

        if(ipc_ctl.m_data_size == 0){
            return 0;
        }

        if(SocketFd::SendSocketData(
            fd, in_data, ipc_ctl.m_data_size, LISTENER_SEND_RECV_TIMEOUT_MS) != ipc_ctl.m_data_size){
            return -1;
        }

        return 0;
    }

    void ProcessStats()
    {
        Statistics::Recorder::GetTitle(m_oss);
        {
            ScopedUbReadLocker lock(Fd<SocketFd>::GetRWLock());
            SocketFd **socket_fd_obj_map = Fd<SocketFd>::GetFdObjMap();
            for (uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i){
                if(socket_fd_obj_map[i] == nullptr){
                    continue;
                }

                socket_fd_obj_map[i]->OutputStats(m_oss);
            } 
        }
        Statistics::Recorder::FillEmptyForm(m_oss);
    }

    bool m_internal_epoll_enable = false;
    int m_uds_fd = -1;
    int m_epoll_fd = -1;
    int m_wakeup_fd = -1;
    std::ostringstream m_oss;
    uint8_t m_cache_buffer[CACHE_BUFFER_LEN]{0};
};

/* The reason for using a singleton implementation independently rather than inheriting it from the context is to 
 * avoid creating and occupying unnecessary memory when the statistic-related functionality is not enabled. */
 class GlobalStatsMgr final : public Listener {
    public:
    static ALWAYS_INLINE GlobalStatsMgr *GetGlobalStatsMgr()
    {
        static GlobalStatsMgr mgr;
        return &mgr;
    }

    static void GlobalStatsMgrEventLoop()
    {
        GlobalStatsMgr *mgr = GetGlobalStatsMgr();
        while(m_running){
            mgr->Poll();
        }
    }

    private:
    GlobalStatsMgr()
    {
        if (InternalEpollEnable() != 0){
            throw std::runtime_error("Failed to enable internal epoll logic");
        }

        try {
            m_event_loop = new std::thread(GlobalStatsMgrEventLoop);
        } catch (const std::exception& e) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
            throw std::runtime_error("Failed to launch internal thread for statistics");
        }
    }

    ~GlobalStatsMgr()
    {
        m_running = false;
        if(m_event_loop != nullptr){
            WakeupEpoll();
            m_event_loop->join();
            delete m_event_loop;
        }
    }

    std::thread *m_event_loop = nullptr;
    static volatile bool m_running;
 };

};

#endif