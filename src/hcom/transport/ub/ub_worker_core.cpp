/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifdef UB_BUILD_ENABLED
#include <unistd.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "ub_worker.h"
#include "net_ub_endpoint.h"

namespace ock {
namespace hcom {
std::string &WorkerTypeToString(UBWorkerType tp)
{
    static std::string workerTypeString[3] = {"sender", "receiver", "sender&receiver"};
    static std::string unknownWorkerType = "unknown worker type";
    if (tp != UB_SENDER && tp != UB_RECEIVER && tp != UB_SENDER_RECEIVER) {
        return unknownWorkerType;
    }
    return workerTypeString[tp];
}

std::string &PollingModeToString(UBPollingMode m)
{
    static std::string workerModeString[2] = {"busy_polling", "cq_event_polling"};
    static std::string unknownWorkerMode = "unknown worker mode";
    if (m != UB_BUSY_POLLING && m != UB_EVENT_POLLING) {
        return unknownWorkerMode;
    }
    return workerModeString[m];
}

UBWorker::UBWorker(const std::string &name, UBContext *ctx, const UBWorkerOptions &options,
    const NetMemPoolFixedPtr &memPool, const NetMemPoolFixedPtr &sglMemPool)
    : mName(name),
      mUBContext(ctx),
      mOpCtxMemPool(memPool),
      mSglCtxMemPool(sglMemPool),
      mOptions(options),
      mProgressThreadStarted(false)
{
    if (mUBContext != nullptr) {
        mUBContext->IncreaseRef();
    }

    mProgressCpuId = options.cpuId;
    mProgressBatchSize = options.pollingBatchSize;
    OBJ_GC_INCREASE(UBWorker);
}

UResult UBWorker::Initialize()
{
    if (mInited) {
        return UB_OK;
    }

    if (mUBContext == nullptr || mUBContext->mUrmaContext == nullptr) {
        NN_LOG_ERROR("UB Context is null, probably not initialized");
        return UB_PARAM_INVALID;
    }

    // create and init CQ
    auto tmpCQ = new (std::nothrow)
        UBJfc(DetailName(), mUBContext, mOptions.workerMode == UB_EVENT_POLLING, reinterpret_cast<uintptr_t>(this));
    if (tmpCQ == nullptr) {
        NN_LOG_ERROR("Failed to new UBJfc in UBWorker " << DetailName() << ", probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }

    tmpCQ->SetJfcCount(mOptions.completionQueueDepth);

    UResult result = UB_OK;
    if ((result = tmpCQ->Initialize()) != UB_OK) {
        NN_LOG_ERROR("Failed to initialize UBJfc in UBWorker " << DetailName() << ", result " << result);
        delete tmpCQ;
        tmpCQ = nullptr;
        return result;
    }

    if ((result = mOpCtxInfoPool.Initialize(mOpCtxMemPool)) != UB_OK) {
        NN_LOG_ERROR("Failed to initialize operation context info pool in UBWorker " << DetailName());
        delete tmpCQ;
        tmpCQ = nullptr;
        return result;
    }

    if ((result = mSglCtxInfoPool.Initialize(mSglCtxMemPool)) != UB_OK) {
        NN_LOG_ERROR("Failed to initialize sgl context info pool in UBWorker " << DetailName());
        delete tmpCQ;
        tmpCQ = nullptr;
        return result;
    }

    if ((result = mJettyPtrMap.Initialize()) != UB_OK) {
        NN_LOG_ERROR("Failed to initialize jetty ptr map in UBWorker " << DetailName());
        delete tmpCQ;
        tmpCQ = nullptr;
        return result;
    }

    mUBJfc = tmpCQ;
    mUBJfc->IncreaseRef();
    mInited = true;
    return UB_OK;
}

UResult UBWorker::UnInitialize()
{
    if (!mInited) {
        return UB_OK;
    }

    if (mUBJfc != nullptr) {
        mUBJfc->DecreaseRef();
        mUBJfc = nullptr;
    }

    if (mUBContext != nullptr) {
        mUBContext->DecreaseRef();
        mUBContext = nullptr;
    }

    if (mOpCtxMemPool != nullptr) {
        mOpCtxMemPool.Set(nullptr);
    }

    mOpCtxInfoPool.UnInitialize();

    mInited = false;
    return UB_OK;
}

UResult UBWorker::ReInitializeCQ()
{
    if (!mInited) {
        return UB_OK;
    }

    if (mUBJfc != nullptr) {
        mUBJfc->DecreaseRef();
        mUBJfc = nullptr;
    }

    // create and init CQ
    auto tmpCQ = new (std::nothrow)
        UBJfc(DetailName(), mUBContext, mOptions.workerMode == UB_EVENT_POLLING, reinterpret_cast<uintptr_t>(this));
    if (tmpCQ == nullptr) {
        NN_LOG_ERROR("Failed to new UBJfc in UBWorker " << DetailName() <<
            " in reinitialization, probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }

    tmpCQ->SetJfcCount(mOptions.completionQueueDepth);

    UResult result = UB_OK;
    if ((result = tmpCQ->Initialize()) != UB_OK) {
        NN_LOG_ERROR("Failed to initialize UBJfc in UBWorker " << DetailName() << ", result " << result);
        delete tmpCQ;
        tmpCQ = nullptr;
        return result;
    }

    mUBJfc = tmpCQ;
    mUBJfc->IncreaseRef();

    return UB_OK;
}

UResult UBWorker::Start()
{
    if (!mInited) {
        NN_LOG_ERROR("Failed to start UBWorker " << DetailName() << " as not initialized");
        return UB_WORKER_NOT_INITIALIZED;
    }

    if (mOptions.dontStartWorkers) {
        NN_LOG_INFO("Do not start workers " << DetailName());
        return UB_OK;
    }

    if ((mOptions.workerType == UB_RECEIVER || mOptions.workerType == UB_SENDER_RECEIVER) &&
        mNewRequestHandler == nullptr) {
        NN_LOG_ERROR("New request handler is not registered yet in UBWorker " << DetailName());
        return UB_WORKER_REQUEST_HANDLER_NOT_SET;
    }

    if ((mOptions.workerType == UB_SENDER || mOptions.workerType == UB_SENDER_RECEIVER) &&
        mSendPostedHandler == nullptr) {
        NN_LOG_ERROR("Send request posted handler is not registered yet in UBWorker " << DetailName());
        return UB_WORKER_SEND_POSTED_HANDLER_NOT_SET;
    }

    if (mOneSideDoneHandler == nullptr) {
        NN_LOG_WARN("One side done handler is not registered yet in UBWorker " << DetailName());
    }

    mNeedStop = false;
    std::thread tmpThread(&UBWorker::RunInThread, this);
    mProgressThread = std::move(tmpThread);
    std::string threadName = "UBWkr" + mIndex.ToString();
    if (pthread_setname_np(mProgressThread.native_handle(), threadName.c_str()) != 0) {
        NN_LOG_WARN("Unable to set name of UBWorker progress thread");
    }

    if (mProgressCpuId != -1) {
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(mProgressCpuId, &cpuSet);
        if (pthread_setaffinity_np(mProgressThread.native_handle(), sizeof(cpuSet), &cpuSet) != 0) {
            NN_LOG_WARN("Unable to bind UBWorker" << mIndex.ToString() << " << to cpu " << mProgressCpuId);
        }
    }

    while (!mProgressThreadStarted.load()) {
        usleep(NN_NO10);
    }

    return UB_OK;
}

UResult UBWorker::Stop()
{
    mNeedStop = true;
    if (mProgressThread.native_handle()) {
        mProgressThread.join();
    }
    return UB_OK;
}

void UBWorker::DoWithBusyPolling()
{
    // allocate wc vector
    auto *wc = static_cast<urma_cr_t *>(calloc(mProgressBatchSize, sizeof(urma_cr_t)));
    if (wc == nullptr) {
        NN_LOG_ERROR("Failed to allocate wc in UBWorker " << DetailName() << ", thread exiting");
        return;
    }

    uint32_t pollCount = 0;
    UBJetty *lastBrokenQp = nullptr;
    urma_cr_status_t lastErrorWcStatus = URMA_CR_SUCCESS;

    while (!mNeedStop) {
        try {
            pollCount = mProgressBatchSize;
            if (BusyPolling(wc, pollCount)) {
                continue;
            }
            TRACE_DELAY_BEGIN(UB_WORKER_BUSY_POLLING);
            ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);
            TRACE_DELAY_END(UB_WORKER_BUSY_POLLING, 0);
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime incorrect signal in UBWorker::RunInThread '" << ex.what() <<
                "', ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown signal in UBWorker::RunInThread, ignore and continue");
        }
    }

    free(wc);
    wc = nullptr;
}

void UBWorker::DoWithCQEventPolling()
{
    // allocate wc vector
    auto *wc = static_cast<urma_cr_t *>(calloc(mProgressBatchSize, sizeof(urma_cr_t)));
    if (wc == nullptr) {
        NN_LOG_ERROR("Failed to allocate wc in UBWorker " << DetailName() << ", thread exiting");
        return;
    }

    uint32_t pollCount = 0;
    uint32_t pollTimeOut = 0;
    UBJetty *lastBrokenQp = nullptr;
    urma_cr_status_t lastErrorWcStatus = URMA_CR_SUCCESS;

    while (!mNeedStop) {
        try {
            pollCount = mProgressBatchSize;
            pollTimeOut = mOptions.eventPollingTimeout;
            if (CqEventPolling(wc, pollCount, pollTimeOut)) {
                continue;
            }
            TRACE_DELAY_BEGIN(UB_WORKER_EVENT_POLLING);
            ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);
            TRACE_DELAY_END(UB_WORKER_EVENT_POLLING, 0);
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime incorrect signal in UB worker thread '" << ex.what() << "', ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown signal in UB worker thread, ignore and continue");
        }
    }

    free(wc);
    wc = nullptr;
}

void UBWorker::RunInThread()
{
    if (mOptions.threadPriority != 0) {
        if (NN_UNLIKELY(setpriority(PRIO_PROCESS, 0, mOptions.threadPriority) != 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_WARN("Unable to set worker thread priority in ub worker " << mName << ", as " <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    mProgressThreadStarted.store(true);
    NN_LOG_INFO("UBWorker " << DetailName() << ", cpuId: " << mProgressCpuId << ", cq count: " <<
        ((mUBJfc != nullptr) ? mUBJfc->GetCQCount() : 0) << ", polling batch size: " << mProgressBatchSize <<
        ", more " << mOptions.ToString() << "] working thread started");

    if (mOptions.workerMode == UB_BUSY_POLLING) {
        DoWithBusyPolling();
    } else if (mOptions.workerMode == UB_EVENT_POLLING) {
        DoWithCQEventPolling();
    } else {
        NN_LOG_ERROR("Un-reachable");
    }

    NN_LOG_INFO("UBWorker " << DetailName() << " working thread exiting");
}

UResult UBWorker::Create(const std::string &name, UBContext *ctx, const UBWorkerOptions &options,
    NetMemPoolFixedPtr memPool, NetMemPoolFixedPtr sglMemPool, UBWorker *&outWorker)
{
    if (ctx == nullptr || name.empty()) {
        NN_LOG_ERROR("Failed to create ub worker as ctx is nullptr or name empty");
        return UB_PARAM_INVALID;
    }

    auto tmp = new (std::nothrow) UBWorker(name, ctx, options, std::move(memPool), std::move(sglMemPool));
    if (tmp == nullptr) {
        NN_LOG_ERROR("Failed to create UBWorker, probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }

    outWorker = tmp;
    return UB_OK;
}

UResult UBWorker::CreateQP(UBJetty *&qp)
{
    if (NN_UNLIKELY(!mInited)) {
        NN_LOG_ERROR("Failed to create qp with UBWorker " << DetailName() << " as not initialized");
        return UB_WORKER_NOT_INITIALIZED;
    }

    JettyOptions jettyOptions(mOptions.qpSendQueueSize, mOptions.qpReceiveQueueSize, mOptions.qpMrSegSize,
        mOptions.qpMrSegCount, mOptions.slave, mOptions.ubcMode);
    qp = new (std::nothrow) UBJetty(DetailName(), UBJetty::NewId(), mUBContext, mUBJfc, jettyOptions);
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to create qp with UBWorker " << DetailName() << ", probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }

    qp->SetUpContext1(reinterpret_cast<uintptr_t>(this));
    return UB_OK;
}
}
}
#endif
