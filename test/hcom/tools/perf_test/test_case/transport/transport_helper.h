/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_TRANSPORT_HELPER_H
#define HCOM_PERF_TEST_TRANSPORT_HELPER_H

#include "hcom/hcom.h"
#include "common/perf_test_common.h"
#include "common/perf_test_config.h"

namespace hcom {
namespace perftest {
using NewEpHandler = std::function<int(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)>;
using EpBrokenHandler = std::function<void(const ock::hcom::UBSHcomNetEndpointPtr &ep)>;
using ReqRecvHandler = std::function<int(const ock::hcom::UBSHcomNetRequestContext &ctx)>;
using OneSideDoneHandler = std::function<int(const ock::hcom::UBSHcomNetRequestContext &ctx)>;
using ReqPostedHandler = std::function<int(const ock::hcom::UBSHcomNetRequestContext &ctx)>;

class TransportHelper {
public:
    TransportHelper(const PerfTestConfig &cfg);
    bool FillNetDriverOption(ock::hcom::UBSHcomNetDriverOptions &opts);
    bool CreateMemoryRegion(MrInfo &mrInfo);

    bool CreateNetDriver();
    void DestroyNetDriver();
    inline ock::hcom::UBSHcomNetDriver *GetNetDriver() const
    {
        return mDriver;
    }

    void RegisterNewEPHandler(const NewEpHandler &handler);
    void RegisterEpBrokenHandler(const EpBrokenHandler &handler);
    void RegisterReqRecvHandler(const ReqRecvHandler &handler);
    void RegisterOneSideDoneHandler(const OneSideDoneHandler &handler);
    void RegisterReqPostedHandler(const ReqPostedHandler &handler);

private:
    PerfTestConfig mCfg;
    ock::hcom::UBSHcomNetDriver *mDriver = nullptr;
    std::vector<ock::hcom::UBSHcomNetMemoryRegionPtr> mMrVector;

    NewEpHandler mNewEpHandler;
    EpBrokenHandler mEpBrokenHandler;
    ReqRecvHandler mReqRecvHandler;
    OneSideDoneHandler mOneSideDoneHandler;
    ReqPostedHandler mReqPostedHandler;
};
}
}

#endif