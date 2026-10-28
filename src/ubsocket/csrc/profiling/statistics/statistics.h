/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the statistics utility for ubsocket, etc
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
#include <cstring>
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
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "statistics_statsmgr.h"
#include "under_api/dl_libc_api.h"
#ifdef URMA_DLOPEN_BACKEND_ENABLED
#include "core/urma/urma_socket.h"
#endif

using namespace ock::ubs;
using ock::ubs::ArraySet;
using ock::ubs::ArraySet;
using ock::ubs::LibcApi;
using ock::ubs::ReadLocker;
using ock::ubs::SocketConnHelper;
using ock::ubs::SocketType;
#ifdef URMA_DLOPEN_BACKEND_ENABLED
using ock::ubs::urma::UrmaSocket;
#endif

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
        int ret = snprintf(name, sizeof(name), "ubscli-%u", (uint32_t)getpid());
        if (ret < 0 || ret >= (int)sizeof(name)) {
            throw std::runtime_error(std::string("Failed to copy unix domain socket name, error ") +
                                     std::to_string(ret));
        }

        //Set the first character to an empty character.
        addr.sun_path[0] = '\0';
        // Copy the name to the ramaining part
        strncpy(addr.sun_path + 1, name, UDS_SUN_PATH_NAME_MAX);

        m_uds_fd = LibcApi::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_uds_fd < 0) {
            throw std::runtime_error(std::string("Failed to create unix domain socket, ") +
                                     ock::ubs::Func::Error2Str(errno));
        }

        if (LibcApi::bind(m_uds_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            (void)LibcApi::close(m_uds_fd);
            throw std::runtime_error(std::string("Failed to bind unix domain socket, ") +
                                     ock::ubs::Func::Error2Str(errno));
        }

        if (LibcApi::listen(m_uds_fd, 1) < 0) {
            (void)LibcApi::close(m_uds_fd);
            throw std::runtime_error(std::string("Failed to listen unix domain socket, ") +
                                     ock::ubs::Func::Error2Str(errno));
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
            UBS_VLOG_ERR("Failed to add epoll control event, %s\n", ock::ubs::Func::Error2Str(errno));
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

    uint32_t GetSockNum(bool skipListen = false)
    {
        uint32_t sockNum = 0;
        ArraySet<Socket>::GetInstance().ForEach([&](int fd, Socket *sock) {
            if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
                (skipListen && sock->create_type_ == SOCK_CREATE_TYPE_LISTEN)) {
                return;
            }
            sockNum++;
        });
        return sockNum;
    }

    void GetAllSocketData(CLISocketData *data, uint32_t sockNum)
    {
        if (data == nullptr) {
            return;
        }
        uint32_t doneNum = 0;
        ArraySet<Socket>::GetInstance().ForEach([&](int fd, Socket *sock) {
            if (doneNum >= sockNum) {
                return;
            }
            if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
                sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
                return;
            }
            data->socketId = fd;
            static_cast<SocketBase *>(sock)->GetStatsMgr()->GetSocketCLIData(data);
            data += 1;
            doneNum += 1;
        });
    }

    void Poll(void)
    {
        struct epoll_event events[MAX_EPOLL_EVENT_NUM];
        // Do not set a timeout to reduce the core usage of the listening thread.
        int ev_num = LibcApi::epoll_wait(m_epoll_fd, events, MAX_EPOLL_EVENT_NUM, -1);
        if (ev_num == -1) {
            UBS_VLOG_ERR("epoll_wait() failed in statistics poll, epfd: %d, maxevents: %d, timeout: %d, error: %d, "
                         "errmsg: %s\n",
                         m_epoll_fd, MAX_EPOLL_EVENT_NUM, -1, errno, ock::ubs::Func::Error2Str(errno));
            return;
        }

        ProcessEpollEvents(events, ev_num);
    }

    // 处理 Epoll 事件。当返回 false 时表示 polling 线程正在退出。通常这意味着
    // ubsocket_uninit() 已经被调用.
    virtual bool ProcessEpollEvents(const struct epoll_event *events, const int evNum)
    {
        for (int i = 0; i < evNum; i++) {
            if (events[i].data.fd == m_wakeup_fd) {
                /* The current epoll event reported from the wakeup fd only indicates that
                 * the program needs to exit as soon as possible, so it directly returns. */
                AckWakeupEpoll();
                return false;
            }
            if (events[i].data.fd == m_uds_fd) {
                Process(events[i].events);
            }
        }
        return true;
    }

    void DealDelayOperation(CLIDelayHeader &delayHeader, std::string &outTracePointStr, CLIControlHeader header)
    {
        int ret = UBS_OK;
        switch (header.mType) {
            case CLITypeParam::TRACE_OP_QUERY:
                ret = Profiling::Combine(outTracePointStr);
                delayHeader.tracePointDataSize = outTracePointStr.size();
                if (ret != UBS_OK) {
                    UBS_VLOG_WARN("Cli request profiling combine is not completed.");
                }
                break;
            case CLITypeParam::TRACE_OP_RESET:
                Profiling::Reset();
                break;
            case CLITypeParam::TRACE_OP_ENABLE_TRACE:
                ret = Profiling::Init(ProfilingTPId::UBSOCKET_PROF_COUNT, GlobalSetting::UBS_PROF_DUMP_PATH.c_str(),
                                      GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN);
                if (ret != UBS_OK) {
                    UBS_VLOG_WARN("Profiling is not initialize.");
                }
                GlobalSetting::UBS_PROF_ENABLE = true;
                break;
            case CLITypeParam::INVALID:
            default:
                delayHeader.retCode = -1;
                UBS_VLOG_WARN("OpType error for trace operation.");
                break;
        }
    }

    void ProcessDelayRequest(int fd, CLIMessage &msg, CLIControlHeader header)
    {
        CLIDelayHeader delayHeader{};
        std::string outTracePointStr;
        DealDelayOperation(delayHeader, outTracePointStr, header);
        uint32_t headerSize = sizeof(CLIDelayHeader);
        uint32_t traceDataSize = outTracePointStr.size();
        uint32_t totalSize = headerSize + traceDataSize;
        // malloc mem base on socket cnt
        if (!msg.AllocateIfNeed(totalSize)) {
            UBS_VLOG_ERR("Failed to alloc reponsese memory.");
            return;
        }
        msg.ResetBuf();
        // collect data
        memcpy(msg.Data(), &delayHeader, headerSize);

        if (delayHeader.tracePointDataSize > 0) {
            uint8_t *data = reinterpret_cast<uint8_t *>(msg.Data()) + sizeof(CLIDelayHeader);
            memcpy(data, outTracePointStr.c_str(), traceDataSize);
        }
        header.Reset();
        header.mDataSize = totalSize;
        if (SocketConnHelper::SendSocketData(fd, &header, sizeof(CLIControlHeader), LISTENER_SEND_RECV_TIMEOUT_MS) !=
            sizeof(CLIControlHeader)) {
            UBS_VLOG_ERR("Failed to send CLIControlHeader.");
            return;
        }

        if (SocketConnHelper::SendSocketData(fd, msg.Data(), totalSize, LISTENER_SEND_RECV_TIMEOUT_MS) != totalSize) {
            UBS_VLOG_ERR("Failed to send CLISocketData.");
            return;
        }
    }

    void ProcessStatRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
    {
        // collect socket count
        uint32_t headerSize = sizeof(CLIDataHeader);
        uint32_t sockNum = GetSockNum(true);
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
        CLIheader.poolTotalNum = 0;
        CLIheader.poolAvailableNum = 0;
        CLIheader.poolInUseNum = 0;
        // collect data
        memcpy(msg.Data(), &CLIheader, headerSize);

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

    void Process(uint32_t events)
    {
        CLIMessage msg{};
        if ((events & ((uint32_t)EPOLLERR | EPOLLHUP)) != 0) {
            return;
        }

        int fd = LibcApi::accept(m_uds_fd, NULL, NULL);
        if (fd < 0) {
            UBS_VLOG_ERR("Failed to accept connection, %s\n", ock::ubs::Func::Error2Str(errno));
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
        } else if (header.mCmdId == CLICommand::DELAY) {
            ProcessDelayRequest(fd, msg, header);
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
            ArraySet<Socket>::GetInstance().ForEach([&](int fd, Socket *sock) {
                if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP) {
                    return;
                }
                static_cast<SocketBase *>(sock)->GetStatsMgr()->OutputStats(fd, m_oss);
            });
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
    static ALWAYS_INLINE GlobalStatsMgr *GetGlobalStatsMgr(const UbsTransMode trans_mode)
    {
        static GlobalStatsMgr mgr(trans_mode);
        return &mgr;
    }

    static void GlobalStatsMgrEventLoop(const UbsTransMode trans_mode)
    {
        GlobalStatsMgr *mgr = GetGlobalStatsMgr(trans_mode);
        while (m_running) {
            mgr->Poll();
        }
    }

    bool ProcessEpollEvents(const epoll_event *events, const int evNum) override
    {
        // get urma perf info and update
        return Listener::ProcessEpollEvents(events, evNum) && (StatsMgr::UpdateReTxCount(m_trans_mode), true);
    }

private:
    explicit GlobalStatsMgr(UbsTransMode trans_mode)
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
    UbsTransMode m_trans_mode;
};

}; // namespace Statistics

#endif
