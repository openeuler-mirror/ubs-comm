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

#ifdef RDMA_BUILD_ENABLED
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "test_rdma_c.hpp"
#include "capi/hcom_c.h"
#include "string.h"
#include "hcom.h"
#include "common/net_util.h"
#include "transport/rdma/verbs/net_rdma_sync_endpoint.h"
#include "transport/rdma/verbs/net_rdma_async_endpoint.h"
#include "transport/rdma/rdma_mr_dm_buf.h"
#include "transport/rdma/rdma_mr_fixed_buf.h"
#include "transport/rdma/verbs/rdma_worker.h"
#include "fake_ibv.h"
#include "transport/rdma/verbs/net_rdma_driver.h"
#include "ut_helper.h"

TestCaseRdmaC::TestCaseRdmaC() {}

void TestCaseRdmaC::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestCaseRdmaC::TearDown()
{
    GlobalMockObject::verify();
}

using namespace ock::hcom;
#ifdef MOCK_VERBS
#ifdef __cplusplus
extern "C" {
#endif
int fake_ibv_post_send(fake_qp_t *my_qp, struct ibv_send_wr *wr);
int fake_post_read(fake_qp_t *my_qp, struct ibv_send_wr *wr);
int fake_post_write(fake_qp_t *my_qp, struct ibv_send_wr *wr);
#ifdef __cplusplus
}
#endif
#endif
// cpp case
using CTestOpCode = enum {
    C_GET_MR = 1,
    C_SET_MR,
    C_CHECK_SYNC_RESPONSE,
    C_SEND_RAW,
};

#define CHECK_RESULT_TRUE(result) \
    EXPECT_EQ(true, result);      \
    if (!result) {                \
        return;                   \
    }

#define CLEAN_UP_ALL_STUBS() GlobalMockObject::verify()

constexpr uint64_t C_SYNC_SEND_VALUE = 0xAAAA0000;
constexpr uint64_t C_SYNC_RECEIVE_VALUE = 0x0000AAAA;
constexpr uint64_t C_SET_MSG_SUCCESS = 0xCCCCCCCC;
constexpr uint64_t ASYNC_RW_COUNT = 4;
constexpr uint64_t C_RDMA_LISTEN_PORT = 7778;
// server
Net_Driver cServerDriver = 0;
Net_EndPoint epServer = 0;
Net_MemoryRegion mrRegion[NN_NO4];

char *ipSeg = IP_SEG;
char *certPath;
bool enableTls = true;
Net_DriverCipherSuite cipherSuite = C_AES_GCM_256;

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));
TestRegMrInfo cServerLocalMrInfo[NN_NO4];

char *join(char *a, char *b)
{
    char *c = (char *)malloc(strlen(a) + strlen(b) + 1);
    if (c == NULL)
        exit(1);
    char *tempc = c;
    while (*a != '\0') {
        *c++ = *a++;
    }
    while ((*c++ = *b++) != '\0') {
    }
    return tempc;
}

static int SNewEndPoint(Net_EndPoint newEp, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_INFO("ep new");
    epServer = newEp;
    return 0;
}

static int SEndPointBroken(Net_EndPoint bEp, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_INFO("ep broken");
    return 0;
}

static int SRequestReceived(Net_RequestContext *ctx, uint64_t usrCtx)
{
    int result = 0;
    Net_SendRequest rsp = { 0 };
    if (ctx->type == C_RECEIVED) {
        if (ctx->opCode == C_GET_MR) {
            rsp.data = (uintptr_t)cServerLocalMrInfo;
            rsp.size = sizeof(cServerLocalMrInfo);
            if ((result = Net_EPPostSend(ctx->ep, ctx->opCode, &rsp)) != 0) {
                NN_LOG_INFO("failed to post message to data to server, result " << result);
                return result;
            }

            NN_LOG_TRACE_INFO("request rsp Mr info");
            for (uint16_t i = 0; i < NN_NO4; i++) {
                NN_LOG_TRACE_INFO("idx:" << i << " key:" << cServerLocalMrInfo[i].lKey << " address:" <<
                    cServerLocalMrInfo[i].lAddress << " size" << cServerLocalMrInfo[i].size);
            }
        } else if (ctx->opCode == C_SET_MR) {
            memset(cServerLocalMrInfo, 0, sizeof(cServerLocalMrInfo));
            uint64_t ret = C_SET_MSG_SUCCESS;
            rsp.data = (uintptr_t)&ret;
            rsp.size = sizeof(uint64_t);
            if ((result = Net_EPPostSend(ctx->ep, ctx->opCode, &rsp)) != 0) {
                NN_LOG_INFO("failed to post message to data to server, result " << result);
                return result;
            }
        } else if (ctx->opCode == C_CHECK_SYNC_RESPONSE) {
            uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ctx->msgData));
            EXPECT_EQ(C_SYNC_SEND_VALUE, *readValue);
            uint64_t returnValue = C_SYNC_RECEIVE_VALUE;
            rsp.data = (uintptr_t)&returnValue;
            rsp.size = sizeof(uint64_t);
            if ((result = Net_EPPostSend(ctx->ep, ctx->opCode, &rsp)) != 0) {
                NN_LOG_INFO("failed to post message to data to server, result " << result);
                return result;
            }
        }
    } else if (ctx->type == C_RECEIVED_RAW) {
        if (ctx->seqNo == C_SEND_RAW) {
            uint64_t returnValue = 0;
            rsp.data = (uintptr_t)&returnValue;
            rsp.size = sizeof(uint64_t);
            if ((result = Net_EPPostSendRaw(ctx->ep, &rsp, ctx->seqNo)) != 0) {
                NN_LOG_INFO("failed to post message to data to server, result " << result);
                return result;
            }
        }
    }

    return 0;
}

static int SRequestPosted(Net_RequestContext *ctx, uint64_t usrCtx)
{
    NN_LOG_TRACE_INFO("posted");
    return 0;
}

static int SOneSideDone(Net_RequestContext *ctx, uint64_t usrCtx)
{
    NN_LOG_TRACE_INFO("one side done");
    return 0;
}

static void SIdle(uint8_t wkrGrpIdx, uint16_t workerIndex, uint64_t usrCtx) {}

static void SErase(char *pass, int len) {}

static int SVerify(void *x509, const char *path)
{
    NN_LOG_INFO("verify");
    return 0;
}

static int SCertCallback(const char *name, char **value)
{
    char cert[] = "/server/cert.pem";
    *value = join(certPath, cert);
    printf("cert callback v: %s \n", *value);
    return 0;
}

static int SPrivateKeyCallback(const char *name, char **value, char **keyPass, Net_TlsKeyPassErase *erase)
{
    printf("private key cb");
    static char content[] = "huawei";
    *keyPass = content;
    char keypem[] = "/server/key.pem";
    *value = join(certPath, keypem);
    *erase = &SErase;
    return 0;
}

static int SCACallback(const char *name, char **caPath, char **crlPath, Net_PeerCertVerifyType *peerCertVerifyType,
    Net_TlsCertVerify *verify)
{
    char caCert[] = "/CA/cacert.pem";
    *caPath = join(certPath, caCert);
    *peerCertVerifyType = C_VERIFY_BY_DEFAULT;
    return 0;
}

bool CServerCreateDriver()
{
    int result = 0;
    Net_DriverOptions options;

    if (cServerDriver != 0) {
        NN_LOG_ERROR("cServerDriver already created");
        return false;
    }

    result = Net_DriverCreate(C_DRIVER_RDMA, "c_server", 1, &cServerDriver);
    if (result != 0) {
        NN_LOG_ERROR("failed to create cServerDriver already created");
        return false;
    }

    bzero(&options, sizeof(Net_DriverOptions));
    options.mode = C_EVENT_POLLING;
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 8192;
    options.enableTls = enableTls;
    options.cipherSuite = cipherSuite;
    strcpy(options.netDeviceIpMask, ipSeg);
    sprintf(options.workerGroupsCpuSet, "%u-%u", 20, 20);

    Net_DriverRegisterEpHandler(cServerDriver, C_EP_NEW, &SNewEndPoint, 1);
    Net_DriverRegisterEpHandler(cServerDriver, C_EP_BROKEN, &SEndPointBroken, 1);
    Net_DriverRegisterOpHandler(cServerDriver, C_OP_REQUEST_RECEIVED, &SRequestReceived, 1);
    Net_DriverRegisterOpHandler(cServerDriver, C_OP_REQUEST_POSTED, &SRequestPosted, 1);
    Net_DriverRegisterOpHandler(cServerDriver, C_OP_READWRITE_DONE, &SOneSideDone, 1);
    Net_DriverRegisterIdleHandler(cServerDriver, &SIdle, 1);
    if (enableTls) {
        Net_DriverRegisterTLSCb(cServerDriver, &SCertCallback, &SPrivateKeyCallback, &SCACallback);
    }

    Net_DriverSetOobIpAndPort(cServerDriver, "127.0.0.1", C_RDMA_LISTEN_PORT);

    if ((result = Net_DriverInitialize(cServerDriver, options)) != 0) {
        NN_LOG_ERROR("failed to initialize cServerDriver " << result);
        return false;
    }
    NN_LOG_INFO("cServerDriver initialized");

    if ((result = Net_DriverStart(cServerDriver)) != 0) {
        NN_LOG_INFO("failed to start cServerDriver " << result);
        return false;
    }
    NN_LOG_INFO("cServerDriver started");

    return true;
}

bool CServerRegSglMem()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        int result = Net_DriverCreateMemoryRegion(cServerDriver, NN_NO8, &mrRegion[i]);
        if (result != 0) {
            NN_LOG_INFO("reg mr failed");
            return false;
        }

        Net_MemoryRegionInfo mrInfo;
        result = Net_DriverGetMemoryRegionInfo(mrRegion[i], &mrInfo);
        if (result != 0) {
            NN_LOG_INFO("parse mr failed");
            return false;
        }
        cServerLocalMrInfo[i].lAddress = mrInfo.lAddress;
        cServerLocalMrInfo[i].lKey = mrInfo.lKey;
        cServerLocalMrInfo[i].size = mrInfo.size;
        memset((void *)(cServerLocalMrInfo[i].lAddress), 0, cServerLocalMrInfo[i].size);
    }

    return true;
}

// client
TestRegMrInfo cRemoteMrInfo[NN_NO4];
TestRegMrInfo cSelfLocalMrInfo[NN_NO4];
Net_MemoryRegion clientMrInfo[NN_NO4];
sem_t cSem;
uint32_t cExecCount = 0;
Net_Driver cDriver = 0;
Net_EndPoint cAsyncEp = 0;
Net_EndPoint cSyncEp = 0;

int16_t asyncWorkerCpuId = 10;
void CSendAsyncReadWriteRequest(Net_ReadWriteSge *iov, uint64_t index)
{
    int result = 0;

    Net_ReadWriteSglRequest req;
    req.iov = iov;
    req.iovCount = NN_NO4;
    req.upCtxSize = 0;
    result = Net_EPPostSglRead(cAsyncEp, &req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }
    sem_wait(&cSem);

    NN_LOG_TRACE_INFO("sgl read value idx:" << cExecCount++);
    for (uint16_t i = 0; i < NN_NO4; i++) {
        uint64_t *readValue = (uint64_t *)((void *)(iov[i].lAddress));
        uint64_t value = *readValue;
        EXPECT_EQ(value, index);
        NN_LOG_TRACE_INFO("value[" << i << "]=" << *readValue);
        *readValue = ++value;
    }

    result = Net_EPPostSglWrite(cAsyncEp, &req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }
    sem_wait(&cSem);

    Net_ReadWriteRequest buffReq = { 0 };
    buffReq.lMRA = iov[0].lAddress;
    buffReq.rMRA = iov[0].rAddress;
    buffReq.lKey = iov[0].lKey;
    buffReq.rKey = iov[0].rKey;
    buffReq.size = iov[0].size;
    result = Net_EPPostRead(cAsyncEp, &buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }
    sem_wait(&cSem);
    uint64_t *readBuff = reinterpret_cast<uint64_t *>((void *)(iov[0].lAddress));
    uint64_t readValue = *readBuff;
    EXPECT_EQ(readValue, index + 1);

    result = Net_EPPostWrite(cAsyncEp, &buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to read data from server");
        return;
    }
    sem_wait(&cSem);
}

void CAsyncReadWriteRequest()
{
    Net_ReadWriteSge iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = cSelfLocalMrInfo[i].lAddress;
        iov[i].rAddress = cRemoteMrInfo[i].lAddress;
        iov[i].lKey = cSelfLocalMrInfo[i].lKey;
        iov[i].rKey = cRemoteMrInfo[i].lKey;
        iov[i].size = NN_NO8;
    }
    sem_init(&cSem, 0, 0);
    for (int i = 0; i < NN_NO4; i++) {
        CSendAsyncReadWriteRequest(iov, i);
    }
}

void CAsyncPostSend()
{
    uint64_t data = C_SYNC_SEND_VALUE;
    Net_SendRequest req = { 0 };
    int result = 0;

    req.data = (uintptr_t)&data;
    req.size = sizeof(data);

    result = Net_EPPostSend(cAsyncEp, C_CHECK_SYNC_RESPONSE, &req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server" << result);
        return;
    }
    sem_wait(&cSem);

    result = Net_EPPostSend(cAsyncEp, C_SET_MR, &req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server" << result);
        return;
    }
    sem_wait(&cSem);
}

void CAsyncRequest()
{
    CAsyncPostSend();
    CAsyncReadWriteRequest();
}

void CSyncPostSend()
{
    uint64_t data = C_SYNC_SEND_VALUE;
    Net_SendRequest req = { 0 };
    int result = 0;

    req.data = (uintptr_t)&data;
    req.size = sizeof(data);

    result = Net_EPPostSend(cSyncEp, C_CHECK_SYNC_RESPONSE, &req);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server" << result);
        return;
    }

    result = Net_EPWaitCompletion(cSyncEp, -1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait data to server" << result);
        return;
    }

    Net_ResponseContext *ctx;
    result = Net_EPReceive(cSyncEp, -1, &ctx);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to receive raw data to server" << result);
        return;
    }

    result = Net_EPPostSendRaw(cSyncEp, &req, C_SEND_RAW);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to post message to data to server" << result);
        return;
    }

    result = Net_EPWaitCompletion(cSyncEp, -1);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to wait data to server" << result);
        return;
    }

    result = Net_EPReceiveRaw(cSyncEp, -1, &ctx);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_ERROR("failed to receive raw data to server" << result);
        return;
    }
}

void CSyncRequest()
{
    CSyncPostSend();
}


int CRequestReceived(Net_RequestContext *ctx, uint64_t usrCtx)
{
    if (ctx->type == C_RECEIVED) {
        if (ctx->opCode == C_CHECK_SYNC_RESPONSE) {
            uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ctx->msgData));
            EXPECT_EQ(C_SYNC_RECEIVE_VALUE, *readValue);
            sem_post(&cSem);
        } else if (ctx->opCode == C_SET_MR) {
            uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ctx->msgData));
            EXPECT_EQ(C_SET_MSG_SUCCESS, *readValue);
            sem_post(&cSem);
        } else if (ctx->opCode == C_GET_MR) {
            memcpy(cRemoteMrInfo, ctx->msgData, ctx->msgSize);
            sem_post(&cSem);
        }
    } else if (ctx->type == C_RECEIVED_RAW) {
    }

    return 0;
}

static int CEndPointBroken(Net_EndPoint bep, uint64_t usrCtx, const char *payLoad)
{
    NN_LOG_INFO("end point " << Net_EPGetContext(bep) << " broken");
    return 0;
}


int CRequestPosted(Net_RequestContext *ctx, uint64_t usrCtx)
{
    return 0;
}

int COneSideDone(Net_RequestContext *ctx, uint64_t usrCtx)
{
    sem_post(&cSem);
    return 0;
}

void CIdle(uint8_t wkrGrpIdx, uint16_t workerIndex, uint64_t usrCtx) {}

static void CErase(char *pass, int len) {}

static int CVerify(void *x509, const char *path)
{
    NN_LOG_INFO("verify");
    return 0;
}

static int CCertCallback(const char *name, char **value)
{
    char cert[] = "/client/cert.pem";
    *value = join(certPath, cert);
    return 1;
}

static int CPrivateKeyCallback(const char *name, char **value, char **keyPass, Net_TlsKeyPassErase *erase)
{
    static char content[] = "huawei";
    *keyPass = content;
    char keyPerm[] = "/client/key.pem";
    *value = join(certPath, keyPerm);
    *erase = &CErase;

    return 1;
}

static int CCACallback(const char *name, char **caPath, char **crlPath, Net_PeerCertVerifyType *verifyType,
    Net_TlsCertVerify *cb)
{
    char caCert[] = "/CA/cacert.pem";
    *caPath = join(certPath, caCert);
    *verifyType = C_VERIFY_BY_NONE;
    return 1;
}

static bool CCreateDriver()
{
    int result = 0;
    Net_DriverOptions options;

    if (cDriver != 0) {
        NN_LOG_ERROR("cDriver already created");
        return false;
    }

    Net_DeviceInfo deviceInfo;
    EXPECT_EQ(Net_LocalSupport(C_DRIVER_RDMA, &deviceInfo), 1);

    result = Net_DriverCreate(C_DRIVER_RDMA, "c_client", 0, &cDriver);
    if (result != 0) {
        NN_LOG_ERROR("failed to create cDriver already created");
        return false;
    }

    bzero(&options, sizeof(Net_DriverOptions));
    options.mode = C_EVENT_POLLING;
    options.mrSendReceiveSegSize = 2048;
    options.mrSendReceiveSegCount = 8192;
    strcpy(options.netDeviceIpMask, ipSeg);
    options.qpSendQueueSize = 512;
    options.qpReceiveQueueSize = 512;
    options.version = 1;
    options.enableTls = enableTls;
    options.cipherSuite = cipherSuite;

    Net_DriverRegisterEpHandler(cDriver, C_EP_BROKEN, &CEndPointBroken, 2);
    Net_DriverRegisterOpHandler(cDriver, C_OP_REQUEST_RECEIVED, &CRequestReceived, 2);
    Net_DriverRegisterOpHandler(cDriver, C_OP_REQUEST_POSTED, &CRequestPosted, 2);
    Net_DriverRegisterOpHandler(cDriver, C_OP_READWRITE_DONE, &COneSideDone, 2);

    if (enableTls) {
        Net_DriverRegisterTLSCb(cDriver, &CCertCallback, &CPrivateKeyCallback, &CCACallback);
    }

    auto handle = Net_DriverRegisterIdleHandler(cDriver, &CIdle, 2);
    EXPECT_NE(handle, 0);

    Net_DriverSetOobIpAndPort(cDriver, "0.0.0.0", C_RDMA_LISTEN_PORT);

    if ((result = Net_DriverInitialize(cDriver, options)) != 0) {
        NN_LOG_ERROR("failed to initialize cDriver " << result);
        return false;
    }
    printf("cDriver initialized");

    if ((result = Net_DriverStart(cDriver)) != 0) {
        NN_LOG_ERROR("failed to start cDriver %d" << result);
        return false;
    }
    NN_LOG_INFO("cDriver started");

    return true;
}

bool CAsyncConnect()
{
    int result = 0;

    if (cDriver == 0) {
        NN_LOG_ERROR("cDriver is null");
        return false;
    }

    if ((result = Net_DriverConnect(cDriver, "hello world", &cAsyncEp, 0)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result %d" << result);
        return false;
    }

    Net_EPSetContext(cAsyncEp, 0);
    EXPECT_EQ(Net_EPGetContext(cAsyncEp), 0);

    // default config worker group = 1, num = 1, index = 0
    EXPECT_EQ(Net_EPGetWorkerIndex(cAsyncEp), 0);
    EXPECT_EQ(Net_EPGetWorkerGroupIndex(cAsyncEp), 0);

    // config when create driver
    EXPECT_EQ(Net_EPGetListenPort(cAsyncEp), C_RDMA_LISTEN_PORT);
    EXPECT_EQ(Net_EPGetVersion(cAsyncEp), 1);

    sem_init(&cSem, 0, 0);
    Net_SendRequest req = { 0 };

    char value[] = "hello world";
    req.data = (uintptr_t)value;
    req.size = sizeof(value);

    if ((result = Net_EPPostSend(cAsyncEp, C_GET_MR, &req)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return false;
    }

    sem_wait(&cSem);
    return true;
}

bool CSyncConnect()
{
    int result = 0;

    if (cDriver == 0) {
        NN_LOG_ERROR("cDriver is null");
        return false;
    }

    if ((result = Net_DriverConnectToIpPort(cDriver, "0.0.0.0", C_RDMA_LISTEN_PORT, "hello world", &cSyncEp,
        NET_C_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result %d" << result);
        return false;
    }

    return true;
}

bool CClientRegSglMem()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        auto result = Net_DriverCreateMemoryRegion(cDriver, NN_NO8, &clientMrInfo[i]);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }

        Net_MemoryRegionInfo regMr;
        result = Net_DriverGetMemoryRegionInfo(clientMrInfo[i], &regMr);
        if (result != NN_OK) {
            printf("parse mr failed\n");
            return false;
        }
        cSelfLocalMrInfo[i].lAddress = regMr.lAddress;
        cSelfLocalMrInfo[i].lKey = regMr.lKey;
        cSelfLocalMrInfo[i].size = regMr.size;
        memset(reinterpret_cast<void *>(cSelfLocalMrInfo[i].lAddress), 0, cSelfLocalMrInfo[i].size);
    }

    char *buff = (char *)malloc(4096);
    Net_MemoryRegion tmp;
    auto result = Net_DriverCreateAssignMemoryRegion(cDriver, (uintptr_t)buff, 4096, &tmp);
    EXPECT_EQ(result, 0);
    if (result == 0) {
        Net_DriverDestroyMemoryRegion(cDriver, tmp);
    }

    return true;
}

bool CClientUnRegSglMem()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        Net_DriverDestroyMemoryRegion(cDriver, clientMrInfo[i]);
    }

    return true;
}

static int ValidateTlsCert()
{
    char *buffer;

    if ((buffer = getcwd(NULL, 0)) == NULL) {
        NN_LOG_ERROR("Cet path for TLS cert failed");
        return -1;
    }

    char *currentPath = buffer;
    char base[] = "/../test/opensslcrt/normalCert1";
    certPath = join(currentPath, base);

    char cacert[] = "/CA/cacert.pem";
    if (::access(join(certPath, cacert), F_OK) != 0) {
        NN_LOG_ERROR("cacert.pem cannot be found under " << certPath);
        return -1;
    }

    char cert[] = "/server/cert.pem";
    if (::access(join(certPath, cert), F_OK) != 0) {
        NN_LOG_ERROR("cert.pem cannot be found under " << certPath);
        return -1;
    }

    char key[] = "/server/key.pem";
    if (::access(join(certPath, key), F_OK) != 0) {
        NN_LOG_ERROR("key.pem cannot be found under " << certPath);
        return -1;
    }

    return 0;
}

TEST_F(TestCaseRdmaC, RDMA_C_BASIC_OPERATE)
{
    MOCK_VERSION

    if (enableTls) {
        ValidateTlsCert();
    }

    int result = 0;

    result = CServerCreateDriver();
    CHECK_RESULT_TRUE(result);
    result = CServerRegSglMem();
    CHECK_RESULT_TRUE(result);

    result = CCreateDriver();
    CHECK_RESULT_TRUE(result);
    result = CAsyncConnect();
    CHECK_RESULT_TRUE(result);
    result = CClientRegSglMem();
    CHECK_RESULT_TRUE(result);
    CAsyncRequest();

    result = CSyncConnect();
    CHECK_RESULT_TRUE(result);
    CSyncRequest();

    CClientUnRegSglMem();

    Net_DriverStop(cServerDriver);
    Net_DriverUnInitialize(cServerDriver);
    Net_DriverDestroy(cServerDriver);

    Net_DriverStop(cDriver);
    Net_DriverUnInitialize(cDriver);
    Net_DriverDestroy(cDriver);
}
#endif