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

namespace ock {
namespace hcom {

UResult UBWorker::PostReceive(UBJetty *qp, uintptr_t bufAddress, uint32_t bufSize, urma_target_seg_t *localSeg)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to PostReceive with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReceive with UBWorker " << DetailName() << " as no ctx left");
        return UB_QP_CTX_FULL;
    }

    /* set to all 0 */
    bzero(ctx, sizeof(UBOpContextInfo));
    ctx->ubJetty = qp;
    ctx->mrMemAddr = bufAddress;
    ctx->dataSize = bufSize;
    ctx->qpNum = qp->QpNum();
    ctx->localSeg = localSeg;
    ctx->opType = UBOpContextInfo::RECEIVE;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    qp->IncreaseRef();

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);

    auto result = qp->PostReceive(bufAddress, bufSize, localSeg, reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->DecreaseRef();
        qp->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

UResult UBWorker::RePostReceive(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr)) {
        NN_LOG_ERROR("Failed to RePostReceive with UBWorker " << DetailName() << " as ctx or its qp is null");
        return UB_PARAM_INVALID;
    }

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    ctx->ubJetty->AddOpCtxInfo(ctx);

    auto result =
        ctx->ubJetty->PostReceive(ctx->mrMemAddr, mOptions.qpMrSegSize, ctx->localSeg, reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        ctx->ubJetty->DecreaseRef();
        ctx->ubJetty->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

UResult UBWorker::PostSend(UBJetty *qp, const UBSendReadWriteRequest &req, urma_target_seg_t *localSeg,
    uint32_t immData)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to PostSend with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSend with UBWorker " << DetailName() << " as no reqInfo left");
        return UB_QP_CTX_FULL;
    }

    if (NN_UNLIKELY(!qp->GetPostSendWr())) {
        NN_LOG_ERROR("Failed to PostSend with UBWorker " << DetailName() << " as no post send wr left");
        mOpCtxInfoPool.Return(ctx);
        return UB_QP_POST_SEND_WR_FULL;
    }
    ctx->ubJetty = qp;
    ctx->mrMemAddr = req.lAddress;
    ctx->dataSize = req.size;
    ctx->qpNum = qp->QpNum();
    ctx->lKey = req.lKey;
    ctx->opType = immData == 0 ? UBOpContextInfo::SEND : UBOpContextInfo::SEND_RAW;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    ctx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0  && NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        qp->ReturnPostSendWr();
        mOpCtxInfoPool.Return(ctx);
        return UB_ERROR;
    }
    qp->IncreaseRef();

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);

    auto result = qp->PostSend(req.lAddress, req.size, localSeg, ctx, immData);
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->ReturnPostSendWr();
        qp->DecreaseRef();
        qp->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

UResult UBWorker::PostSendSglInline(UBJetty *qp, const UBSendSglInlineHeader &header, const UBSendReadWriteRequest &req,
    uint32_t immData)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Verbs Failed to PostSend with RDMAWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Verbs Failed to PostSend with RDMAWorker " << DetailName() << " as no reqInfo left");
        return UB_QP_CTX_FULL;
    }

    if (NN_UNLIKELY(!qp->GetPostSendWr())) {
        NN_LOG_ERROR("Verbs Failed to PostSend with RDMAWorker " << DetailName() << " as no post send wr left");
        mOpCtxInfoPool.Return(ctx);
        return UB_QP_POST_SEND_WR_FULL;
    }
    ctx->ubJetty = qp;
    ctx->mrMemAddr = req.lAddress;
    ctx->dataSize = req.size;
    ctx->lKey = req.lKey;
    ctx->qpNum = qp->QpNum();
    ctx->opType = UBOpContextInfo::SEND_SGL_INLINE;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    ctx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        return UB_PARAM_INVALID;
    }
    qp->IncreaseRef();

    // attach context to qp firstly, because post could be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);

    UBSHcomNetTransDataIov netTransDataIov[NN_NO2];
    netTransDataIov[NN_NO0].address = reinterpret_cast<uintptr_t>(&header);
    netTransDataIov[NN_NO0].size = sizeof(UBSendSglInlineHeader);
    netTransDataIov[NN_NO1].address = req.lAddress;
    netTransDataIov[NN_NO1].size = req.size;

    auto result = qp->PostSendSglInline(netTransDataIov, NN_NO2, reinterpret_cast<uint64_t>(ctx), immData);
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->ReturnPostSendWr();
        qp->DecreaseRef();
        qp->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

UResult UBWorker::PostSendSgl(UBJetty *qp, const UBSHcomNetTransSglRequest &req, const UBSHcomNetTransRequest &tlsReq,
    uint32_t immData, bool isEncrypted)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendSgl with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendSgl with UBWorker " << DetailName() << " as no ctx left");
        return UB_PARAM_INVALID;
    }
    sglCtx->qp = qp;
    sglCtx->result = UB_OK;
    if (NN_UNLIKELY(memcpy_s(sglCtx->iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, req.iov,
        sizeof(UBSHcomNetTransSgeIov) * req.iovCount) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        mSglCtxInfoPool.Return(sglCtx);
        return UB_PARAM_INVALID;
    }
    sglCtx->iovCount = req.iovCount;
    sglCtx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            mSglCtxInfoPool.Return(sglCtx);
            return UB_PARAM_INVALID;
        }
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendSgl with UBWorker " << DetailName() << " as no reqInfo left");
        mSglCtxInfoPool.Return(sglCtx);
        return UB_QP_CTX_FULL;
    }
    if (NN_UNLIKELY(!qp->GetPostSendWr())) {
        NN_LOG_ERROR("Failed to PostSendSgl with UBWorker " << DetailName() << " as no post send wr left");
        mOpCtxInfoPool.Return(ctx);
        mSglCtxInfoPool.Return(sglCtx);
        return UB_QP_POST_SEND_WR_FULL;
    }
    ctx->ubJetty = qp;
    // if not encrypt reqTls lAddress\size\lKey is 0
    ctx->mrMemAddr = tlsReq.lAddress;
    ctx->dataSize = tlsReq.size;
    ctx->lKey = tlsReq.lKey;
    ctx->qpNum = qp->QpNum();
    ctx->opType = UBOpContextInfo::SEND_RAW_SGL;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    ctx->upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
    auto upCtx = static_cast<UBSgeCtxInfo *>((void *)&(ctx->upCtx));
    upCtx->ctx = sglCtx;
    qp->IncreaseRef();

    // attach context to qp firstly, because post could be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);
    UResult result = UB_OK;
    if (isEncrypted != 0) {
        result = qp->PostSend(tlsReq.lAddress, tlsReq.size, reinterpret_cast<urma_target_seg_t *>(tlsReq.srcSeg),
            reinterpret_cast<UBOpContextInfo *>(ctx), immData);
    } else {
        result = qp->PostSendSgl(req.iov, req.iovCount, reinterpret_cast<uintptr_t>(ctx), immData);
    }

    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->ReturnPostSendWr();
        qp->RemoveOpCtxInfo(ctx);
        qp->DecreaseRef();
        mOpCtxInfoPool.Return(ctx);
        mSglCtxInfoPool.Return(sglCtx);
    }
    return result;
}

UResult UBWorker::PostRead(UBJetty *qp, const UBSendReadWriteRequest &req)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with UBWorker " << DetailName() << " as no reqInfo left");
        return UB_QP_CTX_FULL;
    }

    if (NN_UNLIKELY(!qp->GetOneSideWr())) {
        NN_LOG_ERROR("Failed to PostRead with UBWorker " << DetailName() << " as no one side wr left");
        mOpCtxInfoPool.Return(ctx);
        return UB_QP_ONE_SIDE_WR_FULL;
    }
    ctx->ubJetty = qp;
    ctx->mrMemAddr = req.lAddress;
    ctx->dataSize = req.size;
    ctx->qpNum = qp->QpNum();
    ctx->lKey = req.lKey;
    ctx->opType = UBOpContextInfo::READ;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    ctx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        qp->ReturnOneSideWr();
        mOpCtxInfoPool.Return(ctx);
        return UB_ERROR;
    }
    qp->IncreaseRef();

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);

    UResult result = UB_OK;
    result = qp->PostRead(req.lAddress, reinterpret_cast<urma_target_seg_t *>(req.srcSeg), req.rAddress,
        req.rKey, req.size, reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->ReturnOneSideWr();
        qp->DecreaseRef();
        qp->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

UResult UBWorker::CreateOneSideCtx(const UBSgeCtxInfo &sgeInfo, const UBSHcomNetTransSgeIov *iov, uint32_t iovCount,
    uint64_t (&ctxArr)[NET_SGE_MAX_IOV], bool isRead)
{
    if (iov == nullptr || iovCount == NN_NO0 || iovCount > NN_NO4 || ctxArr == nullptr) {
        NN_LOG_ERROR("Urma failed to create oneSide operation ctx because param invalid");
        return UB_PARAM_INVALID;
    }
    for (uint32_t i = 0; i < iovCount; ++i) {
        auto ctx = mOpCtxInfoPool.Get();
        if (NN_UNLIKELY(ctx == nullptr)) {
            NN_LOG_ERROR("Urma failed to oneSide operation with UBWorker " << DetailName() << " as no ctx left");
            for (uint32_t j = 0; j < i; ++j) {
                sgeInfo.ctx->qp->ReturnOneSideWr();
                sgeInfo.ctx->qp->RemoveOpCtxInfo(reinterpret_cast<UBOpContextInfo *>(ctxArr[j]));
                sgeInfo.ctx->qp->DecreaseRef();
                mOpCtxInfoPool.Return(reinterpret_cast<UBOpContextInfo *>(ctxArr[j]));
            }
            return UB_QP_CTX_FULL;
        }

        if (NN_UNLIKELY(!sgeInfo.ctx->qp->GetOneSideWr())) {
            NN_LOG_ERROR("Urma failed to oneSide operation with UBWorker " << DetailName() <<
                " as no one side wr left");
            mOpCtxInfoPool.Return(ctx);
            for (uint32_t j = 0; j < i; ++j) {
                sgeInfo.ctx->qp->ReturnOneSideWr();
                sgeInfo.ctx->qp->RemoveOpCtxInfo(reinterpret_cast<UBOpContextInfo *>(ctxArr[j]));
                sgeInfo.ctx->qp->DecreaseRef();
                mOpCtxInfoPool.Return(reinterpret_cast<UBOpContextInfo *>(ctxArr[j]));
            }
            return UB_QP_ONE_SIDE_WR_FULL;
        }
        ctx->ubJetty = sgeInfo.ctx->qp;
        ctx->mrMemAddr = iov[i].lAddress;
        ctx->dataSize = iov[i].size;
        ctx->qpNum = sgeInfo.ctx->qp->QpNum();
        ctx->lKey = iov[i].lKey;
        ctx->opType = isRead ? UBOpContextInfo::SGL_READ : UBOpContextInfo::SGL_WRITE;
        ctx->opResultType = UBOpContextInfo::SUCCESS;
        ctx->upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
        auto upCtx = static_cast<UBSgeCtxInfo *>((void *)&(ctx->upCtx));
        upCtx->ctx = sgeInfo.ctx;
        upCtx->idx = i;

        sgeInfo.ctx->qp->IncreaseRef();
        sgeInfo.ctx->qp->AddOpCtxInfo(ctx);
        ctxArr[i] = reinterpret_cast<uint64_t>(ctx);
    }
    return UB_OK;
}

UResult UBWorker::PostOneSideSgl(UBJetty *qp, const UBSendSglRWRequest &req, bool isRead)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Urma failed to PostRead with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Urma failed to get from mSglCtxInfoPool ");
        return UB_PARAM_INVALID;
    }

    sglCtx->qp = qp;
    sglCtx->result = UB_OK;
    if (NN_UNLIKELY(memcpy_s(sglCtx->iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, req.iov,
        sizeof(UBSHcomNetTransSgeIov) * req.iovCount) != UB_OK)) {
        NN_LOG_ERROR("Urma failed to copy iov to sglCtx");
        mSglCtxInfoPool.Return(sglCtx);
        return UB_PARAM_INVALID;
    }
    sglCtx->iovCount = req.iovCount;
    sglCtx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
            NN_LOG_ERROR("Urma failed to copy upCtx to sglCtx");
            mSglCtxInfoPool.Return(sglCtx);
            return UB_PARAM_INVALID;
        }
    }

    UBSgeCtxInfo sgeInfo(sglCtx);
    sglCtx->refCount = 0;
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    UResult result = CreateOneSideCtx(sgeInfo, req.iov, req.iovCount, ctxArr, isRead);
    if (result != UB_OK) {
        NN_LOG_ERROR("Urma failed to create one side ctx.");
        mSglCtxInfoPool.Return(sglCtx);
        return result;
    }
    result = qp->PostOneSideSgl(req.iov, req.iovCount, ctxArr, isRead, NET_SGE_MAX_IOV);
    if (NN_UNLIKELY(result != UB_OK)) {
        for (int i = 0; i < req.iovCount; ++i) {
            qp->ReturnOneSideWr();
            qp->RemoveOpCtxInfo(reinterpret_cast<UBOpContextInfo *>(ctxArr[i]));
            qp->DecreaseRef();
            mOpCtxInfoPool.Return(reinterpret_cast<UBOpContextInfo *>(ctxArr[i]));
        }
        mSglCtxInfoPool.Return(sglCtx);
    }
    return result;
}

UResult UBWorker::PostWrite(UBJetty *qp, const UBSendReadWriteRequest &req, UBOpContextInfo::OpType type)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with UBWorker " << DetailName() << " as qp is null");
        return UB_PARAM_INVALID;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with UBWorker " << DetailName() << " as no ctx left");
        return UB_QP_CTX_FULL;
    }
    if (NN_UNLIKELY(!qp->GetOneSideWr())) {
        NN_LOG_ERROR("Failed to PostWrite with UBWorker " << DetailName() << " as no one side wr left");
        mOpCtxInfoPool.Return(ctx);
        return UB_QP_ONE_SIDE_WR_FULL;
    }
    ctx->ubJetty = qp;
    ctx->mrMemAddr = req.lAddress;
    ctx->dataSize = req.size;
    ctx->qpNum = qp->QpNum();
    ctx->lKey = req.lKey;
    ctx->opType = type;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    ctx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        qp->ReturnOneSideWr();
        mOpCtxInfoPool.Return(ctx);
        return UB_ERROR;
    }
    qp->IncreaseRef();

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    qp->AddOpCtxInfo(ctx);

    UResult result = UB_OK;
    result = qp->PostWrite(req.lAddress, reinterpret_cast<urma_target_seg_t *>(req.srcSeg), req.rAddress,
        req.rKey, req.size, reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        qp->ReturnOneSideWr();
        qp->DecreaseRef();
        qp->RemoveOpCtxInfo(ctx);
        mOpCtxInfoPool.Return(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}
}
}
#endif