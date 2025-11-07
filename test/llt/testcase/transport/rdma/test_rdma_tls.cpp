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
#include <unistd.h>

#include "hcom.h"
#include "hcom_utils.h"
#include "common/net_util.h"
#include "string.h"
#include "test_rdma_tls.hpp"
#include "ut_helper.h"

using namespace ock::hcom;

// server
UBSHcomNetDriver *tlsDriver = nullptr;
UBSHcomNetDriver *invalidTlsDriver = nullptr;
UBSHcomNetDriver *notSameCaTlsDriver = nullptr;
UBSHcomNetDriver *certExpiredTlsDriver = nullptr;
UBSHcomNetDriver *certRevokedTlsDriver = nullptr;
UBSHcomNetDriver *cVerifyByNoneTlsDriver = nullptr;
UBSHcomNetDriver *multiLevelCertTlsDriver = nullptr;
UBSHcomNetDriver *abnormalCertChainDriver = nullptr;
UBSHcomNetDriver *normalCertChainDriver = nullptr;
UBSHcomNetDriver *customVerifyTlsDriver = nullptr;

static UBSHcomNetDriverOptions options {};

UBSHcomNetEndpointPtr tlsServerEp = nullptr;
std::string tlsIpSeg = IP_SEG;
std::string certPath;
std::string expiredCertPath;
std::string otherCertPath;
std::string revokedCertPath;
std::string cliVerifyByNoneCertPath;
std::string multiCertPath;
std::string abnormalCertChainPath;
std::string normalCertChainPath;

std::string syncSendValue = "sync send value";
std::string syncReplyValue = "sync response by server value";
std::string asyncSendValue = "async send value";
std::string asyncSendRawValue = "async send raw value";
std::string syncSendRawValue = "sync send raw value";

using TestOpCode = enum {
    CHECK_ASYNC_RESPONSE = 1,
    CHECK_SYNC_RESPONSE,
    SEND_RAW,
    RECEIVE_RAW,
};


using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));
TestRegMrInfo tlsSerlocalMrInfo[NN_NO4];

bool driverInitAndStart(UBSHcomNetDriver *driver)
{
    int result = 0;
    if ((result = driver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("driver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start driver " << result);
        return false;
    }
    NN_LOG_INFO("driver started");
    return true;
}


int TlsServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    tlsServerEp = newEP;
    return 0;
}

void TlsServerEndPointBroken(const UBSHcomNetEndpointPtr &tlsServerEp)
{
    NN_LOG_INFO("end point " << tlsServerEp->Id());
}

int TlsServerRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    std::string req((char *)ctx.Message()->Data(), ctx.Header().dataLength);
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << req.length());

    int result = 0;
    if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED) {
        if (ctx.Header().opCode == CHECK_SYNC_RESPONSE) {
            EXPECT_EQ(syncSendValue.length(), req.length());
            EXPECT_EQ(0, memcmp(syncSendValue.c_str(), req.c_str(), syncSendValue.size()));

            UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(syncReplyValue.c_str())),
                syncReplyValue.length(), 0);

            if ((result = ctx.EndPoint()->PostSend(ctx.Header().opCode, rsp)) != 0) {
                NN_LOG_ERROR("failed to post message to data to server, result " << result);
                return result;
            }
        } else {
            EXPECT_EQ(asyncSendValue.length(), req.length());
            EXPECT_EQ(0, memcmp(asyncSendValue.c_str(), req.c_str(), asyncSendValue.size()));
        }
    } else if (ctx.OpType() == UBSHcomNetRequestContext::NN_RECEIVED_RAW) {
        if (ctx.Header().seqNo == CHECK_SYNC_RESPONSE) {
            EXPECT_EQ(syncSendRawValue.length(), req.length());
            EXPECT_EQ(0, memcmp(syncSendRawValue.c_str(), req.c_str(), syncSendRawValue.size()));

            UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(syncReplyValue.c_str())),
                syncReplyValue.length(), 0);

            if ((result = ctx.EndPoint()->PostSendRaw(rsp, ctx.Header().opCode)) != 0) {
                NN_LOG_ERROR("failed to post message to data to server, result " << result);
                return result;
            }
        } else {
            EXPECT_EQ(asyncSendRawValue.length(), req.length());
            EXPECT_EQ(0, memcmp(asyncSendRawValue.c_str(), req.c_str(), asyncSendRawValue.size()));
        }
    }

    return 0;
}

int TlsServerRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}

int TlsServerOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

static void Erase(void *pass, int len) {}
static int Verify(void *x509, const char *path)
{
    NN_LOG_INFO("verify by custom func");
    return 0;
}

static bool CertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/server/cert.pem";
    return true;
}

static bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = nullptr;
    return true;
}

int ValidateTlsCert()
{
    char *buffer;

    if ((buffer = getcwd(NULL, 0)) == NULL) {
        NN_LOG_ERROR("Cet path for TLS cert failed");
        return -1;
    }

    std::string currentPath = buffer;

    certPath = currentPath + "/../test/opensslcrt/normalCert1";
    otherCertPath = currentPath + "/../test/opensslcrt/normalCert2";
    expiredCertPath = currentPath + "/../test/opensslcrt/expiredCert";
    revokedCertPath = currentPath + "/../test/opensslcrt/crlRevokedCert";
    cliVerifyByNoneCertPath = currentPath + "/../test/opensslcrt/serExpCertCliNoCheck";
    multiCertPath = currentPath + "/../test/opensslcrt/multiLevelCert";
    abnormalCertChainPath = currentPath + "/../test/opensslcrt/abnormalCertChain";
    normalCertChainPath = currentPath + "/../test/opensslcrt/normalCertChain";

    if (!CanonicalPath(certPath)) {
        NN_LOG_ERROR("TLS cert path check failed " << certPath);
        return -1;
    }

    if (!CanonicalPath(otherCertPath)) {
        NN_LOG_ERROR("TLS cert path check failed " << certPath);
        return -1;
    }

    if (!CanonicalPath(expiredCertPath)) {
        NN_LOG_ERROR("TLS cert path check failed " << certPath);
        return -1;
    }

    return 0;
}

void setServerDriverCallback(UBSHcomNetDriver *driver, UBSHcomTLSCertificationCallback CertCallback,
    UBSHcomTLSCaCallback CACallback, UBSHcomTLSPrivateKeyCallback PrivateKeyCallback)
{
    driver->RegisterNewEPHandler(
        std::bind(&TlsServerNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&TlsServerEndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&TlsServerRequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&TlsServerRequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&TlsServerOneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
}

// server with invalid cert path
static bool InvalidCertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/server/cacert.pem";
    return true;
}

static bool InvalidPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/server/cert.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool InvalidCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/key.pem";
    return true;
}

bool ServerCreateDriverWithInvalidTls()
{
    if (invalidTlsDriver != nullptr) {
        NN_LOG_ERROR("invalidTlsDriver already created");
    }

    invalidTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsCertErrServer", true);
    if (invalidTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create invalidTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(invalidTlsDriver, InvalidCertCallback, InvalidCACallback, InvalidPrivateKeyCallback);

    invalidTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(invalidTlsDriver);
}


// server with not same Ca cert
static bool VerifyFailedCertCallback(const std::string &name, std::string &value)
{
    value = otherCertPath + "/server/cert.pem";
    return true;
}

static bool VerifyFailedPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = otherCertPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool VerifyFailedCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = otherCertPath + "/CA/cacert.pem";
    return true;
}

bool ServerCreateDriverTlsNotSameCACert()
{
    if (notSameCaTlsDriver != nullptr) {
        NN_LOG_ERROR("notSameCaTlsDriver already created");
    }

    notSameCaTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "verifyFailedServer", true);
    if (notSameCaTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create notSameCaTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(notSameCaTlsDriver, VerifyFailedCertCallback, VerifyFailedCACallback,
        VerifyFailedPrivateKeyCallback);

    notSameCaTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(notSameCaTlsDriver);
}

// server with expired cert
static bool ExpiredCertCertCallback(const std::string &name, std::string &value)
{
    value = expiredCertPath + "/server/cert.pem";
    return true;
}

static bool ExpiredCertPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = expiredCertPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool ExpiredCertCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = expiredCertPath + "/CA/cacert.pem";
    return true;
}

bool ServerCreateDriverTlsWithExpiredCert()
{
    if (certExpiredTlsDriver != nullptr) {
        NN_LOG_ERROR("certExpiredTlsDriver already created");
    }

    certExpiredTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "certExpiredServer", true);
    if (certExpiredTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create certExpiredTlsDriver already created");
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(certExpiredTlsDriver, ExpiredCertCertCallback, ExpiredCertCACallback,
        ExpiredCertPrivateKeyCallback);

    certExpiredTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(certExpiredTlsDriver);
}

// server with revoked cert
static bool RevokedCertCertCallback(const std::string &name, std::string &value)
{
    value = revokedCertPath + "/server/cert.pem";
    return true;
}

static bool RevokedCertPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = revokedCertPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool RevokedCertCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    crlPath = revokedCertPath + "/CA/ca.crl";
    caPath = revokedCertPath + "/CA/cacert.pem";
    return true;
}

bool ServerCreateDriverTlsWithRevokedCert()
{
    if (certRevokedTlsDriver != nullptr) {
        NN_LOG_ERROR("certRevokedTlsDriver already created");
    }

    certRevokedTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "certRevokedServer", true);
    if (certRevokedTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create certRevokedTlsDriver already created");
        return false;
    }


    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(certRevokedTlsDriver, RevokedCertCertCallback, RevokedCertCACallback,
        RevokedCertPrivateKeyCallback);

    certRevokedTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(certRevokedTlsDriver);
}

// server with client verify by none
static bool CliVerifyByNoneCertCallback(const std::string &name, std::string &value)
{
    value = cliVerifyByNoneCertPath + "/server/cert.pem";
    return true;
}

static bool CliVerifyByNonePrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = cliVerifyByNoneCertPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CliVerifyByNoneACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = cliVerifyByNoneCertPath + "/CA/cacert.pem";
    return true;
}

bool ServerCreateDriverTlsWithCVerifyByNone()
{
    if (cVerifyByNoneTlsDriver != nullptr) {
        NN_LOG_ERROR("cVerifyByNoneTlsDriver already created");
    }

    cVerifyByNoneTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "cliVerifyByNoneServer", true);
    if (certRevokedTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create cVerifyByNoneTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(cVerifyByNoneTlsDriver, CliVerifyByNoneCertCallback, CliVerifyByNoneACallback,
        CliVerifyByNonePrivateKeyCallback);

    cVerifyByNoneTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(cVerifyByNoneTlsDriver);
}

// server with multi level CA cert
static bool MultiLevelCertCallback(const std::string &name, std::string &value)
{
    value = multiCertPath + "/server/server.crt";
    return true;
}

static bool MultiLevelPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = multiCertPath + "/server/server.key";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool MultiLevelCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = multiCertPath + "/CA/rootca.crt";
    std::string secondCa = multiCertPath + "/CA/secondca.crt";
    caPath = rootCa + ":" + secondCa;
    return true;
}

bool ServerCreateDriverTlsWithMultiLevelCert()
{
    if (multiLevelCertTlsDriver != nullptr) {
        NN_LOG_ERROR("multiLevelCertTlsDriver already created");
    }

    multiLevelCertTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "multiLeverCertServer", true);
    if (multiLevelCertTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create multiLevelCertTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(multiLevelCertTlsDriver, MultiLevelCertCallback, MultiLevelCACallback,
        MultiLevelPrivateKeyCallback);

    multiLevelCertTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(multiLevelCertTlsDriver);
}

// server with abnormal cert chain
static bool AbnormalCertChainCertCallback(const std::string &name, std::string &value)
{
    value = abnormalCertChainPath + "/server/cert.pem";
    return true;
}

static bool AbnormalCertChainPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = abnormalCertChainPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool AbnormalCertChainCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = abnormalCertChainPath + "/CA/cacert.pem";
    std::string secondCa = abnormalCertChainPath + "/CA/secondca.crt";
    caPath = rootCa;
    return true;
}

bool ServerCreateDriverTlsWithAbnormalCertChain()
{
    if (abnormalCertChainDriver != nullptr) {
        NN_LOG_ERROR("multiLevelCertTlsDriver already created");
    }

    abnormalCertChainDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA,
        "AbnormalCertChainServer", true);
    if (abnormalCertChainDriver == nullptr) {
        NN_LOG_ERROR("failed to create multiLevelCertTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(abnormalCertChainDriver, AbnormalCertChainCertCallback, AbnormalCertChainCACallback,
        AbnormalCertChainPrivateKeyCallback);

    abnormalCertChainDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(abnormalCertChainDriver);
}

// server with normal cert chain
static bool NormalCertChainCertCallback(const std::string &name, std::string &value)
{
    value = normalCertChainPath + "/server/cert.pem";
    return true;
}

static bool NormalCertChainPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = normalCertChainPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool NormalCertChainCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = normalCertChainPath + "/CA/cacert.pem";
    caPath = rootCa;
    return true;
}

bool ServerCreateDriverTlsWithNormalCertChain()
{
    if (abnormalCertChainDriver != nullptr) {
        NN_LOG_ERROR("abnormalCertChainDriver already created");
    }

    normalCertChainDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "NormalCertChainServer", true);
    if (normalCertChainDriver == nullptr) {
        NN_LOG_ERROR("failed to create normalCertChainDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(normalCertChainDriver, NormalCertChainCertCallback, NormalCertChainCACallback,
        NormalCertChainPrivateKeyCallback);

    normalCertChainDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(normalCertChainDriver);
}

// server with custom verify func
static bool CustomVerifyCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    peerCertVerifyType = ock::hcom::VERIFY_BY_CUSTOM_FUNC;
    return true;
}

bool ServerCreateDriverTlsCustomVerify()
{
    if (customVerifyTlsDriver != nullptr) {
        NN_LOG_ERROR("multiLevelCertTlsDriver already created");
    }

    customVerifyTlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "customVerifyServer", true);
    if (multiLevelCertTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create multiLevelCertTlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(customVerifyTlsDriver, CertCallback, CustomVerifyCACallback, PrivateKeyCallback);

    customVerifyTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(customVerifyTlsDriver);
}

// server in norma case
bool ServerCreateDriverWithTls()
{
    if (tlsDriver != nullptr) {
        NN_LOG_ERROR("tlsDriver already created");
        return false;
    }

    tlsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsServer", true);
    if (tlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsDriver already created");
        return false;
    }

    options.SetNetDeviceIpMask(tlsIpSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    setServerDriverCallback(tlsDriver, CertCallback, CACallback, PrivateKeyCallback);

    tlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsDriver);
}

bool ServerRegSglMemWithTls()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = tlsDriver->CreateMemoryRegion(NN_NO8, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        tlsSerlocalMrInfo[i].lAddress = mr->GetAddress();
        tlsSerlocalMrInfo[i].lKey = mr->GetLKey();
        tlsSerlocalMrInfo[i].size = NN_NO8;
        memset(reinterpret_cast<void *>(tlsSerlocalMrInfo[i].lAddress), 0, NN_NO8);
    }

    return true;
}

// client
UBSHcomNetDriver *tlsClientDriver = nullptr;
UBSHcomNetDriver *tlsClientCertExpiredDriver = nullptr;
UBSHcomNetDriver *tlsClientCertRevokedDriver = nullptr;
UBSHcomNetDriver *tlsClientVerifyByNoneDriver = nullptr;
UBSHcomNetDriver *tlsClientMultiLevelCertDriver = nullptr;
UBSHcomNetDriver *tlsClientAbnormalCertChainDriver = nullptr;
UBSHcomNetDriver *tlsClientNormalCertChainDriver = nullptr;
UBSHcomNetDriver *tlsClientCustomVerifyTlsDriver = nullptr;

UBSHcomNetEndpointPtr tlsClientSyncEp = nullptr;
UBSHcomNetEndpointPtr tlsClientAsyncEp = nullptr;

void TlsClientEndPointBroken(const UBSHcomNetEndpointPtr &clientEp)
{
    NN_LOG_INFO("end point " << clientEp->Id() << " broken");
}

int TlsClientRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int TlsClientRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int TlsClientOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

void setClientDriverCallback(UBSHcomNetDriver *driver, UBSHcomTLSCertificationCallback CertCallback,
    UBSHcomTLSCaCallback CACallback, UBSHcomTLSPrivateKeyCallback PrivateKeyCallback)
{
    driver->RegisterEPBrokenHandler(std::bind(&TlsClientEndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&TlsClientRequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&TlsClientRequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&TlsClientOneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
}

// client in normal case
static bool ClientCertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/client/cert.pem";
    return true;
}

static bool ClientPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool ClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    return true;
}


bool ClientCreateDriverWithTls()
{
    if (tlsClientDriver != nullptr) {
        NN_LOG_ERROR("tlsClientDriver already created");
    }

    tlsClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsClient1", false);
    if (tlsClientDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientDriver already created");
    }
    setClientDriverCallback(tlsClientDriver, &ClientCertCallback, &ClientCACallback, &ClientPrivateKeyCallback);

    tlsClientDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientDriver);
}

bool SyncClientConnectWithInvalidTls()
{
    if (tlsClientDriver == nullptr) {
        NN_LOG_ERROR("tlsClientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);
        return false;
    }

    tlsClientSyncEp->PeerIpAndPort();
    return true;
}

bool SyncClientConnectWithTls()
{
    if (tlsClientDriver == nullptr) {
        NN_LOG_ERROR("tlsClientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

bool AsyncClientConnectWithTls()
{
    if (tlsClientDriver == nullptr) {
        NN_LOG_ERROR("clientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientDriver->Connect("hello world", tlsClientAsyncEp, 0)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

void TlsAsyncRequest()
{
    int result;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(asyncSendValue.c_str())), asyncSendValue.length(), 0);

    if ((result = tlsClientAsyncEp->PostSend(0, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return;
    }

    EXPECT_EQ(NN_OK, result);
}

void TlsAsyncSendRawRequest()
{
    int result = 0;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(asyncSendRawValue.c_str())), asyncSendRawValue.length(), 0);

    if ((result = tlsClientAsyncEp->PostSendRaw(req, 1)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server");
        return;
    }

    EXPECT_EQ(result, NN_OK);
}


void TlsSyncRequests()
{
    int result;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(syncSendValue.c_str())), syncSendValue.length(), 0);

    if ((result = tlsClientSyncEp->PostSend(CHECK_SYNC_RESPONSE, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return;
    }


    if ((result = tlsClientSyncEp->WaitCompletion(0)) != 0) {
        NN_LOG_INFO("failed to wait completion, result " << result);
        return;
    }

    UBSHcomNetResponseContext respCtx {};
    if ((result = tlsClientSyncEp->Receive(respCtx)) != 0) {
        NN_LOG_INFO("failed to get response, result " << result);
        return;
    }

    EXPECT_EQ(NN_OK, result);
    EXPECT_EQ(syncReplyValue.length(), respCtx.Message()->DataLen());
    EXPECT_EQ(0, strncmp(syncReplyValue.c_str(), (char *)respCtx.Message()->Data(), syncReplyValue.length()));
}

void TlsSyncSendRawRequests()
{
    int result;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(syncSendRawValue.c_str())), syncSendRawValue.length(), 0);

    if ((result = tlsClientSyncEp->PostSendRaw(req, CHECK_SYNC_RESPONSE)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return;
    }


    if ((result = tlsClientSyncEp->WaitCompletion(0)) != 0) {
        NN_LOG_INFO("failed to wait completion, result " << result);
        return;
    }

    UBSHcomNetResponseContext respCtx {};
    if ((result = tlsClientSyncEp->ReceiveRaw(respCtx)) != 0) {
        NN_LOG_INFO("failed to get response, result " << result);
        return;
    }

    EXPECT_EQ(NN_OK, result);
    EXPECT_EQ(syncReplyValue.length(), respCtx.Message()->DataLen());
    EXPECT_EQ(0, strncmp(syncReplyValue.c_str(), (char *)respCtx.Message()->Data(), syncReplyValue.length()));
}

// client with expired cert
static bool CertExpiredClientCertCallback(const std::string &name, std::string &value)
{
    value = expiredCertPath + "/client/cert.pem";
    return true;
}

static bool CertExpiredClientPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = expiredCertPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CertExpiredClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = expiredCertPath + "/CA/cacert.pem";
    return true;
}

bool ClientCreateDriverWithTlsExpiredCert()
{
    if (tlsClientCertExpiredDriver != nullptr) {
        NN_LOG_ERROR("tlsClientCertExpiredDriver already created");
    }

    tlsClientCertExpiredDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "certExpiredClient", false);
    if (tlsClientCertExpiredDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientCertExpiredDriver already created");
        return false;
    }
    setClientDriverCallback(tlsClientCertExpiredDriver, &CertExpiredClientCertCallback, &CertExpiredClientCACallback,
        &CertExpiredClientPrivateKeyCallback);

    tlsClientCertExpiredDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientCertExpiredDriver);
}

bool SyncClientConnectWithTlsCertExpired()
{
    if (tlsClientCertExpiredDriver == nullptr) {
        NN_LOG_ERROR("tlsClientCertExpiredDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientCertExpiredDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with revoked cert
static bool CertRevokedClientCertCallback(const std::string &name, std::string &value)
{
    value = revokedCertPath + "/client/cert.pem";
    return true;
}

static bool CertRevokedClientPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = revokedCertPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CertRevokedClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    crlPath = revokedCertPath + "/CA/ca.crl";
    caPath = revokedCertPath + "/CA/cacert.pem";
    return true;
}

bool ClientCreateDriverWithTlsRevokedCert()
{
    if (tlsClientCertRevokedDriver != nullptr) {
        NN_LOG_ERROR("tlsClientCertRevokedDriver already created");
    }

    tlsClientCertRevokedDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "certRevokedClient", false);
    if (tlsClientCertRevokedDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientCertRevokedDriver already created");
        return false;
    }

    setClientDriverCallback(tlsClientCertRevokedDriver, &CertRevokedClientCertCallback, &CertRevokedClientCACallback,
        &CertRevokedClientPrivateKeyCallback);


    tlsClientCertRevokedDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientCertRevokedDriver);
}


bool SyncClientConnectWithTlsCertRevoked()
{
    if (tlsClientCertRevokedDriver == nullptr) {
        NN_LOG_ERROR("tlsClientCertRevokedDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientCertRevokedDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with verify by none
static bool VerifyByNoneClientCertCallback(const std::string &name, std::string &value)
{
    value = cliVerifyByNoneCertPath + "/client/cert.pem";
    return true;
}

static bool VerifyByNoneClientPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = cliVerifyByNoneCertPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool VerifyByNoneClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = cliVerifyByNoneCertPath + "/CA/cacert.pem";
    peerCertVerifyType = ock::hcom::VERIFY_BY_NONE;
    return true;
}

bool ClientCreateDriverWithTlsVerifyByNone()
{
    if (tlsClientVerifyByNoneDriver != nullptr) {
        NN_LOG_ERROR("tlsClientVerifyByNoneDriver already created");
    }

    tlsClientVerifyByNoneDriver =
        UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsClientVerifyByNoneDriver", false);
    if (tlsClientVerifyByNoneDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientVerifyByNoneDriver already created");
        return false;
    }
    setClientDriverCallback(tlsClientVerifyByNoneDriver, &VerifyByNoneClientCertCallback, &VerifyByNoneClientCACallback,
        &VerifyByNoneClientPrivateKeyCallback);

    tlsClientVerifyByNoneDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientVerifyByNoneDriver);
}


bool SyncClientConnectWithTlsVerifyByNone()
{
    if (tlsClientVerifyByNoneDriver == nullptr) {
        NN_LOG_ERROR("tlsClientCertRevokedDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientVerifyByNoneDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with multi level ca
static bool MultiLevelClientCertCallback(const std::string &name, std::string &value)
{
    value = multiCertPath + "/client/client.crt";
    return true;
}

static bool MultiLevelClientPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = multiCertPath + "/client/client.key";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool MultiLevelClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = multiCertPath + "/CA/rootca.crt";
    std::string secondCa = multiCertPath + "/CA/secondca.crt";
    caPath = rootCa + ":" + secondCa;
    return true;
}

bool ClientCreateDriverWithTlsMultiLevelCert()
{
    if (tlsClientMultiLevelCertDriver != nullptr) {
        NN_LOG_ERROR("tlsClientMultiLevelCertDriver already created");
        return false;
    }

    tlsClientMultiLevelCertDriver =
        UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsClientMultiLevelDriver", false);
    if (tlsClientMultiLevelCertDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientMultiLevelCertDriver already created");
        return false;
    }

    setClientDriverCallback(tlsClientMultiLevelCertDriver, &MultiLevelClientCertCallback, &MultiLevelClientCACallback,
        &MultiLevelClientPrivateKeyCallback);

    tlsClientMultiLevelCertDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientMultiLevelCertDriver);
}

bool SyncClientConnectWithMultiLevelCert()
{
    if (tlsClientMultiLevelCertDriver == nullptr) {
        NN_LOG_ERROR("tlsClientCertRevokedDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientMultiLevelCertDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with abnormal cert chain
static bool AbnormalClientCertChainCallback(const std::string &name, std::string &value)
{
    value = abnormalCertChainPath + "/client/cert.pem";
    return true;
}

static bool AbnormalClientCertChainPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass,
    int &len, UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = abnormalCertChainPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool AbnormalClientCertChainCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = abnormalCertChainPath + "/CA/cacert.pem";
    caPath = rootCa;
    return true;
}

bool ClientCreateDriverWithTlsAbnormalCertChain()
{
    if (tlsClientAbnormalCertChainDriver != nullptr) {
        NN_LOG_ERROR("tlsClientAbnormalCertChainDriver already created");
        return false;
    }

    tlsClientAbnormalCertChainDriver =
        UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "tlsClientAbnormalCertChainDriver", false);
    if (tlsClientAbnormalCertChainDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientAbnormalCertChainDriver already created");
        return false;
    }

    setClientDriverCallback(tlsClientAbnormalCertChainDriver, &AbnormalClientCertChainCallback,
        &AbnormalClientCertChainCACallback, &AbnormalClientCertChainPrivateKeyCallback);

    tlsClientAbnormalCertChainDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientAbnormalCertChainDriver);
}

bool SyncClientConnectWithAbnormalCertChain()
{
    if (tlsClientAbnormalCertChainDriver == nullptr) {
        NN_LOG_ERROR("tlsClientAbnormalCertChainDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientAbnormalCertChainDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) !=
        0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with normal cert chain
static bool NormalClientCertChainCallback(const std::string &name, std::string &value)
{
    value = normalCertChainPath + "/client/cert.pem";
    return true;
}

static bool NormalClientCertChainPrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass,
    int &len, UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = normalCertChainPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool NormalClientCertChainCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    std::string rootCa = normalCertChainPath + "/CA/cacert.pem";
    caPath = rootCa;
    return true;
}

bool ClientCreateDriverWithTlsNormalCertChain()
{
    if (tlsClientNormalCertChainDriver != nullptr) {
        NN_LOG_ERROR("tlsClientNormalCertChainDriver already created");
        return false;
    }

    tlsClientNormalCertChainDriver =
        UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "tlsClientNormalCertChainDriver", false);
    if (tlsClientNormalCertChainDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientNormalCertChainDriver already created");
        return false;
    }

    setClientDriverCallback(tlsClientNormalCertChainDriver, &NormalClientCertChainCallback,
        &NormalClientCertChainCACallback, &NormalClientCertChainPrivateKeyCallback);

    tlsClientNormalCertChainDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientNormalCertChainDriver);
}

bool SyncClientConnectWithNormalCertChain()
{
    if (tlsClientNormalCertChainDriver == nullptr) {
        NN_LOG_ERROR("tlsClientNormalCertChainDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientNormalCertChainDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

// client with custom verify func
static bool CustomVerifyClientCACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    peerCertVerifyType = ock::hcom::VERIFY_BY_CUSTOM_FUNC;
    return true;
}

bool ClientCreateDriverWithTlsCustomVerify()
{
    if (tlsClientCustomVerifyTlsDriver != nullptr) {
        NN_LOG_ERROR("tlsClientCustomVerifyTlsDriver already created");
        return false;
    }

    tlsClientCustomVerifyTlsDriver =
        UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rdmaTlsClientCustomVerifyDriver", false);
    if (tlsClientCustomVerifyTlsDriver == nullptr) {
        NN_LOG_ERROR("failed to create tlsClientCustomVerifyTlsDriver already created");
        return false;
    }

    setClientDriverCallback(tlsClientCustomVerifyTlsDriver, &ClientCertCallback, &CustomVerifyClientCACallback,
        &ClientPrivateKeyCallback);

    tlsClientCustomVerifyTlsDriver->OobIpAndPort(BASE_IP, 9998);

    return driverInitAndStart(tlsClientCustomVerifyTlsDriver);
}

bool SyncClientConnectWithCustomVerify()
{
    if (tlsClientCustomVerifyTlsDriver == nullptr) {
        NN_LOG_ERROR("tlsClientCustomVerifyTlsDriver is null");
        return false;
    }

    int result = 0;
    if ((result = tlsClientCustomVerifyTlsDriver->Connect("hello world", tlsClientSyncEp, NET_EP_EVENT_POLLING)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    return true;
}

TestCaseRdmaTLS::TestCaseRdmaTLS() {}

void TestCaseRdmaTLS::SetUp()
{
    ASSERT_EQ(0, ValidateTlsCert());
    MOCKER(ReadRoCEVersionFromFile).stubs().will(returnValue(0));

    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING; // 只支持EVENT模式
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 1024;
    options.prePostReceiveSizePerQP = 32;
    options.enableTls = true;
    options.cipherSuite = ock::hcom::AES_GCM_256;
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestCaseRdmaTLS::TearDown()
{
    GlobalMockObject::verify();
}


TEST_F(TestCaseRdmaTLS, RDMATLSSuccess)
{
    bool result = ServerCreateDriverWithTls();
    EXPECT_EQ(true, result);

    result = ServerRegSglMemWithTls();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTls();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithTls();
    EXPECT_EQ(true, result);
    TlsSyncRequests();
    TlsSyncSendRawRequests();

    result = AsyncClientConnectWithTls();
    EXPECT_EQ(true, result);
    TlsAsyncRequest();
    TlsAsyncSendRawRequest();

    tlsClientDriver->Stop();
    tlsClientDriver->UnInitialize();

    tlsDriver->Stop();
    tlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientDriver->Name());
    UBSHcomNetDriver::DestroyInstance(tlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSCertPathInvalid)
{
    ASSERT_EQ(0, ValidateTlsCert());
    MOCKER(ReadRoCEVersionFromFile).stubs().will(returnValue(0));
    bool result = ServerCreateDriverWithInvalidTls();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTls();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithInvalidTls();
    EXPECT_EQ(false, result);

    tlsClientDriver->Stop();
    tlsClientDriver->UnInitialize();

    invalidTlsDriver->Stop();
    invalidTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientDriver->Name());
    UBSHcomNetDriver::DestroyInstance(invalidTlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSCertNotSameCAFailed)
{
    bool result = ServerCreateDriverTlsNotSameCACert();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTls();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithTls();
    EXPECT_EQ(false, result);

    tlsClientDriver->Stop();
    tlsClientDriver->UnInitialize();

    notSameCaTlsDriver->Stop();
    notSameCaTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientDriver->Name());
    UBSHcomNetDriver::DestroyInstance(notSameCaTlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSCertExpiredFailed)
{
    bool result = ServerCreateDriverTlsWithExpiredCert();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsExpiredCert();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithTlsCertExpired();
    EXPECT_EQ(false, result);

    tlsClientCertExpiredDriver->Stop();
    tlsClientCertExpiredDriver->UnInitialize();

    certExpiredTlsDriver->Stop();
    certExpiredTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientCertExpiredDriver->Name());
    UBSHcomNetDriver::DestroyInstance(certExpiredTlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSCertRevokedFailed)
{
    bool result = ServerCreateDriverTlsWithRevokedCert();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsRevokedCert();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithTlsCertRevoked();
    EXPECT_EQ(false, result);

    tlsClientCertRevokedDriver->Stop();
    tlsClientCertRevokedDriver->UnInitialize();

    certRevokedTlsDriver->Stop();
    certRevokedTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientCertRevokedDriver->Name());
    UBSHcomNetDriver::DestroyInstance(certRevokedTlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSVerifyByNoneInClientSuccess)
{
    bool result = ServerCreateDriverTlsWithCVerifyByNone();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsVerifyByNone();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithTlsVerifyByNone();
    EXPECT_EQ(true, result);

    tlsClientVerifyByNoneDriver->Stop();
    tlsClientVerifyByNoneDriver->UnInitialize();

    cVerifyByNoneTlsDriver->Stop();
    cVerifyByNoneTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientVerifyByNoneDriver->Name());
    UBSHcomNetDriver::DestroyInstance(cVerifyByNoneTlsDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSMultiLevelCertSuccess)
{
    bool result = ServerCreateDriverTlsWithMultiLevelCert();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsMultiLevelCert();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithMultiLevelCert();
    EXPECT_EQ(true, result);

    multiLevelCertTlsDriver->Stop();
    multiLevelCertTlsDriver->UnInitialize();

    tlsClientMultiLevelCertDriver->Stop();
    tlsClientMultiLevelCertDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(multiLevelCertTlsDriver->Name());
    UBSHcomNetDriver::DestroyInstance(tlsClientMultiLevelCertDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSAbnormalCertChainFailed)
{
    bool result = ServerCreateDriverTlsWithAbnormalCertChain();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsAbnormalCertChain();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithAbnormalCertChain();
    EXPECT_EQ(false, result);

    abnormalCertChainDriver->Stop();
    abnormalCertChainDriver->UnInitialize();

    tlsClientAbnormalCertChainDriver->Stop();
    tlsClientAbnormalCertChainDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(abnormalCertChainDriver->Name());
    UBSHcomNetDriver::DestroyInstance(tlsClientAbnormalCertChainDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSNormalCertChainSuccess)
{
    bool result = ServerCreateDriverTlsWithNormalCertChain();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsNormalCertChain();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithNormalCertChain();
    EXPECT_EQ(true, result);

    normalCertChainDriver->Stop();
    normalCertChainDriver->UnInitialize();

    tlsClientNormalCertChainDriver->Stop();
    tlsClientNormalCertChainDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(normalCertChainDriver->Name());
    UBSHcomNetDriver::DestroyInstance(tlsClientNormalCertChainDriver->Name());
}

TEST_F(TestCaseRdmaTLS, RDMATLSVerifyByCustomFuncSuccess)
{
    bool result = ServerCreateDriverTlsCustomVerify();
    EXPECT_EQ(true, result);

    result = ClientCreateDriverWithTlsCustomVerify();
    EXPECT_EQ(true, result);

    result = SyncClientConnectWithCustomVerify();
    EXPECT_EQ(true, result);

    tlsClientCustomVerifyTlsDriver->Stop();
    tlsClientCustomVerifyTlsDriver->UnInitialize();

    customVerifyTlsDriver->Stop();
    customVerifyTlsDriver->UnInitialize();

    UBSHcomNetDriver::DestroyInstance(tlsClientCustomVerifyTlsDriver->Name());
    UBSHcomNetDriver::DestroyInstance(customVerifyTlsDriver->Name());
}
#endif