/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-08-08
 *Note:
 *History: 2025-08-08
*/
#include <iostream>
#include <fstream>
#include <set>
#include <vector>
#include <string>
#include <sstream>
#include <dirent.h>
#include <cstdlib>

#include "urpc_util.h"
#include "scope_exit.h"
#include "brpc_file_descriptor.h"
#include "file_descriptor_async.h"
#include "brpc_context.h"

namespace Brpc {

const char* Context::CPU_LIST_PREFIX_PATH = "/sys/devices/system/node/";
const char* Context::CPU_LIST_SUFFIX_PATH = "/cpulist";
const char* Context::SOCKET_ID_PERFIX_PATH = "/sys/devices/system/cpu/";
const char* Context::SOCKET_ID_SUFFIX_PATH = "/topology/physical_package_id";

constexpr uint16_t CPU_STR_SIZE = 3;
constexpr uint16_t NODE_STR_SIZE = 4;

std::atomic<int> Context::m_ref = {0};
std::atomic<int> Context::m_shmNameSeq;
bool Context::m_ubEnable = false;

::SocketFd *Context::CreateSocketFd(int fd, int event_fd)
{
    ::SocketFd *socket_fd = nullptr;
    try {
        switch (m_socket_fd_trans_mode) {
            case SOCKET_FD_TRANS_MODE_UMQ:
            case SOCKET_FD_TRANS_MODE_UMQ_ZERO_COPY:
                socket_fd = (::SocketFd *)new Brpc::SocketFd(fd, event_fd);
                break;
#ifdef UBS_SHM_BUILD_ENABLED
            case SOCKET_FD_TRANS_MODE_SHM:
                socket_fd = (::SocketFd *)new Brpc::MemSocketFd(fd, event_fd);
                break;
#endif
            default:
                break;
        }
    } catch (std::exception& e) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
        return nullptr;
    }

    return socket_fd;
}

::SocketFd *Context::CreateSocketFd(int fd)
{
    return CreateSocketFd(fd, -1);
}

::EpollFd *Context::CreateEpollFd(int fd)
{
    ::EpollFd *epoll_fd = nullptr;
    try {
        switch (m_socket_fd_trans_mode) {
            case SOCKET_FD_TRANS_MODE_UMQ:
            case SOCKET_FD_TRANS_MODE_UMQ_ZERO_COPY:
                if (m_use_async_epoll_wait) {
                    epoll_fd = (::EpollFd *)new Brpc::async::EpollFdAsync(fd);
                } else {
                    epoll_fd = (::EpollFd *)new Brpc::EpollFd(fd);
                }
                break;
#ifdef UBS_SHM_BUILD_ENABLED
            case SOCKET_FD_TRANS_MODE_SHM:
                epoll_fd = (::EpollFd *)new Brpc::MemEpollFd(fd);
                break;
#endif
            default:
                break;
        }
    } catch (std::exception& e) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
        return nullptr;
    }

    return epoll_fd;
}

// 判断是否ub链接
bool Context::IsProtocolByUb(int fd)
{
    SocketFd *sockfd = (SocketFd *)Fd<::SocketFd>::GetFdObj(fd);
    if (sockfd == nullptr || sockfd->UseTcp()) {
        return false;
    }
    return true;
}

// 解析cpulist字符串
int Context::GetFirstCpuFromCpulist(const std::string &cpuListStr)
{
    if (cpuListStr.empty()) {
        // 表示无效输入
        RPC_ADPT_VLOG_WARN("GetFirstCpuFromCpulist failed, empty cpulist string\n");
        return -1;
    }

    std::stringstream ss(cpuListStr);
    std::string token;

    // 只取第一个逗号分隔的 token
    if (std::getline(ss, token, ',')) {
        size_t dash = token.find('-');
        if (dash != std::string::npos) {
            uint32_t dashStart = 0;
            try {
                dashStart = static_cast<uint32_t>(std::stoi(token.substr(0, dash)));
            } catch (const std::exception& e) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "No valid CPU detected.\n");
                dashStart = 0;
                return -1;
            }
            // 范围形式：如 "0-3"，返回开始的数字
            return dashStart;
        } else {
            // 单个 CPU：如 "5"，直接返回
            uint32_t tokenCPU = 0;
            try {
                tokenCPU = static_cast<uint32_t>(std::stoi(token));
            } catch (const std::exception& e) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "No valid CPU detected.\n");
                tokenCPU = 0;
                return -1;
            }
            return tokenCPU;
        }
    }

    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GetFirstCpuFromCpulist failed, no valid cpu token\n");
    return -1; // 没有找到有效 CPU
}

// 从 CPU ID 获取其 Socket ID（physical_package_id）
int Context::GetSocketIdOfCpu(int cpu)
{
    std::string path =
        std::string(SOCKET_ID_PERFIX_PATH) + "cpu" + std::to_string(cpu) + std::string(SOCKET_ID_SUFFIX_PATH);
    std::ifstream file(path);
    int socketId;
    if (file >> socketId) {
        return socketId;
    }
    RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GetSocketIdOfCpu failed, cpu: %d, path: %s\n", cpu, path.c_str());
    return -1; // 读取失败
}

std::vector<uint32_t> Context::GetSocketIdsViaNumaSysfs()
{
    // 尝试 NUMA 方式获取
    std::vector<uint32_t> numaResult = GetSocketIdsViaNuma();
    if (!numaResult.empty()) {
        return numaResult;
    }

    // NUMA 不可用，回退到 CPU 扫描
    RPC_ADPT_VLOG_WARN("NUMA not available. Direct use CPU topology.\n");
    return GetSocketIdsViaCpuScan();
}

// NUMA 方式获取 Socket IDs
std::vector<uint32_t> Context::GetSocketIdsViaNuma()
{
    std::set<int> socketIds;

    DIR* nodeDir = opendir(CPU_LIST_PREFIX_PATH);
    if (!nodeDir) {
        return {};  // NUMA 不可用
    }

    struct dirent* entry;
    while ((entry = readdir(nodeDir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, NODE_STR_SIZE) == "node" && name.size() > NODE_STR_SIZE) {
            char* end;
            long nodeId = std::strtol(name.substr(NODE_STR_SIZE).c_str(), &end, 10);
            if (*end == '\0' && nodeId >= 0) {
                // 读取该 NUMA 节点的 CPU 列表
                std::string cpuListPath = std::string(CPU_LIST_PREFIX_PATH) + name + std::string(CPU_LIST_SUFFIX_PATH);
                std::ifstream cpuListFile(cpuListPath);
                std::string cpuListStr;
                if (std::getline(cpuListFile, cpuListStr)) {
                    // 解析 CPU 列表，获得第一个 CPU ID
                    int cpu = GetFirstCpuFromCpulist(cpuListStr);
                    if (cpu != -1) {
                        // 根据 CPU ID 获取其 Socket ID
                        int socketId = GetSocketIdOfCpu(cpu);
                        if (socketId >= 0) {
                            socketIds.insert(socketId);
                        }
                    }
                }
            }
        }
    }

    closedir(nodeDir);
    return std::vector<uint32_t>(socketIds.begin(), socketIds.end());
}

// CPU 扫描方式获取 Socket IDs
std::vector<uint32_t> Context::GetSocketIdsViaCpuScan()
{
    std::set<int> socketIds;

    DIR* cpuDir = opendir(SOCKET_ID_PERFIX_PATH);
    if (!cpuDir) {
        return {};
    }

    struct dirent* entry;
    while ((entry = readdir(cpuDir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, CPU_STR_SIZE) == "cpu" && name.size() > CPU_STR_SIZE) {
            char* end;
            long cpuId = std::strtol(name.substr(CPU_STR_SIZE).c_str(), &end, 10);
            if (*end == '\0' && cpuId >= 0) {
                int socketId = GetSocketIdOfCpu(static_cast<int>(cpuId));
                if (socketId >= 0) {
                    socketIds.insert(socketId);
                }
            }
        }
    }

    closedir(cpuDir);
    return std::vector<uint32_t>(socketIds.begin(), socketIds.end());
}

int Context::GetCurrentProcessSocketId()
{
    // 获取当前进程主线程所在的 CPU
    int cpu = sched_getcpu();
    if (cpu < 0) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
            "sched_getcpu() failed, ret: %d, errno: %d, errmsg: %s\n",
            cpu, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        return -1;
    }
    return GetSocketIdOfCpu(cpu);
}

int Context::RegisterAsyncEvent(umq_trans_info_t info)
{
    int afd = umq_async_event_fd_get(&info);
    if (afd == UMQ_INVALID_FD) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "umq_async_event_fd_get() failed, ret: %d\n", afd);
        return -1;
    }

    {
        ScopedUbExclusiveLocker sLock(m_asyncEventRegistryMutex);

        auto iter = m_asyncEventRegistry.end();
        bool inserted = false;
        std::tie(iter, inserted) = m_asyncEventRegistry.insert(std::make_pair(afd, info));
        if (!inserted) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "The async fd %d has already been registered.\n", afd);
            return -1;
        }
    }

    struct epoll_event interest{};
    interest.events = EPOLLIN;
    interest.data.fd = afd;
    int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(m_asyncEventEpollFd, EPOLL_CTL_ADD, afd, &interest);
    if (ret < 0) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
            "epoll_ctl() failed, op: %d, epfd: %d, fd: %d, ret: %d, errno: %d, errmsg: %s\n",
            EPOLL_CTL_ADD, m_asyncEventEpollFd, afd, ret, errno,
            NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        // 回退
        ScopedUbExclusiveLocker sLock(m_asyncEventRegistryMutex);
        m_asyncEventRegistry.erase(afd);
        return -1;
    }
    return 0;
}

void Context::AsyncEventProcess()
{
    struct epoll_event ev[16];
    while (!m_asyncEventThreadStopFlag.load()) {
        int nevents = OsAPiMgr::GetOriginApi()->epoll_wait(m_asyncEventEpollFd, ev, 16, 10);
        if (nevents < 0) {
            if (errno == EINTR) {
                continue;
            }
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
                "epoll_wait on async event thread error, errno=%d, errmsg: %s\n",
                errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
            break;
        }

        // timed-out
        if (nevents == 0) {
            continue;
        }

        for (int i = 0; i < nevents; ++i) {
            const int afd = ev[i].data.fd;

            ScopedUbExclusiveLocker sLock(m_asyncEventRegistryMutex);
            auto iter = m_asyncEventRegistry.find(afd);
            if (iter == m_asyncEventRegistry.end()) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "The async fd %d has not been registered.\n", afd);
                continue;
            }

            umq_async_event_t av;
            int ret = umq_get_async_event(&iter->second, &av);
            if (ret == UMQ_SUCCESS) {
                AsyncEventHandle(&av);
                umq_ack_async_event(&av);
            } else {
                RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
                    "umq_get_async_event() failed, async fd: %d, ret: %d\n",
                    afd, ret);
            }
        }
    }
}

static void Disconnect(uint64_t umqh)
{
    umq_cfg_get_t cfg;
    int ret = umq_cfg_get(umqh, &cfg);
    if (ret != UMQ_SUCCESS) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API,
            "umq_cfg_get() failed in disconnect, umqh: %llu, ret: %d\n",
            static_cast<unsigned long long>(umqh), ret);
        return;
    }

    // 创建 UMQ 时会将 umq_ctx 设置为 brpc 原生的 socket fd.
    int fd = static_cast<int>(cfg.umq_ctx);

    ScopedUbWriteLocker lock(Fd<::SocketFd>::GetRWLock());
    if (auto *sockfd = Fd<::SocketFd>::GetFdObj(fd)) {
        auto *bfd = static_cast<Brpc::SocketFd *>(sockfd);
        bfd->Close();
    }
}

static void DisconnectAll()
{
    ScopedUbWriteLocker lock(Fd<::SocketFd>::GetRWLock());
    auto *fdmap = Fd<::SocketFd>::GetFdObjMap();
    for (int i = 0; i < RPC_ADPT_FD_MAX; ++i) {
        if (auto *bfd = static_cast<Brpc::SocketFd *>(fdmap[i])) {
            // 排除 tcp listener 等不会使用 umq 加速的 socket.
            if (bfd->GetLocalUmqHandle() != UMQ_INVALID_HANDLE) {
                bfd->Close();
            }
        }
    }
}

void Context::AsyncEventHandle(const umq_async_event_t *av)
{
    umq_cfg_get_t cfg;
    switch (av->event_type) {
        case UMQ_EVENT_QH_RQ_CQ_ERR:
        case UMQ_EVENT_QH_SQ_CQ_ERR:
            Disconnect(av->element.umqh);
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "jfc error: disconnect umqh=%llu\n", av->element.umqh);
            break;

        case UMQ_EVENT_QH_RQ_ERR:
            Disconnect(av->element.umqh);
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "jfr error: disconnect umqh=%llu\n", av->element.umqh);
            break;

        case UMQ_EVENT_QH_RQ_LIMIT:
            {
                int ret = umq_cfg_get(av->element.umqh, &cfg);
                if (ret != UMQ_SUCCESS) {
                    RPC_ADPT_VLOG_WARN("umq_cfg_get() failed on queue depth limit, umqh: %llu, ret: %d\n",
                                       static_cast<unsigned long long>(av->element.umqh), ret);
                } else {
                    RPC_ADPT_VLOG_WARN(
                        "jfr queue depth limit: umqh=%llu rx_buf_size=%u tx_buf_size=%u rx_depth=%u tx_depth=%u\n",
                        av->element.umqh, cfg.rx_buf_size, cfg.tx_buf_size, cfg.rx_depth, cfg.tx_depth);
                }
            }
            break;

        case UMQ_EVENT_QH_ERR:
            Disconnect(av->element.umqh);
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "jetty error: disconnect umqh=%llu\n", av->element.umqh);
            break;

        case UMQ_EVENT_QH_LIMIT:
            RPC_ADPT_VLOG_WARN("jetty limit: Not supported yet.\n");
            break;

        case UMQ_EVENT_PORT_ACTIVE:
            RPC_ADPT_VLOG_INFO("port active: port=%d\n", av->element.port_id);
            break;

        case UMQ_EVENT_PORT_DOWN:
            DisconnectAll();
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "port down: port=%d\n", av->element.port_id);
            break;

        case UMQ_EVENT_DEV_FATAL:
            RPC_ADPT_VLOG_WARN("dev fatal: Not supported yet.\n");
            break;

        case UMQ_EVENT_EID_CHANGE:
            RPC_ADPT_VLOG_WARN("eid change: Not supported yet.\n");
            break;

        case UMQ_EVENT_ELR_ERR:
            DisconnectAll();
            RPC_ADPT_VLOG_WARN("entity level error\n");
            break;

        case UMQ_EVENT_ELR_DONE:
            RPC_ADPT_VLOG_WARN("entity level done: Not supported yet.\n");
            break;

        case UMQ_EVENT_OTHER:
            switch (av->trans_info.dev_info.assign_mode) {
                case UMQ_DEV_ASSIGN_MODE_IPV4:
                    RPC_ADPT_VLOG_WARN("Unknown async event occurred: event-type=%d ip-addr=%s\n", av->original_code,
                                       av->trans_info.dev_info.ipv4.ip_addr);
                    break;

                case UMQ_DEV_ASSIGN_MODE_IPV6:
                    RPC_ADPT_VLOG_WARN("Unknown async event occurred: event-type=%d ip-addr=%s\n", av->original_code,
                                       av->trans_info.dev_info.ipv6.ip_addr);
                    break;

                case UMQ_DEV_ASSIGN_MODE_EID:
                    RPC_ADPT_VLOG_WARN("Unknown async event occurred: event-type=%d eid=" EID_FMT "\n",
                                       av->original_code, EID_ARGS(av->trans_info.dev_info.eid.eid));
                    break;

                default:
                    RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "unreachable!\n");
                    break;
            }
            break;
    }
}

int Context::GlobalLockInit() {
    g_socket_epoll_lock = g_rw_lock_ops.create();
    if (g_socket_epoll_lock == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GlobalLockInit create global socket epoll lock failed\n");
        return -1;
    }
    if (Fd<::SocketFd>::GlobalFdInit() != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GlobalLockInit SocketFd::GlobalFdInit() failed\n");
        return -1;
    }
    if (Fd<::EpollFd>::GlobalFdInit() != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GlobalLockInit EpollFd::GlobalFdInit() failed\n");
        return -1;
    }
    if (Fd<Brpc::EpollFd>::GlobalFdInit() != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "GlobalLockInit Brpc::EpollFd::GlobalFdInit() failed\n");
        return -1;
    }
    return 0;
}

}  // namespace Brpc

