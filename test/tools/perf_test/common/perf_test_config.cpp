/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <map>

#include "common/perf_test_logger.h"
#include "common/perf_test_utils.h"
#include "common/perf_test_config.h"

namespace hcom {
namespace perftest {
static const std::vector<std::pair<std::string, PERF_TEST_TYPE>> gPerfTestType = {
    { "TRANSPORT_SEND_LAT", PERF_TEST_TYPE::TRANSPORT_SEND_LAT },
    { "TRANSPORT_SEND_BW", PERF_TEST_TYPE::TRANSPORT_SEND_BW },
    { "TRANSPORT_READ_LAT", PERF_TEST_TYPE::TRANSPORT_READ_LAT },
    { "TRANSPORT_READ_BW", PERF_TEST_TYPE::TRANSPORT_READ_BW },
    { "TRANSPORT_WRITE_LAT", PERF_TEST_TYPE::TRANSPORT_WRITE_LAT },
    { "TRANSPORT_WRITE_BW", PERF_TEST_TYPE::TRANSPORT_WRITE_BW },
    { "SERVICE_SEND_LAT", PERF_TEST_TYPE::SERVICE_SEND_LAT },
    { "SERVICE_SEND_BW", PERF_TEST_TYPE::SERVICE_SEND_BW },
    { "SERVICE_READ_LAT", PERF_TEST_TYPE::SERVICE_READ_LAT },
    { "SERVICE_READ_BW", PERF_TEST_TYPE::SERVICE_READ_BW },
    { "SERVICE_WRITE_LAT", PERF_TEST_TYPE::SERVICE_WRITE_LAT },
    { "SERVICE_WRITE_BW", PERF_TEST_TYPE::SERVICE_WRITE_BW },
};

bool PerfTestConfig::SetType(const std::string &cmd)
{
    for (const auto &item : gPerfTestType) {
        if (PerfTestUtils::IsStringCaseInsensitiveEqual(cmd, item.first)) {
            mType = item.second;
            return true;
        }
    }
    LOG_ERROR("Get perftest type for cmd(" << cmd << ") failed!");
    mType = PERF_TEST_TYPE::UNKNOWN;
    return false;
}

static const std::vector<std::pair<std::string, ock::hcom::UBSHcomNetDriverProtocol>> gPerfTestProtocol = {
    { "RDMA", ock::hcom::UBSHcomNetDriverProtocol::RDMA }, { "TCP", ock::hcom::UBSHcomNetDriverProtocol::TCP },
    { "SHM", ock::hcom::UBSHcomNetDriverProtocol::SHM },   { "UBC", ock::hcom::UBSHcomNetDriverProtocol::UBC },
};

bool PerfTestConfig::SetProtocol(const std::string &cmd)
{
    for (const auto &item : gPerfTestProtocol) {
        if (PerfTestUtils::IsStringCaseInsensitiveEqual(cmd, item.first)) {
            mProtocol = item.second;
            return true;
        }
    }
    LOG_ERROR("Get perftest protocol for cmd(" << cmd << ") failed!");
    mProtocol = ock::hcom::UBSHcomNetDriverProtocol::UNKNOWN;
    return false;
}

PERF_TEST_TYPE PerfTestConfig::GetType() const
{
    return mType;
}

static void HelpInfo(const char *argv0)
{
    std::cout << "[example]" << std::endl;
    std::cout << "hcom_perf --case transport_send_lat --role server --protocol rdma -i 10.10.1.63 -n 1000 -d 0 --all";
    std::cout << std::endl;
    std::cout << "hcom_perf --case transport_send_lat --role client --protocol rdma -i 10.10.1.63 -n 1000 -d 0 --all";
    std::cout << std::endl;
}

void PerfTestConfig::Print()
{
    LOG_DEBUG("mIsServer = " << mIsServer);
    LOG_DEBUG("mIterations = " << mIterations);
    LOG_DEBUG("mSize = " << mSize);
    LOG_DEBUG("oobIp = " << mOobIp);
    LOG_DEBUG("oobPort = " << mOobPort);
    LOG_DEBUG("ipSeg = " << mIpMask);
    LOG_DEBUG("protocol = " << mProtocol);
    LOG_DEBUG("mIsTestAllSize = " << mIsTestAllSize);
    LOG_DEBUG("cpuId = " << mCpuId);
}

PerfTestConfig::PerfTestConfig()
{
    mIsServer = true;
    mIterations = 1000;
    mSize = 1024;
    mOobIp = "";
    mOobPort = 8850;
    mIpMask = "192.168.100.0/24";
    mProtocol = ock::hcom::RDMA;
    mCpuId = -1;
    mIsTestAllSize = false;
}

bool PerfTestConfig::SetIsServer(const std::string &role)
{
    if (PerfTestUtils::IsStringCaseInsensitiveEqual(role, "server")) {
        mIsServer = true;
    } else {
        mIsServer = false;
    }
    return true;
}

bool PerfTestConfig::SelfCheck()
{
    if (GetIterations() >= MAX_ITERATIONS) {
        LOG_WARN("Input Iteration(=" << GetIterations() << ") is larger than MAX_ITERATIONS(=" << MAX_ITERATIONS << ")"
                                     << "iters is set to MAX_ITERATIONS.");
        SetIterations(MAX_ITERATIONS);
    }

    if (GetSize() >= MAX_MESSAGE_SIZE) {
        LOG_WARN("Input size(=" << GetSize() << ") is larger than MAX_MESSAGE_SIZE(=" << MAX_MESSAGE_SIZE << ")"
                                << "size is set to MAX_MESSAGE_SIZE.");
        SetIterations(MAX_ITERATIONS);
    }

    // 如果需要测试所有尺寸，则按照最大尺寸准备缓冲区
    if (GetIsTestAllSize()) {
        SetSize(MAX_MESSAGE_SIZE);
    }
    return true;
}

bool PerfTestConfig::ParseArgs(int argc, char *argv[])
{
    struct option options[] = {
        {"case", required_argument, nullptr, 'C'},
        {"role", required_argument, nullptr, 'R'},
        {"protocol", required_argument, nullptr, 'P'},
        {"ip", required_argument, nullptr, 'i'},
        {"port", optional_argument, nullptr, 'p'},
        {"ipMask", optional_argument, nullptr, 'm'},
        {"all", no_argument, nullptr, 'a'},
        {"size", optional_argument, nullptr, 's'},
        {"iters", optional_argument, nullptr, 'n'},
        {"coreId", optional_argument, nullptr, 'c'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int ret = 0;
    int index = 0;
    char inputChar[] = "C:R:P:i:p:m:a:s:n:c:h";
    while ((ret = getopt_long(argc, argv, inputChar, options, &index)) != -1) {
        switch (ret) {
            case 'C':
                if (!SetType(optarg)) {
                    return false;
                }
                break;
            case 'R':
                if (!SetIsServer(optarg)) {
                    return false;
                }
                break;
            case 'P':
                if (!SetProtocol(optarg)) {
                    return false;
                }
                break;
            case 'i':
                mOobIp = optarg;
                mIpMask = mOobIp + "/16";
                break;
            case 'm':
                // 后续根据需要，扩展支持更多网段
                break;
            case 'p':
                mOobPort = (uint16_t)strtoul(optarg, nullptr, 0);
                break;
            case 'n':
                SetIterations(static_cast<uint32_t>(strtoul(optarg, nullptr, 0)));
                break;
            case 's':
                SetSize(static_cast<uint32_t>(strtoul(optarg, nullptr, 0)));
                break;
            case 'c':
                mCpuId = strtoul(optarg, nullptr, 0);
                break;
            case 'h':
                HelpInfo(argv[0]);
                break;
            case 'a':
                SetIsTestAllSize(true);
                break;
            default:
                break;
        }
    }
    return SelfCheck();
}
}
}