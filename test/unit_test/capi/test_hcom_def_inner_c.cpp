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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_def_inner_c.h"
#include "net_rdma_async_endpoint.h"

namespace ock {
namespace hcom {
class TestHcomDefInnerC : public testing::Test {
public:
    TestHcomDefInnerC();
    virtual void SetUp(void);
    virtual void TearDown(void);
    EpHdlAdp *testEpHdlAdp = nullptr;
    EpOpHdlAdp *testEpOpHdlAdp = nullptr;
    OOBSecInfoProviderAdp *testOobSecInfoProviderAdp = nullptr;
    OOBSecInfoValidatorAdp *testOobSecInfoValidatorAdp = nullptr;
    OOBPskUseSessionAdp *testOobPskUseSessionAdp = nullptr;
    OOBPskFindSessionAdp *testOobPskFindSessionAdp = nullptr;
    EpIdleHdlAdp *testEpIdleHdlAdp = nullptr;
    EpTLSHdlAdp *testEpTLSHdlAdp = nullptr;
    ServiceHdlAdp *testServiceHdlAdp = nullptr;
    ChannelOpHdlAdp *testChannelOpHdlAdp = nullptr;
    ServiceIdleHdlAdp *testServiceIdleHdlAdp = nullptr;
};

TestHcomDefInnerC::TestHcomDefInnerC() {}

void TestHcomDefInnerC::SetUp()
{
    Net_EPHandlerType t = C_EP_NEW;
    Net_EPHandler h = nullptr;
    Net_RequestHandler handler = nullptr;
    uint64_t usrCtx = 0;
    Net_SecInfoProvider provider = nullptr;
    Net_SecInfoValidator validator = nullptr;
    Net_PskUseSessionCb cb = nullptr;
    Net_PskFindSessionCb FindCb = nullptr;
    Net_IdleHandler idleHandler = nullptr;
    ubs_hcom_service_handler_type serviceHandlerType = C_CHANNEL_NEW;
    ubs_hcom_service_channel_policy policy = C_CHANNEL_BROKEN_ALL;
    ubs_hcom_service_channel_handler channelHandler = nullptr;
    ubs_hcom_service_request_handler requestHandler = nullptr;
    testEpHdlAdp = new (std::nothrow) EpHdlAdp(t, h, usrCtx);
    testEpOpHdlAdp = new (std::nothrow) EpOpHdlAdp(handler, usrCtx);
    testOobSecInfoProviderAdp = new (std::nothrow) OOBSecInfoProviderAdp(provider);
    testOobSecInfoValidatorAdp = new (std::nothrow) OOBSecInfoValidatorAdp(validator);
    testOobPskUseSessionAdp = new (std::nothrow) OOBPskUseSessionAdp(cb);
    testOobPskFindSessionAdp = new (std::nothrow) OOBPskFindSessionAdp(FindCb);
    testEpIdleHdlAdp = new (std::nothrow) EpIdleHdlAdp(idleHandler, usrCtx);
    testEpTLSHdlAdp = new (std::nothrow) EpTLSHdlAdp();
    testServiceHdlAdp = new (std::nothrow) ServiceHdlAdp(serviceHandlerType, policy, channelHandler, usrCtx);
    testChannelOpHdlAdp = new (std::nothrow) ChannelOpHdlAdp(requestHandler, usrCtx);
    testServiceIdleHdlAdp = new (std::nothrow) ServiceIdleHdlAdp(idleHandler, usrCtx);
}

void TestHcomDefInnerC::TearDown()
{
    if (testEpHdlAdp != nullptr) {
        delete testEpHdlAdp;
        testEpHdlAdp = nullptr;
    }

    if (testEpOpHdlAdp != nullptr) {
        delete testEpOpHdlAdp;
        testEpOpHdlAdp = nullptr;
    }

    if (testOobSecInfoProviderAdp != nullptr) {
        delete testOobSecInfoProviderAdp;
        testOobSecInfoProviderAdp = nullptr;
    }

    if (testOobSecInfoValidatorAdp != nullptr) {
        delete testOobSecInfoValidatorAdp;
        testOobSecInfoValidatorAdp = nullptr;
    }

    if (testOobPskUseSessionAdp != nullptr) {
        delete testOobPskUseSessionAdp;
        testOobPskUseSessionAdp = nullptr;
    }

    if (testOobPskFindSessionAdp != nullptr) {
        delete testOobPskFindSessionAdp;
        testOobPskFindSessionAdp = nullptr;
    }

    if (testEpIdleHdlAdp != nullptr) {
        delete testEpIdleHdlAdp;
        testEpIdleHdlAdp = nullptr;
    }

    if (testEpTLSHdlAdp != nullptr) {
        delete testEpTLSHdlAdp;
        testEpTLSHdlAdp = nullptr;
    }

    if (testServiceHdlAdp != nullptr) {
        delete testServiceHdlAdp;
        testServiceHdlAdp = nullptr;
    }

    if (testChannelOpHdlAdp != nullptr) {
        delete testChannelOpHdlAdp;
        testChannelOpHdlAdp = nullptr;
    }

    if (testServiceIdleHdlAdp != nullptr) {
        delete testServiceIdleHdlAdp;
        testServiceIdleHdlAdp = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestHcomDefInnerC, EpHdlAdpNewEndPointNullErr)
{
    uint64_t epId = NN_NO8;
    UBSHcomNetWorkerIndex workerIndex{};
    uint32_t workerIdx = 5;
    uint32_t gIdx = 6;
    uint16_t dIdx = 7;
    workerIndex.Set(workerIdx, gIdx, dIdx);
    UBSHcomNetEndpointPtr newEP = new (std::nothrow) NetAsyncEndpoint(epId, nullptr, nullptr, workerIndex);
    std::string ipPort = "1.2.3.4:1234";
    std::string payload = "payload";

    testEpHdlAdp->mHandler = nullptr;
    int ret = testEpHdlAdp->NewEndPoint(ipPort, newEP, payload);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, EpHdlAdpEndPointBrokenNullErr)
{
    uint64_t epId = NN_NO8;
    UBSHcomNetWorkerIndex workerIndex{};
    uint32_t workerIdx = 5;
    uint32_t gIdx = 6;
    uint16_t dIdx = 7;
    workerIndex.Set(workerIdx, gIdx, dIdx);
    UBSHcomNetEndpointPtr newEP = new (std::nothrow) NetAsyncEndpoint(epId, nullptr, nullptr, workerIndex);

    testEpHdlAdp->mHandler = nullptr;
    EXPECT_NO_FATAL_FAILURE(testEpHdlAdp->EndPointBroken(newEP));
}

TEST_F(TestHcomDefInnerC, EpOpHdlAdpRequestedNullErr)
{
    UBSHcomNetRequestContext ctx{};
    testEpOpHdlAdp->mHandler = nullptr;
    int ret = testEpOpHdlAdp->Requested(ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, EpOpHdlAdpRequestedSentMemCpyErr)
{
    UBSHcomNetRequestContext ctx{};
    ctx.mOpType = UBSHcomNetRequestContext::NN_SENT;
    testEpOpHdlAdp->mHandler = [](Net_RequestContext *, uint64_t) { return 1; };
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    int ret = testEpOpHdlAdp->Requested(ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, EpOpHdlAdpRequestedWrittenMemCpyErr)
{
    UBSHcomNetRequestContext ctx{};
    ctx.mOpType = UBSHcomNetRequestContext::NN_WRITTEN;
    testEpOpHdlAdp->mHandler = [](Net_RequestContext *, uint64_t) { return 1; };
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    int ret = testEpOpHdlAdp->Requested(ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, EpOpHdlAdpRequestedSglWrittenMemCpyErr)
{
    UBSHcomNetRequestContext ctx{};
    ctx.mOpType = UBSHcomNetRequestContext::NN_SGL_WRITTEN;
    testEpOpHdlAdp->mHandler = [](Net_RequestContext *, uint64_t) { return 1; };
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    int ret = testEpOpHdlAdp->Requested(ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, OOBSecInfoProviderAdpCreateSecInfoNullErr)
{
    testOobSecInfoProviderAdp->mProvider = nullptr;
    UBSHcomNetDriverSecType type = NET_SEC_DISABLED;
    int64_t flag = 0;
    char *output = nullptr;
    uint32_t outLen = 0;
    bool needAutoFree = true;
    int ret = testOobSecInfoProviderAdp->CreateSecInfo(0, flag, type, output, outLen, needAutoFree);
    EXPECT_EQ(ret, -1);
}

TEST_F(TestHcomDefInnerC, OOBSecInfoProviderAdpCreateSecInfo)
{
    testOobSecInfoProviderAdp->mProvider
        = [](uint64_t, int64_t *, Net_DriverSecType *, char **, uint32_t *, int *e) { return 0; };
    UBSHcomNetDriverSecType type = NET_SEC_DISABLED;
    int64_t flag = 0;
    char *output = nullptr;
    uint32_t outLen = 0;
    bool needAutoFree = true;
    int ret = testOobSecInfoProviderAdp->CreateSecInfo(0, flag, type, output, outLen, needAutoFree);
    EXPECT_EQ(ret, 0);

    type = NET_SEC_VALID_ONE_WAY;
    ret = testOobSecInfoProviderAdp->CreateSecInfo(0, flag, type, output, outLen, needAutoFree);
    EXPECT_EQ(ret, 0);

    type = NET_SEC_VALID_TWO_WAY;
    ret = testOobSecInfoProviderAdp->CreateSecInfo(0, flag, type, output, outLen, needAutoFree);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestHcomDefInnerC, OOBSecInfoValidatorAdpSecInfoValidate)
{
    testOobSecInfoValidatorAdp->mValidator = nullptr;
    int64_t flag = 0;
    char *intput = nullptr;
    uint32_t inputLen = 0;
    int ret = testOobSecInfoValidatorAdp->SecInfoValidate(0, flag, intput, inputLen);
    EXPECT_EQ(ret, -1);

    testOobSecInfoValidatorAdp->mValidator = [](uint64_t, int64_t, const char *, uint32_t) { return 0; };
    ret = testOobSecInfoValidatorAdp->SecInfoValidate(0, flag, intput, inputLen);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestHcomDefInnerC, OOBPskUseSessionAdpUseSession)
{
    testOobPskUseSessionAdp->mCb = nullptr;
    void *ssl = nullptr;
    const void *md = nullptr;
    const unsigned char **id = nullptr;
    size_t *idLen = nullptr;
    void **session = nullptr;
    int ret = testOobPskUseSessionAdp->UseSession(ssl, md, id, idLen, session);
    EXPECT_EQ(ret, 0);

    testOobPskUseSessionAdp->mCb = [](void *, const void *, const unsigned char **, size_t *, void **) { return 1; };
    ret = testOobPskUseSessionAdp->UseSession(ssl, md, id, idLen, session);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestHcomDefInnerC, OOBPskFindSessionAdpFindSession)
{
    testOobPskFindSessionAdp->mCb = nullptr;
    void *ssl = nullptr;
    unsigned char *identity = nullptr;
    size_t identityLen = 0;
    void **session = nullptr;
    int ret = testOobPskFindSessionAdp->FindSession(ssl, identity, identityLen, session);
    EXPECT_EQ(ret, 0);

    testOobPskFindSessionAdp->mCb = [](void *, const unsigned char *, size_t, void **) { return 1; };
    ret = testOobPskFindSessionAdp->FindSession(ssl, identity, identityLen, session);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestHcomDefInnerC, EpIdleHdlAdpIdleHandlerNullErr)
{
    testEpIdleHdlAdp->mHandler = nullptr;
    UBSHcomNetWorkerIndex index{};
    EXPECT_NO_FATAL_FAILURE(testEpIdleHdlAdp->Idle(index));
}

TEST_F(TestHcomDefInnerC, EpTLSHdlAdpTLSCertificationCallbackErr)
{
    testEpTLSHdlAdp->mGetCert = nullptr;
    std::string name = "name";
    std::string path = "path";
    bool ret = testEpTLSHdlAdp->UBSHcomTLSCertificationCallback(name, path);
    EXPECT_EQ(ret, false);

    testEpTLSHdlAdp->mGetCert = [](const char *, char **) { return 0; };
    ret = testEpTLSHdlAdp->UBSHcomTLSCertificationCallback(name, path);
    EXPECT_EQ(ret, false);
}

TEST_F(TestHcomDefInnerC, EpTLSHdlAdpTLSPrivateKeyCallbackErr)
{
    testEpTLSHdlAdp->mGetPriKey = nullptr;
    std::string name = "name";
    std::string path = "path";
    void *keyPass = nullptr;
    int len = 0;
    UBSHcomTLSEraseKeypass cb;
    bool ret = testEpTLSHdlAdp->UBSHcomTLSPrivateKeyCallback(name, path, keyPass, len, cb);
    EXPECT_EQ(ret, false);

    testEpTLSHdlAdp->mGetPriKey = [](const char *, char **, char **, Net_TlsKeyPassErase *) { return 0; };
    ret = testEpTLSHdlAdp->UBSHcomTLSPrivateKeyCallback(name, path, keyPass, len, cb);
    EXPECT_EQ(ret, false);
}

TEST_F(TestHcomDefInnerC, EpTLSHdlAdpTLSCaCallbackErr)
{
    testEpTLSHdlAdp->mGetCA = nullptr;
    std::string name = "name";
    std::string caPath = "caPath";
    std::string crlPath = "crlPath";
    UBSHcomPeerCertVerifyType peerCertVerifyType = VERIFY_BY_DEFAULT;
    UBSHcomTLSCertVerifyCallback cb;
    bool ret = testEpTLSHdlAdp->UBSHcomTLSCaCallback(name, caPath, crlPath, peerCertVerifyType, cb);
    EXPECT_EQ(ret, false);

    testEpTLSHdlAdp->mGetCA
        = [](const char *, char **, char **, Net_PeerCertVerifyType *, Net_TlsCertVerify *) { return 0; };
    ret = testEpTLSHdlAdp->UBSHcomTLSCaCallback(name, caPath, crlPath, peerCertVerifyType, cb);
    EXPECT_EQ(ret, false);
}

TEST_F(TestHcomDefInnerC, ServiceHdlAdpNewChannelErr)
{
    testServiceHdlAdp->mHandler = nullptr;
    std::string ipPort = "1.2.3.4:1234";
    NetChannelPtr newCh = nullptr;
    std::string payload = "payload";
    int ret = testServiceHdlAdp->NewChannel(ipPort, newCh, payload);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, ServiceHdlAdpChannelBrokenErr)
{
    testServiceHdlAdp->mHandler = nullptr;
    NetChannelPtr ch = nullptr;
    EXPECT_NO_FATAL_FAILURE(testServiceHdlAdp->ChannelBroken(ch));
}

TEST_F(TestHcomDefInnerC, ChannelOpHdlAdpRequestedErr)
{
    testChannelOpHdlAdp->mHandler = nullptr;
    NetServiceContext ctx{};
    int ret = testChannelOpHdlAdp->Requested(ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestHcomDefInnerC, ServiceIdleHdlAdpIdle)
{
    testServiceIdleHdlAdp->mServiceHandler = nullptr;
    UBSHcomNetWorkerIndex index{};
    EXPECT_NO_FATAL_FAILURE(testServiceIdleHdlAdp->Idle(index));

    testServiceIdleHdlAdp->mServiceHandler = [](uint8_t, uint16_t, uint64_t) {};
    EXPECT_NO_FATAL_FAILURE(testServiceIdleHdlAdp->Idle(index));
}
}
}