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
#ifndef HCOM_CAPI_V2_HCOM_DEF_INNER_C_H_
#define HCOM_CAPI_V2_HCOM_DEF_INNER_C_H_

#include <unordered_set>
#include <memory>

#include "hcom_c.h"
#include "service_v2/api/hcom_service.h"
#include "hcom_service_c.h"
#include "securec.h"

namespace ock {
namespace hcom {
class EpHdlAdp {
public:
    EpHdlAdp(ubs_hcom_ep_handler_type t, ubs_hcom_ep_handler h, uint64_t usrCtx) : mHandlerType(t),
        mHandler(h), mUsrCtx(usrCtx) {}
    ~EpHdlAdp()
    {
        mHandler = nullptr;
    }

    int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
    {
        if (NN_UNLIKELY(mHandler == nullptr || newEP.Get() == nullptr || mHandlerType != C_EP_NEW)) {
            return NN_INVALID_PARAM;
        }
        newEP->IncreaseRef();
        return mHandler(reinterpret_cast<ubs_hcom_endpoint>(newEP.Get()), mUsrCtx, payload.c_str());
    }

    void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
    {
        if (NN_UNLIKELY(mHandler == nullptr || ep.Get() == nullptr || mHandlerType != C_EP_BROKEN)) {
            return;
        }

        mHandler(reinterpret_cast<ubs_hcom_endpoint>(ep.Get()), mUsrCtx, ep->PeerConnectPayload().c_str());
    }

private:
    ubs_hcom_ep_handler_type mHandlerType;
    ubs_hcom_ep_handler mHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

class EpOpHdlAdp {
public:
    explicit EpOpHdlAdp(ubs_hcom_request_handler handler, uint64_t usrCtx) : mHandler(handler), mUsrCtx(usrCtx) {}

    ~EpOpHdlAdp()
    {
        mHandler = nullptr;
    }

    static void BuildRequestCommonFiled(const UBSHcomNetRequestContext &ctx, ubs_hcom_request_context &localCtx)
    {
        (localCtx).result = static_cast<NResult>((ctx).Result());
        (localCtx).type = static_cast<ubs_hcom_request_type>((ctx).OpType());
        (localCtx).opCode = (ctx).Header().opCode;
        (localCtx).ep = reinterpret_cast<ubs_hcom_endpoint>((ctx).EndPoint().Get());
        (localCtx).seqNo = (ctx).Header().seqNo;
        (localCtx).flags = (ctx).Header().flags;
        (localCtx).errorCode = (ctx).Header().errorCode;
        (localCtx).timeout = (ctx).Header().timeout;
        if ((ctx).Message() != nullptr) {
            (localCtx).msgData = (ctx).Message()->Data();
            (localCtx).msgSize = (ctx).Header().dataLength;
        }
    }

    int Requested(const UBSHcomNetRequestContext &ctx)
    {
        if (NN_UNLIKELY(mHandler == nullptr)) {
            return NN_INVALID_PARAM;
        }

        static thread_local ubs_hcom_request_context localCtx {};
        bzero(&localCtx, sizeof(ubs_hcom_request_context));
        BuildRequestCommonFiled(ctx, localCtx);

        if (ctx.OpType() == UBSHcomNetRequestContext::NN_SENT ||
            ctx.OpType() == UBSHcomNetRequestContext::NN_SENT_RAW) {
            localCtx.originalSend.data = 0;
            localCtx.originalSend.size = ctx.OriginalRequest().size;
            localCtx.originalSend.upCtxSize = ctx.OriginalRequest().upCtxSize;
            if (NN_UNLIKELY(memcpy_s(localCtx.originalSend.upCtxData, sizeof(localCtx.originalSend.upCtxData),
                ctx.OriginalRequest().upCtxData, sizeof(ctx.OriginalRequest().upCtxData)) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy up ctx data");
                return NN_INVALID_PARAM;
            }
        } else if (ctx.OpType() == UBSHcomNetRequestContext::NN_WRITTEN ||
            ctx.OpType() == UBSHcomNetRequestContext::NN_READ) {
            localCtx.originalReq.lMRA = ctx.OriginalRequest().lAddress;
            localCtx.originalReq.rMRA = ctx.OriginalRequest().rAddress;
            localCtx.originalReq.lKey = ctx.OriginalRequest().lKey;
            localCtx.originalReq.rKey = ctx.OriginalRequest().rKey;
            localCtx.originalReq.size = ctx.OriginalRequest().size;
            localCtx.originalReq.upCtxSize = ctx.OriginalRequest().upCtxSize;
            if (NN_UNLIKELY(memcpy_s(localCtx.originalReq.upCtxData, sizeof(localCtx.originalReq.upCtxData),
                ctx.OriginalRequest().upCtxData, sizeof(ctx.OriginalRequest().upCtxData)) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy up ctx data");
                return NN_INVALID_PARAM;
            }
        } else if (ctx.OpType() == UBSHcomNetRequestContext::NN_SGL_WRITTEN ||
            ctx.OpType() == UBSHcomNetRequestContext::NN_SGL_READ ||
            ctx.OpType() == UBSHcomNetRequestContext::NN_SENT_RAW_SGL) {
            localCtx.originalSglReq.iov = reinterpret_cast<ubs_hcom_readwrite_sge *>(ctx.OriginalSgeRequest().iov);
            localCtx.originalSglReq.iovCount = ctx.OriginalSgeRequest().iovCount;
            localCtx.originalSglReq.upCtxSize = ctx.OriginalSgeRequest().upCtxSize;
            if (NN_UNLIKELY(memcpy_s(localCtx.originalSglReq.upCtxData, sizeof(localCtx.originalSglReq.upCtxData),
                ctx.OriginalSgeRequest().upCtxData, sizeof(ctx.OriginalSgeRequest().upCtxData)) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy up ctx data");
                return NN_INVALID_PARAM;
            }
        }

        return mHandler(&localCtx, mUsrCtx);
    }

private:
    ubs_hcom_request_handler mHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

class OOBSecInfoProviderAdp {
public:
    explicit OOBSecInfoProviderAdp(ubs_hcom_secinfo_provider provider) : mProvider(provider) {}

    ~OOBSecInfoProviderAdp()
    {
        mProvider = nullptr;
    }
    int CreateSecInfo(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen,
        bool &needAutoFree)
    {
        if (NN_UNLIKELY(mProvider == nullptr)) {
            return -1;
        }

        auto driSecType = static_cast<ubs_hcom_driver_sec_type>(0);
        int needFree = 0;
        auto ret = mProvider(ctx, &flag, &driSecType, &output, &outLen, &needFree);
        if (ret != 0) {
            return ret;
        }

        if (driSecType == C_NET_SEC_DISABLED) {
            type = NET_SEC_DISABLED;
        } else if (driSecType == C_NET_SEC_ONE_WAY) {
            type = NET_SEC_VALID_ONE_WAY;
        } else if (driSecType == C_NET_SEC_TWO_WAY) {
            type = NET_SEC_VALID_TWO_WAY;
        }

        if (needFree) {
            needAutoFree = true;
        }

        return ret;
    }

private:
    ubs_hcom_secinfo_provider mProvider = nullptr;
};

class OOBSecInfoValidatorAdp {
public:
    explicit OOBSecInfoValidatorAdp(ubs_hcom_secinfo_validator validator) : mValidator(validator) {}

    ~OOBSecInfoValidatorAdp()
    {
        mValidator = nullptr;
    }

    int SecInfoValidate(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
    {
        if (NN_UNLIKELY(mValidator == nullptr)) {
            return -1;
        }
        return mValidator(ctx, flag, input, inputLen);
    }

private:
    ubs_hcom_secinfo_validator mValidator = nullptr;
};

class EpIdleHdlAdp {
public:
    explicit EpIdleHdlAdp(ubs_hcom_idle_handler handler, uint64_t usrCtx) : mHandler(handler), mUsrCtx(usrCtx) {}

    ~EpIdleHdlAdp()
    {
        mHandler = nullptr;
    }

    void Idle(const UBSHcomNetWorkerIndex &index)
    {
        if (NN_UNLIKELY(mHandler == nullptr)) {
            return;
        }

        mHandler(index.grpIdx, index.idxInGrp, mUsrCtx);
    }

private:
    ubs_hcom_idle_handler mHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

class EpTLSHdlAdp {
public:
    EpTLSHdlAdp() = default;

    ~EpTLSHdlAdp() = default;

    inline void SetTLSCertCb(ubs_hcom_tls_get_cert_cb h)
    {
        mGetCert = h;
    }

    inline void SetTLSCaCb(ubs_hcom_tls_get_ca_cb h)
    {
        mGetCA = h;
    }

    inline void SetTLSPrivateKeyCb(ubs_hcom_tls_get_pk_cb h)
    {
        mGetPriKey = h;
    }

    bool UBSHcomTLSPrivateKeyCallback(const std::string &name, std::string &path, void *&keyPass, int len,
        ::ock::hcom::UBSHcomTLSEraseKeypass &callback)
    {
        if (NN_UNLIKELY(mGetPriKey == nullptr)) {
            return false;
        }

        char *privateKeyPath = nullptr;
        char *keyPassWd = nullptr;
        ubs_hcom_tls_keypass_erase erase;

        mGetPriKey(name.c_str(), &privateKeyPath, &keyPassWd, &erase);

        if (NN_UNLIKELY(privateKeyPath == nullptr) || NN_UNLIKELY(keyPassWd == nullptr) ||
            NN_UNLIKELY(erase == nullptr)) {
            NN_LOG_INFO("Failed to get private key, key pass or erase function from callback");
            return false;
        }

        path = privateKeyPath;
        keyPass = keyPassWd;
        callback = std::bind(&EraseCB, erase, std::placeholders::_1, std::placeholders::_2);

        return true;
    }

    bool UBSHcomTLSCertificationCallback(const std::string &name, std::string &path)
    {
        if (NN_UNLIKELY(mGetCert == nullptr)) {
            return false;
        }

        char *certPath = nullptr;
        mGetCert(name.c_str(), &certPath);
        if (NN_UNLIKELY(certPath == nullptr)) {
            NN_LOG_INFO("Failed to get cert path from TLS cert callback.");
            return false;
        }

        path = certPath;

        return true;
    }

    static void EraseCB(ubs_hcom_tls_keypass_erase erase, void *pw, int len)
    {
        erase(reinterpret_cast<char *>(pw), len);
    }

    bool UBSHcomTLSCaCallback(const std::string &name, std::string &caPath, std::string &crlPath,
        UBSHcomPeerCertVerifyType &peerCertVerifyType, ::ock::hcom::UBSHcomTLSCertVerifyCallback &callback)
    {
        if (NN_UNLIKELY(mGetCA == nullptr)) {
            return false;
        }

        char *caPth = nullptr;
        char *crlPth = nullptr;
        ubs_hcom_tls_cert_verify verifyCb = nullptr;
        ubs_hcom_peer_cert_verify_type verifyType = C_VERIFY_BY_DEFAULT;

        mGetCA(name.c_str(), &caPth, &crlPth, &verifyType, &verifyCb);

        if (NN_UNLIKELY(caPth == nullptr)) {
            NN_LOG_INFO("Failed to get CA path from callback");
            return false;
        }

        caPath = caPth;

        if (crlPth != nullptr) {
            crlPath = crlPth;
        }

        callback = verifyCb;

        if (verifyType == C_VERIFY_BY_NONE) {
            peerCertVerifyType = VERIFY_BY_NONE;
        } else if (verifyType == C_VERIFY_BY_DEFAULT) {
            peerCertVerifyType = VERIFY_BY_DEFAULT;
        } else if (verifyType == C_VERIFY_BY_CUSTOM_FUNC) {
            peerCertVerifyType = VERIFY_BY_CUSTOM_FUNC;
        }
        return true;
    }

private:
    ubs_hcom_tls_get_cert_cb mGetCert = nullptr;
    ubs_hcom_tls_get_pk_cb mGetPriKey = nullptr;
    ubs_hcom_tls_get_ca_cb mGetCA = nullptr;
};

class HdlMgr {
public:
    void AddHdlAdp(uintptr_t adp)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        auto iterator = mHdlAdpSet.find(adp);
        if (iterator != mHdlAdpSet.end()) {
            return;
        }

        mHdlAdpSet.insert(adp);
    }

    template <typename C> void RemoveHdlAdp(uintptr_t adp)
    {
        uintptr_t hdlAddr = 0;
        {
            std::lock_guard<std::mutex> guard(mMutex);
            auto iterator = mHdlAdpSet.find(adp);
            if (iterator == mHdlAdpSet.end()) {
                return;
            }

            hdlAddr = *iterator;
            mHdlAdpSet.erase(iterator);
        }

        if (hdlAddr == 0) {
            NN_LOG_ERROR("Remove handle not found");
            return;
        }

        auto adpPtr = reinterpret_cast<C *>(hdlAddr);
        delete adpPtr;
    }

private:
    std::mutex mMutex;
    std::unordered_set<uintptr_t> mHdlAdpSet;
};

class ServiceHdlAdp {
public:
    ServiceHdlAdp(ubs_hcom_service_channel_handler_type t, ubs_hcom_service_channel_policy p,
        ubs_hcom_service_channel_handler h, uint64_t usrCtx)
        : mHandlerType(t), mHandler(h), mUsrCtx(usrCtx) {}
    ServiceHdlAdp(ubs_hcom_service_channel_handler_type t, ubs_hcom_service_channel_handler h, uint64_t usrCtx)
        : mHandlerType(t), mHandler(h), mUsrCtx(usrCtx) {}

    ~ServiceHdlAdp()
    {
        mHandler = nullptr;
    }

    int NewChannel(const std::string &ipPort, const UBSHcomChannelPtr &newCh, const std::string &payload)
    {
        if (NN_UNLIKELY(mHandler == nullptr || newCh.Get() == nullptr || mHandlerType != C_CHANNEL_NEW)) {
            return NN_INVALID_PARAM;
        }

        // increase ref and need call ep_destroy
        newCh->IncreaseRef();

        return mHandler(reinterpret_cast<ubs_hcom_endpoint>(newCh.Get()), mUsrCtx, payload.c_str());
    }

    void ChannelBroken(const UBSHcomChannelPtr &ch)
    {
        if (NN_UNLIKELY(mHandler == nullptr || ch.Get() == nullptr || mHandlerType != C_CHANNEL_BROKEN)) {
            return;
        }

        mHandler(reinterpret_cast<ubs_hcom_endpoint>(ch.Get()), mUsrCtx, ch->GetPeerConnectPayload().c_str());
    }

private:
    ubs_hcom_service_channel_handler_type mHandlerType;
    ubs_hcom_service_channel_handler mHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

class ServiceIdleHdlAdp {
public:
    explicit ServiceIdleHdlAdp(ubs_hcom_idle_handler handler, uint64_t usrCtx)
        : mServiceHandler(handler), mUsrCtx(usrCtx) {}
    ~ServiceIdleHdlAdp()
    {
        mServiceHandler = nullptr;
    }
    void Idle(const UBSHcomNetWorkerIndex &index)
    {
        if (NN_UNLIKELY(mServiceHandler == nullptr)) {
            return;
        }
        mServiceHandler(index.grpIdx, index.idxInGrp, mUsrCtx);
    }

private:
    ubs_hcom_idle_handler mServiceHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

class ChannelOpHdlAdp {
public:
    explicit ChannelOpHdlAdp(ubs_hcom_service_request_handler handler, uint64_t usrCtx)
        : mHandler(handler), mUsrCtx(usrCtx) {}

    ~ChannelOpHdlAdp()
    {
        mHandler = nullptr;
    }
    int Requested(const UBSHcomServiceContext &ctx)
    {
        if (NN_UNLIKELY(mHandler == nullptr)) {
            return NN_INVALID_PARAM;
        }

        return mHandler(reinterpret_cast<ubs_hcom_service_context>(&ctx), mUsrCtx);
    }

private:
    ubs_hcom_service_request_handler mHandler = nullptr;
    uint64_t mUsrCtx = 0;
};

template <typename C>
class ServiceHdlMgr {
public:
    // 添加属于特定Service的指针
    void AddHdlAdp(uintptr_t service, uintptr_t adp)
    {
        std::lock_guard<std::mutex> guard(mMutex);

        // 自动为不存在的Service创建条目
        auto& adpSet = mHdlAdpMap[service];
        if (adpSet.find(adp) != adpSet.end()) {
            return;  // 已存在则不重复添加
        }
        adpSet.insert(adp);
    }

    void RemoveAll(uintptr_t svc)
    {
        std::unordered_set<uintptr_t> adpToDelete;

        {   // 临界区开始
            std::lock_guard<std::mutex> guard(mMutex);
            auto svcIter = mHdlAdpMap.find(svc);
            if (svcIter == mHdlAdpMap.end()) {
                return;  // Service不存在
            }

            // 转移所有权到临时集合
            adpToDelete = std::move(svcIter->second);
            mHdlAdpMap.erase(svcIter);  // 立即移除Service条目
        }   // 临界区结束

        // 在锁外执行资源释放
        for (auto& adp : adpToDelete) {
            if (adp != 0) {  // 防御性检查
                delete reinterpret_cast<C*>(adp);
            }
        }
    }

private:
    std::mutex mMutex;
    std::unordered_map<uintptr_t, std::unordered_set<uintptr_t>> mHdlAdpMap;
};
}
}
#endif // HCOM_CAPI_V2_HCOM_DEF_INNER_C_H_
