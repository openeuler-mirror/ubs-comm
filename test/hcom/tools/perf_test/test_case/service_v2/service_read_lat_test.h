/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_SERVICE_READ_LAT_H
#define HCOM_PERF_TEST_SERVICE_READ_LAT_H
#include <semaphore.h>
#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/service_v2/service_helper.h"

namespace hcom {
namespace perftest {
class ServiceReadLatTest : public PerfTestBase {
public:
    ServiceReadLatTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();

    inline int DoPostRead()
    {
        if (mCtx == nullptr) {
            LOG_ERROR("mCtx is nullptr");
            sem_post(&mSem);
            return -1;
        }
        if (mCh == nullptr) {
            LOG_ERROR("mCh is nullptr");
            sem_post(&mSem);
            return -1;
        }

        const uint64_t iters = mCtx->mIterations;
        rcnt.store(0);
        mCtx->cnt = 0;
        while (mCtx->cnt < iters) {
            mCtx->tposted[mCtx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
            ock::hcom::Callback *newCallback = ock::hcom::UBSHcomNewCallback(
                [this](ock::hcom::UBSHcomServiceContext &context) {
                    this->rcnt.fetch_add(1);
                },
                std::placeholders::_1);
            if (newCallback == nullptr) {
                LOG_ERROR("Create callback failed");
                sem_post(&mSem);
                return -1;
            }
            int res = mCh->Get(mReq, newCallback);
            if (res != 0) {
                LOG_ERROR("Get failed at iteration " << mCtx->cnt);
                sem_post(&mSem);
                return -1;
            }
            ++mCtx->cnt;
            while (mCtx->cnt != static_cast<uint64_t>(rcnt.load()))
                ;
        }
        mCtx->tposted[mCtx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
        LOG_DEBUG("One Iteration Done!");
        sem_post(&mSem);
        return 0;
    }

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
    std::atomic<uint64_t> rcnt{ 0 };
    ServiceHelper mHelper;
    RegMrInfo mPostMrInfo;
    RegMrInfo mPeerMrInfo;
    sem_t mSem;
};
}
}

#endif
