/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_TRANSPORT_SEND_LAT_H
#define HCOM_PERF_TEST_TRANSPORT_SEND_LAT_H

#include <semaphore.h>

#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/transport/transport_helper.h"


namespace hcom {
namespace perftest {
class TransportSendLatTest : public PerfTestBase {
public:
    explicit TransportSendLatTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();
    int DoPostSend();
    int NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload);
    int RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx);
    int OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx);

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
    ock::hcom::UBSHcomNetEndpointPtr mEp = nullptr;
    TransportHelper mHelper;

    char *mDataAddr = nullptr;
    sem_t mSem;
};
}
}

#endif
