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

#ifndef HCOM_OPENSSL_API_WRAPPER_H
#define HCOM_OPENSSL_API_WRAPPER_H

#include "openssl_api_dl.h"

namespace ock {
namespace hcom {
class HcomSsl {
public:
    static const uint32_t SSL_VERIFY_NONE = NN_NO0;
    static const uint32_t SSL_VERIFY_PEER = NN_NO1;
    static const uint32_t SSL_VERIFY_FAIL_IF_NO_PEER_CERT = NN_NO2;
    static const uint32_t SSL_FILETYPE_PEM = NN_NO1;
    static const uint32_t EVP_CTRL_AEAD_SET_IVLEN = NN_NO9;
    static const uint32_t EVP_CTRL_AEAD_GET_TAG = NN_NO16;
    static const uint32_t EVP_CTRL_AEAD_SET_TAG = NN_NO17;
    static const uint32_t OPENSSL_INIT_LOAD_SSL_STRINGS = NN_NO2097152;
    static const uint32_t OPENSSL_INIT_LOAD_CRYPTO_STRINGS = NN_NO2;
    static const uint32_t SSL_CTRL_SET_MIN_PROTO_VERSION = NN_NO123;
    static const uint32_t SSL_CTRL_SET_MAX_PROTO_VERSION = NN_NO124;
    static const uint32_t SSL_ERROR_WANT_READ = NN_NO2;
    static const uint32_t SSL_ERROR_WANT_WRITE = NN_NO3;
    static const uint32_t SSL_NO_TLS1_2_RENEGOTIATION = NN_NO262144;

    static const uint32_t BIO_C_SET_FILENAME = NN_NO108;
    static const uint32_t BIO_CLOSE = NN_NO1;
    static const uint32_t BIO_FP_READ = NN_NO2;
    static const uint32_t X509_V_FLAG_CRL_CHECK = NN_NO4;

    static int OpensslInitSsl(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
    {
        return SSLAPI::initSsl(opts, settings);
    }

    static inline int OpensslInitCrypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
    {
        return SSLAPI::initCypto(opts, settings);
    }

    static inline const SSL_METHOD *TlsClientMethod()
    {
        return SSLAPI::tlsClientMethod();
    }

    static inline const SSL_METHOD *TlsServerMethod()
    {
        return SSLAPI::tlsServerMethod();
    }

    static inline int SslShutdown(SSL *s)
    {
        return SSLAPI::sslShutdown(s);
    }

    static inline int SslSetFd(SSL *s, int fd)
    {
        return SSLAPI::sslSetFd(s, fd);
    }

    static inline SSL *SslNew(SSL_CTX *ctx)
    {
        return SSLAPI::sslNew(ctx);
    }

    static inline void SslFree(SSL *s)
    {
        SSLAPI::sslFree(s);
    }

    static SSL_CTX *SslCtxNew(const SSL_METHOD *method)
    {
        return SSLAPI::sslCtxNew(method);
    }

    static inline void SslCtxFree(SSL_CTX *ctx)
    {
        SSLAPI::sslCtxFree(ctx);
    }

    static inline int SslWrite(SSL *s, const void *buf, int num)
    {
        return SSLAPI::sslWrite(s, buf, num);
    }

    static inline int SslRead(SSL *s, void *buf, int num)
    {
        return SSLAPI::sslRead(s, buf, num);
    }

    static inline int SslConnect(SSL *s)
    {
        return SSLAPI::sslConnect(s);
    }

    static inline int SslAccept(SSL *s)
    {
        return SSLAPI::sslAccept(s);
    }

    static inline int SslGetError(const SSL *s, int retCode)
    {
        return SSLAPI::sslGetError(s, retCode);
    }

    static inline int SslCtxSetCipherSuites(SSL_CTX *ctx, const char *str)
    {
        return SSLAPI::setCipherSuites(ctx, str);
    }

    static inline long SslCtxCtrl(SSL_CTX *ctx, int cmd, long larg, void *parg)
    {
        return SSLAPI::sslCtxCtrl(ctx, cmd, larg, parg);
    }

    static inline const char *SslGetVersion(const SSL *ssl)
    {
        return SSLAPI::sslGetVersion(ssl);
    }

    static inline void SslCtxSetVerify(SSL_CTX *ctx, int mode, int (*cb)(int, X509_STORE_CTX *))
    {
        SSLAPI::sslCtxSetVerify(ctx, mode, cb);
    }

    static inline int SslCtxUsePrivateKeyFile(SSL_CTX *ctx, const char *file, int type)
    {
        return SSLAPI::usePrivKeyFile(ctx, file, type);
    }

    static inline int SslCtxUseCertificateChainFile(SSL_CTX *ctx, const char *file)
    {
        return SSLAPI::useCertChainFile(ctx, file);
    }

    static inline void SslCtxSetDefaultPasswdCbUserdata(SSL_CTX *ctx, void *u)
    {
        SSLAPI::setDefaultPasswdCbUserdata(ctx, u);
    }

    static inline void SslCtxSetCertVerifyCallback(SSL_CTX *ctx, int (*cb)(X509_STORE_CTX *, void *), void *arg)
    {
        SSLAPI::setCertVerifyCallback(ctx, cb, arg);
    }

    static inline int SslCtxLoadVerifyLocations(SSL_CTX *ctx, const char *cafile, const char *capath)
    {
        return SSLAPI::loadVerifyLocations(ctx, cafile, capath);
    }

    static inline int SslCtxCheckPrivateKey(const SSL_CTX *ctx)
    {
        return SSLAPI::checkPrivateKey(ctx);
    }

    static inline void SslCtxSetPskFindSessionCallback(SSL_CTX *ctx, SSL_psk_find_session_cb_func cb)
    {
        SSLAPI::SslCtxSetPskFindSessionCallback(ctx, cb);
    }

    static inline void SslCtxSetPskUseSessionCallback(SSL_CTX *ctx, SSL_psk_use_session_cb_func cb)
    {
        SSLAPI::SslCtxSetPskUseSessionCallback(ctx, cb);
    }

    static inline SSL_SESSION *SslSessionNew()
    {
        return SSLAPI::SslSessionNew();
    }

    static inline int SslSessionSet1MasterKey(SSL_SESSION *sess, const unsigned char *in, size_t len)
    {
        return SSLAPI::SslSessionSet1MasterKey(sess, in, len);
    }

    static inline int SslSessionSetProtocolVersion(SSL_SESSION *sess, int version)
    {
        return SSLAPI::SslSessionSetProtocolVersion(sess, version);
    }

    static inline int SslSessionSetCipher(SSL_SESSION *sess, const SSL_CIPHER *cipher)
    {
        return SSLAPI::SslSessionSetCipher(sess, cipher);
    }

    static inline const SSL_CIPHER *SslCipherFind(SSL *ssl, const unsigned char *ptr)
    {
        return SSLAPI::SslCipherFind(ssl, ptr);
    }

    static inline X509 *SslGetPeerCertificate(const SSL *ssl)
    {
        return SSLAPI::sslGetPeerCertificate(ssl);
    }

    static inline long SslGetVerifyResult(const SSL *ssl)
    {
        return SSLAPI::sslGetVerifyResult(ssl);
    }

    static inline int SslCtxSetOption(const SSL_CTX *ctx, int options)
    {
        return SSLAPI::SslCtxSetOptions(ctx, options);
    }

    static inline const EVP_CIPHER *EvpAes128Gcm()
    {
        return SSLAPI::evpAes128Gcm();
    }

    static inline const EVP_CIPHER *EvpAes256Gcm()
    {
        return SSLAPI::evpAes256Gcm();
    }

    static inline const EVP_CIPHER *EvpAes128Ccm()
    {
        return SSLAPI::evpAes128Ccm();
    }

    static inline const EVP_CIPHER *EvpChacha20Poly1305()
    {
        return SSLAPI::evpChacha20Poly1305();
    }

    static inline EVP_CIPHER_CTX *EvpCipherCtxNew()
    {
        return SSLAPI::evpCipherCtxNew();
    }

    static inline void EvpCipherCtxFree(EVP_CIPHER_CTX *ctx)
    {
        SSLAPI::evpCipherCtxFree(ctx);
    }

    static inline int EvpCipherCtxCtrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
    {
        return SSLAPI::evpCipherCtxCtrl(ctx, type, arg, ptr);
    }

    static inline int EvpEncryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
        const unsigned char *key, const unsigned char *iv)
    {
        return SSLAPI::evpEncryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpEncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
        int inl)
    {
        return SSLAPI::evpEncryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpEncryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        return SSLAPI::evpEncryptFinalEx(ctx, out, outl);
    }

    static inline int EvpDecryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
        const unsigned char *key, const unsigned char *iv)
    {
        return SSLAPI::evpDecryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpDecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
        int inl)
    {
        return SSLAPI::evpDecryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpDecryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        return SSLAPI::evpDecryptFinalEx(ctx, out, outl);
    }

    static inline int RandPoll()
    {
        return SSLAPI::randPoll();
    }

    static inline int RandStatus()
    {
        return SSLAPI::randStatus();
    }

    static inline int RandPrivBytes(unsigned char *buf, int num)
    {
        return SSLAPI::randPrivBytes(buf, num);
    }

    static inline int X509VerifyCert(X509_STORE_CTX *ctx)
    {
        return SSLAPI::x509VerifyCert(ctx);
    }

    static inline const char *X509VerifyCertErrorString(long n)
    {
        return SSLAPI::x509VerifyCertErrorString(n);
    }

    static inline int X509StoreCtxGetError(X509_STORE_CTX *ctx)
    {
        return SSLAPI::x509StoreCtxGetError(ctx);
    }

    static inline X509_CRL *PemReadBioX509Crl(BIO *bp, X509_CRL **x, PEM_PASSWORD_CB *cb, void *u)
    {
        return SSLAPI::pemReadBioX509Crl(bp, x, cb, u);
    }

    static inline const BIO_METHOD *BioSFile(void)
    {
        return SSLAPI::bioSFile();
    }

    static inline BIO *BioNew(const BIO_METHOD *bioMethod)
    {
        return SSLAPI::bioNew(bioMethod);
    }

    static inline int BioCtrl(BIO *bp, int cmd, long larg, void *parg)
    {
        return SSLAPI::bioCtrl(bp, cmd, larg, parg);
    }

    static inline void BioFree(BIO *b)
    {
        return SSLAPI::bioFree(b);
    }

    static inline X509_STORE *X509StoreCtxGet0Store(const X509_STORE_CTX *ctx)
    {
        return SSLAPI::x509StoreCtxGet0Store(ctx);
    }
    static inline void X509StoreCtxSetFlags(X509_STORE_CTX *ctx, unsigned long flags)
    {
        return SSLAPI::x509StoreCtxSetFlags(ctx, flags);
    }
    static inline int X509StoreAddCrl(X509_STORE *xs, X509_CRL *x)
    {
        return SSLAPI::x509StoreAddCrl(xs, x);
    }
    static inline void X509CrlFree(X509_CRL *x)
    {
        return SSLAPI::x509CrlFree(x);
    }

    static inline int Load()
    {
        return SSLAPI::LoadOpensslAPI();
    }

    static inline void UnLoad() {}
};
}
}
#endif // HCOM_OPENSSL_API_WRAPPER_H
