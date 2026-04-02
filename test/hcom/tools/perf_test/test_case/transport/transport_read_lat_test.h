/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef HCOM_TRANSPORT_READ_LAT_TEST_H
#define HCOM_TRANSPORT_READ_LAT_TEST_H

#include <semaphore.h>

#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/transport/transport_helper.h"


namespace hcom {
namespace perftest {
class TransportReadLatTest : public PerfTestBase {
public:
    explicit TransportReadLatTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();

    inline int DoPostRead()
    {
        if (mCtx->cnt < mCtx->mIterations) {
            mCtx->tposted[mCtx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
            int res = mEp->PostRead(mReq);
            if (res != 0) {
                LOG_ERROR("failed to send to server");
            }
            ++mCtx->cnt;
            return 0;
        }

        mCtx->tposted[mCtx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
        LOG_DEBUG("One Iteration Done!");
        sem_post(&mSem);
        return 0;
    }

    int NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload);
    int RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx);
    int OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx);
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
    ock::hcom::UBSHcomNetEndpointPtr mEp = nullptr;
    ock::hcom::UBSHcomNetTransRequest mReq;
    TransportHelper mHelper;
    MrInfo clientMrInfo;
    MrInfo serverMrInfo;
    sem_t mSem;
    void *mTseg = nullptr;
};
}
}

#endif // HCOM_TRANSPORT_READ_LAT_TEST_H
