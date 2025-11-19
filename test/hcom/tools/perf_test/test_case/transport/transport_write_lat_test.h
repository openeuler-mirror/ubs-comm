/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef HCOM_TRANSPORT_READ_LAT_TEST_H
#define HCOM_TRANSPORT_READ_LAT_TEST_H

#include <semaphore.h>
#include <atomic>
#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/transport/transport_helper.h"


namespace hcom {
namespace perftest {
class TransportWriteLatTest : public PerfTestBase {
public:
    explicit TransportWriteLatTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();

    inline int DoPostWrite()
    {
        PerfTestContext *ctx = GetPerfTestContext();
        volatile uint8_t *pollData = reinterpret_cast<uint8_t *>(mPollMrInfo.lAddress + ctx->mSize - 1);
        volatile uint8_t *postData = reinterpret_cast<uint8_t *>(mPostMrInfo.lAddress + ctx->mSize - 1);
        uint64_t num = 0;
        *pollData = num;
        *postData = num;
        ctx->cnt = 0;
        rcnt = 0;
        ccnt.store(0);
        while (ctx->cnt < ctx->mIterations || rcnt < ctx->mIterations || ccnt.load() < ctx->mIterations) {
            if (rcnt < ctx->mIterations && !(ctx->cnt < 1 && !mCfg.GetIsServer())) {
                rcnt++;
                while ((*pollData != rcnt % UINT8_MAX) && ctx->cnt < ctx->mIterations) {
                }
            }
            if (ctx->cnt < ctx->mIterations) {
                ++ctx->cnt;
                ctx->tposted[ctx->cnt - 1] = ock::hcom::MONOTONIC_TIME_NS();
                *postData = ctx->cnt % UINT8_MAX;
                int res = mEp->PostWrite(mReq);
                if (res != 0) {
                    LOG_ERROR("failed to send to server");
                    return -1;
                }
            }
            while (ccnt.load() != ctx->cnt) {
            }
        }
        if (ctx->cnt == ctx->mIterations) {
            ctx->tposted[ctx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
            LOG_DEBUG("One Iteration Done!");
            sem_post(&mSem);
        }
        return 0;
    }

    int NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload);
    int RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx);
    int OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx);
    int EpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep);
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
    TransportHelper mHelper;
    ock::hcom::UBSHcomNetTransRequest mReq;
    MrInfo mPostMrInfo;
    MrInfo mPollMrInfo;
    MrInfo mPeerMrInfo;
    uint64_t rcnt = 0;
    std::atomic<uint64_t> ccnt{ 0 };
    std::atomic<bool> isConnect{ false };
    sem_t mSem;
};
}
}

#endif // HCOM_TRANSPORT_READ_LAT_TEST_H
