/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <regex>

#include "print_stats_mgr.h"

class PrintStatsMgrTest : public testing::Test {
public:
    PrintStatsMgrTest() : testDir("/tmp/ubsocket_test_" + std::to_string(getpid()))
    {
        // empty
    }

    void SetUp() override
    {
        std::filesystem::create_directories(testDir);
    }

    void TearDown() override
    {
        Statistics::PrintStatsMgr::StopStatsCollection();
        std::filesystem::remove_all(testDir);
        GlobalMockObject::verify();
    }

protected:
    std::string testDir;

    std::string ReadFileContent(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return "";
        }
        return std::string((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    void WaitForFile(int maxWaitSeconds = 30)
    {
        for (int i = 0; i < maxWaitSeconds; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(maxWaitSeconds));
        }
    }
};
 
TEST_F(PrintStatsMgrTest, FileOutput)
{
    uint64_t traceTime = 2;
    uint64_t traceFileSize = 15;

    Statistics::PrintStatsMgr::StartStatsCollection(traceTime, testDir.c_str(), traceFileSize);

    WaitForFile();

    std::string pattern = testDir + "/ubsocket_kpi_*.json";
    std::vector<std::string> matchingFiles;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(testDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::regex re("^ubsocket_kpi_.*\\.json$");
                if (std::regex_match(filename, re)) {
                    matchingFiles.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        FAIL() << "Failed to read directory: " << e.what();
    }

    EXPECT_FALSE(matchingFiles.empty());

    std::string content = ReadFileContent(matchingFiles[0]);

    EXPECT_FALSE(content.empty());

    Statistics::PrintStatsMgr::StopStatsCollection();
}