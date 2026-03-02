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
#include "ub_urma_wrapper_public_jetty.h"

namespace ock {
namespace hcom {

std::string EidToStr(urma_eid_t urmaEid)
{
    std::string str = "";
    for (int i = 0; i < URMA_EID_SIZE; i++) {
        str += std::to_string(urmaEid.raw[i]);
    }
    return str;
}


uint32_t UBPublicJetty::G_INDEX = 1;

// public jetty import remote jetty
UResult UBPublicJetty::ImportPublicJetty(const urma_eid_t &remoteEid, uint32_t jettyId)
{
    // import remote jetty
    urma_rjetty_t remoteJetty{};
    remoteJetty.jetty_id.id = jettyId;
    remoteJetty.jetty_id.eid = remoteEid;
    remoteJetty.trans_mode = URMA_TM_RM;
    remoteJetty.type = URMA_JETTY;
    urma_token_t token{0};

    NN_LOG_INFO("Local public jetty id: " << mUrmaJetty->jetty_id.id << ", local eid: " <<
        EidToStr(mUrmaJetty->jetty_id.eid) << "; Remote public jetty id: " << remoteJetty.jetty_id.id <<
        ", remote eid: " << EidToStr(remoteJetty.jetty_id.eid));
    mTargetJetty = HcomUrma::ImportJetty(mUBContext->mUrmaContext, &remoteJetty, &token);
    if (mTargetJetty == nullptr) {
        NN_LOG_ERROR("Failed to import public jetty");
        return UB_QP_IMPORT_FAILED;
    }
    NN_LOG_INFO("Local public jetty id: " << mUrmaJetty->jetty_id.id << ", local eid: " <<
        EidToStr(mUrmaJetty->jetty_id.eid) << "; Remote public jetty id: " << mTargetJetty->id.id << ", remote eid: " <<
        EidToStr(mTargetJetty->id.eid));

    return UB_OK;
}

void UBPublicJetty::FillJfsCfg(urma_jfs_cfg_t *jfs_cfg)
{
    jfs_cfg->user_ctx = reinterpret_cast<uint64_t>(this);
    jfs_cfg->jfc = mSendJfc->mUrmaJfc;
    jfs_cfg->trans_mode = URMA_TM_RM;
    jfs_cfg->depth = JETTY_MAX_SEND_WR;
    jfs_cfg->max_sge = static_cast<uint8_t>(mUBContext->mMaxSge);
    jfs_cfg->flag.value = 0;
    jfs_cfg->flag.bs.multi_path = 1;
}

void UBPublicJetty::FillJfrCfg(urma_jfr_cfg_t *jfr_cfg)
{
    jfr_cfg->user_ctx = reinterpret_cast<uint64_t>(this);
    jfr_cfg->jfc = mRecvJfc->mUrmaJfc;
    jfr_cfg->trans_mode = URMA_TM_RM;
    jfr_cfg->depth = JETTY_MAX_RECV_WR;
    jfr_cfg->max_sge = static_cast<uint8_t>(mUBContext->mMaxSge);
    jfr_cfg->id = 0;
    jfr_cfg->flag.bs.tag_matching = URMA_NO_TAG_MATCHING;
}

// create a public jetty
UResult UBPublicJetty::CreateUrmaPublicJetty(uint32_t id)
{
    if (mUBContext == nullptr || mUBContext->mUrmaContext == nullptr || mSendJfc == nullptr ||
        mSendJfc->mUrmaJfc == nullptr) {
        NN_LOG_ERROR("Invalid parameter for jetty creating");
        return UB_PARAM_INVALID;
    }

    // jfs cfg
    urma_jfs_cfg_t jfs_cfg{};
    FillJfsCfg(&jfs_cfg);
    // jfr cfg
    urma_jfr_cfg_t jfr_cfg{};
    FillJfrCfg(&jfr_cfg);
    // jetty flag
    urma_jetty_flag_t jetty_flag{};
    jetty_flag.bs.share_jfr = 1;
    // jetty cfg
    urma_jetty_cfg_t jetty_cfg{};
    jetty_cfg.id = id; // 非0，公知jetty
    jetty_cfg.flag = jetty_flag;
    jetty_cfg.jfs_cfg = jfs_cfg;
    jetty_cfg.jfr_cfg = &jfr_cfg;
    // create jetty
    urma_jetty_t *tmpJetty = nullptr;
    mJfr = HcomUrma::CreateJfr(mUBContext->mUrmaContext, &jfr_cfg);
    if (mJfr == nullptr) {
        NN_LOG_ERROR("urma create jfr failed");
        return UB_PARAM_INVALID;
    }

    jetty_cfg.shared.jfc = mRecvJfc->mUrmaJfc;
    jetty_cfg.shared.jfr = mJfr;
    tmpJetty = HcomUrma::CreateJetty(mUBContext->mUrmaContext, &jetty_cfg);
    if (tmpJetty == nullptr) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create urma jetty for UBJetty " << mName << ", errno " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        HcomUrma::DeleteJfr(mJfr);
        mJfr = nullptr;
        return UB_QP_CREATE_FAILED;
    }
    mUrmaJetty = tmpJetty;
    mUrmaJettyId = mUrmaJetty->jetty_id.id;
    NN_LOG_INFO("Create public jetty success, jetty id: " << mUrmaJettyId << ", local eid: " <<
        EidToStr(mUrmaJetty->jetty_id.eid) << ", jfr id: " << mJfr->jfr_id.id << ", recv jfc id: " <<
        mRecvJfc->mUrmaJfc->jfc_id.id << ", send jfc id: " << mSendJfc->mUrmaJfc->jfc_id.id << ", multi_path: " <<
        mUrmaJetty->jetty_cfg.jfs_cfg.flag.bs.multi_path);
    return UB_OK;
}

// create jetty mr for public jetty
UResult UBPublicJetty::CreateJettyMr()
{
    NResult result = NN_OK;
    uint32_t segCount = isServer ? NN_NO32 : NN_NO8;
    // create mr pool for send/receive and initialize
    if ((result = UBMemoryRegionFixedBuffer::Create(mName, mUBContext, PUBLIC_JETTY_SEG_SIZE, segCount, 0, mJettyMr))
        != 0) {
        NN_LOG_ERROR("Failed to create mr for send/receive in public jetty " << mName << ", result " << result);
        return result;
    }
    mJettyMr->IncreaseRef();
    if ((result = mJettyMr->Initialize()) != 0) {
        NN_LOG_ERROR("Failed to initialize mr for send/receive in public jetty " << mName << ", result " << result);
        return result;
    }

    return UB_OK;
}

UResult UBPublicJetty::CreateCtxInfoPool()
{
    uint16_t blkSize = NN_NextPower2(sizeof(UBOpContextInfo));
    uint16_t blkCnt = isServer ? NN_NO32 : NN_NO8;

    mCtxInfoPool = new (std::nothrow) UBFixedMemPool(blkSize, blkCnt);
    if (mCtxInfoPool == nullptr) {
        NN_LOG_ERROR("Failed to create context info pool for public jetty probably out of memory");
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    mCtxInfoPool->IncreaseRef();
    auto result = mCtxInfoPool->Initialize();
    if (result != UB_OK) {
        NN_LOG_ERROR("Failed to initialize context info pool for public jetty");
        mCtxInfoPool->UnInitialize();
    }

    return result;
}

// inirialzie public jetty
UResult UBPublicJetty::InitializePublicJetty(uint32_t id)
{
    auto result = CreateJettyMr();
    if (result != UB_OK) {
        NN_LOG_ERROR("Failed to create jetty mr in public jetty");
        return result;
    }
    if ((result = CreateCtxInfoPool()) != UB_OK) {
        NN_LOG_ERROR("Failed to create context info pool in public jetty");
        return result;
    }
    result = CreateUrmaPublicJetty(id);
    if (result != UB_OK) {
        NN_LOG_ERROR("Failed to create um jetty in public jetty");
        return result;
    }
    if (isServer) {
        mThreadPool = new (std::nothrow) UBThreadPool(NN_NO16);
        if (NN_UNLIKELY(mThreadPool == nullptr)) {
            NN_LOG_ERROR("Create ub thread pool failed");
            return UB_ERROR;
        }
        mThreadPool->Initialize();
    }
    return UB_OK;
}

// start public jetty
UResult UBPublicJetty::StartPublicJetty()
{
    if (mIsStarted) {
        return UB_OK;
    }
    mIsStarted = true;
    uintptr_t mrBufAddress = 0;
    uint32_t prePostCount = isServer ? NN_NO32 : NN_NO4;
    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create mr address array in Driver " << mName << ", probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }
    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);
    if (!mJettyMr->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("failed to get free mr from pool, mr is not enough");
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    urma_target_seg_t *localSeg = reinterpret_cast<urma_target_seg_t *>(mJettyMr->GetMemorySeg());
    uint32_t i = 0;
    for (; i < prePostCount; i++) {
        uintptr_t buf = 0;
        if (NN_UNLIKELY(!mCtxInfoPool->GetFreeBuffer(buf))) {
            NN_LOG_ERROR("Failed to get a free context info buffer from pool");
            mJettyMr->ReturnBuffer(mrBufAddress);
            return UB_MEMORY_ALLOCATE_FAILED;
        }
        auto *ctx = reinterpret_cast<UBOpContextInfo *>(buf);
        bzero(ctx, sizeof(UBOpContextInfo));
        ctx->mrMemAddr = mrSegs[i];
        ctx->dataSize = PUBLIC_JETTY_SEG_SIZE;
        ctx->localSeg = localSeg;
        ctx->opType = UBOpContextInfo::RECEIVE;
        ctx->opResultType = UBOpContextInfo::SUCCESS;

        if (PostReceive(mrSegs[i], PUBLIC_JETTY_SEG_SIZE, localSeg, reinterpret_cast<uint64_t>(ctx)) != 0) {
            NN_LOG_ERROR("Failed to postrecv in start public jetty");
            mJettyMr->ReturnBuffer(ctx->mrMemAddr);
            mCtxInfoPool->ReturnBuffer(reinterpret_cast<uintptr_t>(ctx));
            return UB_ERROR;
        }
    }
    if (isServer) {
        mNeedStop = false;
        std::thread tmpThread(&UBPublicJetty::RunInThread, this);
        mPublicJettyPollingThread = std::move(tmpThread);
    }

    return UB_OK;
}

int UBPublicJetty::NewRequest(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->mrMemAddr == 0)) {
        NN_LOG_ERROR("Ctx or mrMemAddr is null in public jetty");
        return NN_ERROR;
    }
    auto exchangeInfo = reinterpret_cast<JettyConnHeader *>(ctx->mrMemAddr);
    auto msgType = exchangeInfo->msgType;
    switch (msgType) {
        case (UrmaConnectMsgType::CONNECT_REQ):
            mNewConnectionHandler(ctx);
            break;
        case (UrmaConnectMsgType::EXCHANGE_MSG):
            break;
        default:
            NN_LOG_ERROR("exchangeInfo invalid msgType " << exchangeInfo->msgType);
    }

    ctx->opType = UBOpContextInfo::RECEIVE;
    ctx->opResultType = UBOpContextInfo::SUCCESS;

    if (PostReceive(ctx->mrMemAddr, PUBLIC_JETTY_SEG_SIZE, GetMemorySeg(), reinterpret_cast<uint64_t>(ctx)) != 0) {
        NN_LOG_ERROR("Failed to post receive in new request handler");
        mJettyMr->ReturnBuffer(ctx->mrMemAddr);
        mCtxInfoPool->ReturnBuffer(reinterpret_cast<uintptr_t>(ctx));
        return UB_QP_POST_RECEIVE_FAILED;
    }
    return UB_OK;
}

int UBPublicJetty::SendFinished(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(!ReturnBuffer(ctx->mrMemAddr))) {
        NN_LOG_ERROR("Failed to return buffer mr to jetty mr pool");
    }

    if (NN_UNLIKELY(!mCtxInfoPool->ReturnBuffer(reinterpret_cast<uintptr_t>(ctx)))) {
        NN_LOG_ERROR("Failed to return context info in public jetty");
    }

    return UB_OK;
}

void UBPublicJetty::ProcessWorkerCompletion(UBOpContextInfo *ctx)
{
    NN_LOG_INFO("Start process worker completion thread id: " << pthread_self());
    switch (ctx->opType) {
        case (UBOpContextInfo::OpType::RECEIVE):
            NewRequest(ctx);
            break;
        default:
            NN_LOG_ERROR("Poll cq invalid OpType " << ctx->opType);
    }
    NN_LOG_INFO("End process worker completion thread id: " << pthread_self());
}

void UBPublicJetty::ProcessPollingResult(urma_cr_t &wc)
{
    UBOpContextInfo *ctx = nullptr;
    ctx = reinterpret_cast<UBOpContextInfo *>(wc.user_ctx);
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Ctx is null in public jetty polling");
        return;
    }
    ctx->dataSize = wc.completion_len;
    if (mThreadPool == nullptr) {
        NN_LOG_ERROR("Failed to submit conn task as ub thrad pool not initialize");
        return;
    }
    // optimize to thread poll in next version
    mThreadPool->Submit([this, ctx]() {
        this->ProcessWorkerCompletion(ctx);
    });
}

// public jetty polling thread
void UBPublicJetty::RunInThread()
{
    NN_LOG_INFO("OOB server public jetty accept thread started success, load balancer " <<
        (mWorkerLb == nullptr ? "null" : mWorkerLb->ToString()));
    urma_cr_t wc{};
    uint32_t pollCount = 0;
    while (!mNeedStop) {
        try {
            pollCount = 1;
            // avoid urma event poll zero cqe bug
            mRecvJfc->ProgressV(&wc, pollCount);
            if (pollCount != 0) {
                ProcessPollingResult(wc);
            }
            usleep(NN_NO100000); // 100ms
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime incorrect signal in UBWorker::RunInThread '" << ex.what() <<
                "', ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown signal in UBWorker::RunInThread, ignore and continue");
        }
    }
}

inline void FillSendWr(urma_jfs_wr_t &wr, uint64_t ctx, urma_sge_t *localSge, urma_target_jetty_t *targetJetty)
{
    wr.user_ctx = reinterpret_cast<uint64_t>(ctx);
    wr.send.src.sge = localSge;
    wr.send.src.num_sge = 1;
    wr.send.imm_data = 0;
    wr.next = nullptr;
    wr.opcode = URMA_OPC_SEND;
    wr.flag.bs.complete_enable = 1;
    wr.tjetty = targetJetty;
}
// send a message to target jetty
UResult UBPublicJetty::SendByPublicJetty(const void *buf, uint32_t size)
{
    if (NN_UNLIKELY(mUrmaJetty == nullptr || mTargetJetty == nullptr)) {
        NN_LOG_ERROR("Failed to send by public jetty as local jetty or target jetty is nullptr");
        return UB_QP_NOT_INITIALIZED;
    }

    uintptr_t mrBufAddress = 0;
    NResult res = NN_OK;
    if (!mJettyMr->GetFreeBuffer(mrBufAddress)) {
        NN_LOG_ERROR("failed to get free mr from pool, mr is not enough");
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    urma_target_seg_t *localSeg = GetMemorySeg();
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress), PUBLIC_JETTY_SEG_SIZE, buf, size) != 0)) {
        NN_LOG_ERROR("Failed to copy oob port range");
        return UB_PARAM_INVALID;
    }

    uintptr_t ctxBuffer = 0;
    if (NN_UNLIKELY(!mCtxInfoPool->GetFreeBuffer(ctxBuffer))) {
        NN_LOG_ERROR("Failed to get a free context info buffer from pool");
        mJettyMr->ReturnBuffer(mrBufAddress);
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    auto *ctx = reinterpret_cast<UBOpContextInfo *>(ctxBuffer);
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to get ctx in public jetty");
        return UB_QP_CTX_FULL;
    }
    ctx->mrMemAddr = mrBufAddress;
    ctx->dataSize = size;
    ctx->localSeg = localSeg;
    ctx->opType = UBOpContextInfo::SEND;
    ctx->opResultType = UBOpContextInfo::SUCCESS;

    urma_jfs_wr_t *bad_wr;
    urma_sge_t local_sge{};
    local_sge.addr = mrBufAddress;
    local_sge.len = size;
    local_sge.tseg = localSeg;

    urma_jfs_wr_t wr{};
    FillSendWr(wr, reinterpret_cast<uint64_t>(ctx), &local_sge, mTargetJetty);

    auto ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
    if (NN_UNLIKELY(ret != 0)) {
        NN_LOG_ERROR("Failed to post send request to public jetty " << mName << ", result " << ret);
        mJettyMr->ReturnBuffer(mrBufAddress);
        mCtxInfoPool->ReturnBuffer(ctxBuffer);
        return UB_QP_POST_SEND_FAILED;
    }

    return UB_OK;
}

UResult UBPublicJetty::PostReceive(uintptr_t bufAddr, uint32_t bufSize, urma_target_seg_t *localSeg, uint64_t context)
{
    if (NN_UNLIKELY(mUrmaJetty == nullptr || bufAddr == 0 || bufSize == 0 || localSeg == nullptr)) {
        NN_LOG_ERROR("Failed to postrecv as mUrmaJetty or bufAddr or bufSize or localSeg is null");
        return NN_INVALID_PARAM;
    }

    urma_jfr_wr_t *bad_wr;

    urma_sge_t local_sge{};
    local_sge.addr = bufAddr;
    local_sge.len = bufSize;
    local_sge.tseg = localSeg;

    urma_jfr_wr_t wr{};
    wr.src.sge = &local_sge;
    wr.src.num_sge = 1;
    wr.next = nullptr;
    wr.user_ctx = context;

    NN_LOG_DEBUG("[Post Buffer] ------ urma_post_jetty_recv_wr2, jetty id: " << mUrmaJetty->jetty_id.id <<
        ", jfc id: " << mRecvJfc->mUrmaJfc->jfc_id.id);
    auto ret = HcomUrma::PostJettyRecvWr(mUrmaJetty, &wr, &bad_wr);
    if (NN_UNLIKELY(ret != 0)) {
        NN_LOG_ERROR("Failed to post receive request to jetty " << mName << ", result " << ret);
        return UB_QP_POST_RECEIVE_FAILED;
    }

    return UB_OK;
}

UResult UBPublicJetty::CheckRecvResult(urma_cr_t wc, uint32_t size, UResult result, uint32_t pollCount,
    int32_t timeoutInMs)
{
    // 若pollCount == 0，大概率是超时还未收到事件，返回失败
    if (pollCount == 0) {
        NN_LOG_ERROR("polled 0 cqe, jetty id: " << mUrmaJetty->jetty_id.id << ", jfc id: " <<
            mRecvJfc->mUrmaJfc->jfc_id.id << "timeout: " << timeoutInMs << " ms");
        return UB_CQ_POLLING_FAILED;
    }

    // 若pollCount非0，判断result
    if (NN_UNLIKELY(result != UB_OK)) {
        NN_LOG_ERROR("Failed to event polling in public jetty Receive res = " << result << ", polling timeout " <<
            timeoutInMs << " ms");
        return result;
    }

    if (NN_UNLIKELY(wc.status != URMA_CR_SUCCESS)) {
        NN_LOG_ERROR("Poll cq failed in public jetty Receive wcStatus " << wc.status);
        return UB_CQ_WC_WRONG;
    }

    if (NN_UNLIKELY(wc.completion_len != size)) {
        NN_LOG_ERROR("Failed to Receive in public jetty Receive as expect size:" << size << " actual size: " <<
            wc.completion_len);
        return UB_CQ_WC_WRONG;
    }
    return UB_OK;
}

UResult UBPublicJetty::Receive(void *buf, uint32_t size)
{
    if (NN_UNLIKELY(buf == nullptr || size == 0)) {
        NN_LOG_ERROR("Failed to Receive as invalid param");
        return UB_PARAM_INVALID;
    }

    UResult result = UB_OK;
    urma_cr_t wc{};
    int32_t timeoutInMs = TimeSecToMs(mPollTimeout);
    uint32_t pollCount = 1;

    // avoid urma event poll zero cqe bug
    auto start = NetMonotonic::TimeMs();
    int64_t pollTime = 0;
    do {
        pollCount = 1;
        result = mRecvJfc->ProgressV(&wc, pollCount);
        pollTime = (int64_t)(NetMonotonic::TimeMs() - start);
        if (pollCount == 0 && timeoutInMs >= 0 && pollTime > timeoutInMs) {
            NN_LOG_ERROR("Busy poll failed pollCount = " << pollCount << " pollTime = " << pollTime << " in recv");
            return UB_CQ_EVENT_GET_TIMOUT;
        }
        usleep(NN_NO100000); // 100ms
    } while (result == UB_OK && pollCount == 0);
    if (CheckRecvResult(wc, size, result, pollCount, timeoutInMs) != UB_OK) {
        return UB_ERROR;
    }

    UBOpContextInfo *ctx = reinterpret_cast<UBOpContextInfo *>(wc.user_ctx);
    if (ctx == nullptr || ctx->mrMemAddr == 0) {
        NN_LOG_ERROR("Failed to Receive as ctx is nullptr");
        return UB_ERROR;
    }

    if (NN_UNLIKELY(memcpy_s(buf, size, (void *)(ctx->mrMemAddr), size) != SER_OK)) {
        NN_LOG_ERROR("Failed to copy data");
        return UB_ERROR;
    }
    // postrecv and return ctx
    ctx->opType = UBOpContextInfo::RECEIVE;
    ctx->opResultType = UBOpContextInfo::SUCCESS;

    if (PostReceive(ctx->mrMemAddr, PUBLIC_JETTY_SEG_SIZE, GetMemorySeg(), reinterpret_cast<uint64_t>(ctx)) != 0) {
        NN_LOG_ERROR("Failed to post receive in jetty receive");
        mJettyMr->ReturnBuffer(ctx->mrMemAddr);
        mCtxInfoPool->ReturnBuffer(reinterpret_cast<uintptr_t>(ctx));
        return UB_QP_POST_RECEIVE_FAILED;
    }
    return UB_OK;
}

UResult UBPublicJetty::PollingCompletion()
{
    if (NN_UNLIKELY(mRecvJfc == nullptr || mRecvJfc->mUrmaJfc == nullptr)) {
        NN_LOG_ERROR("Failed to polling completion with public jetty as jfc is null");
        return UB_EP_NOT_INITIALIZED;
    }
    int32_t timeoutInMs = TimeSecToMs(mPollTimeout);
    urma_cr_t wc{};
    uint32_t pollCount = 1;
    NResult result = UB_OK;

    // avoid urma event poll zero cqe bug
    auto start = NetMonotonic::TimeMs();
    int64_t pollTime = 0;
    do {
        pollCount = 1;
        result = mRecvJfc->ProgressV(&wc, pollCount);
        pollTime = (int64_t)(NetMonotonic::TimeMs() - start);
        if (pollCount == 0 && timeoutInMs >= 0 && pollTime > timeoutInMs) {
            NN_LOG_ERROR("Busy poll completion failed pollCount = " << pollCount << " pollTime = " << pollTime);
            return UB_CQ_EVENT_GET_TIMOUT;
        }
        usleep(NN_NO100000); // 100ms
    } while (result == UB_OK && pollCount == 0);
    // 若pollCount == 0，大概率是超时还未收到事件，返回失败
    if (pollCount == 0) {
        NN_LOG_ERROR("polled 0 cqe, jetty id: " << mUrmaJetty->jetty_id.id << ", jfc id: " <<
            mRecvJfc->mUrmaJfc->jfc_id.id << "timeout: " << timeoutInMs << " ms");
        return UB_CQ_POLLING_FAILED;
    }

    // 若pollCount非0，判断result
    if (NN_UNLIKELY(result != UB_OK)) {
        NN_LOG_ERROR("Failed to epoll in jetty recv res = " << result << ", polling timeout " << timeoutInMs << " ms");
        return result;
    }

    if (NN_UNLIKELY(wc.status != URMA_CR_SUCCESS)) {
        NN_LOG_WARN("Poll cq failed in public jetty wcStatus " << wc.status);
        if (NN_UNLIKELY(wc.status == URMA_CR_WR_SUSPEND_DONE || wc.status == URMA_CR_WR_FLUSH_ERR_DONE)) {
            NN_LOG_WARN("Polled a fake cqe in public jetty");
            return UB_ERROR;
        }
    }

    auto ctx = reinterpret_cast<UBOpContextInfo *>(wc.user_ctx);
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Ctx is null in public jetty polling completion");
        return UB_ERROR;
    }

    if (NN_UNLIKELY(!ReturnBuffer(ctx->mrMemAddr))) {
        NN_LOG_ERROR("Failed to return buffer mr to jetty mr pool");
    }

    if (NN_UNLIKELY(!mCtxInfoPool->ReturnBuffer(reinterpret_cast<uintptr_t>(ctx)))) {
        NN_LOG_ERROR("Failed to return context info in public jetty");
    }

    return UB_OK;
}

void UBPublicJetty::Stop()
{
    std::lock_guard<std::mutex> lock(mStopMutex);
    if (!mIsStarted) {
        return;
    }
    mIsStarted = false;
    mNeedStop = true;
    if (mPublicJettyPollingThread.joinable()) {
        mPublicJettyPollingThread.join();
    }
    if (mThreadPool != nullptr) {
        mThreadPool->Stop();
        delete mThreadPool;
        mThreadPool = nullptr;
    }
    int result = 0;
    if (mUrmaJetty != nullptr) {
        struct urma_jetty_attr attr = {};
        attr.mask = JETTY_STATE;
        attr.state = URMA_JETTY_STATE_ERROR;
        result = HcomUrma::ModifyJetty(mUrmaJetty, &attr);
        if (result != 0) {
            NN_LOG_ERROR("Failed to modify jetty to URMA_JETTY_STATE_ERROR, urma result = " << result);
        }
    }
    if (mTargetJetty != nullptr) {
        result = HcomUrma::UnimportJetty(mTargetJetty);
        mTargetJetty = nullptr;
        if (result != 0) {
            NN_LOG_ERROR("Failed to unimport target jetty, urma result = " << result);
        }
    }
    if (mUrmaJetty != nullptr) {
        result = HcomUrma::DeleteJetty(mUrmaJetty);
        mUrmaJetty = nullptr;
        if (result != 0) {
            NN_LOG_ERROR("Failed to delete jetty, urma result = " << result);
        } else {
            NN_LOG_INFO("Delete public jetty success, jetty id " << mUrmaJettyId);
        }
    }
    if (mJfr != nullptr) {
        result = HcomUrma::DeleteJfr(mJfr);
        mJfr = nullptr;
        if (result != 0) {
            NN_LOG_ERROR("Failed to delete jfr, urma result = " << result);
        }
    }
}

// public jetty clear resource
UResult UBPublicJetty::UnInitialize()
{
    Stop();
    if (mJettyMr != nullptr) {
        mJettyMr->DecreaseRef();
        mJettyMr = nullptr;
    }
    if (mCtxInfoPool != nullptr) {
        mCtxInfoPool->DecreaseRef();
        mCtxInfoPool = nullptr;
    }
    if (mSendJfc != nullptr) {
        mSendJfc->DecreaseRef();
    }

    if (mRecvJfc != nullptr && mRecvJfc != mSendJfc) {
        mRecvJfc->DecreaseRef();
    }
    mSendJfc = nullptr;
    mRecvJfc = nullptr;

    if (mUBContext != nullptr) {
        mUBContext->DecreaseRef();
    }

    if (mWorkerLb.Get() != nullptr) {
        mWorkerLb.Set(nullptr);
    }
    NN_LOG_INFO("Uninitialize public jetty success, jetty id: " << mUrmaJettyId);
    return UB_OK;
}

// get a free buffer from mr
bool UBPublicJetty::GetFreeBuff(uintptr_t &item)
{
    return mJettyMr->GetFreeBuffer(item);
}

// get N free buffers from mr
bool UBPublicJetty::GetFreeBufferN(uintptr_t *&items, uint32_t n)
{
    return mJettyMr->GetFreeBufferN(items, n);
}

// return a free buffer to mr
bool UBPublicJetty::ReturnBuffer(uintptr_t value)
{
    return mJettyMr->ReturnBuffer(value);
}

urma_target_seg_t *UBPublicJetty::GetMemorySeg()
{
    return reinterpret_cast<urma_target_seg_t *>(mJettyMr->GetMemorySeg());
}
} // namespace hcom
} // namespace ock
#endif