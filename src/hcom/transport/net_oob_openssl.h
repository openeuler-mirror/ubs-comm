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
#ifndef OCK_HCOM_OOB_OPENSSL_12334324233_H
#define OCK_HCOM_OOB_OPENSSL_12334324233_H

#include <netinet/tcp.h>
#include <sys/socket.h>

#include "net_oob_ssl.h"
#include "net_security_rand.h"
#include "net_util.h"
#include "openssl_api_wrapper.h"
#include "rdma_verbs_wrapper_qp.h"

namespace ock {
namespace hcom {
class OOBOpenSSLConnection : public OOBSSLConnection {
public:
    explicit OOBOpenSSLConnection(int fd) : OOBSSLConnection(fd) {}
    ~OOBOpenSSLConnection() override;

    NResult Send(void *buf, uint32_t size) const override;
    NResult Receive(void *buf, uint32_t size) const override;

    NResult InitSSL(bool server) override;

    SSL *TransferSsl();

private:
    NResult CommLoad(bool server) override;
    NResult VerifyCA(bool server) override;

    static int CaCallbackWrapper(X509_STORE_CTX *ctx, void *arg);
    static X509_CRL *LoadCertRevokeListFile(const char *crlFile);
    static int DefaultSslCertVerify(X509_STORE_CTX *x509ctx, const char *arg);
    static int PskFindCallbackWrapper(SSL *ssl, const unsigned char *identity, size_t identityLen, SSL_SESSION **sess);
    static int PskUseCallbackWrapper(SSL *ssl, const EVP_MD *md, const unsigned char **id, size_t *idlen,
        SSL_SESSION **sess);
    static UBSHcomPskFindSessionCb mOpenSslPskFindSessionCb;
    static UBSHcomPskUseSessionCb mOpenSslPskUseSessionCb;
    NResult SetPSKCallback(bool isServer);

    SSL *mSsl = nullptr;
    SSL_CTX *mSslCtx = nullptr;
};
}
}

#endif // OCK_HCOM_OOB_OPENSSL_12334324233_H
