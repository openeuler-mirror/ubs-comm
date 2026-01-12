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

#ifndef HCOM_UB_URMA_WRAPPER_JETTY_H
#define HCOM_UB_URMA_WRAPPER_JETTY_H
#ifdef UB_BUILD_ENABLED

#include <unistd.h>

#include <algorithm>
#include "urma_ubagg.h"

#include "hcom_utils.h"
#include "net_util.h"
#include "net_oob.h"
#include "ub_common.h"
#include "net_load_balance.h"
#include "net_ctx_info_pool.h"
#include "net_mem_pool_fixed.h"
#include "hcom_obj_statistics.h"
#include "under_api/urma/urma_api_wrapper.h"
#include "ub_mr_fixed_buf.h"
#include "ub_urma_wrapper_jfc.h"

namespace ock {
namespace hcom {

struct UBJettyExchangeInfo {
    urma_jetty_id_t jettyId{};
    urma_eid_t eid{};
    uint32_t token = 0;
    uintptr_t hbAddress = 0;
    uint64_t hbKey = 0;
    uint64_t hbMrSize = 0;
    uint32_t maxSendWr = JETTY_MAX_SEND_WR;
    uint32_t maxReceiveWr = JETTY_MAX_RECV_WR;
    uint32_t receiveSegSize = NN_NO1024;
    uint32_t receiveSegCount = NN_NO64;
    bool isNeedSendHb = true;
} __attribute__((packed));

enum class UBJettyState : uint8_t {
    RESET,  ///< 初始状态
    READY,  ///< 可收发数据
    ERROR,  ///< 调用 modify jetty error之后
};

class UBJetty {
public:
    UBJetty(const std::string &name, uint32_t id, UBContext *ctx, UBJfc *jfc, JettyOptions jettyOptions = {})
        : mName(name), mId(id), mUBContext(ctx), mSendJfc(jfc), mRecvJfc(jfc), mJettyOptions(jettyOptions)
    {
        if (mUBContext != nullptr) {
            mUBContext->IncreaseRef();
        }

        if (mSendJfc != nullptr) {
            mSendJfc->IncreaseRef();
        }

        OBJ_GC_INCREASE(UBJetty);
    }

    ~UBJetty()
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBJetty);
    }

    /* call urma_create_jetty to create real jetty */
    UResult CreateUrmaJetty(uintptr_t seg_pa, uint32_t seg_len, uint32_t seg_count, uint32_t token = 0);
    UResult CreateJettyMr();
    UBMemoryRegionFixedBuffer *GetJettyMr();

    UResult Initialize(uint32_t seg_count, unsigned long memid, uint32_t token = 0);
    UResult UnInitialize();

    /// 清理 op ctx 等资源
    void Cleanup();

    /// 清理在 FLUSH_ERR_DONE 之后被 post 的 op ctx等资源
    void Flush()
    {
        Cleanup();
    }
    /*
    exchange information needs to be transformed by other channel (e.g. tcp connection)
    1 firstly do the initialization
    2 got qp exchange info from peer
    3 call this function to change qp state to ready state (INIT & RTS & RTR)
    */
    UResult ChangeToReady(UBJettyExchangeInfo &exInfo);

    /* after qp initialized, retrieve the qp qp_num for exchange */
    UResult FillExchangeInfo(UBJettyExchangeInfo &exInfo);
    void StoreExchangeInfo(UBJettyExchangeInfo *exInfo);
    UBJettyExchangeInfo &GetExchangeInfo();
    UResult ImportAndBindJetty(uint32_t token = 0);

    inline urma_target_seg_t *ImportSeg(uintptr_t addr, uint32_t bufSize, uint64_t token)
    {
        uint32_t tokenId = static_cast<uint32_t>(token);
        urma_token_t tokenValue = {static_cast<uint32_t>(token >> NN_NO32)};
        urma_seg_t remoteSeg{};
        remoteSeg.len = bufSize;
        remoteSeg.ubva.va = addr;
        remoteSeg.token_id = tokenId;
        remoteSeg.ubva.eid = mRemoteJettyInfo->eid;
        remoteSeg.attr.bs.token_policy = URMA_TOKEN_PLAIN_TEXT;

        urma_import_seg_flag_t flag{};
        flag.bs.cacheable = URMA_NON_CACHEABLE;
        flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
        flag.bs.mapping = URMA_SEG_NOMAP;

        return HcomUrma::ImportSeg(mUBContext->mUrmaContext, &remoteSeg, &tokenValue, 0, flag);
    }

    inline UBSHcomNetDriverProtocol GetProtocol()
    {
        return mUBContext->protocol;
    }

    inline UResult PostReceive(uintptr_t bufAddr, uint32_t bufSize, urma_target_seg_t *localSeg, uint64_t context)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        urma_jfr_wr_t *bad_wr;

        urma_sge_t local_sge{};
        local_sge.addr = bufAddr;
        local_sge.len = bufSize;
        local_sge.tseg = localSeg;

        urma_jfr_wr_t wr{};
        wr.user_ctx = context;
        wr.src.sge = &local_sge;
        wr.src.num_sge = 1;
        wr.next = nullptr;

        NN_LOG_DEBUG("[Post Buffer] ------ urma_post_jetty_recv_wr1, jetty id: " << mUrmaJetty->jetty_id.id
        << ", jfc id: " << mRecvJfc->mUrmaJfc->jfc_id.id);
        auto ret = HcomUrma::PostJettyRecvWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_ERROR("Failed to post receive request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_RECEIVE_FAILED;
        }

        return UB_OK;
    }

    inline UResult PostSend(uintptr_t bufAddr, uint32_t bufSize, urma_target_seg_t *localSeg, UBOpContextInfo *context,
        uint32_t immData = 0)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        auto qpUpContext = context->ubJetty->GetUpContext();
        UBSHcomNetEndpointPtr ep = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
        UBSHcomNetTransHeader *header = (UBSHcomNetTransHeader *)bufAddr;
        uint64_t epId = ep->Id();

        // 如果是普通send，存在header，header中的seqNo是序号
        // 如果是send_raw，不存在header，immData是序号
        if (context->opType == UBOpContextInfo::SEND) {
            NN_LOG_DEBUG("[Request Send] ------ ep id = " << epId << ", headerCrc = "
            << header->headerCrc << ", opCode = " << header->opCode << ", flags = " << header->flags << ", seqNo = "
            << header->seqNo << ",timeout = " << header->timeout << ", errCode = " << header->errorCode
            << ", dataLength = " << header->dataLength << ", status = " <<
            UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::IN_URMA));
        } else {
            NN_LOG_DEBUG("[Request Send] ------ ep id = " << epId << ", seqNo = " << immData
            << ", bufSize = " << bufSize << ", bufhead = " << *(reinterpret_cast<uint32_t*>(bufAddr))
            << ", status = " << UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::IN_URMA));
        }

        urma_jfs_wr_t *bad_wr;
        urma_sge_t local_sge{};
        local_sge.addr = bufAddr;
        local_sge.len = bufSize;
        local_sge.tseg = localSeg;

        urma_jfs_wr_t wr{};
        wr.user_ctx = reinterpret_cast<uint64_t>(context);
        wr.send.src.sge = &local_sge;
        wr.send.src.num_sge = 1;
        wr.send.imm_data = immData;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_SEND_IMM;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;

        auto ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_ERROR("Failed to post send request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_SEND_FAILED;
        }

        return UB_OK;
    }

    inline UResult PostSendSglInline(UBSHcomNetTransDataIov *iov, uint32_t iovCount, uint64_t context,
        uint32_t immData = 0)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        urma_jfs_wr_t *badWR;
        urma_sge_t list[NN_NO4] = {};
        urma_target_seg_t srcSeg[NET_SGE_MAX_IOV] = {};
        for (uint32_t i = 0; i < iovCount; i++) {
            list[i].addr = iov[i].address;
            list[i].len = iov[i].size;
        }

        urma_jfs_wr_t wr {};
        wr.user_ctx = reinterpret_cast<uint64_t>(context);
        wr.send.src.sge = list;
        wr.send.src.num_sge = iovCount;
        wr.send.imm_data = immData;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_SEND_IMM;
        wr.flag.bs.complete_enable = 1;
        wr.flag.bs.inline_flag = 1;
        wr.tjetty = mTargetJetty;

        auto result = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &badWR);
        if (NN_UNLIKELY(result != 0)) {
            NN_LOG_ERROR("Failed to post send request to jetty " << mName << ", result " << result);
            return UB_QP_POST_SEND_FAILED;
        }

        return UB_OK;
    }

    inline UResult PostSendSglInlineUbc(UBSHcomNetTransDataIov *iov, uint32_t iovCount, uint64_t context,
        urma_target_seg_t **tseg, uint32_t immData = 0)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        urma_jfs_wr_t *badWR;
        urma_sge_t list[NN_NO4] = {};
        for (uint32_t i = 0; i < iovCount; i++) {
            list[i].addr = iov[i].address;
            list[i].len = iov[i].size;
            list[i].tseg = tseg[i];
        }

        urma_jfs_wr_t wr {};
        wr.user_ctx = reinterpret_cast<uint64_t>(context);
        wr.send.src.sge = list;
        wr.send.src.num_sge = iovCount;
        wr.send.imm_data = immData;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_SEND_IMM;
        wr.flag.bs.inline_flag = 1;
        wr.flag.bs.complete_enable = 1;

        auto result = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &badWR);
        if (NN_UNLIKELY(result != 0)) {
            NN_LOG_ERROR("Failed to post send sgl request to jetty " << mName << ", result " << result);
            return UB_QP_POST_SEND_FAILED;
        }

        return UB_OK;
    }

    /// @brief 发送 send 请求，使用 sgl 方式
    /// @param iov       [in] 将要发送的向量。仅需填充 lAddress, lkey 和 size.
    /// @param iovCount  [in] 向量长度
    /// @param context   [in] Service 层上下文
    /// @param immData   [in] 附带的立即数
    UResult PostSendSgl(UBSHcomNetTransSgeIov *iov, uint32_t iovCount, uint64_t context, uint32_t immData = 0)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        urma_jfs_wr_t *bad_wr;
        urma_sge_t list[NET_SGE_MAX_IOV];
        urma_target_seg_t srcSeg[NET_SGE_MAX_IOV] = {};
        for (uint32_t i = 0; i < iovCount; i++) {
            list[i].addr = iov[i].lAddress;
            list[i].len = iov[i].size;
            list[i].tseg = reinterpret_cast<urma_target_seg_t *>(iov[i].srcSeg);
        }

        urma_jfs_wr_t wr{};
        wr.user_ctx = reinterpret_cast<uint64_t>(context);
        wr.send.src.sge = list;
        wr.send.src.num_sge = iovCount;
        wr.send.imm_data = immData;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_SEND_IMM;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;

        auto ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr); // urma_post_jetty_send_wr
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_ERROR("Failed to post send sgl request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_SEND_FAILED;
        }

        return UB_OK;
    }

    inline UResult PostRead(uintptr_t bufAddr, urma_target_seg_t *srcSeg, uintptr_t dstBufAddr,
        urma_target_seg_t *dstSeg, uint32_t bufSize, uint64_t context)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        int ret = 0;

        urma_jfs_wr_t *bad_wr;
        urma_sge_t src_sge{};
        src_sge.addr = bufAddr;
        src_sge.len = bufSize;
        src_sge.tseg = srcSeg;

        urma_sge_t dst_sge{};
        dst_sge.addr = dstBufAddr;
        dst_sge.len = bufSize;
        dst_sge.tseg = dstSeg;

        urma_jfs_wr_t wr{};
        wr.user_ctx = context;
        wr.rw.src.sge = &dst_sge;
        wr.rw.src.num_sge = 1;
        wr.rw.dst.sge = &src_sge;
        wr.rw.dst.num_sge = 1;
        wr.next = nullptr;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;
        wr.opcode = URMA_OPC_READ;

        ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_ERROR("Failed to post read request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_WRITE_FAILED;
        }

        return UB_OK;
    }

    inline void FillUrmaTargetSeg(urma_target_seg_t &tseg, uintptr_t addr, uint32_t bufSize, uint32_t token)
    {
        tseg.seg.ubva.va = addr;
        tseg.seg.len = bufSize;
        tseg.seg.token_id = token;
        tseg.urma_ctx = mUBContext->mUrmaContext;
    }

    /// @brief 发送 Read 请求，从对端读取数据至本端
    /// @param bufAddr     [in] 本地 MR 目标地址
    /// @param ltoken      [in] 本地 MR 访问 key
    /// @param dstBufAddr  [in] 对端 MR 地址
    /// @param rtoken      [in] 对端 MR 远程访问 key
    /// @param bufSize     [in] 读取大小
    /// @param context     [in] `UBOpContextInfo`, 事件完成时通过此 context 找回对应 Jetty 以及上层 Service 层的上下文
    UResult PostRead(uintptr_t bufAddr, urma_target_seg_t *ltseg, uintptr_t dstBufAddr, uint64_t rtoken,
        uint32_t bufSize, uint64_t context)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }
        urma_target_seg_t *dstSeg = ImportSeg(dstBufAddr, bufSize, rtoken);
        if (dstSeg == nullptr) {
            NN_LOG_ERROR("Failed to import dstSeg");
            return UB_QP_POST_READ_FAILED;
        }

        urma_jfs_wr_t *bad_wr;
        urma_sge_t src_sge{};
        src_sge.addr = bufAddr;
        src_sge.tseg = ltseg;
        src_sge.len = bufSize;

        urma_sge_t dst_sge{};
        dst_sge.addr = dstBufAddr;
        dst_sge.tseg = dstSeg;
        dst_sge.len = bufSize;

        urma_jfs_wr_t wr{};
        wr.user_ctx = context;
        wr.rw.src.sge = &dst_sge;
        wr.rw.src.num_sge = 1;
        wr.rw.dst.sge = &src_sge;
        wr.rw.dst.num_sge = 1;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_READ;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;

        auto ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            ret = HcomUrma::UnimportSeg(dstSeg);
            if (NN_UNLIKELY(ret != 0)) {
                NN_LOG_WARN("Unable to unImport Seg " << mName << ", result " << ret);
            }
            NN_LOG_ERROR("Failed to post read request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_READ_FAILED;
        }
        ret = HcomUrma::UnimportSeg(dstSeg);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_WARN("Unable to unImport Seg " << mName << ", result " << ret);
        }
        return UB_OK;
    }

    inline UResult PostWrite(uintptr_t bufAddr, urma_target_seg_t *srcSeg, uintptr_t dstBufAddr,
        urma_target_seg_t *dstSeg, uint32_t bufSize, uint64_t context)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        int ret = 0;

        urma_jfs_wr_t *bad_wr;
        urma_sge_t src_sge{};
        src_sge.addr = bufAddr;
        src_sge.len = bufSize;
        src_sge.tseg = srcSeg;

        urma_sge_t dst_sge{};
        dst_sge.addr = dstBufAddr;
        dst_sge.len = bufSize;
        dst_sge.tseg = dstSeg;

        urma_jfs_wr_t wr{};
        wr.user_ctx = context;
        wr.rw.src.sge = &src_sge;
        wr.rw.src.num_sge = 1;
        wr.rw.dst.sge = &dst_sge;
        wr.rw.dst.num_sge = 1;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_WRITE;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;

        ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_ERROR("Failed to post write request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_WRITE_FAILED;
        }

        return UB_OK;
    }

    /// @brief 发送 Write 请求，将数据从本端写入至对端
    /// @param bufAddr     [in] 本地 MR 源地址
    /// @param ltoken      [in] 本地 MR 访问 key
    /// @param dstBufAddr  [in] 对端 MR 地址
    /// @param rtoken      [in] 对端 MR 远程访问 key
    /// @param bufSize     [in] 写入大小
    /// @param context     [in] `UBOpContextInfo`, 事件完成时通过此 context 找回对应 Jetty 以及上层 Service 层的上下文
    UResult PostWrite(uintptr_t bufAddr, urma_target_seg_t *ltseg, uintptr_t dstBufAddr, uint64_t rtoken,
        uint32_t bufSize, uint64_t context)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }
        urma_target_seg_t *dstSeg = ImportSeg(dstBufAddr, bufSize, rtoken);
        if (dstSeg == nullptr) {
            NN_LOG_ERROR("Failed to import dstSeg");
            return UB_QP_POST_WRITE_FAILED;
        }

        urma_jfs_wr_t *bad_wr;
        urma_sge_t src_sge{};
        src_sge.addr = bufAddr;
        src_sge.tseg = ltseg;
        src_sge.len = bufSize;

        urma_sge_t dst_sge{};
        dst_sge.addr = dstBufAddr;
        dst_sge.tseg = dstSeg;
        dst_sge.len = bufSize;

        urma_jfs_wr_t wr{};
        wr.user_ctx = context;
        wr.rw.dst.sge = &dst_sge;
        wr.rw.dst.num_sge = 1;
        wr.rw.src.sge = &src_sge;
        wr.rw.src.num_sge = 1;
        wr.next = nullptr;
        wr.opcode = URMA_OPC_WRITE;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = mTargetJetty;

        auto ret = HcomUrma::PostJettySendWr(mUrmaJetty, &wr, &bad_wr);
        if (NN_UNLIKELY(ret != 0)) {
            ret = HcomUrma::UnimportSeg(dstSeg);
            if (NN_UNLIKELY(ret != 0)) {
                NN_LOG_WARN("Unable to unImport Seg " << mName << ", result " << ret);
            }
            NN_LOG_ERROR("Failed to post write request to jetty " << mName << ", result " << ret);
            return UB_QP_POST_WRITE_FAILED;
        }

        ret = HcomUrma::UnimportSeg(dstSeg);
        if (NN_UNLIKELY(ret != 0)) {
            NN_LOG_WARN("Unable to unImport Seg " << mName << ", result " << ret);
        }
        return UB_OK;
    }

    /// @brief 发送单边 read/write 请求，采用 sgl 方式
    /// @param iov       [in] 将要发送的向量。仅需填充 lAddress, lkey, rAddress, rkey 和 size.
    /// @param iovCount  [in] 向量长度
    /// @param context   [in] Service 层上下文
    /// @param isRead    [in] 是否选择发送 read 请求，当为 false 时发送 write 请求。
    /// @param ctxLen    [in] context 数组长度
    UResult PostOneSideSgl(UBSHcomNetTransSgeIov *iov, uint32_t iovCount, uint64_t *context,
        bool isRead, uint8_t ctxLen)
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr || mState != UBJettyState::READY)) {
            return UB_QP_NOT_INITIALIZED;
        }

        urma_jfs_wr_t *badWR;
        urma_jfs_wr_t wrList[NET_SGE_MAX_IOV] = {};
        urma_target_seg_t srcSeg[NET_SGE_MAX_IOV] = {};
        urma_sge_t src_sge[NET_SGE_MAX_IOV] = {};
        urma_target_seg_t *dstSeg[NET_SGE_MAX_IOV] = {};
        urma_sge_t dst_sge[NET_SGE_MAX_IOV] = {};
        UResult ret = UB_OK;
        uint32_t i = 0;
        for (; i < iovCount; i++) {
            FillUrmaTargetSeg(srcSeg[i], iov[i].lAddress, iov[i].size, iov[i].lKey);
            src_sge[i].addr = iov[i].lAddress;
            src_sge[i].len = iov[i].size;
            src_sge[i].tseg = static_cast<urma_target_seg_t *>(iov[i].srcSeg);

            dstSeg[i] = ImportSeg(iov[i].rAddress, iov[i].size, iov[i].rKey);
            if (dstSeg[i] == nullptr) {
                NN_LOG_ERROR("Failed to import dstSeg");
                ret = isRead ? UB_QP_POST_READ_FAILED : UB_QP_POST_WRITE_FAILED;
                break;
            }

            dst_sge[i].addr = iov[i].rAddress;
            dst_sge[i].len = iov[i].size;
            dst_sge[i].tseg = dstSeg[i];

            auto &wr = wrList[i];
            wr.user_ctx = context[i];
            wr.rw.src.num_sge = 1;
            wr.rw.dst.num_sge = 1;
            if (isRead) {
                wr.opcode = URMA_OPC_READ;
                wr.rw.src.sge = &dst_sge[i];
                wr.rw.dst.sge = &src_sge[i];
            } else {
                wr.opcode = URMA_OPC_WRITE;
                wr.rw.src.sge = &src_sge[i];
                wr.rw.dst.sge = &dst_sge[i];
            }
            wr.next = (i + 1 == iovCount) ? nullptr : &wrList[i + 1];
            wr.tjetty = mTargetJetty;
            wr.flag.bs.complete_enable = 1;
        }

        if (ret == UB_OK) {
            auto result = HcomUrma::PostJettySendWr(mUrmaJetty, wrList, NET_SGE_MAX_IOV, &badWR);
            if (NN_UNLIKELY(result != 0)) {
                NN_LOG_ERROR("Urma failed to post oneSide request to jetty " << mName << ", result " << result);
                ret = isRead ? UB_QP_POST_READ_FAILED : UB_QP_POST_WRITE_FAILED;
            }
        }

        for (uint32_t index = 0; index < i; ++index) {
            auto result = HcomUrma::UnimportSeg(dstSeg[index]);
            if (NN_UNLIKELY(result != 0)) {
                NN_LOG_WARN("Unable to unImport Seg " << mName << ", result " << result);
            }
        }
        return ret;
    }

    inline uint32_t GetId() const
    {
        return mId;
    }

    inline void SetUpId(uint64_t id)
    {
        mUpId = id;
    }

    inline uint64_t GetUpId() const
    {
        return mUpId;
    }

    inline const std::string &GetName() const
    {
        return mName;
    }

    inline void SetName(const std::string &value)
    {
        mName = value;
    }

    inline const std::string &GetPeerIpAndPort() const
    {
        return mPeerIpPort;
    }

    void SetPeerIpAndPort(const std::string &value);

    uint32_t GetPostSendMaxSize() const;

    inline uint8_t GetPortNum() const
    {
        return mUBContext->mPortNumber;
    }

    inline void SetUpContext(uintptr_t ctx)
    {
        mUpContext = ctx;
    }

    inline uintptr_t GetUpContext() const
    {
        return mUpContext;
    }

    inline void SetUpContext1(uintptr_t ctx)
    {
        mUpContext1 = ctx;
    }

    inline uintptr_t GetUpContext1() const
    {
        return mUpContext1;
    }

    inline uint32_t GetJettyId()
    {
        if (mUrmaJetty != nullptr) {
            return mUrmaJetty->jetty_id.id;
        }
        return 0;
    }

    bool GetFreeBuff(uintptr_t &item);
    bool ReturnBuffer(uintptr_t value);
    bool GetFreeBufferN(uintptr_t *&items, uint32_t n);
    uint64_t GetLKey();
    urma_target_seg_t *GetMemorySeg();

    inline void AddOpCtxInfo(UBOpContextInfo *ctxInfo)
    {
        if (NN_LIKELY(ctxInfo != nullptr)) {
            // bi-direction linked list, 4 step to insert to head
            ctxInfo->prev = &mCtxPosted;
            mLock.Lock();
            // head -><- first -><- second -><- third -> nullptr
            // insert into the head place
            ctxInfo->next = mCtxPosted.next;
            if (mCtxPosted.next != nullptr) {
                mCtxPosted.next->prev = ctxInfo;
            }
            mCtxPosted.next = ctxInfo;
            ++mCtxPostedCount;
            mLock.Unlock();
        }
    }

    inline void RemoveOpCtxInfo(UBOpContextInfo *ctxInfo)
    {
        if (NN_LIKELY(ctxInfo != nullptr)) {
            // bi-direction linked list, 4 step to remove one
            mLock.Lock();

            // repeat remove
            if (ctxInfo->prev == nullptr) {
                mLock.Unlock();
                return;
            }

            // head-><- first -><- second -><- third -> nullptr
            ctxInfo->prev->next = ctxInfo->next;
            if (ctxInfo->next != nullptr) {
                ctxInfo->next->prev = ctxInfo->prev;
            }
            --mCtxPostedCount;

            ctxInfo->prev = nullptr;
            ctxInfo->next = nullptr;
            mLock.Unlock();
        }
    }

    // need to call this when qp broken, to get these contexts to return mrs
    inline void GetCtxPosted(UBOpContextInfo *&remaining)
    {
        mLock.Lock();
        // head -> first -><- second -><- third -> nullptr
        remaining = mCtxPosted.next;
        mCtxPosted.next = nullptr;
        mCtxPostedCount = 0;
        mLock.Unlock();
    }

    /// 获取 Jetty 发送队列的长度。
    inline uint32_t GetSendQueueSize()
    {
        int32_t ref = __sync_fetch_and_add(&mPostSendRef, 0);
        ref = std::max(0, std::min(ref, mPostSendMaxWr));
        return static_cast<uint32_t>(mPostSendMaxWr - ref);
    }

    /// 获取所有提交至 Jetty 队列中的任务个数，总数为 PostReceive + PostSend 族
    /// 函数的和。因为 RDMA 有 prePostReceive 机制，所以它的值一般会大于等于
    /// prePostReceiveSizePerQP 的值。
    /// \see prePostReceiveSizePerQP
    inline uint32_t GetPostedCount()
    {
        mLock.Lock();
        auto tmp = mCtxPostedCount;
        mLock.Unlock();
        return tmp;
    }

    inline bool GetPostSendWr(uint32_t times = NN_NO8, uint32_t sleepUs = NN_NO64)
    {
        while (times-- > 0) {
            if (NN_LIKELY(__sync_sub_and_fetch(&mPostSendRef, 1) >= 0)) {
                return true;
            }
            __sync_add_and_fetch(&mPostSendRef, 1);
            usleep(sleepUs);
        }
        return false;
    }

    inline void ReturnPostSendWr()
    {
        int32_t ref = __sync_add_and_fetch(&mPostSendRef, 1);
        if (ref > mPostSendMaxWr) {
            NN_LOG_WARN("[UB] Posted send requests " << ref << " over capacity " << mPostSendMaxWr);
        }
    }

    inline bool GetOneSideWr(uint32_t times = NN_NO8, uint32_t sleepUs = NN_NO64)
    {
        while (times-- > 0) {
            if (NN_LIKELY(__sync_sub_and_fetch(&mOneSideRef, 1) >= 0)) {
                return true;
            }
            __sync_add_and_fetch(&mOneSideRef, 1);
            usleep(sleepUs);
        }
        return false;
    }

    inline void ReturnOneSideWr()
    {
        int32_t ref = __sync_add_and_fetch(&mOneSideRef, 1);
        if (ref > mOneSideMaxWr) {
            NN_LOG_WARN("[UB] Posted one side requests " << ref << " over capacity " << mOneSideMaxWr);
        }
    }

    // UBC Heartbeat
    NResult CreateHBMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr);
    void DestroyHBMemoryRegion(UBSHcomNetMemoryRegionPtr &mr);

    inline uintptr_t GetNextLocalHBAddress()
    {
        uint64_t nextOffset = __sync_fetch_and_add(&mLocalNextOffset, NN_NO4) % mHBLocalMr->Size();
        return mHBLocalMr->GetAddress() + nextOffset;
    }

    inline uint64_t GetLocalHBKey() const
    {
        return mHBLocalMr->GetLKey();
    }

    void GetRemoteHbInfo(UBJettyExchangeInfo &info)
    {
        uint64_t nextOffset = __sync_fetch_and_add(&mRemoteNextOffset, NN_NO4) % mHBRemoteMr->Size();
        info.hbAddress = mHBRemoteMr->GetAddress() + nextOffset;
        info.hbKey = mHBRemoteMr->GetLKey();
        info.hbMrSize = NN_NO4;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

public:
    static uint32_t NewId()
    {
        return __sync_fetch_and_add(&G_INDEX, 1);
    }

    inline uint32_t QpNum() const
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr)) {
            return 0xffffffff;
        }

        return mUrmaJettyId;
    }

    inline uint32_t PostRegMrSize() const
    {
        return mJettyOptions.mrSegSize;
    }

    UBJettyState State() const
    {
        return mState;
    }

    // stop jetty
    UResult Stop();

private:
    void FillJettyCfg(urma_jetty_cfg_t &jetty_cfg, uintptr_t seg_pa, uintptr_t seg_va, uint32_t seg_len,
        uint32_t seg_count);
    void FillJfsCfg(urma_jfs_cfg_t *jfs_cfg);
    void FillJfrCfg(urma_jfr_cfg_t *jfr_cfg, uint32_t token = 0);
    UResult ChangeToInit(urma_jetty_attr_t &attr);
    UResult ChangeToReceive(UBJettyExchangeInfo &exInfo, urma_jetty_attr_t &attr);
    UResult ChangeToSend(urma_jetty_attr_t &attr);
    UResult SetMaxSendWrConfig(UBJettyExchangeInfo &exInfo);

private:
    std::string mName;
    std::string mPeerIpPort;
    uint32_t mId = 0;
    uint64_t mUpId = 0;
    std::atomic<UBJettyState> mState{UBJettyState::RESET};
    std::mutex mStopMutex;

    UBContext *mUBContext = nullptr;
    UBJfc *mSendJfc = nullptr;
    UBJfc *mRecvJfc = nullptr;
    urma_jfr_t *mJfr = nullptr;
    JettyOptions mJettyOptions{};
    uint32_t mUrmaJettyId = 0; // mUrmaJetty->jetty_id.id
    urma_jetty_t *mUrmaJetty = nullptr;
    urma_target_jetty_t *mTargetJetty = nullptr;
    std::unique_ptr<UBJettyExchangeInfo> mRemoteJettyInfo; // 对端建链时交换信息
    uintptr_t mUpContext = 0;
    uintptr_t mUpContext1 = 0;
    NetSpinLock mLock;
    UBOpContextInfo mCtxPosted{};
    uint32_t mCtxPostedCount{ 0 };
    UBMemoryRegionFixedBuffer *mJettyMr = nullptr;

    int32_t mOneSideMaxWr = JETTY_MAX_SEND_WR - NN_NO64;
    int32_t mOneSideRef = JETTY_MAX_SEND_WR - NN_NO64;
    int32_t mPostSendMaxWr = NN_NO64;
    uint32_t mPostSendMaxSize = NN_NO1024;
    int32_t mPostSendRef = NN_NO64;

    UBSHcomNetMemoryRegionPtr mHBLocalMr = nullptr;
    UBSHcomNetMemoryRegionPtr mHBRemoteMr = nullptr;
    uint64_t mLocalNextOffset = 0;
    uint64_t mRemoteNextOffset = 0;

    friend class NetDriverUBWithOob;
    friend class NetHeartbeat;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    static uint32_t G_INDEX;
};
} // namespace hcom
} // namespace ock

#endif
#endif // HCOM_UB_URMA_WRAPPER_JETTY_H
