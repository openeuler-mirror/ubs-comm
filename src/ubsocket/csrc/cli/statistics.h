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
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include "cli_message.h"
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_socket_helper.h"
#include "core/umq/umq_socket.h"
#include "net_common.h"
#include "probe_manager.h"
#include "statistics_statsmgr.h"
#include "umq_dfx_api.h"
#include "umq_dfx_types.h"
#include "umq_errno.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using ock::ubs::LibcApi;
using ock::ubs::ReadLocker;
using ock::ubs::SocketConnHelper;
using ock::ubs::SocketSet;
using ock::ubs::SocketType;
using ock::ubs::UmqApi;
using ock::ubs::umq::UmqSocket;

namespace Statistics {

class Listener {
public:
    struct __attribute__((packed)) CtrlHead {
        uint16_t m_module_id;
        uint16_t m_cmd_id;
        uint32_t m_error_code;
        uint32_t m_data_size;
    };

    enum RpcAdptCmdType {
        UBS_CMD_STATS,
        UBS_CMD_MAX
    };

#ifdef UBSOCKET_UNIT_TEST
    const static uint32_t LISTENER_SEND_RECV_TIMEOUT_MS = 100;
#else
    const static uint32_t LISTENER_SEND_RECV_TIMEOUT_MS = 8000;
#endif
    const static uint32_t MAX_EPOLL_EVENT_NUM = 32;
    const static uint32_t MAX_EPOLL_FD_NUM = 16;
    const static uint32_t CACHE_BUFFER_LEN = 8192;
    const static uint32_t UDS_SUN_PATH_NAME_MAX = 32;
#ifdef UBSOCKET_UNIT_TEST
    const static uint32_t UDS_SEND_RECV_TIMEOUT_S = 1;
#else
    const static uint32_t UDS_SEND_RECV_TIMEOUT_S = 8;
#endif

    class fd_guard {
    public:
        fd_guard() : mfd(-1) {}
        explicit fd_guard(int fd) : mfd(fd) {}

        ~fd_guard()
        {
            if (mfd >= 0) {
                LibcApi::close(mfd);
                mfd = -1;
            }
        }

        fd_guard(const fd_guard &) = delete;
        void operator=(const fd_guard &) = delete;

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
        if (ret < 0 || ret >= (int)sizeof(name)) {
            throw std::runtime_error(std::string("Failed to copy unix domain socket name, error ") +
                                     std::to_string(ret));
        }

        //Set the first character to an empty character.
        addr.sun_path[0] = '\0';
        // Copy the name to the ramaining part
        ret = strncpy_s(addr.sun_path + 1, sizeof(addr.sun_path) - 1, name, UDS_SUN_PATH_NAME_MAX);
        if (ret != EOK) {
            throw std::runtime_error(std::string("Failed to construct unix domain socket name, error ") +
                                     std::to_string(ret));
        }

        m_uds_fd = LibcApi::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_uds_fd < 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to create unix domain socket, ") +
                                     NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }

        if (LibcApi::bind(m_uds_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            (void)LibcApi::close(m_uds_fd);
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to bind unix domain socket, ") +
                                     NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }

        if (LibcApi::listen(m_uds_fd, 1) < 0) {
            (void)LibcApi::close(m_uds_fd);
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            throw std::runtime_error(std::string("Failed to listen unix domain socket, ") +
                                     NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    virtual ~Listener()
    {
        if (m_internal_epoll_enable) {
            if (m_wakeup_fd >= 0) {
                (void)LibcApi::close(m_wakeup_fd);
            }

            if (m_epoll_fd >= 0) {
                (void)LibcApi::close(m_epoll_fd);
            }
        }

        if (m_uds_fd >= 0) {
            (void)LibcApi::close(m_uds_fd);
        }
    }

    int InternalEpollEnable()
    {
        m_epoll_fd = LibcApi::epoll_create(MAX_EPOLL_FD_NUM);
        if (m_epoll_fd < 0) {
            UBS_VLOG_ERR("Failed to create epoll file descriptor\n");
            return -1;
        }

        m_wakeup_fd = eventfd(0, EFD_NONBLOCK);
        if (m_wakeup_fd == -1) {
            UBS_VLOG_ERR("Failed to create wakeup event file descriptor\n");
            goto CLEAN_EPOLL;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = m_wakeup_fd;
        if (LibcApi::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_wakeup_fd, &ev) == -1) {
            UBS_VLOG_ERR("Failed to add epoll event for wakeup event file descriptor\n");
            goto CLEAN_ALL_RESOURCE;
        }

        ev.data.fd = m_uds_fd;
        if (LibcApi::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_uds_fd, &ev) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("Failed to add epoll control event, %s\n",
                         NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            goto CLEAN_ALL_RESOURCE;
        }

        m_internal_epoll_enable = true;

        return 0;

    CLEAN_ALL_RESOURCE:
        LibcApi::close(m_wakeup_fd);
        m_wakeup_fd = -1;

    CLEAN_EPOLL:
        LibcApi::close(m_epoll_fd);
        m_epoll_fd = -1;

        return -1;
    }

    uint32_t GetSockNum()
    {
        uint32_t sockNum = 0;
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            sockNum++;
        }
        return sockNum;
    }

    void GetAllSocketData(CLISocketData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketCLIData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void GetAllProbeData(std::vector<CLIProbeData> &outDataVec)
    {
        Statistics::ProbeManager::GetInstance().GetCLIProbeData(outDataVec);
    }

    void GetAllFlowControlData(CLIFlowControlData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketFlowControlData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void GetAllQbufPoolData(CLIQbufPoolData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketQbufPoolData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void GetAllUmqInfoData(CLIUmqInfoData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketUmqInfoData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void GetAllIoPacketData(CLIIoPacketData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketIoPacketData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void GetAllUmqPerfData(CLIUmqPerfData *data, uint32_t sockNum)
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        uint32_t doneNum = 0;
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX && doneNum < sockNum; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            data->socketId = i;
            ((UmqSocket *)socketMap[i].Get())->GetSocketUmqPerfData(data);
            data += 1;
            doneNum += 1;
        }
    }

    void Poll(void)
    {
        struct epoll_event events[MAX_EPOLL_EVENT_NUM];
        // Do not set a timeout to reduce the core usage of the listening thread.
        int ev_num = LibcApi::epoll_wait(m_epoll_fd, events, MAX_EPOLL_EVENT_NUM, -1);
        if (ev_num == -1) {
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("epoll_wait() failed in statistics poll, epfd: %d, maxevents: %d, timeout: %d, "
                         "errmsg: %s\n",
                         m_epoll_fd, MAX_EPOLL_EVENT_NUM, -1, errno,
                         NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            return;
        }

        ProcessEpollEvents(events, ev_num);
    }

    virtual void ProcessEpollEvents(const struct epoll_event *events, const int evNum)
    {
        for (int i = 0; i < evNum; i++) {
            if (events[i].data.fd == m_wakeup_fd) {
                /* The current epoll event reported from the wakeup fd only indicates that
                 * the program needs to exit as soon as possible, so it directly returns. */
                AckWakeupEpoll();
                return;
            }
            if (events[i].data.fd == m_uds_fd) {
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
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        CLIheader.reTxCount = StatsMgr::GetReTxCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllSocketData(reinterpret_cast<CLISocketData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLISocketData\n");
            return;
        }
    }

    void ProcessFlowControlRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLIFlowControlData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        CLIheader.reTxCount = StatsMgr::GetReTxCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllFlowControlData(reinterpret_cast<CLIFlowControlData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLIFlowControlData\n");
            return;
        }
    }

    void ProcessProbeRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // 1. 获取数据
        std::vector<CLIProbeData> probeDataList;
        GetAllProbeData(probeDataList);

        uint32_t sockNum = static_cast<uint32_t>(probeDataList.size());

        // 2. 计算大小并分配内存
        uint32_t headerSize = sizeof(CLIProbeHeader);
        uint32_t sockDataSize = sockNum * sizeof(CLIProbeData);
        uint32_t totalSize = headerSize + sockDataSize;

        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();

        // 3. 填充 Header
        CLIProbeHeader cliHeader{};
        cliHeader.socketNum = sockNum;

        // 拷贝 Header
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &cliHeader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }

        // 4. 填充 Data 利用 vector 的连续内存特性，可以直接内存拷贝
        uint8_t *dataPtr = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIProbeHeader);

        if (sockNum > 0) {
            // 一次性把整个数组拷贝过去，效率最高
            if (memcpy_s(dataPtr, sockDataSize, probeDataList.data(), sockDataSize) != 0) {
                UBS_VLOG_ERR("Failed to memcpy probe data\n");
                return;
            }
        }

        // 5. 发送数据
        header.Reset();
        header.mDataSize = totalSize;

        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send payload data\n");
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
        int ret = UmqApi::umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
        if (ret != 0) {
            UBS_VLOG_ERR("Failed to get urma topo\n");
            return;
        }
        umq_route_list_t filteredList{};
        uint32_t filterNum = 0;
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            filteredList.routes[filterNum++] = route_list.routes[i];
        }

        filteredList.route_num = filterNum;
        if (filteredList.route_num == 0) {
            UBS_VLOG_ERR("Failed to get urma topo is zero\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, &filteredList, sizeof(umq_route_list_t),
                                             LISTENER_SEND_RECV_TIMEOUT_MS) != sizeof(umq_route_list_t)) {
            UBS_VLOG_ERR("Failed to send umq route list\n");
        }

        return;
    }

    void ProcessQbufPoolRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLIQbufPoolData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllQbufPoolData(reinterpret_cast<CLIQbufPoolData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLIQbufPoolData\n");
            return;
        }
    }

    void ProcessUmqInfoRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLIUmqInfoData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllUmqInfoData(reinterpret_cast<CLIUmqInfoData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLIUmqInfoData\n");
            return;
        }
    }

    void ProcessIoRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLIIoPacketData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllIoPacketData(reinterpret_cast<CLIIoPacketData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLIIoPacketData\n");
            return;
        }
    }

    void ProcessUmqRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum();
        uint32_t sockDataSize = sockNum * sizeof(CLIUmqPerfData);
        uint32_t totalSize = headerSize + sockDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc response memory\n");
            return;
        }
        msg.ResetBuf();
        CLIDataHeader CLIheader{};
        CLIheader.socketNum = sockNum;
        CLIheader.connNum = StatsMgr::GetConnCount();
        CLIheader.activeConn = StatsMgr::GetActiveConnCount();
        // collect data
        if (memcpy_s(msg.Data(), msg.GetBufLen(), &CLIheader, headerSize) != 0) {
            UBS_VLOG_ERR("Failed to memcpy cli header\n");
            return;
        }
        uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDataHeader);
        GetAllUmqPerfData(reinterpret_cast<CLIUmqPerfData *>(data), sockNum);
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLIUmqPerfData\n");
            return;
        }
    }

    uint64_t GetFirstUmqHandle()
    {
        ReadLocker lock(SocketSet::Instance().GetRWLock());
        SocketPtr *socketMap = SocketSet::Instance().GetSocketObj();
        for (uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i) {
            if (socketMap[i].Get() == nullptr || socketMap[i].Get()->Type() == SocketType::SOCK_TYPE_TCP) {
                continue;
            }
            // Get main umq handle (from brpc_file_descriptor)
            uint64_t umqh = GetMainUmqHandleFromSocket(socketMap[i]);
            if (umqh != UMQ_INVALID_HANDLE) {
                return umqh;
            }
        }
        return UMQ_INVALID_HANDLE;
    }

    uint64_t GetMainUmqHandleFromSocket(SocketPtr socket)
    {
        // For now, return UMQ_INVALID_HANDLE to avoid compilation issues
        // We'll need to properly include Brpc::SocketFd header in the future
        return UMQ_INVALID_HANDLE;
    }

    void Process(uint32_t events)
    {
        CLIMessage msg{};
        if ((events & ((uint32_t)EPOLLERR | EPOLLHUP)) != 0) {
            return;
        }

        int fd = LibcApi::accept(m_uds_fd, NULL, NULL);
        if (fd < 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("Failed to accept connection, %s\n",
                         NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return;
        }
        fd_guard tmpFd(fd);
        struct timeval tv = {};
        tv.tv_sec = UDS_SEND_RECV_TIMEOUT_S;
        if (LibcApi::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
            UBS_VLOG_ERR("Failed to set socket send timeout option\n");
            // fd_guard 会自动 close
            return;
        }

        if (LibcApi::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
            UBS_VLOG_ERR("Failed to set socket recv timeout option\n");
            // fd_guard 会自动 close
            return;
        }

        CLIControlHeader header{};
        if (SocketConnHelper::RecvSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to recv CLIControlHeader\n");
            return;
        }

        if (header.mCmdId == CLICommand::STAT) {
            ProcessStatRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::TOPO) {
            ProcessTopoRequest(fd, header);
        } else if (header.mCmdId == CLICommand::FLOW_CONTROL) {
            ProcessFlowControlRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::QBUF_POOL) {
            ProcessQbufPoolRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::UMQ_INFO) {
            ProcessUmqInfoRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::IO) {
            ProcessIoRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::UMQ) {
            ProcessUmqRequest(fd, msg, header);
        } else if (header.mCmdId == CLICommand::PROBE) {
            ProcessProbeRequest(fd, msg, header);
        }
        return;
    }

    void WakeupEpoll()
    {
        uint64_t value = 1;
        ssize_t n = LibcApi::write(m_wakeup_fd, &value, sizeof(value));
        if (n != sizeof(value)) {
            UBS_VLOG_ERR("Failed to wakeup listen thread\n");
        }
    }

    void AckWakeupEpoll()
    {
        uint64_t value;
        ssize_t n = LibcApi::read(m_wakeup_fd, &value, sizeof(value));
        if (n != sizeof(value)) {
            UBS_VLOG_ERR("Failed to acknowledge wakeup listen thread\n");
        }
    }

    int GetFd()
    {
        return m_uds_fd;
    }

protected:
    int RecvCmd(int fd, CtrlHead &ipc_ctl)
    {
        if (SocketConnHelper::RecvSocketData(fd, &ipc_ctl, sizeof(CtrlHead), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CtrlHead)) {
            UBS_VLOG_ERR("Failed to recv CLIControlHeader\n");
            return -1;
        }

        if (ipc_ctl.m_data_size > CACHE_BUFFER_LEN) {
            // Currently, using 8KB of memory is more than sufficient.
            UBS_VLOG_ERR("Received data size %u exceeds cache buffer length %u\n", ipc_ctl.m_data_size,
                         CACHE_BUFFER_LEN);
            return -1;
        }

        if (ipc_ctl.m_data_size == 0) {
            return 0;
        }

        if (SocketConnHelper::RecvSocketData(fd, m_cache_buffer, ipc_ctl.m_data_size, LISTENER_SEND_RECV_TIMEOUT_MS) !=
            ipc_ctl.m_data_size) {
            UBS_VLOG_ERR("Failed to recv cache data\n");
            return -1;
        }

        return 0;
    }

    int SendCmd(int fd, CtrlHead &ipc_ctl, const char *in_data)
    {
        if (SocketConnHelper::SendSocketData(fd, &ipc_ctl, sizeof(CtrlHead), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CtrlHead)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader\n");
            return -1;
        }

        if (ipc_ctl.m_data_size == 0) {
            return 0;
        }

        if (SocketConnHelper::SendSocketData(fd, in_data, ipc_ctl.m_data_size, LISTENER_SEND_RECV_TIMEOUT_MS) !=
            ipc_ctl.m_data_size) {
            UBS_VLOG_ERR("Failed to send cache data\n");
            return -1;
        }

        return 0;
    }

    void ProcessStats()
    {
        Statistics::Recorder::GetTitle(m_oss);
        {
            ReadLocker lock(SocketSet::Instance().GetRWLock());
            SocketPtr *socket_fd_obj_map = SocketSet::Instance().GetSocketObj();
            for (uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i) {
                UmqSocket *sock = (UmqSocket *)socket_fd_obj_map[i].Get();
                if (sock == nullptr) {
                    continue;
                }
                sock->OutputStats(m_oss);
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
    static ALWAYS_INLINE GlobalStatsMgr *GetGlobalStatsMgr(const umq_trans_mode_t trans_mode)
    {
        static GlobalStatsMgr mgr(trans_mode);
        return &mgr;
    }

    static void GlobalStatsMgrEventLoop(const umq_trans_mode_t trans_mode)
    {
        GlobalStatsMgr *mgr = GetGlobalStatsMgr(trans_mode);
        while (m_running) {
            mgr->Poll();
        }
    }

    void ProcessEpollEvents(const epoll_event *events, const int evNum) override
    {
        // get urma perf info and update
        StatsMgr::UpdateReTxCount(m_trans_mode);
        Listener::ProcessEpollEvents(events, evNum);
    }

private:
    explicit GlobalStatsMgr(umq_trans_mode_t trans_mode)
    {
        m_trans_mode = trans_mode;
        if (InternalEpollEnable() != 0) {
            throw std::runtime_error("Failed to enable internal epoll logic");
        }

        try {
            m_event_loop = new std::thread(GlobalStatsMgrEventLoop, trans_mode);
        } catch (const std::exception &e) {
            UBS_VLOG_ERR("%s\n", e.what());
            throw std::runtime_error("Failed to launch internal thread for statistics");
        }
    }

    ~GlobalStatsMgr()
    {
        m_running = false;
        if (m_event_loop != nullptr) {
            WakeupEpoll();
            m_event_loop->join();
            delete m_event_loop;
        }
    }

    std::thread *m_event_loop = nullptr;
    static volatile bool m_running;
    umq_trans_mode_t m_trans_mode;
};

}; // namespace Statistics

#endif
