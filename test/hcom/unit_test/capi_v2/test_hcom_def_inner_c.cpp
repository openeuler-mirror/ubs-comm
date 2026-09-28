/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

#include "hcom_def_inner_c.h"
#include "service_v2/api/hcom_service_context.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint64_t TEST_USR_CTX = 42;
constexpr int TEST_HANDLER_RET = 7;
constexpr uint8_t TEST_GRP_IDX = 3;
constexpr uint16_t TEST_IDX_IN_GRP = 5;
constexpr int TEST_SEC_RET = 5;
constexpr char TEST_PAYLOAD[] = "hello-payload";
constexpr char TEST_CERT_PATH[] = "/tmp/cert.pem";
constexpr char TEST_CA_PATH[] = "/tmp/ca.pem";
constexpr char TEST_CRL_PATH[] = "/tmp/crl.pem";
constexpr char TEST_KEY_PATH[] = "/tmp/server.key";
constexpr char TEST_KEY_PASS[] = "secret";
constexpr char TCP_URL[] = "tcp://127.0.0.1:9981";
constexpr char TEST_PEER[] = "peer";

// --- configurable outputs for the static C callbacks ---
int g_eraseCalls = 0;
ubs_hcom_driver_sec_type g_secTypeToReturn = C_NET_SEC_DISABLED;
int g_needFreeToReturn = 0;
char *g_certPathToReturn = nullptr;
char *g_keyPathToReturn = nullptr;
char *g_keyPassToReturn = nullptr;
ubs_hcom_tls_keypass_erase g_eraseToReturn = nullptr;
char *g_caPathToReturn = nullptr;
char *g_crlPathToReturn = nullptr;
ubs_hcom_peer_cert_verify_type g_verifyTypeToReturn = C_VERIFY_BY_DEFAULT;

// object with a live counter to verify HdlMgr delete behaviour
class TrackedObj {
public:
    TrackedObj()
    {
        alive++;
    }

    ~TrackedObj()
    {
        alive--;
    }

    static int alive;
};

int TrackedObj::alive = 0;

// --- endpoint stub, only used as a handle placeholder ---
class InnerTestEndpoint : public UBSHcomNetEndpoint {
public:
    InnerTestEndpoint() : UBSHcomNetEndpoint(1, UBSHcomNetWorkerIndex()) {}

    ~InnerTestEndpoint() override = default;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        (void)epOptions;
        return NN_OK;
    }

    uint32_t GetSendQueueCount() override
    {
        return 0;
    }

    const std::string &PeerIpAndPort() override
    {
        return mPeer;
    }

    const std::string &UdsName() override
    {
        return mPeer;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        ip = mPeer;
        port = 1;
        return false;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)opCode;
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
                     const UBSHcomNetTransOpInfo &opInfo) override
    {
        (void)opCode;
        (void)request;
        (void)opInfo;
        return NN_ERROR;
    }

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSendRawNoCpy(const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostRead(const UBSHcomNetTransRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostRead(const UBSHcomNetTransSglRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostWrite(const UBSHcomNetTransRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult WaitCompletion(int32_t timeout) override
    {
        (void)timeout;
        return NN_ERROR;
    }

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        (void)timeout;
        (void)ctx;
        return NN_ERROR;
    }

    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        (void)timeout;
        (void)ctx;
        return NN_ERROR;
    }

private:
    std::string mPeer = TEST_PEER;
};

// --- channel stub ---
class InnerTestChannel : public UBSHcomChannel {
public:
    int32_t Send(const UBSHcomRequest &req, const Callback *done) override
    {
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t Call(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback *done) override
    {
        (void)req;
        (void)rsp;
        (void)done;
        return NN_ERROR;
    }

    int32_t Reply(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback *done) override
    {
        (void)ctx;
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t CallWithHlc(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback *done) override
    {
        (void)req;
        (void)rsp;
        (void)done;
        return NN_ERROR;
    }

    int32_t ReplyWithHlc(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback *done) override
    {
        (void)ctx;
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t Put(const UBSHcomOneSideRequest &req, const Callback *done) override
    {
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t Get(const UBSHcomOneSideRequest &req, const Callback *done) override
    {
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t PutV(const UBSHcomOneSideSglRequest &req, const Callback *done) override
    {
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t GetV(const UBSHcomOneSideSglRequest &req, const Callback *done) override
    {
        (void)req;
        (void)done;
        return NN_ERROR;
    }

    int32_t SetFlowControlConfig(const UBSHcomFlowCtrlOptions &opt) override
    {
        (void)opt;
        return NN_ERROR;
    }

    void SetChannelTimeOut(int16_t oneSideTimeout, int16_t twoSideTimeout) override
    {
        (void)oneSideTimeout;
        (void)twoSideTimeout;
    }

    int32_t SetTwoSideThreshold(const UBSHcomTwoSideThreshold &threshold) override
    {
        (void)threshold;
        return NN_ERROR;
    }

    void SetTraceId(const std::string &traceId) override
    {
        (void)traceId;
    }

    uint64_t GetId() override
    {
        return 1;
    }

    std::string GetPeerConnectPayload() override
    {
        return TEST_PAYLOAD;
    }

    int32_t GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo) override
    {
        (void)idInfo;
        return NN_ERROR;
    }

    int32_t SendFds(int fds[], uint32_t len) override
    {
        (void)fds;
        (void)len;
        return NN_ERROR;
    }

    int32_t ReceiveFds(int fds[], uint32_t len, int32_t timeoutSec) override
    {
        (void)fds;
        (void)len;
        (void)timeoutSec;
        return NN_ERROR;
    }

    void SetUpCtx(uint64_t ctx) override
    {
        (void)ctx;
    }

    uint64_t GetUpCtx() override
    {
        return 0;
    }

    auto SpliceMessage(const UBSHcomNetRequestContext &ctx, bool isResp)
        -> std::tuple<SpliceMessageResultType, SerResult, std::string> override
    {
        (void)ctx;
        (void)isResp;
        return std::make_tuple(SpliceMessageResultType::OK, SER_OK, std::string());
    }

    SerResult Initialize(std::vector<UBSHcomEndpointPtr> &ep, uintptr_t ctxMemPool, uintptr_t periodicMgr,
                         uintptr_t pgTable, uint32_t ctxStoreCapacity = NN_NO2097152) override
    {
        (void)ep;
        (void)ctxMemPool;
        (void)periodicMgr;
        (void)pgTable;
        (void)ctxStoreCapacity;
        return SER_OK;
    }

    void UnInitialize() override {}

    std::string ToString() override
    {
        return "ut-channel";
    }

    void SetUuid(const std::string &uuid) override
    {
        (void)uuid;
    }

    void SetPayload(const std::string &payLoad) override
    {
        (void)payLoad;
    }

    void SetBrokenInfo(UBSHcomChannelBrokenPolicy policy, const UBSHcomServiceChannelBrokenHandler &broken) override
    {
        (void)policy;
        (void)broken;
    }

    void SetEpBroken(uint32_t index) override
    {
        (void)index;
    }

    void SetChannelState(UBSHcomChannelState state) override
    {
        (void)state;
    }

    void SetMultiRail(bool multiRail, uint32_t threshold) override
    {
        (void)multiRail;
        (void)threshold;
    }

    void SetDriverNum(uint16_t driverNum) override
    {
        (void)driverNum;
    }

    void SetTotalBandWidth(uint32_t bandWidth) override
    {
        (void)bandWidth;
    }

    void SetEnableMrCache(bool enableMrCache) override
    {
        (void)enableMrCache;
    }

    bool AllEpBroken() override
    {
        return false;
    }

    bool NeedProcessBroken() override
    {
        return false;
    }

    void ProcessIoInBroken() override {}

    void InvokeChannelBrokenCb(UBSHcomChannelPtr &channel) override
    {
        (void)channel;
    }

    std::string GetUuid() override
    {
        return "ut-uuid";
    }

    uintptr_t GetTimerList() override
    {
        return 0;
    }

    uint32_t GetLocalIp() override
    {
        return 0;
    }

    uint16_t GetDelayEraseTime() override
    {
        return 0;
    }

    HcomServiceCtxStore *GetCtxStore() override
    {
        return nullptr;
    }

    UBSHcomChannelCallBackType GetCallBackType() override
    {
        return UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    }

    int32_t Recv(const UBSHcomServiceContext &context, uintptr_t address, uint32_t size, const Callback *done) override
    {
        (void)context;
        (void)address;
        (void)size;
        (void)done;
        return NN_ERROR;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
};

// --- configurable static C callbacks ---
int g_epNewCalls = 0;
int g_epBrokenCalls = 0;
int g_reqCalls = 0;
int g_idleCalls = 0;
int g_chnCalls = 0;
int g_svcReqCalls = 0;
uint64_t g_lastUsrCtx = 0;
ubs_hcom_request_context *g_lastReqCtx = nullptr;

int TestEpNewHandler(ubs_hcom_endpoint ep, uint64_t usrCtx, const char *payLoad)
{
    (void)ep;
    (void)payLoad;
    g_epNewCalls++;
    g_lastUsrCtx = usrCtx;
    return TEST_HANDLER_RET;
}

int TestEpBrokenHandler(ubs_hcom_endpoint ep, uint64_t usrCtx, const char *payLoad)
{
    (void)ep;
    (void)payLoad;
    g_epBrokenCalls++;
    g_lastUsrCtx = usrCtx;
    return 0;
}

int TestReqHandler(ubs_hcom_request_context *ctx, uint64_t usrCtx)
{
    g_reqCalls++;
    g_lastUsrCtx = usrCtx;
    g_lastReqCtx = ctx;
    return TEST_HANDLER_RET;
}

void TestIdleHandler(uint8_t wkrGrpIdx, uint16_t idxInGrp, uint64_t usrCtx)
{
    (void)wkrGrpIdx;
    (void)idxInGrp;
    g_idleCalls++;
    g_lastUsrCtx = usrCtx;
}

int TestChannelHandler(ubs_hcom_channel channel, uint64_t usrCtx, const char *payLoad)
{
    (void)channel;
    (void)payLoad;
    g_chnCalls++;
    g_lastUsrCtx = usrCtx;
    return TEST_HANDLER_RET;
}

int TestServiceReqHandler(ubs_hcom_service_context ctx, uint64_t usrCtx)
{
    (void)ctx;
    g_svcReqCalls++;
    g_lastUsrCtx = usrCtx;
    return TEST_HANDLER_RET;
}

int TestSecProviderFail(uint64_t ctx, int64_t *flag, ubs_hcom_driver_sec_type *type, char **output, uint32_t *outLen,
                        int *needFree)
{
    (void)ctx;
    (void)flag;
    (void)type;
    (void)output;
    (void)outLen;
    (void)needFree;
    return TEST_SEC_RET;
}

int TestSecProviderOk(uint64_t ctx, int64_t *flag, ubs_hcom_driver_sec_type *type, char **output, uint32_t *outLen,
                      int *needFree)
{
    (void)ctx;
    *flag = 1;
    *type = g_secTypeToReturn;
    static char buf[] = "sec-info";
    *output = buf;
    *outLen = 8;
    *needFree = g_needFreeToReturn;
    return 0;
}

int TestSecValidator(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    (void)ctx;
    (void)flag;
    (void)input;
    (void)inputLen;
    return TEST_HANDLER_RET;
}

int TestGetCert(const char *name, char **certPath)
{
    (void)name;
    *certPath = g_certPathToReturn;
    return 0;
}

int TestGetPk(const char *name, char **priKeyPath, char **keyPass, ubs_hcom_tls_keypass_erase *erase)
{
    (void)name;
    *priKeyPath = g_keyPathToReturn;
    *keyPass = g_keyPassToReturn;
    *erase = g_eraseToReturn;
    return 0;
}

int TestGetCa(const char *name, char **caPath, char **crlPath, ubs_hcom_peer_cert_verify_type *verifyType,
              ubs_hcom_tls_cert_verify *verifyCb)
{
    (void)name;
    *caPath = g_caPathToReturn;
    *crlPath = g_crlPathToReturn;
    *verifyType = g_verifyTypeToReturn;
    *verifyCb = nullptr;
    return 0;
}

void TestEraseCb(char *keyPass, int len)
{
    (void)keyPass;
    (void)len;
    g_eraseCalls++;
}

void ResetCounters()
{
    g_epNewCalls = 0;
    g_epBrokenCalls = 0;
    g_reqCalls = 0;
    g_idleCalls = 0;
    g_chnCalls = 0;
    g_svcReqCalls = 0;
    g_eraseCalls = 0;
    g_lastUsrCtx = 0;
    g_lastReqCtx = nullptr;
    g_secTypeToReturn = C_NET_SEC_DISABLED;
    g_needFreeToReturn = 0;
    g_certPathToReturn = nullptr;
    g_keyPathToReturn = nullptr;
    g_keyPassToReturn = nullptr;
    g_eraseToReturn = nullptr;
    g_caPathToReturn = nullptr;
    g_crlPathToReturn = nullptr;
    g_verifyTypeToReturn = C_VERIFY_BY_DEFAULT;
}
} // namespace

class TestHcomDefInnerC : public testing::Test {
public:
    TestHcomDefInnerC() = default;
    ~TestHcomDefInnerC() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestHcomDefInnerC::SetUp()
{
    ResetCounters();
}

void TestHcomDefInnerC::TearDown() {}

TEST_F(TestHcomDefInnerC, TestEpHdlAdpNewEndPointGuards)
{
    auto *ep = new InnerTestEndpoint();
    UBSHcomNetEndpointPtr epPtr(ep);
    // null handler
    EpHdlAdp nullHandler(C_EP_NEW, nullptr, TEST_USR_CTX);
    EXPECT_EQ(nullHandler.NewEndPoint(TCP_URL, epPtr, TEST_PAYLOAD), NN_INVALID_PARAM);
    // wrong handler type
    EpHdlAdp wrongType(C_EP_BROKEN, &TestEpNewHandler, TEST_USR_CTX);
    EXPECT_EQ(wrongType.NewEndPoint(TCP_URL, epPtr, TEST_PAYLOAD), NN_INVALID_PARAM);
    // null endpoint
    EpHdlAdp valid(C_EP_NEW, &TestEpNewHandler, TEST_USR_CTX);
    UBSHcomNetEndpointPtr nullEp;
    EXPECT_EQ(valid.NewEndPoint(TCP_URL, nullEp, TEST_PAYLOAD), NN_INVALID_PARAM);
    EXPECT_EQ(g_epNewCalls, 0);
}

TEST_F(TestHcomDefInnerC, TestEpHdlAdpNewEndPointInvoke)
{
    auto *ep = new InnerTestEndpoint();
    UBSHcomNetEndpointPtr epPtr(ep);
    int32_t refBefore = ep->GetRef();
    EpHdlAdp adp(C_EP_NEW, &TestEpNewHandler, TEST_USR_CTX);
    EXPECT_EQ(adp.NewEndPoint(TCP_URL, epPtr, TEST_PAYLOAD), TEST_HANDLER_RET);
    EXPECT_EQ(g_epNewCalls, 1);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX);
    // the adapter keeps one reference on the endpoint
    EXPECT_EQ(ep->GetRef(), refBefore + 1);
    ep->DecreaseRef();
}

TEST_F(TestHcomDefInnerC, TestEpHdlAdpBrokenGuardsAndInvoke)
{
    auto *ep = new InnerTestEndpoint();
    UBSHcomNetEndpointPtr epPtr(ep);
    EpHdlAdp nullHandler(C_EP_BROKEN, nullptr, TEST_USR_CTX);
    nullHandler.EndPointBroken(epPtr);
    EXPECT_EQ(g_epBrokenCalls, 0);

    EpHdlAdp wrongType(C_EP_NEW, &TestEpBrokenHandler, TEST_USR_CTX);
    wrongType.EndPointBroken(epPtr);
    EXPECT_EQ(g_epBrokenCalls, 0);

    EpHdlAdp valid(C_EP_BROKEN, &TestEpBrokenHandler, TEST_USR_CTX);
    UBSHcomNetEndpointPtr nullEp;
    valid.EndPointBroken(nullEp);
    EXPECT_EQ(g_epBrokenCalls, 0);

    valid.EndPointBroken(epPtr);
    EXPECT_EQ(g_epBrokenCalls, 1);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX);
}

TEST_F(TestHcomDefInnerC, TestEpOpHdlAdpNullHandler)
{
    EpOpHdlAdp adp(nullptr, TEST_USR_CTX);
    UBSHcomNetRequestContext ctx;
    EXPECT_EQ(adp.Requested(ctx), NN_INVALID_PARAM);
    EXPECT_EQ(g_reqCalls, 0);
}

TEST_F(TestHcomDefInnerC, TestEpOpHdlAdpSentPath)
{
    EpOpHdlAdp adp(&TestReqHandler, TEST_USR_CTX);
    UBSHcomNetRequestContext ctx;
    ctx.mOpType = UBSHcomNetRequestContext::NN_SENT;
    ctx.mHeader.opCode = 9;
    ctx.mHeader.seqNo = 77;
    ctx.mOriginalReq.size = 128;
    ctx.mOriginalReq.upCtxSize = 4;
    EXPECT_EQ(adp.Requested(ctx), TEST_HANDLER_RET);
    EXPECT_EQ(g_reqCalls, 1);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX);
    ASSERT_NE(g_lastReqCtx, nullptr);
    EXPECT_EQ(g_lastReqCtx->opCode, 9);
    EXPECT_EQ(g_lastReqCtx->seqNo, 77);
    EXPECT_EQ(g_lastReqCtx->originalSend.size, 128);
    EXPECT_EQ(g_lastReqCtx->originalSend.upCtxSize, 4);
}

TEST_F(TestHcomDefInnerC, TestEpOpHdlAdpReadWritePath)
{
    EpOpHdlAdp adp(&TestReqHandler, TEST_USR_CTX);
    UBSHcomNetRequestContext ctx;
    ctx.mOpType = UBSHcomNetRequestContext::NN_WRITTEN;
    ctx.mOriginalReq.lAddress = 0x1000;
    ctx.mOriginalReq.rAddress = 0x2000;
    ctx.mOriginalReq.lKey = 11;
    ctx.mOriginalReq.rKey = 22;
    ctx.mOriginalReq.size = 256;
    EXPECT_EQ(adp.Requested(ctx), TEST_HANDLER_RET);
    ASSERT_NE(g_lastReqCtx, nullptr);
    EXPECT_EQ(g_lastReqCtx->originalReq.lMRA, 0x1000);
    EXPECT_EQ(g_lastReqCtx->originalReq.rMRA, 0x2000);
    EXPECT_EQ(g_lastReqCtx->originalReq.lKey, 11);
    EXPECT_EQ(g_lastReqCtx->originalReq.rKey, 22);
    EXPECT_EQ(g_lastReqCtx->originalReq.size, 256);
}

TEST_F(TestHcomDefInnerC, TestEpOpHdlAdpSglPath)
{
    EpOpHdlAdp adp(&TestReqHandler, TEST_USR_CTX);
    UBSHcomNetRequestContext ctx;
    ctx.mOpType = UBSHcomNetRequestContext::NN_SENT_RAW_SGL;
    UBSHcomNetTransSgeIov iovs[2] = {};
    ctx.mOriginalSglReq.iov = iovs;
    ctx.mOriginalSglReq.iovCount = 2;
    EXPECT_EQ(adp.Requested(ctx), TEST_HANDLER_RET);
    ASSERT_NE(g_lastReqCtx, nullptr);
    EXPECT_EQ(g_lastReqCtx->originalSglReq.iov, reinterpret_cast<ubs_hcom_readwrite_sge *>(iovs));
    EXPECT_EQ(g_lastReqCtx->originalSglReq.iovCount, 2);
}

TEST_F(TestHcomDefInnerC, TestEpOpHdlAdpUnknownOpType)
{
    EpOpHdlAdp adp(&TestReqHandler, TEST_USR_CTX);
    UBSHcomNetRequestContext ctx;
    ctx.mOpType = UBSHcomNetRequestContext::NN_RNDV;
    EXPECT_EQ(adp.Requested(ctx), TEST_HANDLER_RET);
    EXPECT_EQ(g_reqCalls, 1);
}

TEST_F(TestHcomDefInnerC, TestOobSecInfoProvider)
{
    OOBSecInfoProviderAdp nullProvider(nullptr);
    int64_t flag = 0;
    UBSHcomNetDriverSecType type = NET_SEC_DISABLED;
    char *output = nullptr;
    uint32_t outLen = 0;
    bool needAutoFree = false;
    EXPECT_EQ(nullProvider.CreateSecInfo(1, flag, type, output, outLen, needAutoFree), -1);

    // provider failure propagates
    OOBSecInfoProviderAdp failProvider(&TestSecProviderFail);
    EXPECT_EQ(failProvider.CreateSecInfo(1, flag, type, output, outLen, needAutoFree), TEST_SEC_RET);

    // success: type mapping and need-free flag
    OOBSecInfoProviderAdp okProvider(&TestSecProviderOk);
    g_secTypeToReturn = C_NET_SEC_ONE_WAY;
    g_needFreeToReturn = 1;
    EXPECT_EQ(okProvider.CreateSecInfo(1, flag, type, output, outLen, needAutoFree), 0);
    EXPECT_EQ(type, NET_SEC_VALID_ONE_WAY);
    EXPECT_TRUE(needAutoFree);
    EXPECT_EQ(flag, 1);

    g_secTypeToReturn = C_NET_SEC_TWO_WAY;
    g_needFreeToReturn = 0;
    needAutoFree = false;
    EXPECT_EQ(okProvider.CreateSecInfo(2, flag, type, output, outLen, needAutoFree), 0);
    EXPECT_EQ(type, NET_SEC_VALID_TWO_WAY);
    EXPECT_FALSE(needAutoFree);

    g_secTypeToReturn = C_NET_SEC_DISABLED;
    EXPECT_EQ(okProvider.CreateSecInfo(3, flag, type, output, outLen, needAutoFree), 0);
    EXPECT_EQ(type, NET_SEC_DISABLED);
}

TEST_F(TestHcomDefInnerC, TestOobSecInfoValidator)
{
    OOBSecInfoValidatorAdp nullValidator(nullptr);
    EXPECT_EQ(nullValidator.SecInfoValidate(1, 2, "data", 4), -1);
    OOBSecInfoValidatorAdp validator(&TestSecValidator);
    EXPECT_EQ(validator.SecInfoValidate(1, 2, "data", 4), TEST_HANDLER_RET);
}

TEST_F(TestHcomDefInnerC, TestIdleAdps)
{
    UBSHcomNetWorkerIndex index{};
    index.grpIdx = TEST_GRP_IDX;
    index.idxInGrp = TEST_IDX_IN_GRP;

    EpIdleHdlAdp nullIdle(nullptr, TEST_USR_CTX);
    nullIdle.Idle(index);
    EXPECT_EQ(g_idleCalls, 0);

    EpIdleHdlAdp idle(&TestIdleHandler, TEST_USR_CTX);
    idle.Idle(index);
    EXPECT_EQ(g_idleCalls, 1);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX);

    ServiceIdleHdlAdp nullSvcIdle(nullptr, TEST_USR_CTX);
    nullSvcIdle.Idle(index);
    EXPECT_EQ(g_idleCalls, 1);

    ServiceIdleHdlAdp svcIdle(&TestIdleHandler, TEST_USR_CTX + 1);
    svcIdle.Idle(index);
    EXPECT_EQ(g_idleCalls, 2);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX + 1);
}

TEST_F(TestHcomDefInnerC, TestTlsPrivateKeyCallback)
{
    EpTLSHdlAdp adp;
    std::string path;
    void *keyPass = nullptr;
    UBSHcomTLSEraseKeypass callback;
    // callback not set
    EXPECT_FALSE(adp.UBSHcomTLSPrivateKeyCallback("svc", path, keyPass, 0, callback));

    adp.SetTLSPrivateKeyCb(&TestGetPk);
    // all outputs null -> false
    EXPECT_FALSE(adp.UBSHcomTLSPrivateKeyCallback("svc", path, keyPass, 0, callback));
    g_keyPathToReturn = const_cast<char *>(TEST_KEY_PATH);
    EXPECT_FALSE(adp.UBSHcomTLSPrivateKeyCallback("svc", path, keyPass, 0, callback));
    g_keyPassToReturn = const_cast<char *>(TEST_KEY_PASS);
    EXPECT_FALSE(adp.UBSHcomTLSPrivateKeyCallback("svc", path, keyPass, 0, callback));
    g_eraseToReturn = &TestEraseCb;
    EXPECT_TRUE(adp.UBSHcomTLSPrivateKeyCallback("svc", path, keyPass, 0, callback));
    EXPECT_EQ(path, TEST_KEY_PATH);
    EXPECT_STREQ(static_cast<const char *>(keyPass), TEST_KEY_PASS);
    // the bound erase callback forwards to the C function
    callback(keyPass, 0);
    EXPECT_EQ(g_eraseCalls, 1);
}

TEST_F(TestHcomDefInnerC, TestTlsCertificationCallback)
{
    EpTLSHdlAdp adp;
    std::string path;
    EXPECT_FALSE(adp.UBSHcomTLSCertificationCallback("svc", path));
    adp.SetTLSCertCb(&TestGetCert);
    EXPECT_FALSE(adp.UBSHcomTLSCertificationCallback("svc", path));
    g_certPathToReturn = const_cast<char *>(TEST_CERT_PATH);
    EXPECT_TRUE(adp.UBSHcomTLSCertificationCallback("svc", path));
    EXPECT_EQ(path, TEST_CERT_PATH);
}

TEST_F(TestHcomDefInnerC, TestTlsCaCallback)
{
    EpTLSHdlAdp adp;
    std::string caPath;
    std::string crlPath;
    UBSHcomPeerCertVerifyType verifyType = VERIFY_BY_DEFAULT;
    UBSHcomTLSCertVerifyCallback callback;
    EXPECT_FALSE(adp.UBSHcomTLSCaCallback("svc", caPath, crlPath, verifyType, callback));

    adp.SetTLSCaCb(&TestGetCa);
    EXPECT_FALSE(adp.UBSHcomTLSCaCallback("svc", caPath, crlPath, verifyType, callback));
    g_caPathToReturn = const_cast<char *>(TEST_CA_PATH);
    // no crl path, default verify type
    EXPECT_TRUE(adp.UBSHcomTLSCaCallback("svc", caPath, crlPath, verifyType, callback));
    EXPECT_EQ(caPath, TEST_CA_PATH);
    EXPECT_TRUE(crlPath.empty());
    EXPECT_EQ(verifyType, VERIFY_BY_DEFAULT);

    g_crlPathToReturn = const_cast<char *>(TEST_CRL_PATH);
    g_verifyTypeToReturn = C_VERIFY_BY_NONE;
    EXPECT_TRUE(adp.UBSHcomTLSCaCallback("svc", caPath, crlPath, verifyType, callback));
    EXPECT_EQ(crlPath, TEST_CRL_PATH);
    EXPECT_EQ(verifyType, VERIFY_BY_NONE);

    g_verifyTypeToReturn = C_VERIFY_BY_CUSTOM_FUNC;
    EXPECT_TRUE(adp.UBSHcomTLSCaCallback("svc", caPath, crlPath, verifyType, callback));
    EXPECT_EQ(verifyType, VERIFY_BY_CUSTOM_FUNC);
}

TEST_F(TestHcomDefInnerC, TestHdlMgrAddRemove)
{
    HdlMgr mgr;
    // removing an unknown handle is a no-op
    mgr.RemoveHdlAdp<int>(reinterpret_cast<uintptr_t>(this));

    auto *tracker = new TrackedObj();
    mgr.AddHdlAdp(reinterpret_cast<uintptr_t>(tracker));
    mgr.AddHdlAdp(reinterpret_cast<uintptr_t>(tracker)); // duplicated add is ignored
    EXPECT_EQ(TrackedObj::alive, 1);
    mgr.RemoveHdlAdp<TrackedObj>(reinterpret_cast<uintptr_t>(tracker));
    EXPECT_EQ(TrackedObj::alive, 0);
    mgr.RemoveHdlAdp<TrackedObj>(reinterpret_cast<uintptr_t>(tracker));
    EXPECT_EQ(TrackedObj::alive, 0);
}

TEST_F(TestHcomDefInnerC, TestServiceHdlAdpNewChannel)
{
    auto *ch = new InnerTestChannel();
    UBSHcomChannelPtr chPtr(ch);
    // null handler
    ServiceHdlAdp nullHandler(C_CHANNEL_NEW, nullptr, TEST_USR_CTX);
    EXPECT_EQ(nullHandler.NewChannel(TCP_URL, chPtr, TEST_PAYLOAD), NN_INVALID_PARAM);
    // wrong type
    ServiceHdlAdp wrongType(C_CHANNEL_BROKEN, &TestChannelHandler, TEST_USR_CTX);
    EXPECT_EQ(wrongType.NewChannel(TCP_URL, chPtr, TEST_PAYLOAD), NN_INVALID_PARAM);
    // null channel
    ServiceHdlAdp valid(C_CHANNEL_NEW, &TestChannelHandler, TEST_USR_CTX);
    UBSHcomChannelPtr nullCh;
    EXPECT_EQ(valid.NewChannel(TCP_URL, nullCh, TEST_PAYLOAD), NN_INVALID_PARAM);
    // invoke
    int32_t refBefore = ch->GetRef();
    EXPECT_EQ(valid.NewChannel(TCP_URL, chPtr, TEST_PAYLOAD), TEST_HANDLER_RET);
    EXPECT_EQ(g_chnCalls, 1);
    EXPECT_EQ(ch->GetRef(), refBefore + 1);
    ch->DecreaseRef();

    // broken guards
    valid.ChannelBroken(chPtr);
    EXPECT_EQ(g_chnCalls, 1);
    ServiceHdlAdp brokenAdp(C_CHANNEL_BROKEN, &TestChannelHandler, TEST_USR_CTX);
    brokenAdp.ChannelBroken(nullCh);
    EXPECT_EQ(g_chnCalls, 1);
    brokenAdp.ChannelBroken(chPtr);
    EXPECT_EQ(g_chnCalls, 2);
    ServiceHdlAdp nullBroken(C_CHANNEL_BROKEN, nullptr, TEST_USR_CTX);
    nullBroken.ChannelBroken(chPtr);
    EXPECT_EQ(g_chnCalls, 2);
}

TEST_F(TestHcomDefInnerC, TestChannelOpHdlAdpRequested)
{
    ChannelOpHdlAdp nullHandler(nullptr, TEST_USR_CTX);
    UBSHcomServiceContext ctx;
    EXPECT_EQ(nullHandler.Requested(ctx), NN_INVALID_PARAM);

    ChannelOpHdlAdp adp(&TestServiceReqHandler, TEST_USR_CTX);
    EXPECT_EQ(adp.Requested(ctx), TEST_HANDLER_RET);
    EXPECT_EQ(g_svcReqCalls, 1);
    EXPECT_EQ(g_lastUsrCtx, TEST_USR_CTX);
}

TEST_F(TestHcomDefInnerC, TestServiceHdlMgrRemoveAll)
{
    ServiceHdlMgr<TrackedObj> mgr;
    // removing an unknown service is a no-op
    mgr.RemoveAll(1);
    auto *a = new TrackedObj();
    auto *b = new TrackedObj();
    auto *c = new TrackedObj();
    mgr.AddHdlAdp(100, reinterpret_cast<uintptr_t>(a));
    mgr.AddHdlAdp(100, reinterpret_cast<uintptr_t>(a)); // duplicated add ignored
    mgr.AddHdlAdp(100, reinterpret_cast<uintptr_t>(b));
    mgr.AddHdlAdp(200, reinterpret_cast<uintptr_t>(c));
    EXPECT_EQ(TrackedObj::alive, 3);
    mgr.RemoveAll(100); // deletes a (once) and b
    EXPECT_EQ(TrackedObj::alive, 1);
    mgr.RemoveAll(100); // already removed, no-op
    EXPECT_EQ(TrackedObj::alive, 1);
    mgr.RemoveAll(200); // deletes c
    EXPECT_EQ(TrackedObj::alive, 0);
}
} // namespace hcom
} // namespace ock
