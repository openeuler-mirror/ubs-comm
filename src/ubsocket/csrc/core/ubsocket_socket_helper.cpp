/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
* ubs-comm is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*      http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/
#include "ubsocket_socket_helper.h"

namespace ock {
namespace ubs {
bool SocketConnHelper::IsUbsConnection(const int &fd)
{
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::TFO) {
        tcp_info info{};
        socklen_t len = sizeof(info);
        bool is_tfo_connection = false;
        if (LibcApi::getsockopt(fd, SOL_TCP, TCP_INFO, &info, &len) == 0) {
            // check TCPI_OPT_SYN_DATA
            is_tfo_connection = (info.tcpi_options & TCPI_OPT_SYN_DATA) != 0;
        }
        UBS_VLOG_INFO("Current tcpi_options: 0x%x, tfo connection: %s \n", info.tcpi_options,
                      is_tfo_connection ? "true" : "false");
        return is_tfo_connection;
    }
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::UB_SOCK_OPT) {
        int opt = -1;
        socklen_t len = sizeof(opt);
        if (LibcApi::getsockopt(fd, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, &len) == 0) {
            UBS_VLOG_INFO("UB handshake socket option is %s. \n", opt == 1 ? "enabled" : "disabled");
            return opt == 1;
        }
        return false;
    }
    UBS_VLOG_WARN("Unsupported handshake mode: 0x%x\n", static_cast<unsigned int>(GlobalSetting::UBS_HAND_SHAKE_MODE));
    return false;
}

std::string SocketConnHelper::ExtractIpFromSockAddr(const struct sockaddr *address)
{
    if (address == nullptr) {
        return "";
    }
    char ip_str[INET6_ADDRSTRLEN] = {0};
    const char *result = nullptr;
    if (address->sa_family == AF_INET) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        result = inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN);
    } else if (address->sa_family == AF_INET6) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        result = inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
    }
    return (result != nullptr) ? std::string(ip_str) : "";
}

uint16_t SocketConnHelper::ExtractPortFromSockAddr(const struct sockaddr *address)
{
    uint16_t port = 0;
    if (address == nullptr) {
        return port;
    }
    if (address->sa_family == AF_INET) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        port = ntohs(addr_in->sin_port);
    } else if (address->sa_family == AF_INET6) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        port = ntohs(addr_in6->sin6_port);
    }
    return port;
}

ssize_t SocketConnHelper::SendSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t sent = 0;
    size_t total = size;
    auto start = std::chrono::high_resolution_clock::now();
    while (total != 0) {
        if (IsTimeout(start, timeout_ms)) {
            errno = ETIMEDOUT;
            return sent;
        }
        sent = LibcApi::send(fd, cur, total, MSG_NOSIGNAL);
        if (errno == EAGAIN) {
            // reset errno to 0
            errno = 0;
            if (sent > 0) {
                total -= sent;
                cur += sent;
            }
            continue;
        }
        if (sent <= 0 || errno != 0) {
            UBS_VLOG_ERR("send() failed, ret: %zd, errno: %d, errmsg: %s, sent: %zd\n", sent, errno,
                         Func::Error2Str(errno), sent);
            return sent;
        } else {
            UBS_VLOG_DEBUG("Send socket message successful, fd: %d, sent = %zd, total: %zu\n", fd, sent, size);
        }
        total -= sent;
        cur += sent;
    }
    return size;
}

ssize_t SocketConnHelper::RecvSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    // reset errno to 0
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t received = 0;
    size_t total = size;
    auto start = std::chrono::high_resolution_clock::now();
    while (total != 0) {
        if (IsTimeout(start, timeout_ms)) {
            errno = ETIMEDOUT;
            return received;
        }
        received = LibcApi::recv(fd, cur, total, MSG_NOSIGNAL);
        if (errno == EAGAIN) {
            // reset errno to 0
            errno = 0;
            if (received > 0) {
                total -= received;
                cur += received;
            }

            continue;
        }
        if (received == 0) {
            UBS_VLOG_INFO("The connection has been closed by peer.\n");
            return 0;
        } else if (received < 0) {
            UBS_VLOG_ERR("recv() failed, ret: %zd, errno: %d, errmsg: %s, received: %zd, fd: %d\n", received, errno,
                         Func::Error2Str(errno), received, fd);
            return received;
        } else {
            UBS_VLOG_DEBUG("Receive socket message successful, fd: %d, received: %zd, total: %zu\n", fd, received,
                           size);
        }
        total -= received;
        cur += received;
    }
    return size;
}

void SocketConnHelper::FlushSocketMsg(int fd)
{
    // reset errno to 0
    errno = 0;
    char tmp_buf[FLUSH_SOCKET_MSG_BUFFER_LEN];
    ssize_t received = 0;
    do {
        received = LibcApi::recv(fd, tmp_buf, FLUSH_SOCKET_MSG_BUFFER_LEN, MSG_NOSIGNAL);
        if (errno == EAGAIN || errno == EINTR) {
            // reset errno to 0
            errno = 0;
            continue;
        }

        if (received < 0 || errno != 0) {
            return;
        }
    } while (received > 0);
}

int SocketConnHelper::GetCurrentProcessSocketId()
{
    // 获取当前进程主线程所在的 CPU
    int cpu = sched_getcpu();
    if (cpu < 0) {
        UBS_VLOG_ERR("sched_getcpu() failed, ret: %d, errno: %d, errmsg: %s\n", cpu, errno, Func::Error2Str(errno));
        return -1;
    }
    return SocketConnHelper::GetSocketIdOfCpu(cpu);
}

// 从 CPU ID 获取其 Socket ID（physical_package_id）
int SocketConnHelper::GetSocketIdOfCpu(int cpu)
{
    std::string path =
        std::string(SOCKET_ID_PERFIX_PATH) + "cpu" + std::to_string(cpu) + std::string(SOCKET_ID_SUFFIX_PATH);
    std::ifstream file(path);
    int socketId;
    if (file >> socketId) {
        return socketId;
    }
    UBS_VLOG_ERR("GetSocketIdOfCpu failed, cpu: %d, path: %s\n", cpu, path.c_str());
    return -1; // 读取失败
}

std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaNumaSysfs()
{
    // 尝试 NUMA 方式获取
    std::vector<uint32_t> numaResult = SocketConnHelper::GetSocketIdsViaNuma();
    if (!numaResult.empty()) {
        return numaResult;
    }

    // NUMA 不可用，回退到 CPU 扫描
    UBS_VLOG_WARN("NUMA not available. Direct use CPU topology.\n");
    return SocketConnHelper::GetSocketIdsViaCpuScan();
}

std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaNuma()
{
    std::set<int> socketIds;

    DIR *nodeDir = opendir(CPU_LIST_PREFIX_PATH);
    if (!nodeDir) {
        return {}; // NUMA 不可用
    }

    struct dirent *entry;
    while ((entry = readdir(nodeDir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, NODE_STR_SIZE) == "node" && name.size() > NODE_STR_SIZE) {
            char *end;
            std::string nodeIdStr = name.substr(NODE_STR_SIZE);
            long nodeId = std::strtol(nodeIdStr.c_str(), &end, 10);
            if (*end == '\0' && nodeId >= 0) {
                // 读取该 NUMA 节点的 CPU 列表
                std::string cpuListPath = std::string(CPU_LIST_PREFIX_PATH) + name + std::string(CPU_LIST_SUFFIX_PATH);
                std::ifstream cpuListFile(cpuListPath);
                std::string cpuListStr;
                if (std::getline(cpuListFile, cpuListStr)) {
                    // 解析 CPU 列表，获得第一个 CPU ID
                    int cpu = SocketConnHelper::GetFirstCpuFromCpulist(cpuListStr);
                    if (cpu != -1) {
                        // 根据 CPU ID 获取其 Socket ID
                        int socketId = SocketConnHelper::GetSocketIdOfCpu(cpu);
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
std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaCpuScan()
{
    std::set<int> socketIds;

    DIR *cpu_dir = opendir(SOCKET_ID_PERFIX_PATH);
    if (!cpu_dir) {
        return {};
    }

    struct dirent *entry;
    while ((entry = readdir(cpu_dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, CPU_STR_SIZE) == "cpu" && name.size() > CPU_STR_SIZE) {
            char *end;
            std::string cpuIdStr = name.substr(CPU_STR_SIZE);
            long cpuId = std::strtol(cpuIdStr.c_str(), &end, 10);
            if (*end == '\0' && cpuId >= 0) {
                int socketId = SocketConnHelper::GetSocketIdOfCpu(static_cast<int>(cpuId));
                if (socketId >= 0) {
                    socketIds.insert(socketId);
                }
            }
        }
    }

    closedir(cpu_dir);
    return std::vector<uint32_t>(socketIds.begin(), socketIds.end());
}

// 解析cpulist字符串
int SocketConnHelper::GetFirstCpuFromCpulist(const std::string &cpuListStr)
{
    if (cpuListStr.empty()) {
        // 表示无效输入
        UBS_VLOG_WARN("GetFirstCpuFromCpulist empty, empty cpulist string\n");
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
            } catch (const std::exception &e) {
                UBS_VLOG_ERR("No valid CPU detected.\n");
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
            } catch (const std::exception &e) {
                UBS_VLOG_ERR("No valid CPU detected.\n");
                tokenCPU = 0;
                return -1;
            }
            return tokenCPU;
        }
    }

    UBS_VLOG_ERR("GetFirstCpuFromCpulist failed, no valid cpu token\n");
    return -1; // 没有找到有效 CPU
}

} // namespace ubs
} // namespace ock