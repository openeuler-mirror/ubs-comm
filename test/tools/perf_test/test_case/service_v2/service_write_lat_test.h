/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_SERVICE_WRITE_LAT_H
#define HCOM_PERF_TEST_SERVICE_WRITE_LAT_H
#include <semaphore.h>
#include "hcom/hcom.h"
#include "test_case/perf_test_base.h"
#include "test_case/service_v2/service_helper.h"

namespace hcom {
namespace perftest {
class ServiceWriteLatTest : public PerfTestBase {
public:
    ServiceWriteLatTest(const PerfTestConfig &cfg) : PerfTestBase(cfg), mHelper(cfg){};
    bool Initialize() override;
    void UnInitialize() override;
    bool RunTest(PerfTestContext *ctx) override;

private:
    bool Connect();

    inline int DoPostWrite()
    {
        volatile uint64_t *pollData = reinterpret_cast<uint64_t *>(mPollMrInfo.lAddress);
        volatile uint64_t *postData = reinterpret_cast<uint64_t *>(mPostMrInfo.lAddress);
        uint64_t num = 0;
        *pollData = num;
        *postData = num;
        PerfTestContext *ctx = GetPerfTestContext();
        ctx->cnt = 0;
        rcnt = 0;
        ccnt.store(0);
        while (ctx->cnt < ctx->mIterations || rcnt < ctx->mIterations ||
            static_cast<uint64_t>(ccnt.load()) < ctx->mIterations) {
            if (rcnt < ctx->mIterations && !(ctx->cnt < 1 && !mCfg.GetIsServer())) {
                rcnt++;
                while ((*pollData != rcnt) && ctx->cnt < ctx->mIterations)
                    ;
            }
            if (ctx->cnt < ctx->mIterations) {
                ++ctx->cnt;
                ock::hcom::Callback *newCallback = ock::hcom::UBSHcomNewCallback(
                    [this](ock::hcom::UBSHcomServiceContext &context) { this->ccnt.fetch_add(1); }, std::placeholders::_1);
                if (newCallback == nullptr) {
                    LOG_ERROR("Create callback failed");
                    sem_post(&mSem);
                    return -1;
                }
                *postData = ctx->cnt;
                ctx->tposted[mCtx->cnt - 1] = ock::hcom::MONOTONIC_TIME_NS();
                int res = mCh->Put(mReq, newCallback);
                if (res != 0) {
                    if (newCallback != nullptr) {
                        delete newCallback;
                    }
                    LOG_ERROR("failed to write to server");
                    sem_post(&mSem);
                    return res;
                }
            }

            while (ctx->cnt != static_cast<uint64_t>(ccnt.load()))
                ;
        }
        if (ctx->cnt == ctx->mIterations) {
            ctx->tposted[ctx->cnt] = ock::hcom::MONOTONIC_TIME_NS();
            LOG_DEBUG("One Iteration Done!");
            sem_post(&mSem);
        }
        return 0;
    }

    int NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch, const std::string &payload);
    int RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx);
    void ChannelBroken(const ock::hcom::UBSHcomChannelPtr &ch);
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
    RegMrInfo mPollMrInfo;
    RegMrInfo mPeerMrInfo;
    uint64_t rcnt = 0;
    std::atomic<int> ccnt{ 0 };
    std::atomic<bool> isConnect{ false };
    sem_t mSem;
};
}
}

#endif
