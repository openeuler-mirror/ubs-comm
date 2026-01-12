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

#include "brpc_file_descriptor.h"
#include "brpc_context.h"

namespace Brpc {

const char* Context::CPU_LIST_PREFIX_PATH = "/sys/devices/system/node/";
const char* Context::CPU_LIST_SUFFIX_PATH = "/cpulist";
const char* Context::SOCKET_ID_PERFIX_PATH = "/sys/devices/system/cpu/";
const char* Context::SOCKET_ID_SUFFIX_PATH = "/topology/physical_package_id";

constexpr uint16_t CPU_STR_SIZE = 3;
constexpr uint16_t NODE_STR_SIZE = 4;

std::atomic<int> Context::m_ref = {0};

::SocketFd *Context::CreateSocketFd(int fd, int event_fd)
{
    ::SocketFd *socket_fd = nullptr;
    try {
        switch (m_socket_fd_trans_mode) {
            case SOCKET_FD_TRANS_MODE_UMQ:
            case SOCKET_FD_TRANS_MODE_UMQ_ZERO_COPY:
                socket_fd = (::SocketFd *)new Brpc::SocketFd(fd, event_fd);
                break;
            default:
                break;      
        }
    } catch (std::exception& e) {
        RPC_ADPT_VLOG_ERR("%s\n", e.what());
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
                epoll_fd = (::EpollFd *)new Brpc::EpollFd(fd);
                break;
            default:
                break;      
        }
    } catch (std::exception& e) {
        RPC_ADPT_VLOG_ERR("%s\n", e.what());
        return nullptr;
    }

    return epoll_fd;
}

// 解析cpulist字符串
int Context::GetFirstCpuFromCpulist(const std::string &cpuListStr)
{
    if (cpuListStr.empty()) {
        // 表示无效输入
        return -1;
    }

    std::stringstream ss(cpuListStr);
    std::string token;

    // 只取第一个逗号分隔的 token
    if (std::getline(ss, token, ',')) {
        size_t dash = token.find('-');
        if (dash != std::string::npos) {
            // 范围形式：如 "0-3"，返回开始的数字
            return std::atoi(token.substr(0, dash).c_str());
        } else {
            // 单个 CPU：如 "5"，直接返回
            return std::atoi(token.c_str());
        }
    }
    
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
        return -1;
    }
    return GetSocketIdOfCpu(cpu);
}

}