/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client display data, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "utracer_info.h"
#include "cli_terminal_display.h"

namespace Statistics {

static constexpr int IPV6_HEXTET_COUNT = 8;
static constexpr int IPV6_MAX_COLONS = 7;
static constexpr int IPV6_HEXTET_BYTE_COUNT = 2;
static constexpr int BYTE_BIT_WIDTH = 8;

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

void TerminalDisplay::DisplayDelayTraceInfo(uint8_t *data, uint32_t dataLen)
{
    uint32_t headerSize = sizeof(CLIDelayHeader);
    if (dataLen < headerSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    CLIDelayHeader header{};
    if (memcpy_s(&header, headerSize, data, headerSize) != 0) {
        CLI_LOG("Failed to memcpy CLIDataHeader\n");
        return;
    }
    if (header.retCode != 0) {
        printf("Error occur while deal delay operation\n");
        return;
    }
    uint32_t traceNum = header.tracePointNum;
    uint32_t expectedSize = headerSize + traceNum * sizeof(TranTraceInfo);
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }
    auto* traceInfos = reinterpret_cast<TranTraceInfo *>(data + headerSize);
    for (uint32_t i = 0; i < traceNum; i++) {
        if (i == 0) {
            printf("%s \n", TranTraceInfo::HeaderString().data());
        }
        printf("%s \n", traceInfos[i].ToString().data());
    }
    printf("Success to deal delay operation.\n");
}

void TerminalDisplay::PrintHeader(CLIDataHeader &header)
{
    PrintTitle("CLI STATISTICS MONITOR");
    PrintItem("Total Sockets", header.socketNum);
    PrintItem("Connect Calls", header.connNum);
    PrintItem("Active Conns", header.activeConn);
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
    PrintDataItem("Remote Ip", ConvertCharArrayToIP(sockData->remoteIp), colorGrey, false);
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

std::string TerminalDisplay::ConvertCharArrayToIP(const char* ipArray)
{
    char ipStr[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET, ipArray, ipStr, sizeof(ipStr)) == NULL) {
        if (inet_ntop(AF_INET6, ipArray, ipStr, sizeof(ipStr)) == NULL) {
            return "Invalid IP";
        }
    }
    return std::string(ipStr);
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
}