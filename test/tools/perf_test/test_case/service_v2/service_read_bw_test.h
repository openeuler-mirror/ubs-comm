/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_SERVICE_READ_BW_H
#define HCOM_PERF_TEST_SERVICE_READ_BW_H
#include <semaphore.h>
#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/service_v2/service_helper.h"

namespace hcom {
namespace perftest {
class ServiceReadBwTest : public PerfTestBase {
public:
    ServiceReadBwTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();
    int DoPostRead();
    int NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch, const std::string &payload);
    int RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx);
    bool RegMemory();
    bool ExchangeAddress();

private:
    bool SetPerfTestContext(PerfTestContext *ctx)
    {
        if (ctx == nullptr) {
            return false;
        }
        mCtx = ctx;
        return true;
    }

    PerfTestContext *GetPerfTestContext() const
    {
        return mCtx;
    }
    PerfTestContext *mCtx = nullptr;

private:
    ock::hcom::UBSHcomChannelPtr mCh = nullptr;
    ock::hcom::UBSHcomOneSideRequest mReq;
    ServiceHelper mHelper;
    RegMrInfo mPostMrInfo;
    RegMrInfo mPeerMrInfo;
    sem_t mSem;
};
}
}

#endif
