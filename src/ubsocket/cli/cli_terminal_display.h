/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client display data, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#ifndef CLI_TERMINAL_DISPLAY
#define CLI_TERMINAL_DISPLAY

#include "securec.h"
#include "cli_message.h"
#include "umq_dfx_api.h"

namespace Statistics {
class TerminalDisplay {
    static constexpr const char* cursorHome = "\033[H";
    static constexpr const char* clearScreen = "\033[2J";
    static constexpr const char* colorRed = "\033[31m";
    static constexpr const char* colorGreen = "\033[32m";
    static constexpr const char* colorYellow = "\033[33m";
    static constexpr const char* colorBlue = "\033[34m";
    static constexpr const char* colorGrey = "\033[30m";
    static constexpr const char* colorBold = "\033[1m";
    static constexpr const char* colorReset = "\033[0m";
    static constexpr const char* underline = "\033[4m";

    static constexpr uint8_t byteDataPrecision = 2;
public:
    TerminalDisplay()
    {
        DetectTerminal();
    }
    ~TerminalDisplay()
    {
        printf("%s\n", colorReset);
    }
    bool Istty() const
    {
        return mIstty;
    }
    void DisplaySocketInfo(uint8_t *data, const uint32_t dataLen);
    void DisplayFlowControlInfo(uint8_t *data, const uint32_t dataLen);
    void DisplayTopoInfo(umq_route_list_t *data, const uint32_t dataLen);
    // data display
    void PrintHeader(CLIDataHeader &header);
    void PrintTitle(std::string title);
    void PrintItem(std::string name, uint32_t number);
    void PrintSubTitle();
    void PrintSubTitleItem(std::string name);
    void PrintDelimiter();
    void PrintData(CLISocketData *sockData);
    void PrintDataItem(std::string name, std::string data, const char* color, bool useGrey);
    void NewLine();
    void Refresh();
    std::string BytesToHumanReadable(uint64_t bytes);
    std::string ConvertTimeToString(uint64_t timestamp);
    std::string ConvertEidToString(const uint8_t* eidArray, size_t length);
    // topo display
    void DisplayDelayTraceInfo(uint8_t *data, uint32_t dataLen);

private:
    void DetectTerminal()
    {
        mIstty = (isatty(STDOUT_FILENO) == 1);
    }
    bool mIstty = false;
    int mTerminalWidth = 120;
};
}
#endif