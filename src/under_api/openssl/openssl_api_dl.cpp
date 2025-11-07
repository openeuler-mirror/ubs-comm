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
#include <dlfcn.h>
#include <unistd.h>

#include "hcom_utils.h"
#include "openssl_api_dl.h"

#define DLSYM(handle, type, ptr, sym)                 \
    do {                                              \
        auto ptr1 = dlsym((handle), (sym));           \
        if (ptr1 == nullptr) {                        \
            NN_LOG_ERROR("Failed to load " << (sym)); \
            dlclose(handle);                          \
            return -1;                                \
        }                                             \
        (ptr) = (type)ptr1;                           \
    } while (0)

/** @brief Adapt to different OpenSSL versions. */
#define DLSYM_UPDATE(handle, type, ptr, sym1, sym2)        \
    do {                                                   \
        auto ptr1 = dlsym((handle), (sym1));               \
        if (ptr1 == nullptr) {                             \
            ptr1 = dlsym((handle), (sym2));                \
            if (ptr1 == nullptr) {                         \
                NN_LOG_ERROR("Failed to load " << (sym1)); \
                dlclose(handle);                           \
                return -1;                                 \
            }                                              \
        }                                                  \
        (ptr) = (type)ptr1;                                \
    } while (0)

namespace ock {
namespace hcom {
FuncInit SSLAPI::initSsl = nullptr;
FuncInit SSLAPI::initCypto = nullptr;
FuncOpensslCleanup SSLAPI::opensslCleanup = nullptr;

FuncGetMethod SSLAPI::tlsServerMethod = nullptr;
FuncGetMethod SSLAPI::tlsClientMethod = nullptr;
FuncSslOperation SSLAPI::sslShutdown = nullptr;
FuncSslFd SSLAPI::sslSetFd = nullptr;
FuncSslNew SSLAPI::sslNew = nullptr;
FuncSslFree SSLAPI::sslFree = nullptr;
FuncSslCtxNew SSLAPI::sslCtxNew = nullptr;
FuncSslCtxFree SSLAPI::sslCtxFree = nullptr;
FuncSslWrite SSLAPI::sslWrite = nullptr;
FuncSslRead SSLAPI::sslRead = nullptr;
FuncSslOperation SSLAPI::sslConnect = nullptr;
FuncSslOperation SSLAPI::sslAccept = nullptr;
FuncSslGetError SSLAPI::sslGetError = nullptr;

FuncSslCtxCtrl SSLAPI::sslCtxCtrl = nullptr;
FuncSslGetCurrentCipher SSLAPI::sslGetCurrentCipher = nullptr;
FuncSslGetVersion SSLAPI::sslGetVersion = nullptr;
FuncSetCipherSuites SSLAPI::setCipherSuites = nullptr;
FuncUsePrivKeyFile SSLAPI::usePrivKeyFile = nullptr;
FuncUseCertChainFile SSLAPI::useCertChainFile = nullptr;
FuncSslCtxSetVerify SSLAPI::sslCtxSetVerify = nullptr;
FuncSetDefaultPasswdCbUserdata SSLAPI::setDefaultPasswdCbUserdata = nullptr;
FuncSetCertVerifyCallback SSLAPI::setCertVerifyCallback = nullptr;
FuncLoadVerifyLocations SSLAPI::loadVerifyLocations = nullptr;
FuncCheckPrivateKey SSLAPI::checkPrivateKey = nullptr;
FuncSslGetVerifyResult SSLAPI::sslGetVerifyResult = nullptr;
FuncSslGetPeerCertificate SSLAPI::sslGetPeerCertificate = nullptr;
FuncSslCtxSetOptions SSLAPI::SslCtxSetOptions = nullptr;
FuncSslCtxSetPskFindSessionCallback SSLAPI::SslCtxSetPskFindSessionCallback = nullptr;
FuncSslCtxSetPskUseSessionCallback SSLAPI::SslCtxSetPskUseSessionCallback = nullptr;

FuncSslSessionNew SSLAPI::SslSessionNew = nullptr;
FuncSslSessionSet1MasterKey SSLAPI::SslSessionSet1MasterKey = nullptr;
FuncSslSessionSetProtocolVersion SSLAPI::SslSessionSetProtocolVersion = nullptr;
FuncSslSessionSetCipher SSLAPI::SslSessionSetCipher = nullptr;
FuncSslCipherFind SSLAPI::SslCipherFind = nullptr;

FuncEvpAesCipher SSLAPI::evpAes128Gcm = nullptr;
FuncEvpAesCipher SSLAPI::evpAes256Gcm = nullptr;
FuncEvpAesCipher SSLAPI::evpAes128Ccm = nullptr;
FuncEvpAesCipher SSLAPI::evpChacha20Poly1305 = nullptr;

FuncEvpCipherCtxNew SSLAPI::evpCipherCtxNew = nullptr;
FuncEvpCipherCtxFree SSLAPI::evpCipherCtxFree = nullptr;
FuncEvpCipherCtxCtrl SSLAPI::evpCipherCtxCtrl = nullptr;

FuncEvpEncryptInitEx SSLAPI::evpEncryptInitEx = nullptr;
FuncEvpEncryptUpdate SSLAPI::evpEncryptUpdate = nullptr;
FuncEvpEncryptFinalEx SSLAPI::evpEncryptFinalEx = nullptr;
FuncEvpDecryptInitEx SSLAPI::evpDecryptInitEx = nullptr;
FuncEvpDecryptUpdate SSLAPI::evpDecryptUpdate = nullptr;
FuncEvpDecryptFinalEx SSLAPI::evpDecryptFinalEx = nullptr;

FuncRandPoll SSLAPI::randPoll = nullptr;
FuncRandStatus SSLAPI::randStatus = nullptr;
FuncRandBytes SSLAPI::randBytes = nullptr;
FuncRandBytes SSLAPI::randPrivBytes = nullptr;
FuncRandSeed SSLAPI::randSeed = nullptr;

FuncX509VerifyCert SSLAPI::x509VerifyCert = nullptr;
FuncX509VerifyCertErrorString SSLAPI::x509VerifyCertErrorString = nullptr;
FuncX509StoreCtxGetError SSLAPI::x509StoreCtxGetError = nullptr;
FuncPemReadBioX509Crl SSLAPI::pemReadBioX509Crl = nullptr;
FuncBioSFile SSLAPI::bioSFile = nullptr;
FuncBioNew SSLAPI::bioNew = nullptr;
FuncBioFree SSLAPI::bioFree = nullptr;
FuncBioCtrl SSLAPI::bioCtrl = nullptr;
FuncX509StoreCtxGet0Store SSLAPI::x509StoreCtxGet0Store = nullptr;
FuncX509StoreCtxSetFlags SSLAPI::x509StoreCtxSetFlags = nullptr;
FuncX509StoreAddCrl SSLAPI::x509StoreAddCrl = nullptr;
FuncX509CrlFree SSLAPI::x509CrlFree = nullptr;

bool SSLAPI::gLoaded = false;
const char *SSLAPI::gOpensslEnvPath = "HCOM_OPENSSL_PATH";
const char *SSLAPI::gOpensslLibSslName = "libssl.so";
const char *SSLAPI::gOpensslLibCryptoName = "libcrypto.so";
const char *SSLAPI::gSep = "/";

int SSLAPI::GetLibPath(std::string &libSslPath, std::string &libCryptoPath)
{
    char *envPath = ::getenv(gOpensslEnvPath);
    if (envPath == nullptr) {
        libSslPath = gOpensslLibSslName;
        libCryptoPath = gOpensslLibCryptoName;
        return 0;
    }

    std::string opensslPath = envPath;
    if (!CanonicalPath(opensslPath)) {
        NN_LOG_ERROR("env set for openssl is invalid " << gOpensslEnvPath);
        return -1;
    }

    libCryptoPath = opensslPath + gSep + gOpensslLibCryptoName;
    if (::access(libCryptoPath.c_str(), F_OK) != 0) {
        NN_LOG_ERROR("libcrypto.so path set in env is invalid");
        return -1;
    }

    libSslPath = opensslPath + gSep + gOpensslLibSslName;
    if (::access(libSslPath.c_str(), F_OK) != 0) {
        NN_LOG_ERROR("libssl.so path set in env is invalid");
        return -1;
    }
    return 0;
}

int SSLAPI::LoadSSLSymbols(void *sslHandle)
{
    DLSYM(sslHandle, FuncInit, initSsl, "OPENSSL_init_ssl");
    DLSYM(sslHandle, FuncInit, initCypto, "OPENSSL_init_crypto");
    DLSYM(sslHandle, FuncOpensslCleanup, opensslCleanup, "OPENSSL_cleanup");
    DLSYM(sslHandle, FuncGetMethod, tlsServerMethod, "TLS_server_method");
    DLSYM(sslHandle, FuncGetMethod, tlsClientMethod, "TLS_client_method");
    DLSYM(sslHandle, FuncSslOperation, sslShutdown, "SSL_shutdown");
    DLSYM(sslHandle, FuncSslFd, sslSetFd, "SSL_set_fd");
    DLSYM(sslHandle, FuncSslNew, sslNew, "SSL_new");
    DLSYM(sslHandle, FuncSslFree, sslFree, "SSL_free");
    DLSYM(sslHandle, FuncSslCtxNew, sslCtxNew, "SSL_CTX_new");
    DLSYM(sslHandle, FuncSslCtxFree, sslCtxFree, "SSL_CTX_free");
    DLSYM(sslHandle, FuncSslWrite, sslWrite, "SSL_write");
    DLSYM(sslHandle, FuncSslRead, sslRead, "SSL_read");
    DLSYM(sslHandle, FuncSslOperation, sslConnect, "SSL_connect");
    DLSYM(sslHandle, FuncSslOperation, sslAccept, "SSL_accept");
    DLSYM(sslHandle, FuncSslGetError, sslGetError, "SSL_get_error");
    DLSYM(sslHandle, FuncSetCipherSuites, setCipherSuites, "SSL_CTX_set_ciphersuites");
    DLSYM(sslHandle, FuncSslCtxCtrl, sslCtxCtrl, "SSL_CTX_ctrl");
    DLSYM(sslHandle, FuncSslGetCurrentCipher, sslGetCurrentCipher, "SSL_get_current_cipher");
    DLSYM(sslHandle, FuncSslGetVersion, sslGetVersion, "SSL_get_version");
    DLSYM(sslHandle, FuncUsePrivKeyFile, usePrivKeyFile, "SSL_CTX_use_PrivateKey_file");
    DLSYM(sslHandle, FuncUseCertChainFile, useCertChainFile, "SSL_CTX_use_certificate_chain_file");
    DLSYM(sslHandle, FuncSslCtxSetVerify, sslCtxSetVerify, "SSL_CTX_set_verify");
    DLSYM(sslHandle, FuncSetDefaultPasswdCbUserdata, setDefaultPasswdCbUserdata,
        "SSL_CTX_set_default_passwd_cb_userdata");
    DLSYM(sslHandle, FuncSetCertVerifyCallback, setCertVerifyCallback, "SSL_CTX_set_cert_verify_callback");
    DLSYM(sslHandle, FuncLoadVerifyLocations, loadVerifyLocations, "SSL_CTX_load_verify_locations");
    DLSYM(sslHandle, FuncCheckPrivateKey, checkPrivateKey, "SSL_CTX_check_private_key");
    DLSYM(sslHandle, FuncSslGetVerifyResult, sslGetVerifyResult, "SSL_get_verify_result");
    DLSYM_UPDATE(sslHandle, FuncSslGetPeerCertificate, sslGetPeerCertificate, "SSL_get_peer_certificate",
        "SSL_get1_peer_certificate");
    DLSYM(sslHandle, FuncSslCtxSetOptions, SslCtxSetOptions, "SSL_CTX_set_options");
    DLSYM(sslHandle, FuncSslCtxSetPskFindSessionCallback, SslCtxSetPskFindSessionCallback,
        "SSL_CTX_set_psk_find_session_callback");
    DLSYM(sslHandle, FuncSslCtxSetPskUseSessionCallback, SslCtxSetPskUseSessionCallback,
        "SSL_CTX_set_psk_use_session_callback");
    DLSYM(sslHandle, FuncSslSessionNew, SslSessionNew, "SSL_SESSION_new");
    DLSYM(sslHandle, FuncSslSessionSet1MasterKey, SslSessionSet1MasterKey, "SSL_SESSION_set1_master_key");
    DLSYM(sslHandle, FuncSslSessionSetProtocolVersion, SslSessionSetProtocolVersion,
        "SSL_SESSION_set_protocol_version");
    DLSYM(sslHandle, FuncSslSessionSetCipher, SslSessionSetCipher, "SSL_SESSION_set_cipher");
    DLSYM(sslHandle, FuncSslCipherFind, SslCipherFind, "SSL_CIPHER_find");
    return 0;
}

int SSLAPI::LoadCryptoSymbols(void *cryptoHandle)
{
    DLSYM(cryptoHandle, FuncEvpCipherCtxNew, evpCipherCtxNew, "EVP_CIPHER_CTX_new");
    DLSYM(cryptoHandle, FuncEvpCipherCtxFree, evpCipherCtxFree, "EVP_CIPHER_CTX_free");
    DLSYM(cryptoHandle, FuncEvpCipherCtxCtrl, evpCipherCtxCtrl, "EVP_CIPHER_CTX_ctrl");
    DLSYM(cryptoHandle, FuncEvpEncryptInitEx, evpEncryptInitEx, "EVP_EncryptInit_ex");
    DLSYM(cryptoHandle, FuncEvpEncryptUpdate, evpEncryptUpdate, "EVP_EncryptUpdate");
    DLSYM(cryptoHandle, FuncEvpEncryptFinalEx, evpEncryptFinalEx, "EVP_EncryptFinal_ex");
    DLSYM(cryptoHandle, FuncEvpDecryptInitEx, evpDecryptInitEx, "EVP_DecryptInit_ex");
    DLSYM(cryptoHandle, FuncEvpDecryptUpdate, evpDecryptUpdate, "EVP_DecryptUpdate");
    DLSYM(cryptoHandle, FuncEvpDecryptFinalEx, evpDecryptFinalEx, "EVP_DecryptFinal_ex");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes128Gcm, "EVP_aes_128_gcm");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes256Gcm, "EVP_aes_256_gcm");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes128Ccm, "EVP_aes_128_ccm");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpChacha20Poly1305, "EVP_chacha20_poly1305");

    DLSYM(cryptoHandle, FuncRandPoll, randPoll, "RAND_poll");
    DLSYM(cryptoHandle, FuncRandStatus, randStatus, "RAND_status");
    DLSYM(cryptoHandle, FuncRandBytes, randBytes, "RAND_bytes");
    DLSYM(cryptoHandle, FuncRandBytes, randPrivBytes, "RAND_priv_bytes");
    DLSYM(cryptoHandle, FuncRandSeed, randSeed, "RAND_seed");

    DLSYM(cryptoHandle, FuncX509VerifyCert, x509VerifyCert, "X509_verify_cert");
    DLSYM(cryptoHandle, FuncX509VerifyCertErrorString, x509VerifyCertErrorString, "X509_verify_cert_error_string");
    DLSYM(cryptoHandle, FuncX509StoreCtxGetError, x509StoreCtxGetError, "X509_STORE_CTX_get_error");
    DLSYM(cryptoHandle, FuncPemReadBioX509Crl, pemReadBioX509Crl, "PEM_read_bio_X509_CRL");
    DLSYM(cryptoHandle, FuncBioSFile, bioSFile, "BIO_s_file");
    DLSYM(cryptoHandle, FuncBioNew, bioNew, "BIO_new");
    DLSYM(cryptoHandle, FuncBioFree, bioFree, "BIO_free");
    DLSYM(cryptoHandle, FuncBioCtrl, bioCtrl, "BIO_ctrl");
    DLSYM(cryptoHandle, FuncX509StoreCtxGet0Store, x509StoreCtxGet0Store, "X509_STORE_CTX_get0_store");
    DLSYM(cryptoHandle, FuncX509StoreCtxSetFlags, x509StoreCtxSetFlags, "X509_STORE_CTX_set_flags");
    DLSYM(cryptoHandle, FuncX509StoreAddCrl, x509StoreAddCrl, "X509_STORE_add_crl");
    DLSYM(cryptoHandle, FuncX509CrlFree, x509CrlFree, "X509_CRL_free");
    return 0;
}

int SSLAPI::LoadOpensslAPI()
{
    NN_LOG_INFO("Starting to load openssl api");
    if (gLoaded) {
        return 0;
    }

    std::string libSslPath;
    std::string libCryptoPath;
    if (GetLibPath(libSslPath, libCryptoPath) != 0) {
        return -1;
    }

    auto sslHandle = dlopen(libSslPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (sslHandle == nullptr) {
        NN_LOG_ERROR("Failed to dlopen libssl.so err: " << dlerror());
        return -1;
    }

    if (LoadSSLSymbols(sslHandle) == -1) {
        dlclose(sslHandle);
        return -1;
    }

    auto cryptoHandle = dlopen(libCryptoPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (cryptoHandle == nullptr) {
        NN_LOG_ERROR("Failed to dlopen libcrypto.so err: " << dlerror());
        dlclose(sslHandle);
        return -1;
    }

    if (LoadCryptoSymbols(cryptoHandle) == -1) {
        dlclose(sslHandle);
        dlclose(cryptoHandle);
        return -1;
    }
    gLoaded = true;
    return 0;
}
}
}