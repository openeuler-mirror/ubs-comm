/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client display data, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/
#include <iomanip>
#include <sstream>

#include "cli_terminal_display.h"
#include "umq_dfx_types.h"
#include "umq_types.h"

#include "profiling/statistics/tx_stat_defs.h"
#include "profiling/statistics/rx_stat_defs.h"

namespace Statistics {

static constexpr int IPV6_HEXTET_COUNT = 8;
static constexpr int IPV6_MAX_COLONS = 7;
static constexpr int IPV6_HEXTET_BYTE_COUNT = 2;
static constexpr int BYTE_BIT_WIDTH = 8;
static constexpr int MAX_FLOW_CONTROL_STR = 4096;
constexpr int COL_WIDTH_MIN = 20;
constexpr int COL_WIDTH_MAX = 45;    // 45 is the max width of the column
constexpr int PROF_VALUE_SUM = 7;   // fast mode: name + 6 stats
constexpr int PROF_VALUE_SUM_EXT = 9; // ext mode: name + 6 stats + p99 + p9999
constexpr int STAT_STR_BUF_SIZE = 8192;
constexpr int PERF_STAT_STR_BUF_SIZE = 16384;
constexpr int QBUF_POOL_STAT_STR_BUF_SIZE = 32768; // per-SC breakdown output is large, same as server-side dump
constexpr int TIME_STR_BUF_SIZE = 80;

/* 防止整数溢出绕过长度校验：在 size_t（64 位）中计算 headerSize + count * elementSize，
 * 避免截断到 uint32_t 后被取模绕过。若结果超出 uint32_t 范围，必然不等于 dataLen，
 * 校验自然失败。count=... elementSize=1 时等价于纯加法校验。 */
static inline bool ValidatePayloadSize(uint32_t headerSize, uint32_t count, size_t elementSize,
                                       uint32_t dataLen) noexcept
{
    size_t expected = static_cast<size_t>(headerSize) + static_cast<size_t>(count) * elementSize;
    return expected == static_cast<size_t>(dataLen);
}

char *In6AddrToFullStr(const struct in6_addr *in6Addr, char *dstBuf, size_t bufSize)
{
    if (in6Addr == nullptr || dstBuf == nullptr || bufSize < INET6_ADDRSTRLEN) {
        return nullptr;
    }
    memset(dstBuf, 0, bufSize);

    char *pos = dstBuf;
    for (int i = 0; i < IPV6_HEXTET_COUNT; i++) {
        uint16_t segment = (uint16_t)(in6Addr->s6_addr[IPV6_HEXTET_BYTE_COUNT * i] << BYTE_BIT_WIDTH) |
                           in6Addr->s6_addr[IPV6_HEXTET_BYTE_COUNT * i + 1];
        int written = snprintf(pos, bufSize - (pos - dstBuf), "%04x", segment);
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
    if (num == 0) {
        CLI_LOG("Filter num is zero no topo data");
        return;
    }
    if (num > UMQ_MAX_ROUTES) {
        CLI_LOG("Invalid route num: %u, max %d\n", num, UMQ_MAX_ROUTES);
        return;
    }
    umq_route_t *data = routeList->routes;
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
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);

    uint32_t SocketNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, SocketNum, sizeof(CLISocketData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);
    PrintSubTitle();
    CLISocketData *sockData = reinterpret_cast<CLISocketData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        PrintData(sockData);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayFlowControlInfo(uint8_t *data, const uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);

    uint32_t SocketNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, SocketNum, sizeof(CLIFlowControlData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char fcStatStr[MAX_FLOW_CONTROL_STR] = {};
    CLIFlowControlData *sockData = reinterpret_cast<CLIFlowControlData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_flow_control_stats_to_str(&(sockData->umqFlowControlStat), fcStatStr, MAX_FLOW_CONTROL_STR) < 0) {
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
    printf("%-8s | %-10s | %-10s | %-10s | %-12s | %-12s | %-12s\n", "FD", "UBS RTT(ns)", "CliΔ(ns)", "SrvΔ(ns)",
           "UMQ RTT(ns)", "UMQ CliΔ(ns)", "UMQ SrvΔ(ns)");
    printf("---------------------------------------------------------------------------------------------------"
           "-------------------------------\n");
}

// --- 辅助函数：打印单行概览数据 ---
void PrintProbeRow(const CLIProbeData *probeData)
{
    // 1. 基础转换
    double clientDelta = (double)(probeData->client_recv_rsp_time_ns - probeData->client_send_time_ns);
    double serverDelta = (double)(probeData->server_rsp_time_ns - probeData->server_recv_time_ns);
    double ubsRtt = clientDelta - serverDelta;

    // 2. UMQ 差值计算
    double umqClientDelta = (double)(probeData->umq_client_recv_time_ns - probeData->umq_client_post_time_ns);
    double umqServerDelta = (double)(probeData->umq_server_rsp_time_ns - probeData->umq_server_recv_time_ns);

    // 3. UMQ RTT 计算 (Client Δ - Server Δ)
    double umqRtt = umqClientDelta - umqServerDelta;

    // 4. 打印
    printf("%-8d | %-10.3f | %-10.3f | %-10.3f | %-12.3f | %-12.3f | %-12.3f\n", probeData->fd, ubsRtt, clientDelta,
           serverDelta, umqRtt, umqClientDelta, umqServerDelta);
}

// --- 辅助函数：打印详细打点信息 ---
void PrintProbeDetails(const CLIProbeData *probeData)
{
    // Client 端
    printf("  +-- [Client] ubsocket_client_send(ns): %-10lu | ubsocket_client_recv(ns): %-10lu\n",
           probeData->client_send_time_ns, probeData->client_recv_rsp_time_ns);

    // UMQ Client
    printf("  |            umq_post(ns): %-10lu | umq_recv(ns): %-10lu\n", probeData->umq_client_post_time_ns,
           probeData->umq_client_recv_time_ns);

    // Server 端
    printf("  +-- [Server] ubsocket_server_recv(ns): %-10lu | ubsocket_server_rsp(ns): %-10lu\n",
           probeData->server_recv_time_ns, probeData->server_rsp_time_ns);

    // UMQ Server
    printf("  |            umq_recv(ns): %-10lu | umq_rsp(ns): %-10lu\n", probeData->umq_server_recv_time_ns,
           probeData->umq_server_rsp_time_ns);

    printf("\n"); // 分隔空行
}

// --- 主函数 ---
void TerminalDisplay::DisplayProbeInfo(uint8_t *data, const uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    // 1. 基础校验
    uint32_t headerSize = sizeof(CLIProbeHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    // 2. 拷贝头部
    CLIProbeHeader header{};
    memcpy(&header, data, headerSize);

    // 3. 长度一致性校验
    uint32_t sockNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, sockNum, sizeof(CLIProbeData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    Refresh();

    // 4. 打印统计头部
    printf("\n%s=== Probe Statistics ===%s\n", colorBold, colorReset);
    printf("Total Probes (SocketNum): %s%u%s\n", colorBold, sockNum, colorReset);

    PrintProbeHeader();

    // 5. 将原始指针数据封装进 vector，统一后续处理逻辑
    std::vector<CLIProbeData> probeDataList(reinterpret_cast<CLIProbeData *>(data + headerSize), // 起始位置
                                            reinterpret_cast<CLIProbeData *>(data + dataLen)     // 结束位置
    );

    // 6. 循环处理数据 (使用 range-based for 循环)
    for (const auto &probeData : probeDataList) {
        PrintProbeRow(&probeData);     // 打印概览行
        PrintProbeDetails(&probeData); // 打印详情
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
    PrintItem64("Pool Total Capacity", header.poolTotalNum);
    PrintItem64("Pool Available Count", header.poolAvailableNum);
    PrintItem64("Pool In Use Count", header.poolInUseNum);
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

void TerminalDisplay::PrintItem64(std::string name, uint64_t number)
{
    printf("%s%s%-15s: %s", colorBold, colorBlue, name.c_str(), colorReset);
    printf("%s%s%llu%s\n", colorBold, colorYellow, (unsigned long long)number, colorReset);
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
    PrintDelimiter();
    PrintSubTitleItem("Bigdata CtrlRcv");
    PrintDelimiter();
    PrintSubTitleItem("Bigdata Read");
    PrintDelimiter();
    PrintSubTitleItem("Bigdata CtrlSnd");
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
    PrintDelimiter();
    PrintDataItem("Bigdata CtrlRcv", std::to_string(sockData->bigdataCtrlRecv), colorCyan, sockData->bigdataCtrlRecv == 0);
    PrintDelimiter();
    PrintDataItem("Bigdata Read", std::to_string(sockData->bigdataRead), colorCyan, sockData->bigdataRead == 0);
    PrintDelimiter();
    PrintDataItem("Bigdata CtrlSnd", std::to_string(sockData->bigdataCtrlSend), colorCyan, sockData->bigdataCtrlSend == 0);
    NewLine();
}

void TerminalDisplay::PrintDataItem(std::string name, std::string data, const char *color, bool useGrey)
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
    const char *units[] = {"B", "K", "M", "G", "T"};
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
    char buffer[TIME_STR_BUF_SIZE];
    (void)strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_struct);
    return std::string(buffer);
}

std::string TerminalDisplay::ConvertEidToString(const uint8_t *eidArray, size_t length)
{
    std::stringstream ss;
    for (size_t i = 0; i < length; i += 2) {
        uint16_t val = (eidArray[i] << 8) | eidArray[i + 1];
        ss << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(val);
        if (i != length - 2) {
            ss << ":";
        }
    }
    return ss.str();
}

void TerminalDisplay::DisplayQbufPoolInfo(uint8_t *data, uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t oneSockSize = sizeof(CLIDataHeader) + sizeof(CLIQbufPoolData);
    if (dataLen < oneSockSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, sizeof(CLIDataHeader));

    Refresh();
    PrintHeader(header);

    char qbufPoolStatStr[STAT_STR_BUF_SIZE] = {};
    CLIQbufPoolData *sockData = reinterpret_cast<CLIQbufPoolData *>(data + sizeof(CLIDataHeader));
    if (umq_qbuf_pool_stats_to_str(&(sockData->umqQbufPoolStat), qbufPoolStatStr, sizeof(qbufPoolStatStr)) < 0) {
        CLI_LOG("Failed to generate qbuf pool info string\n");
    }
    printf("%s", qbufPoolStatStr);
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayUmqInfo(uint8_t *data, uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);

    uint32_t SocketNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, SocketNum, sizeof(CLIUmqInfoData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char umqInfoStr[STAT_STR_BUF_SIZE] = {};
    CLIUmqInfoData *sockData = reinterpret_cast<CLIUmqInfoData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_info_to_str(&(sockData->umqInfo), umqInfoStr, sizeof(umqInfoStr)) < 0) {
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
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);

    uint32_t SocketNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, SocketNum, sizeof(CLIIoPacketData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char ioPacketStatStr[STAT_STR_BUF_SIZE] = {};
    CLIIoPacketData *sockData = reinterpret_cast<CLIIoPacketData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_io_stats_to_str(&(sockData->umqPacketStat), ioPacketStatStr, sizeof(ioPacketStatStr)) < 0) {
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
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);

    uint32_t SocketNum = header.socketNum;
    if (!ValidatePayloadSize(headerSize, SocketNum, sizeof(CLIUmqPerfData), dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    // print data
    Refresh();
    PrintHeader(header);

    char umqPerfStatStr[PERF_STAT_STR_BUF_SIZE] = {};
    CLIUmqPerfData *sockData = reinterpret_cast<CLIUmqPerfData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        if (umq_stats_perf_to_str(&(sockData->umqPerfStat), umqPerfStatStr, sizeof(umqPerfStatStr)) < 0) {
            CLI_LOG("Failed to generate umq perf stats string\n");
        }
        printf("Socket %d:\n", i);
        printf("%s", umqPerfStatStr);
        sockData += 1;
    }
    NewLine();

    // umq tp perf info
    sockData = reinterpret_cast<CLIUmqPerfData *>(data + headerSize);
    for (uint32_t i = 0; i < SocketNum; i++) {
        printf("%s", sockData->umqTpPerfBuf);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayDelayTraceInfo(uint8_t *data, uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDelayHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDelayHeader header{};
    memcpy(&header, data, headerSize);

    if (header.retCode != 0) {
        printf("Error occur while deal delay operation\n");
        return;
    }
    if (!ValidatePayloadSize(headerSize, header.tracePointDataSize, 1, dataLen)) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    if (dataLen == headerSize) {
        PrintProfValue();
        return;
    }

    std::string recvDataStr(reinterpret_cast<char *>(data + headerSize), header.tracePointDataSize);
    printf("%s", recvDataStr.c_str());
    printf("Success to deal delay operation. \n");
}

void TerminalDisplay::PrintProfValue()
{
    std::ostringstream oss;
    oss << "Success to deal delay operation. \n";
    printf("%s", oss.str().c_str());
}

static void PrintTxStatRow(const CLITxStatData *d)
{
    using namespace ock::ubs::txstat;
    printf("%-8llu | %-10u | %-11u | %-10u | %-13u | %-6u | %-7u | %-8u | %-10u | %-5u | "
           "%-7u | %-15u | %-6u | %-10u | %-9u | %-9u\n",
           static_cast<unsigned long long>(d->socketId),
           d->post_err[POST_ERR_EAGAIN_ALL], d->post_err[POST_ERR_EAGAIN_PART],
           d->post_err[POST_ERR_ENOBUFS_ALL], d->post_err[POST_ERR_ENOBUFS_PART],
           d->post_err[POST_ERR_EMLINK], d->post_err[POST_ERR_ETIMEDOUT],
           d->post_err[POST_ERR_EFLOWCTL], d->post_err[POST_ERR_NO_BADQBUF],
           d->post_err[POST_ERR_OTHER],
           d->cqe_err[CQE_ERR_RNR], d->cqe_err[CQE_ERR_ACK_TIMEOUT], d->cqe_err[CQE_ERR_FC],
           d->cqe_err[CQE_ERR_REMOTE], d->cqe_err[CQE_ERR_LOCAL], d->cqe_err[CQE_ERR_OTHER]);
}

void TerminalDisplay::DisplayTxStatInfo(uint8_t *data, const uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);
    uint32_t sockNum = header.socketNum;
    uint32_t expectedSize = headerSize + sockNum * sizeof(CLITxStatData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    Refresh();
    PrintTitle("CLI TX STATISTICS MONITOR");
    NewLine();
    printf("%-8s | %-10s | %-11s | %-10s | %-13s | %-6s | %-7s | %-8s | %-10s | %-5s | "
           "%-7s | %-15s | %-6s | %-10s | %-9s | %-9s\n",
           "SocketFd", "eagain_all", "eagain_part", "enobufs_all", "enobufs_part",
           "emlink", "timeout", "eflowctl", "no_badqbuf", "other",
           "cqe_rnr", "cqe_ack_timeout", "cqe_fc", "cqe_remote", "cqe_local", "cqe_other");
    CLITxStatData *sockData = reinterpret_cast<CLITxStatData *>(data + headerSize);
    for (uint32_t i = 0; i < sockNum; i++) {
        PrintTxStatRow(sockData);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

static void PrintRxStatRow(const CLIRxStatData *d)
{
    using namespace ock::ubs::rxstat;
    printf("%-8llu | %-14u | %-9u | %-17u | %-16u | %-13u | %-6u | %-10u | %-9u | %-13u | "
           "%-8u | %-9u | %-16u | %-16u | %-10u | %-11u\n",
           static_cast<unsigned long long>(d->socketId),
           d->poll_err[RX_POLL_GET_EVENT_FAIL], d->poll_err[RX_POLL_FAIL],
           d->poll_err[RX_REFILL_ALLOC_FAIL], d->poll_err[RX_REFILL_POST_FAIL],
           d->poll_err[RX_QBUF_POP_FAIL],
           d->cqe_err[RXCQE_FC], d->cqe_err[RXCQE_REMOTE], d->cqe_err[RXCQE_LOCAL],
           d->cqe_err[RXCQE_ACK_TIMEOUT], d->cqe_err[RXCQE_RNR], d->cqe_err[RXCQE_OTHER],
           d->dataset_err[RX_DATASET_NO_BLOCK], d->dataset_err[RX_FLOW_CTRL_FAILED],
           d->dataset_err[RX_REARM_FAIL], d->dataset_err[RX_PEER_CLOSED]);
}

void TerminalDisplay::DisplayRxStatInfo(uint8_t *data, const uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    uint32_t headerSize = sizeof(CLIDataHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDataHeader header{};
    memcpy(&header, data, headerSize);
    uint32_t sockNum = header.socketNum;
    uint32_t expectedSize = headerSize + sockNum * sizeof(CLIRxStatData);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    Refresh();
    PrintTitle("CLI RX STATISTICS MONITOR");
    NewLine();
    printf("%-8s | %-14s | %-9s | %-17s | %-16s | %-13s | %-6s | %-10s | %-9s | %-13s | "
           "%-8s | %-9s | %-16s | %-16s | %-10s | %-11s\n",
           "SocketFd", "get_event_fail", "poll_fail", "refill_alloc_fail",
           "refill_post_fail", "qbuf_pop_fail", "rxe_fc", "rxe_remote", "rxe_local",
           "rxe_ack_timeout", "rxe_rnr", "rxe_other", "dataset_no_block",
           "flow_ctrl_failed", "rearm_fail", "peer_closed");
    CLIRxStatData *sockData = reinterpret_cast<CLIRxStatData *>(data + headerSize);
    for (uint32_t i = 0; i < sockNum; i++) {
        PrintRxStatRow(sockData);
        sockData += 1;
    }
    NewLine();
    printf("%sPress Ctrl+C to exit%s\n", colorBold, colorReset);
}

void TerminalDisplay::DisplayQbufPoolStatsInfo(uint8_t *data, uint32_t dataLen)
{
    if (data == nullptr) {
        CLI_LOG("Invalid data, data is null\n");
        return;
    }
    if (dataLen != sizeof(CLIQbufPoolStatsData)) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIQbufPoolStatsData *statsData = reinterpret_cast<CLIQbufPoolStatsData *>(data);

    PrintTitle("CLI GLOBAL QBUF POOL (NORMAL + TINY) STATISTICS");
    NewLine();
    if (statsData->retCode != 0) {
        printf("%sFailed to get qbuf pool info, retCode: %d%s\n", colorRed, statsData->retCode, colorReset);
        return;
    }

    /* 与 30s 周期落盘 ubsocket_qbuf.txt 相同的格式化输出 */
    char poolStatStr[QBUF_POOL_STAT_STR_BUF_SIZE] = {};
    if (umq_qbuf_pool_stats_to_str(&(statsData->umqQbufPoolStat), poolStatStr, sizeof(poolStatStr)) <= 0) {
        CLI_LOG("Failed to generate qbuf pool stats string\n");
        return;
    }
    printf("%s", poolStatStr);
    NewLine();
}
} // namespace Statistics