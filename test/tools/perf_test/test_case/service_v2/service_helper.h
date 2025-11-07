/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_SERVICE_HELPER_H
#define HCOM_SERVICE_HELPER_H

#include "hcom/hcom_service.h"
#include "hcom/hcom_service_context.h"
#include "hcom/hcom_service_channel.h"
#include "hcom/hcom.h"
#include "common/perf_test_common.h"
#include "common/perf_test_config.h"

namespace hcom {
namespace perftest {
using UBSHcomServiceNewChannelHandler =
    std::function<int(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &, const std::string &payload)>;
using UBSHcomServiceChannelBrokenHandler = std::function<void(const ock::hcom::UBSHcomChannelPtr &)>;
using UBSHcomServiceRecvHandler = std::function<int(ock::hcom::UBSHcomServiceContext &)>;
using UBSHcomServiceSendHandler = std::function<int(const ock::hcom::UBSHcomServiceContext &)>;
using UBSHcomServiceOneSideDoneHandler = std::function<int(const ock::hcom::UBSHcomServiceContext &)>;

class RegMrInfo {
public:
    uintptr_t lAddress = 0;
    ock::hcom::UBSHcomMemoryKey lKey;
    uint32_t size = 0;
};

class ServiceHelper {
public:
    ServiceHelper(const PerfTestConfig &cfg);
    bool CreateMemoryRegion(RegMrInfo &mrInfo);
    bool CreateService();
    void DestroyService();
    inline ock::hcom::UBSHcomService *GetNetService() const
    {
        return mService;
    }
    void RegisterNewChHandler(const UBSHcomServiceNewChannelHandler &handler);
    void RegisterChBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler);
    void RegisterRecvHandler(const UBSHcomServiceRecvHandler &handler);
    void RegisterSendHandler(const UBSHcomServiceSendHandler &handler);
    void RegisterOneSideDoneHandler(const UBSHcomServiceOneSideDoneHandler &handler);

private:
    PerfTestConfig mCfg;
    ock::hcom::UBSHcomService *mService = nullptr;
    std::vector<ock::hcom::UBSHcomRegMemoryRegion> mMrVector;

    UBSHcomServiceNewChannelHandler mNewChHandler;
    UBSHcomServiceChannelBrokenHandler mChBrokenHandler;
    UBSHcomServiceRecvHandler mRecvHandler;
    UBSHcomServiceSendHandler mSendHandler;
    UBSHcomServiceOneSideDoneHandler mOneSideDoneHandler;
};
}
}
#endif // HCOM_SERVICE_HELPER_H
