/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client display data, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_terminal_display.h"
#include "umq_dfx_types.h"
#include "umq_types.h"


namespace Statistics {

static constexpr int IPV6_HEXTET_COUNT = 8;
static constexpr int IPV6_MAX_COLONS = 7;
static constexpr int IPV6_HEXTET_BYTE_COUNT = 2;
static constexpr int BYTE_BIT_WIDTH = 8;
static constexpr int MAX_FLOW_CONTROL_STR = 4096;

char* In6AddrToFullStr(const struct in6_addr *in6Addr, char *dstBuf, size_t bufSize)
{
    if (in6Addr == nullptr || dstBuf == nullptr || bufSize < INET6_ADDRSTRLEN) {
        return nullptr;
    }
    if (memset_s(dstBuf, bufSize, 0, bufSize) != 0) {
        CLI_LOG("Failed to memset buf\n");
        return nullptr;
    }
    char *pos = dstBuf;
    for (int i = 0; i < IPV6_HEXTET_COUNT; i++) {
        uint16_t segment = (uint16_t)(in6Addr->s6_addr[IPV6_HEXTET_BYTE_COUNT * i] << BYTE_BIT_WIDTH) |
            in6Addr->s6_addr[IPV6_HEXTET_BYTE_COUNT * i + 1];
        int written = snprintf_s(pos, bufSize - (pos - dstBuf), bufSize - (pos - dstBuf), "%04x", segment);
        if (written <= 0 || written >= (int)(bufSize - (pos - dstBuf))) {
            return nullptr;
        }
        pos += written;
        if (i < IPV6_MAX_COLONS) {
            if (pos + 1 >= dstBuf + bufSize) {
                return nullptr;
            }
            *pos++ = ':';
        }
    }
    return dstBuf;
}

void TerminalDisplay::DisplayTopoInfo(umq_route_list_t *routeList, const uint32_t dataLen)
{
    if (dataLen != sizeof(umq_route_list_t)) {
        CLI_LOG("Invalid data\n");
        return;
    }
    uint32_t num = routeList->route_num;
    umq_route_t *data = routeList->routes;
    if (num == 0) {
        CLI_LOG("Filter num is zero no topo data");
        return;
    }
    PrintTitle("CLI UB Topology Query");
    NewLine();
    for (uint32_t i = 0; i < num; i++) {
        char srcEid[INET6_ADDRSTRLEN] = {0};
        char dstEid[INET6_ADDRSTRLEN] = {0};
        if (In6AddrToFullStr(reinterpret_cast<struct in6_addr *>(&data->src_eid), srcEid, sizeof(srcEid)) == nullptr) {
            CLI_LOG("Convert src to full format failed\n");
            return;
        }
        if (In6AddrToFullStr(reinterpret_cast<struct in6_addr *>(&data->dst_eid), dstEid, sizeof(dstEid)) == nullptr) {
            CLI_LOG("Convert dst to full format failed\n");
            return;
        }
        printf("%s%sPort Eid Pair %u%s\n", colorBold, colorBlue, i, colorReset);
        printf("%s%sSrc: %s%s\n", colorBold, colorYellow, srcEid, colorReset);
        printf("%s%sDst: %s%s\n", colorBold, colorYellow, dstEid, colorReset);
        NewLine();
        data += 1;
    }
}

void TerminalDisplay::DisplaySocketInfo(uint8_t *data, const uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLISocketData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);
    PrintSubTitle();
    CLISocketData* sockData = reinterpret_cast<CLISocketData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        PrintData(sockData);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayFlowControlInfo(uint8_t *data, const uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLIFlowControlData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char fcStatStr[MAX_FLOW_CONTROL_STR] = {};
    CLIFlowControlData* sockData = reinterpret_cast<CLIFlowControlData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_flow_control_stats_to_str(&(sockData->umqFlowControlStat),
            fcStatStr, MAX_FLOW_CONTROL_STR) < 0) {
                CLI_LOG("Failed to generate flow control info string\n");
            }
        printf("Socket %d:\n", i);
        printf("%s", fcStatStr);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

// --- 辅助函数：打印表头 ---
void PrintProbeHeader()
{
    printf("\n");
    printf("%-8s | %-10s | %-10s | %-10s | %-12s | %-12s | %-12s\n",
            "FD", "UBS RTT(ns)", "CliΔ(ns)", "SrvΔ(ns)",
            "UMQ RTT(ns)", "UMQ CliΔ(ns)", "UMQ SrvΔ(ns)");
    printf("---------------------------------------------------------------------------------------------------"
           "-------------------------------\n");
}

// --- 辅助函数：打印单行概览数据 ---
void PrintProbeRow(const CLIProbeData* probeData)
{
    // 1. 基础转换
    double clientDelta = (double)(probeData->client_recv_rsp_time_ns -
                                    probeData->client_send_time_ns);
    double serverDelta = (double)(probeData->server_rsp_time_ns -
                                    probeData->server_recv_time_ns);
    double ubsRtt = clientDelta - serverDelta;

    // 2. UMQ 差值计算
    double umqClientDelta = (double)(probeData->umq_client_recv_time_ns -
                                            probeData->umq_client_post_time_ns);
    double umqServerDelta = (double)(probeData->umq_server_rsp_time_ns -
                                            probeData->umq_server_recv_time_ns);

    // 3. UMQ RTT 计算 (Client Δ - Server Δ)
    double umqRtt = umqClientDelta - umqServerDelta;

    // 4. 打印
    printf("%-8d | %-10.3f | %-10.3f | %-10.3f | %-12.3f | %-12.3f | %-12.3f\n",
            probeData->fd, ubsRtt, clientDelta, serverDelta,
            umqRtt, umqClientDelta, umqServerDelta);
}

// --- 辅助函数：打印详细打点信息 ---
void PrintProbeDetails(const CLIProbeData* probeData)
{
    // Client 端
    printf("  +-- [Client] ubsocket_client_send(ns): %-10lu | ubsocket_client_recv(ns): %-10lu\n",
           probeData->client_send_time_ns, probeData->client_recv_rsp_time_ns);

    // UMQ Client
    printf("  |            umq_post(ns): %-10lu | umq_recv(ns): %-10lu\n",
           probeData->umq_client_post_time_ns, probeData->umq_client_recv_time_ns);

    // Server 端
    printf("  +-- [Server] ubsocket_server_recv(ns): %-10lu | ubsocket_server_rsp(ns): %-10lu\n",
           probeData->server_recv_time_ns, probeData->server_rsp_time_ns);

    // UMQ Server
    printf("  |            umq_recv(ns): %-10lu | umq_rsp(ns): %-10lu\n",
           probeData->umq_server_recv_time_ns, probeData->umq_server_rsp_time_ns);

    printf("\n"); // 分隔空行
}

// --- 主函数 ---
void TerminalDisplay::DisplayProbeInfo(uint8_t *data, const uint32_t dataLen)
{
    // 1. 基础校验
    uint32_t headerSize = sizeof(CLIProbeHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    // 2. 拷贝头部
    CLIProbeHeader header{};
    if (memcpy_s(&header, sizeof(CLIProbeHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIProbeHeader\n");
        return;
    }

    // 3. 长度一致性校验
    uint32_t sockNum = header.socketNum;
    uint32_t expectedSize = headerSize + sockNum * sizeof(CLIProbeData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    Refresh();

    // 4. 打印统计头部
    printf("\n%s=== Probe Statistics ===%s\n", colorBold, colorReset);
    printf("Total Probes (SocketNum): %s%u%s\n", colorBold, sockNum, colorReset);

    PrintProbeHeader();

    // 5. 将原始指针数据封装进 vector，统一后续处理逻辑
    std::vector<CLIProbeData> probeDataList(
        reinterpret_cast<CLIProbeData*>(data + headerSize), // 起始位置
        reinterpret_cast<CLIProbeData*>(data + dataLen)     // 结束位置
    );

    // 6. 循环处理数据 (使用 range-based for 循环)
    for (const auto& probeData : probeDataList) {
        PrintProbeRow(&probeData);      // 打印概览行
        PrintProbeDetails(&probeData);  // 打印详情
    }

    // 7. 结束提示
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::PrintHeader(CLIDataHeader &header)
{
    PrintTitle("CLI STATISTICS MONITOR");
    PrintItem("Total Sockets", header.socketNum);
    PrintItem("Connect Calls", header.connNum);
    PrintItem("Active Conns", header.activeConn);
    PrintItem("ReTx Count", header.reTxCount);
    NewLine();
}

void TerminalDisplay::PrintTitle(std::string title)
{
    printf("%s%s%s%s%s\n", colorBold, colorGreen, underline, title.c_str(), colorReset);
}

void TerminalDisplay::PrintItem(std::string name, uint32_t number)
{
    printf("%s%s%-15s: %s", colorBold, colorBlue, name.c_str(), colorReset);
    printf("%s%s%u%s\n", colorBold, colorYellow, number, colorReset);
}

void TerminalDisplay::PrintSubTitle()
{
    PrintSubTitleItem("SocketFd");
    PrintDelimiter();
    PrintSubTitleItem("Creation Time");
    printf("      ");
    PrintDelimiter();
    PrintSubTitleItem("Remote Ip");
    printf("      ");
    PrintDelimiter();
    PrintSubTitleItem("Local Eid");
    printf("                              ");
    PrintDelimiter();
    PrintSubTitleItem("Romote Eid");
    printf("                             ");
    PrintDelimiter();
    PrintSubTitleItem("Recv Packets");
    printf(" ");
    PrintSubTitleItem("Send Packets");
    PrintDelimiter();
    PrintSubTitleItem("Recv Bytes");
    printf(" ");
    PrintSubTitleItem("Send Bytes");
    PrintDelimiter();
    PrintSubTitleItem("Error Packets");
    PrintDelimiter();
    PrintSubTitleItem("Lost Packets");
    NewLine();
}

void TerminalDisplay::PrintSubTitleItem(std::string name)
{
    printf("%s%s%s%s%s", colorBold, colorBlue, underline, name.c_str(), colorReset);
}

void TerminalDisplay::PrintDelimiter()
{
    printf(" ");
    printf("%s%s%s%s", colorBold, colorBlue, "|", colorReset);
    printf(" ");
}

void TerminalDisplay::PrintData(CLISocketData *sockData)
{
    PrintDataItem("SocketFd", std::to_string(sockData->socketId), colorRed, false);
    PrintDelimiter();
    PrintDataItem("Creation Time", ConvertTimeToString(sockData->createTime), colorGrey, false);
    PrintDelimiter();
    PrintDataItem("Remote Ip      ", sockData->remoteIp, colorGrey, false);
    PrintDelimiter();
    PrintDataItem("Local Eid", ConvertEidToString(sockData->localEid, UMQ_EID_SIZE), colorBlue, false);
    PrintDelimiter();
    PrintDataItem("Remote Eid", ConvertEidToString(sockData->remoteEid, UMQ_EID_SIZE), colorBlue, false);
    PrintDelimiter();
    PrintDataItem("Recv Packets", std::to_string(sockData->recvPackets), colorGreen, sockData->recvPackets == 0);
    printf(" ");
    PrintDataItem("Send Packets", std::to_string(sockData->sendPackets), colorGreen, sockData->sendPackets == 0);
    PrintDelimiter();
    PrintDataItem("Recv Bytes", BytesToHumanReadable(sockData->recvBytes), colorYellow, sockData->recvBytes == 0);
    printf(" ");
    PrintDataItem("Send Bytes", BytesToHumanReadable(sockData->sendBytes), colorYellow, sockData->sendBytes == 0);
    PrintDelimiter();
    PrintDataItem("Error Packets", std::to_string(sockData->errorPackets), colorRed, sockData->errorPackets == 0);
    PrintDelimiter();
    PrintDataItem("Lost Packets", std::to_string(sockData->lostPackets), colorRed, sockData->lostPackets == 0);
    NewLine();
}

void TerminalDisplay::PrintDataItem(std::string name, std::string data, const char* color, bool useGrey)
{
    if (useGrey) {
        color = colorGrey;
    }
    int width = name.length() > data.length() ? name.length() : data.length();
    printf("%s%s%*s%s", colorBold, color, width, data.c_str(), colorReset);
}

void TerminalDisplay::NewLine()
{
    printf("\n");
}

void TerminalDisplay::Refresh()
{
    printf("%s%s", clearScreen, cursorHome);
}

std::string TerminalDisplay::BytesToHumanReadable(uint64_t bytes)
{
    const char* units[] = {"B", "K", "M", "G", "T"};
    const uint64_t base = 1024;
    uint32_t index = 0;
    double value = static_cast<double>(bytes);
    while (value >= base && index < (sizeof(units) / sizeof(units[0]) - 1)) {
        value /= base;
        index++;
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(byteDataPrecision) << value << units[index];
    return ss.str();
}

std::string TerminalDisplay::ConvertTimeToString(uint64_t timestamp)
{
    struct tm time_struct;
    time_t time_seconds = static_cast<time_t>(timestamp);
    localtime_r(&time_seconds, &time_struct);
    char buffer[80];
    (void)strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_struct);
    return std::string(buffer);
}

std::string TerminalDisplay::ConvertEidToString(const uint8_t* eidArray, size_t length)
{
    std::stringstream ss;
    for (size_t i = 0; i < length; i += 2) {
        uint16_t val = (eidArray[i] << 8) | eidArray[i+1];
        ss << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(val);
        if (i != length - 2) {
            ss << ":";
        }
    }
    return ss.str();
}

void TerminalDisplay::DisplayQbufPoolInfo(uint8_t *data, uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLIQbufPoolData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char qbufPoolStatStr[8192] = {}; // 8KB buffer for qbuf pool stats
    CLIQbufPoolData* sockData = reinterpret_cast<CLIQbufPoolData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_qbuf_pool_stats_to_str(&(sockData->umqQbufPoolStat),
            qbufPoolStatStr, sizeof(qbufPoolStatStr)) < 0) {
                CLI_LOG("Failed to generate qbuf pool info string\n");
            }
        printf("Socket %d:\n", i);
        printf("%s", qbufPoolStatStr);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayUmqInfo(uint8_t *data, uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLIUmqInfoData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char umqInfoStr[8192] = {}; // 8KB buffer for umq info
    CLIUmqInfoData* sockData = reinterpret_cast<CLIUmqInfoData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_info_to_str(&(sockData->umqInfo),
            umqInfoStr, sizeof(umqInfoStr)) < 0) {
                CLI_LOG("Failed to generate umq info string\n");
            }
        printf("Socket %d:\n", i);
        printf("%s", umqInfoStr);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayIoPacketInfo(uint8_t *data, uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLIIoPacketData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char ioPacketStatStr[8192] = {}; // 8KB buffer for io packet stats
    CLIIoPacketData* sockData = reinterpret_cast<CLIIoPacketData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_io_stats_to_str(&(sockData->umqPacketStat),
            ioPacketStatStr, sizeof(ioPacketStatStr)) < 0) {
                CLI_LOG("Failed to generate io packet stats string\n");
            }
        printf("Socket %d:\n", i);
        printf("%s", ioPacketStatStr);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayUmqPerfInfo(uint8_t *data, uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    if (memcpy_s(&header, sizeof(CLIDataHeader), data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    uint32_t SocketNum = header.socketNum;
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLIUmqPerfData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char umqPerfStatStr[8192] = {}; // 8KB buffer for umq perf stats
    CLIUmqPerfData* sockData = reinterpret_cast<CLIUmqPerfData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_stats_perf_to_str(&(sockData->umqPerfStat),
            umqPerfStatStr, sizeof(umqPerfStatStr)) < 0) {
                CLI_LOG("Failed to generate umq perf stats string\n");
            }
        printf("Socket %d:\n", i);
        printf("%s", umqPerfStatStr);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}
}