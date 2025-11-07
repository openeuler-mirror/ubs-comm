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

#include "sock_wrapper.h"
#include "openssl_api_wrapper.h"

namespace ock {
namespace hcom {

class TestSockWrapper : public testing::Test {
public:
    TestSockWrapper();
    virtual void SetUp(void);
    virtual void TearDown(void);
    Sock *mSock = nullptr;
    SockOpContextInfo *ctx = nullptr;
    SockSglContextInfo *sendCtx = nullptr;
    SockHeaderReqInfo *reqInfo = nullptr;
};

TestSockWrapper::TestSockWrapper() {}

void TestSockWrapper::SetUp()
{
    SockType mT = SOCK_UDS;
    std::string mName = "TestSockWrapper";
    uint64_t mId = 1;
    int mFd = -1;
    SockOptions mSockOptions{};
    mSock = new (std::nothrow) Sock(mT, mName, mId, mFd, mSockOptions);
    ASSERT_TRUE(mSock != nullptr);
    ctx = new (std::nothrow) SockOpContextInfo();
    ASSERT_TRUE(ctx != nullptr);
    sendCtx = new (std::nothrow) SockSglContextInfo();
    ASSERT_TRUE(sendCtx != nullptr);
    reqInfo = new (std::nothrow) SockHeaderReqInfo();
    ASSERT_TRUE(reqInfo != nullptr);
    ctx->sendCtx = sendCtx;
    ctx->headerRequest = reqInfo;
}

void TestSockWrapper::TearDown()
{
    if (mSock != nullptr) {
        delete mSock;
        mSock = nullptr;
    }

    if (sendCtx != nullptr) {
        delete sendCtx;
        sendCtx = nullptr;
    }

    if (reqInfo != nullptr) {
        delete reqInfo;
        reqInfo = nullptr;
    }

    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestSockWrapper, TestInitializeValidateOptionsFail)
{
    mSock->mInited = false;
    SockWorkerOptions options;
    MOCKER_CPP(&Sock::ValidateOptions).stubs().will(returnValue(1));
    SResult ret = mSock->Initialize(options);
    EXPECT_EQ(ret, static_cast<SResult>(NN_NO1));
}

TEST_F(TestSockWrapper, TestInitializeMemAllocateFail)
{
    mSock->mInited = false;
    SockWorkerOptions options;
    MOCKER_CPP(&Sock::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&Sock::SetSockOption).stubs().will(returnValue(0));
    MOCKER_CPP(&SockBuff::ExpandIfNeed).stubs().will(returnValue(false));
    SResult ret = mSock->Initialize(options);
    EXPECT_EQ(ret, SS_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestSockWrapper, TestValidateOptions)
{
    mSock->mOptions.receiveBufSizeKB = 0;
    SResult ret = mSock->ValidateOptions();
    EXPECT_EQ(ret, SS_OK);
}

TEST_F(TestSockWrapper, TestSetSockOptionSetRecvBufferFail)
{
    mSock->mFd = 1;
    SockWorkerOptions workerOptions{};
    workerOptions.sockReceiveBufKB = 1;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(-1));
    SResult ret = mSock->SetSockOption(workerOptions);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
    mSock->mFd = -1;
}

TEST_F(TestSockWrapper, TestSetSockOptionSetSendBufferFail)
{
    mSock->mFd = 1;
    SockWorkerOptions workerOptions{};
    workerOptions.sockReceiveBufKB = 0;
    workerOptions.sockSendBufKB = 1;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(-1));
    SResult ret = mSock->SetSockOption(workerOptions);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
    mSock->mFd = -1;
}

TEST_F(TestSockWrapper, TestSetSockOptionGetSendBufferFail)
{
    mSock->mFd = 1;
    SockWorkerOptions workerOptions{};
    workerOptions.sockReceiveBufKB = 0;
    workerOptions.sockSendBufKB = 0;
    MOCKER_CPP(getsockopt).stubs().will(returnValue(-1)).then(returnValue(0));
    SResult ret = mSock->SetSockOption(workerOptions);
    EXPECT_EQ(ret, SS_TCP_GET_OPTION_FAILED);
    ret = mSock->SetSockOption(workerOptions);
    EXPECT_EQ(ret, SS_TCP_GET_OPTION_FAILED);
    mSock->mFd = -1;
}

TEST_F(TestSockWrapper, TestSetBlockingSendTimeoutFail)
{
    MOCKER_CPP(setsockopt).stubs().will(returnValue(-1));
    SResult ret = mSock->SetBlockingSendTimeout(1);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
}

TEST_F(TestSockWrapper, TestSetBlockingIoGetControlValueFail)
{
    SResult ret = mSock->SetBlockingIo();
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
}

TEST_F(TestSockWrapper, TestSetNonBlockingIoGetControlValueFail)
{
    SResult ret = mSock->SetNonBlockingIo();
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
}

TEST_F(TestSockWrapper, TestSetBlockingIoFail)
{
    UBSHcomEpOptions epOptions{};
    MOCKER_CPP(&Sock::SetBlockingIo, SResult(Sock::*)()).stubs().will(returnValue(0)).then(returnValue(-1));
    MOCKER_CPP(&Sock::SetBlockingSendTimeout).stubs().will(returnValue(-1));

    SResult ret = mSock->SetBlockingIo(epOptions);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
    ret = mSock->SetBlockingIo(epOptions);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);
}

TEST_F(TestSockWrapper, TestGetSendQueueCount)
{
    MOCKER_CPP(&NetRingBuffer<SockOpContextInfo *>::Size).stubs().will(returnValue(1));
    uint32_t ret = mSock->GetSendQueueCount();
    EXPECT_EQ(ret, 1);
}

TEST_F(TestSockWrapper, TestSendRealConnHeaderParamFail)
{
    SResult ret = mSock->SendRealConnHeader(-1, nullptr, 0);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostSendParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostSend(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostSend)
{
    mSock->mTcpBlockingMode = true;
    mSock->mOptions.sendZCopy = true;
    mSock->mCbByWorkerInBlocking = true;
    ssize_t size = NN_NO128;
    MOCKER_CPP(&writev).stubs().will(returnValue(size));
    MOCKER_CPP(&NetRingBuffer<SockOpContextInfo *>::PushBack).stubs().will(returnValue(true));
    SResult ret = mSock->PostSend(ctx);
    EXPECT_EQ(ret, SS_SOCK_SEND_EAGAIN);
}

TEST_F(TestSockWrapper, TestPostSendSglParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostSendSgl(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostSendSglFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostSendSgl(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = EAGAIN;
    ret = mSock->PostSendSgl(ctx);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    errno = 0;
    ret = mSock->PostSendSgl(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);

    mSock->mEnableTls = true;
    ctx->opType = SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL;
    ret = mSock->PostSendSgl(ctx);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestSockWrapper, TestPostSendSglHeaderFail)
{
    SockTransHeader header{};
    UBSHcomNetTransSglRequest req{};
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = 0;
    ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostSendSglHeaderTlsFail)
{
    SockTransHeader header{};
    UBSHcomNetTransSglRequest req{};
    mSock->mEnableTls = true;
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = EAGAIN;
    ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    errno = 0;
    ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostSendSglHeaderSSLSendFail)
{
    SockTransHeader header{};
    UBSHcomNetTransSgeIov *iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    UBSHcomNetTransSglRequest req = UBSHcomNetTransSglRequest(iov, 1, 1);
    mSock->mEnableTls = true;
    ssize_t size = NN_NO128;
    MOCKER_CPP(&writev).stubs().will(returnValue(size));
    MOCKER_CPP(&Sock::SSLSend).stubs()
        .will(returnValue(static_cast<int>(SS_OOB_SSL_WRITE_ERROR)))
        .then(returnValue(static_cast<int>(SS_TIMEOUT)));
    SResult ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    ret = mSock->PostSendSgl(header, req);
    EXPECT_EQ(ret, SS_TIMEOUT);

    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

TEST_F(TestSockWrapper, TestPostSendHeadParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostSendHead(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostSendHeadFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(::send).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostSendHead(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = EAGAIN;
    ret = mSock->PostSendHead(ctx);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    errno = 0;
    ret = mSock->PostSendHead(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostSendHeadSuccess)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = NN_NO128;
    MOCKER_CPP(::send).stubs().will(returnValue(size));
    SResult ret = mSock->PostSendHead(ctx);
    EXPECT_EQ(ret, SS_OK);
}

TEST_F(TestSockWrapper, TestPostReadParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostRead(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostReadFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostRead(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = 0;
    ret = mSock->PostRead(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostWriteParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostWrite(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostWriteFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = static_cast<ssize_t>(sizeof(SockTransHeader)) - 1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostWrite(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = EAGAIN;
    ret = mSock->PostWrite(ctx);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    errno = 0;
    ret = mSock->PostWrite(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostReceiveHeaderFail)
{
    SockTransHeader header {};
    mSock->mRevTimeoutSecond = -1;

    MOCKER_CPP(setsockopt).stubs().will(returnValue(-1)).then(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(0));

    SResult ret = mSock->PostReceiveHeader(header, 1);
    EXPECT_EQ(ret, SS_TCP_SET_OPTION_FAILED);

    ret = mSock->PostReceiveHeader(header, 1);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);
}

TEST_F(TestSockWrapper, TestPostReceiveBodyParamFail)
{
    void *buff = nullptr;
    uint32_t dataLength = 0;
    bool isOneSide = true;

    SResult ret = mSock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(ret, SS_PARAM_INVALID);

    buff = malloc(NN_NO1024);
    ret = mSock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(ret, SS_PARAM_INVALID);

    free(buff);
}

TEST_F(TestSockWrapper, TestPostReceiveBodyFail)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = true;
    mSock->mEnableTls = false;
    MOCKER_CPP(::recv).stubs().will(returnValue(0));

    errno = EAGAIN;
    SResult ret = mSock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(ret, SS_TIMEOUT);
    free(buff);
}

TEST_F(TestSockWrapper, TestPostReceiveBodyTlsFail)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = false;
    mSock->mEnableTls = true;
    MOCKER_CPP(&Sock::SSLRead).stubs()
        .will(returnValue(static_cast<int>(SS_TIMEOUT)))
        .then(returnValue(static_cast<int>(SS_SSL_READ_FAILED)));

    SResult ret = mSock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(ret, SS_TIMEOUT);

    ret = mSock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(ret, SS_SSL_READ_FAILED);

    free(buff);
}

TEST_F(TestSockWrapper, TestPostReadSglParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostReadSgl(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostReadSglFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = NN_NO1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostReadSgl(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = 0;
    ret = mSock->PostReadSgl(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostReadSglAckParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostReadSglAck(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostReadSglAckFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = NN_NO1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostReadSglAck(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = EAGAIN;
    ret = mSock->PostReadSglAck(ctx);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    errno = 0;
    ret = mSock->PostReadSglAck(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestPostReadSglAckSuccess)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = NN_NO128;
    MOCKER_CPP(&writev).stubs().will(returnValue(size));
    SResult ret = mSock->PostReadSglAck(ctx);
    EXPECT_EQ(ret, SS_OK);
}

TEST_F(TestSockWrapper, TestPostWriteSglParamFail)
{
    SockOpContextInfo *nullCtx = nullptr;
    SResult ret = mSock->PostWriteSgl(nullCtx);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestSockWrapper, TestPostWriteSglFail)
{
    mSock->mTcpBlockingMode = true;
    ssize_t size = NN_NO1;
    MOCKER_CPP(&writev).stubs().will(returnValue(0)).then(returnValue(size));
    SResult ret = mSock->PostWriteSgl(ctx);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = 0;
    ret = mSock->PostWriteSgl(ctx);
    EXPECT_EQ(ret, SS_TIMEOUT);
}

TEST_F(TestSockWrapper, TestSSLSendOpenssl)
{
    uint32_t readLen = 0;
    MOCKER_CPP(HcomSsl::SslWrite).stubs().will(returnValue(0));
    MOCKER_CPP(HcomSsl::SslGetError).stubs().will(returnValue(0));

    SResult ret = mSock->SSLSend(nullptr, 0, readLen);
    EXPECT_EQ(ret, SS_OOB_SSL_WRITE_ERROR);
}

TEST_F(TestSockWrapper, TestSSLReadOpenssl)
{
    uint32_t readLen = 0;
    MOCKER_CPP(HcomSsl::SslRead).stubs().will(returnValue(0));
    MOCKER_CPP(HcomSsl::SslGetError).stubs().will(returnValue(0));

    SResult ret = mSock->SSLRead(nullptr, 0, readLen);
    EXPECT_EQ(ret, SS_SSL_READ_FAILED);
}

TEST_F(TestSockWrapper, TestPostSendSglSsl)
{
    struct iovec iov[NN_NO5];
    SResult ret = mSock->PostSendSglSsl(ctx, iov);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    ssize_t size = NN_NO128;
    MOCKER_CPP(&writev).stubs()
        .will(returnValue(static_cast<ssize_t>(0)))
        .then(returnValue(sizeof(SockTransHeader) - 1))
        .then(returnValue(size));
    ret = mSock->PostSendSglSsl(ctx, iov);
    EXPECT_EQ(ret, SS_TCP_RETRY);

    errno = 0;
    ret = mSock->PostSendSglSsl(ctx, iov);
    EXPECT_EQ(ret, SS_TIMEOUT);
    
    MOCKER_CPP(&Sock::SSLSend).stubs()
        .will(returnValue(static_cast<int>(SS_OOB_SSL_WRITE_ERROR)))
        .then(returnValue(static_cast<int>(SS_TIMEOUT)));
    errno = 0;
    ctx->sendCtx->iovCount = 1;
    ret = mSock->PostSendSglSsl(ctx, iov);
    EXPECT_EQ(ret, SS_SOCK_SEND_FAILED);

    ret = mSock->PostSendSglSsl(ctx, iov);
    EXPECT_EQ(ret, SS_TIMEOUT);
    
    ctx->sendBuff = nullptr;
}
}
}