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
#include <cstdlib>
#include <dlfcn.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/limits.h>

#include "openssl_api_wrapper.h"
#include "net_oob_ssl.h"
#include "net_oob_openssl.h"

namespace ock {
namespace hcom {
#define OOB_SSL_LAYER_CHECK_RET(_condition, _msg) \
    do {                                          \
        if (_condition) {                         \
            NN_LOG_ERROR(_msg);                   \
            return NN_OOB_SSL_INIT_ERROR;         \
        }                                         \
    } while (0)

#define OOB_SSL_LAYER_CHECK_RET_ERASE_RET(_cond, _msg) \
    do {                                               \
        if (_cond) {                                   \
            NN_LOG_ERROR(_msg);                        \
            if (erase) {                               \
                erase(keyPass, passLen);               \
            }                                          \
            return NN_OOB_SSL_INIT_ERROR;              \
        }                                              \
    } while (0)

UBSHcomPskFindSessionCb OOBOpenSSLConnection::mOpenSslPskFindSessionCb = nullptr;
UBSHcomPskUseSessionCb OOBOpenSSLConnection::mOpenSslPskUseSessionCb = nullptr;

OOBOpenSSLConnection::~OOBOpenSSLConnection()
{
    if (mSsl != nullptr) {
        HcomSsl::SslShutdown(mSsl);
        HcomSsl::SslFree(mSsl);
        mSsl = nullptr;
    }
    if (mSslCtx != nullptr) {
        HcomSsl::SslCtxFree(mSslCtx);
        mSslCtx = nullptr;
    }
}

SSL *OOBOpenSSLConnection::TransferSsl()
{
    auto tmp = mSsl;
    mSsl = nullptr;
    return tmp;
}

/* OOBOpenSSLConnection */
NResult OOBOpenSSLConnection::Send(void *buf, uint32_t size) const
{
    if (NN_UNLIKELY(buf == nullptr) || NN_UNLIKELY(size == 0) || NN_UNLIKELY(mSsl == nullptr) ||
        NN_UNLIKELY(size > INT_MAX)) {
        NN_LOG_ERROR("Invalid param for TLS send");
        return NN_PARAM_INVALID;
    }

    int len = static_cast<int>(size);
    while (len > 0) {
        int ret = HcomSsl::SslWrite(mSsl, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buf) + size - len), len);
        if (ret <= 0) {
            int sslErrCode = HcomSsl::SslGetError(mSsl, ret);
            NN_LOG_ERROR("Failed to write data to TLS channel, ret: " << ret << ", errno: " << sslErrCode <<
                " write Len: " << size);
            return NN_OOB_SSL_WRITE_ERROR;
        }
        len -= ret;
    }
    return NN_OK;
}

NResult OOBOpenSSLConnection::Receive(void *buf, uint32_t size) const
{
    if (NN_UNLIKELY(buf == nullptr) || NN_UNLIKELY(size == 0) || NN_UNLIKELY(mSsl == nullptr) ||
        NN_UNLIKELY(size > INT_MAX)) {
        NN_LOG_ERROR("Invalid param for TLS receive");
        return NN_PARAM_INVALID;
    }

    int len = static_cast<int>(size);
    while (len > 0) {
        int ret = HcomSsl::SslRead(mSsl, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buf) + size - len), len);
        if (ret <= 0) {
            int sslErrCode = HcomSsl::SslGetError(mSsl, ret);
            NN_LOG_ERROR("Failed to read data from TLS channel, ret: " << ret << ", errno: " << sslErrCode <<
                ", read Len: " << len);
            return NN_OOB_SSL_READ_ERROR;
        }
        len -= ret;
    }
    return NN_OK;
}

int OOBOpenSSLConnection::DefaultSslCertVerify(X509_STORE_CTX *x509ctx, const char *arg)
{
    auto crlPath = arg;
    const int checkSuccess = 1;
    const int checkFailed = -1;

    if (crlPath != nullptr && strlen(crlPath) != 0) {
        X509_CRL *crl = LoadCertRevokeListFile(crlPath);
        if (crl == nullptr) {
            NN_LOG_ERROR("Failed to load cert revocation list");
            return checkFailed;
        }
        X509_STORE *x509Store = HcomSsl::X509StoreCtxGet0Store(x509ctx);
        HcomSsl::X509StoreCtxSetFlags(x509ctx, (unsigned long)HcomSsl::X509_V_FLAG_CRL_CHECK);
        auto result = HcomSsl::X509StoreAddCrl(x509Store, crl);
        if (result != NN_NO1) {
            NN_LOG_ERROR("Store add crl failed ret:" << result);
            HcomSsl::X509CrlFree(crl);
            return checkFailed;
        }
        HcomSsl::X509CrlFree(crl);
    }

    auto verifyResult = HcomSsl::X509VerifyCert(x509ctx);
    if (verifyResult != NN_NO1) {
        NN_LOG_ERROR("Verify failed in callback"
            << " error: " << HcomSsl::X509VerifyCertErrorString(HcomSsl::X509StoreCtxGetError(x509ctx)));
        return checkFailed;
    }
    return checkSuccess;
}

int OOBOpenSSLConnection::CaCallbackWrapper(X509_STORE_CTX *x509ctx, void *arg)
{
    if (x509ctx == nullptr || arg == nullptr) {
        return 0;
    }
    const int checkSuccess = 1;
    const int checkFailed = -1;

    auto conn = reinterpret_cast<OOBOpenSSLConnection *>(arg);
    int ret = -1;
    if (conn->mPeerCertVerifyType == VERIFY_BY_CUSTOM_FUNC) {
        if (conn->mCertVerifyCallback == nullptr) {
            NN_LOG_ERROR("Cert verification failed for cert verify in callback is null.");
            return checkFailed;
        }
        ret = conn->mCertVerifyCallback(x509ctx, conn->mCrlPath.c_str());
    } else {
        ret = conn->DefaultSslCertVerify(x509ctx, conn->mCrlPath.c_str());
    }
    if (ret < 0) {
        NN_LOG_ERROR("Cert verification failed, please check or set valid cert.");
        return checkFailed;
    }
    return checkSuccess;
}

int OOBOpenSSLConnection::PskFindCallbackWrapper(SSL *ssl, const unsigned char *identity, size_t identityLen,
    SSL_SESSION **sess)
{
    return mOpenSslPskFindSessionCb(ssl, identity, identityLen, reinterpret_cast<void **>(sess));
}

int OOBOpenSSLConnection::PskUseCallbackWrapper(SSL *ssl, const EVP_MD *md, const unsigned char **id, size_t *idlen,
    SSL_SESSION **sess)
{
    return mOpenSslPskUseSessionCb(ssl, md, id, idlen, reinterpret_cast<void **>(sess));
}

NResult OOBOpenSSLConnection::SetPSKCallback(bool isServer)
{
    if (isServer) {
        if (mPskFindSessionCb == nullptr) {
            NN_LOG_WARN("Callback for psk find session is not set at server");
            return NN_OK;
        }
        mOpenSslPskFindSessionCb = mPskFindSessionCb;
        HcomSsl::SslCtxSetPskFindSessionCallback(mSslCtx, &PskFindCallbackWrapper);
    } else {
        if (mPskUseSessionCb == nullptr) {
            NN_LOG_WARN("Callback for psk use session is not set at client");
            return NN_OK;
        }
        mOpenSslPskUseSessionCb = mPskUseSessionCb;
        HcomSsl::SslCtxSetPskUseSessionCallback(mSslCtx, &PskUseCallbackWrapper);
    }
    return NN_OK;
}

NResult OOBOpenSSLConnection::InitSSL(bool isServer)
{
    /* SSL_library_init() */
    auto ret = HcomSsl::OpensslInitSsl(0, nullptr);
    OOB_SSL_LAYER_CHECK_RET((ret <= 0), "Failed to load openssl library");

    /* SSL_load_error_strings() */
    ret = HcomSsl::OpensslInitSsl(HcomSsl::OPENSSL_INIT_LOAD_SSL_STRINGS | HcomSsl::OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
        nullptr);
    OOB_SSL_LAYER_CHECK_RET((ret <= 0), "Failed to initialize openssl library");

    if (isServer) {
        OOB_SSL_LAYER_CHECK_RET((mCertCallback == nullptr || mKeyCallback == nullptr) && mPskFindSessionCb == nullptr,
            "Both callback for cert and callback for find psk is not set at server");
        mSslCtx = HcomSsl::SslCtxNew(HcomSsl::TlsServerMethod());
    } else {
        OOB_SSL_LAYER_CHECK_RET(mCaCallback == nullptr && mPskUseSessionCb == nullptr,
            "Both callback for cert and callback for find psk is not set at client");
        mSslCtx = HcomSsl::SslCtxNew(HcomSsl::TlsClientMethod());
    }
    OOB_SSL_LAYER_CHECK_RET(mSslCtx == nullptr, "SslCtxNew() failed");

    HcomSsl::SslCtxCtrl(mSslCtx, HcomSsl::SSL_CTRL_SET_MAX_PROTO_VERSION, GetTLSVersion(), nullptr);
    if (GetTLSVersion() == TLS_1_2) {
        ret = HcomSsl::SslCtxSetOption(mSslCtx, HcomSsl::SSL_NO_TLS1_2_RENEGOTIATION);
        OOB_SSL_LAYER_CHECK_RET(ret <= 0, "Failed to set renegotiation");
    }

    switch (GetCipherSuite()) {
        case AES_GCM_128:
            ret = HcomSsl::SslCtxSetCipherSuites(mSslCtx, "TLS_AES_128_GCM_SHA256");
            break;
        case AES_GCM_256:
            ret = HcomSsl::SslCtxSetCipherSuites(mSslCtx, "TLS_AES_256_GCM_SHA384");
            break;
        case AES_CCM_128:
            ret = HcomSsl::SslCtxSetCipherSuites(mSslCtx, "TLS_AES_128_CCM_SHA256");
            break;
        case CHACHA20_POLY1305:
            ret = HcomSsl::SslCtxSetCipherSuites(mSslCtx, "TLS_CHACHA20_POLY1305_SHA256");
            break;
        default:
            ret = NN_NO0;
    }
    OOB_SSL_LAYER_CHECK_RET(ret <= 0, "Failed to set cipher suites to TLS context");

    NN_LOG_INFO("test : commload");
    bool success = (CommLoad(isServer) == NN_OK);
    if (isServer) {
        OOB_SSL_LAYER_CHECK_RET(!success, "Failed to initialize TLS context for encryption at server");
        NN_LOG_INFO("SSL Server accept one SSL client [" << HcomSsl::SslGetVersion(mSsl) << "]");
    } else {
        OOB_SSL_LAYER_CHECK_RET(!success, "Failed to initialize TLS context for encryption at client");
    }
    return NN_OK;
}

NResult OOBOpenSSLConnection::VerifyCA(bool isServer)
{
    std::string caPath;
    bool result = mCaCallback(mIpAndPort, caPath, mCrlPath, mPeerCertVerifyType, mCertVerifyCallback);
    OOB_SSL_LAYER_CHECK_RET(!result, "Failed to get CA cert, UBSHcomTLSCaCallback return false");

    if (mPeerCertVerifyType == VERIFY_BY_NONE && !isServer) {
        HcomSsl::SslCtxSetVerify(mSslCtx, HcomSsl::SSL_VERIFY_NONE, nullptr);
    } else {
        HcomSsl::SslCtxSetVerify(mSslCtx, HcomSsl::SSL_VERIFY_PEER | HcomSsl::SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        HcomSsl::SslCtxSetCertVerifyCallback(mSslCtx, &this->CaCallbackWrapper, this);
    }
    OOB_SSL_LAYER_CHECK_RET(caPath.empty(), "Failed to get valid CA cert path via callback, it is empty.");

    std::vector<std::string> caFileList;
    NetFunc::NN_SplitStr(caPath, ":", caFileList);

    for (auto &caFile : caFileList) {
        OOB_SSL_LAYER_CHECK_RET(!CanonicalPath(caFile), "Failed to get valid CA cert path via callback");
        auto ret = HcomSsl::SslCtxLoadVerifyLocations(mSslCtx, caFile.c_str(), nullptr);
        OOB_SSL_LAYER_CHECK_RET(ret <= 0, "TLS load verify file : " << caFile << "failed");
    }
    return NN_OK;
}

NResult OOBOpenSSLConnection::CommLoad(bool isServer)
{
    /* Check the peer's CA */
    if (mCaCallback != nullptr) {
        auto result = VerifyCA(isServer);
        if (NN_UNLIKELY(result != NN_OK)) {
            return result;
        }
    }

    /* Set private key and check */
    int passLen = 0;
    void *keyPass = nullptr;
    UBSHcomTLSEraseKeypass erase = nullptr;
    NN_LOG_INFO("test : 2");
    if (mCertCallback != nullptr && mKeyCallback != nullptr) {
        std::string certPath;
        bool result = mCertCallback(mIpAndPort, certPath);
        OOB_SSL_LAYER_CHECK_RET(!result, "TLS callback get CERT path failed");
        OOB_SSL_LAYER_CHECK_RET(!CanonicalPath(certPath), "get invalid cert path");
        NN_LOG_INFO("test : 3");

        std::string keyPath;
        result = mKeyCallback(mIpAndPort, keyPath, keyPass, passLen, erase);
        OOB_SSL_LAYER_CHECK_RET(!result, "TLS callback get private-key path failed");
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(!CanonicalPath(keyPath), "get invalid keyPath");
        NN_LOG_INFO("test : 4");
        /* load cert chain */
        auto ret = HcomSsl::SslCtxUseCertificateChainFile(mSslCtx, certPath.c_str());
        NN_LOG_INFO("test : 5");
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0, "TLS use certification file chain failed");
        HcomSsl::SslCtxSetDefaultPasswdCbUserdata(mSslCtx, keyPass);
        NN_LOG_INFO("test : 6");
        /* load private key */
        ret = HcomSsl::SslCtxUsePrivateKeyFile(mSslCtx, keyPath.c_str(), HcomSsl::SSL_FILETYPE_PEM);
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0, "TLS use private-key file failed");
        NN_LOG_INFO("test : 7");
        /* check private key */
        ret = HcomSsl::SslCtxCheckPrivateKey(mSslCtx);
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0, "TLS check private-key failed");
        NN_LOG_INFO("test : 8");
    }
    NN_LOG_INFO("test : 9");

    /* set psk callback */
    if (mPskFindSessionCb != nullptr || mPskUseSessionCb != nullptr) {
        NN_LOG_INFO("test : 10");
        auto ret = SetPSKCallback(isServer);
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret != NN_OK, "Failed to set psk callback");
    }

    NN_LOG_INFO("test : 11");
    mSsl = HcomSsl::SslNew(mSslCtx);
    OOB_SSL_LAYER_CHECK_RET_ERASE_RET(mSsl == nullptr, "Failed to new TLS, probably out of memory");

    NN_LOG_INFO("test : 12");
    auto ret = HcomSsl::SslSetFd(mSsl, mFD);
    OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0, "Failed to set fd to TLS, result " << ret);

    NN_LOG_INFO("test : 13");
    /* Server will accept and Client will connect */
    ret = isServer ? HcomSsl::SslAccept(mSsl) : HcomSsl::SslConnect(mSsl);
    if (isServer) {
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0,
            "TLS Failed to accept new TLS connection, result " << ret << " failed");
    } else {
        NN_LOG_INFO("test : 14");
        OOB_SSL_LAYER_CHECK_RET_ERASE_RET(ret <= 0, "TLS Failed to connect to TLS server, result " << ret << " failed");
    }
    NN_LOG_INFO("test : 15");

    if (erase != nullptr) {
        NN_LOG_INFO("test : 16");
        erase(keyPass, passLen);
    }
    return NN_OK;
}

X509_CRL *OOBOpenSSLConnection::LoadCertRevokeListFile(const char *crlFile)
{
    // check whether file is exist
    char *realCrlPath = realpath(crlFile, nullptr);
    if (realCrlPath == nullptr) {
        return nullptr;
    }

    // load crl file
    BIO *in = HcomSsl::BioNew(HcomSsl::BioSFile());
    if (in == nullptr) {
        free(realCrlPath);
        realCrlPath = nullptr;
        return nullptr;
    }

    int result = HcomSsl::BioCtrl(in, HcomSsl::BIO_C_SET_FILENAME, HcomSsl::BIO_CLOSE | HcomSsl::BIO_FP_READ,
        const_cast<char *>(realCrlPath));
    if (result <= 0) {
        (void)HcomSsl::BioFree(in);
        free(realCrlPath);
        realCrlPath = nullptr;
        return nullptr;
    }

    X509_CRL *crl = HcomSsl::PemReadBioX509Crl(in, nullptr, nullptr, nullptr);
    if (crl == nullptr) {
        (void)HcomSsl::BioFree(in);
        free(realCrlPath);
        realCrlPath = nullptr;
        return nullptr;
    }

    (void)HcomSsl::BioFree(in);
    free(realCrlPath);
    realCrlPath = nullptr;
    return crl;
}
}
}
