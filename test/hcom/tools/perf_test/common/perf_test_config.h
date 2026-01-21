/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_CONFIG_H
#define HCOM_PERF_TEST_CONFIG_H

#include <unistd.h>
#include <getopt.h>
#include <cstdint>

#include "hcom/hcom.h"

namespace hcom {
namespace perftest {
constexpr uint32_t MAX_MESSAGE_SIZE = 2097152; // 2MB = 2^21B
// option中的mrSendReceiveSegSize要配置为 MAX_MESSAGE_SIZE + HCOM_HEADER_SIZE
// sizeof(UBSHcomNetTransHeader) = 32
constexpr uint32_t HCOM_HEADER_SIZE = 1024;     // 需要内存对齐
constexpr uint32_t MAX_ITERATIONS = 200000;

enum class PERF_TEST_TYPE {
    // 固定使用偶数枚举值代表时延，奇数枚举值代表带宽
    TRANSPORT_SEND_LAT = 0,
    TRANSPORT_SEND_BW = 1,
    TRANSPORT_READ_LAT = 2,
    TRANSPORT_READ_BW = 3,
    TRANSPORT_WRITE_LAT = 4,
    TRANSPORT_WRITE_BW = 5,

    SERVICE_SEND_LAT = 100,
    SERVICE_SEND_BW = 101,
    SERVICE_READ_LAT = 102,
    SERVICE_READ_BW = 103,
    SERVICE_WRITE_LAT = 104,
    SERVICE_WRITE_BW = 105,

    UNKNOWN = 0xFFFF
};

class PerfTestConfig {
public:
    PerfTestConfig();
    bool ParseArgs(int argc, char *argv[]);

    std::string GetOobIp() const
    {
        return mOobIp;
    }

    uint16_t GetOobPort() const
    {
        return mOobPort;
    }

    bool SetProtocol(const std::string &cmd);
    ock::hcom::UBSHcomNetDriverProtocol GetProtocol() const
    {
        return mProtocol;
    }
    void Print();

    bool SetType(const std::string &cmd);
    PERF_TEST_TYPE GetType() const;

    bool SetIsServer(const std::string &cmd);
    bool GetIsServer() const
    {
        return mIsServer;
    }

    void SetIterations(uint64_t iters)
    {
        mIterations = iters;
    }

    uint64_t GetIterations() const
    {
        return mIterations;
    }

    void SetSize(uint32_t size)
    {
        mSize = size;
    }

    uint32_t GetSize() const
    {
        return mSize;
    }

    void SetIsTestAllSize(bool flag)
    {
        mIsTestAllSize = flag;
    }

    bool GetIsTestAllSize() const
    {
        return mIsTestAllSize;
    }

    bool GetIsBwNoPeak()
    {
        // 不确定该参数对bw结果的影响，固定返回false
        return false;
    }

    void SetCpuId(int16_t cpuId)
    {
        mCpuId = cpuId;
    }

    int32_t GetCpuId() const
    {
        return mCpuId;
    }

    std::string GetIpMask() const
    {
        return mIpMask;
    }

private:
    // 检查参数有效性
    bool SelfCheck();

private:
    PERF_TEST_TYPE mType;                   // 测试类型
    ock::hcom::UBSHcomNetDriverProtocol mProtocol; // 底层通信协议类型
    bool mIsServer;                         // 是否为server
    std::string mOobIp;                     // server OOB Ip
    uint16_t mOobPort;                      // server OOB Port
    std::string mIpMask;                    // OOB网段，client server相同
    int16_t mCpuId;                         // 亲和性配置，-1为不绑核
    uint64_t mIterations;                   // 测试次数
    uint32_t mSize;                         // 测试的最大包大小
    bool mIsTestAllSize = false;            // 是否测试所有大小的包
};
}
}
#endif