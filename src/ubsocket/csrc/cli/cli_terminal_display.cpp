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

namespace Statistics {

static constexpr int COL_WIDTH_MIN = 20;
static constexpr int COL_WIDTH_MAX = 30;
static constexpr int PROF_VALUE_SUM = 7;

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
    uint32_t expectedSize = headerSize + SocketNum * sizeof(CLISocketData);
    if (dataLen != expectedSize) {
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
    PrintDataItem("Local Eid", ConvertEidToString(sockData->localEid, CLI_EID_SIZE), colorBlue, false);
    PrintDelimiter();
    PrintDataItem("Remote Eid", ConvertEidToString(sockData->remoteEid, CLI_EID_SIZE), colorBlue, false);
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
    char buffer[80];
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
    uint32_t expectedSize = headerSize + header.tracePointDataSize;
    if (dataLen != expectedSize) {
        CLI_LOG("Invalid data size\n");
        return;
    }

    if (dataLen == headerSize) {
        PrintProfValue();
        return;
    }

    std::string recvDataStr(reinterpret_cast<char *>(data + headerSize), header.tracePointDataSize);
    PrintProfData(recvDataStr);
}

void TerminalDisplay::PrintProfTitle(std::ostringstream &oss)
{
    constexpr int timeBufSize = 32;
    time_t now = time(nullptr);
    char timeBuf[timeBufSize];
    struct tm timeInfo;
    if (localtime_r(&now, &timeInfo) != nullptr) {
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
    } else {
        timeBuf[0] = '\0';
        CLI_LOG("Failed to create timeStamp.\n");
    }
    oss << "timeStamp: " << timeBuf << "\n";
    oss << std::left << std::setw(COL_WIDTH_MAX) << "[TRACE_NAME]" << std::setw(COL_WIDTH_MIN) << "SUCCESS"
        << std::setw(COL_WIDTH_MIN) << "FAILURE" << std::setw(COL_WIDTH_MIN) << "TOTAL(ns)" << std::setw(COL_WIDTH_MIN)
        << "AVG(ns)" << std::setw(COL_WIDTH_MIN) << "MAX(ns)" << std::setw(COL_WIDTH_MIN) << "MIN(ns)" << "\n";
}

void TerminalDisplay::PrintProfData(std::string &outPutData)
{
    std::ostringstream oss;
    PrintProfTitle(oss);

    auto tp_items = Split(outPutData, ';');
    for (const auto &item : tp_items) {
        if (item.empty()) {
            continue;
        }
        auto fields = Split(item, ',');
        if (fields.size() != PROF_VALUE_SUM) {
            continue;
        }
        oss << std::left;
        for (auto i = 0; i < fields.size(); ++i) {
            if (!i) {
                oss << std::setw(COL_WIDTH_MAX);
            } else {
                oss << std::setw(COL_WIDTH_MIN);
            }
            oss << fields[i];
        }
        oss << "\n";
    }
    oss << "Success to deal delay operation. \n";
    printf("%s", oss.str().c_str());
}

void TerminalDisplay::PrintProfValue()
{
    std::ostringstream oss;
    oss << "Success to deal delay operation. \n";
    printf("%s", oss.str().c_str());
}
} // namespace Statistics
