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
#ifndef OCK_HCOM_OOB_SSL_12334324233_H
#define OCK_HCOM_OOB_SSL_12334324233_H

#include <netinet/tcp.h>
#include <sys/socket.h>

#include "net_oob.h"
#include "net_security_rand.h"

namespace ock {
namespace hcom {
class OOBSSLConnection : public OOBTCPConnection {
public:
    explicit OOBSSLConnection(int fd) : OOBTCPConnection(fd) {}

    ~OOBSSLConnection() override;

    /*
     * @brief Initialize the SSL lib, and build the TLS, openssl make sure
     * it can be call multi-times, but really do once.
     * @return true for success
     */
    virtual NResult InitSSL(bool server) {};

    /*
     * @brief server send the secret.
     * @return true for success
     */
    NResult SendSecret();

    /*
     * @brief client recv and update secrets.
     * @return true for success
     */
    NResult RecvSecret();

    NResult SSLClientRecvHandler(int fd);

    void SetTLSCallback(const UBSHcomTLSCertificationCallback &certCB, const UBSHcomTLSPrivateKeyCallback &keyCB,
        const UBSHcomTLSCaCallback &caCB)
    {
        mCertCallback = certCB;
        mKeyCallback = keyCB;
        mCaCallback = caCB;
    }

    void SetPSKCallback(const UBSHcomPskFindSessionCb &pskFindSessionCb, const UBSHcomPskUseSessionCb &pskUseSessionCb)
    {
        mPskFindSessionCb = pskFindSessionCb;
        mPskUseSessionCb = pskUseSessionCb;
    }

    NetSecrets &Secret()
    {
        return mSecret;
    }

    inline UBSHcomNetCipherSuite GetCipherSuite() const
    {
        return mCipherSuite;
    }

    inline void SetTlsOptions(UBSHcomNetCipherSuite cipherSuite, UBSHcomTlsVersion tlsVersion)
    {
        mCipherSuite = cipherSuite;
        mTlsVersion = tlsVersion;
    }

    inline uint32_t GetTLSVersion() const
    {
        return mTlsVersion;
    }

protected:
    /* Server and Client build the TLS */
    virtual NResult CommLoad(bool server) {};
    virtual NResult VerifyCA(bool server) {};

    NetSecrets mSecret;
    UBSHcomTLSCertificationCallback mCertCallback = nullptr;
    UBSHcomTLSPrivateKeyCallback mKeyCallback = nullptr;
    UBSHcomTLSCertVerifyCallback mCertVerifyCallback = nullptr;
    UBSHcomTLSCaCallback mCaCallback = nullptr;
    std::string mCrlPath;
    UBSHcomPeerCertVerifyType mPeerCertVerifyType = VERIFY_BY_DEFAULT;
    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;
    UBSHcomTlsVersion mTlsVersion = TLS_1_3;

    UBSHcomPskFindSessionCb mPskFindSessionCb = nullptr;
    UBSHcomPskUseSessionCb mPskUseSessionCb = nullptr;
};

class OOBSSLServer : public OOBTCPServer {
public:
    OOBSSLServer(NetDriverOobType t, const std::string &ipOrName, uint16_t portOrPerm,
        UBSHcomTLSPrivateKeyCallback &keyCB, UBSHcomTLSCertificationCallback &certCB, UBSHcomTLSCaCallback &caCB)
        : OOBTCPServer(t, ipOrName, portOrPerm), mTlsPrivateKeyCb(keyCB), mTlsCertCb(certCB), mTlsCaCallback(caCB)
    {}

    OOBSSLServer(NetDriverOobType t, const std::string &ipOrName, uint16_t portOrPerm, bool isCheck,
        UBSHcomTLSPrivateKeyCallback &keyCB, UBSHcomTLSCertificationCallback &certCB, UBSHcomTLSCaCallback &caCB)
        : OOBTCPServer(t, ipOrName, portOrPerm, isCheck),
          mTlsPrivateKeyCb(keyCB),
          mTlsCertCb(certCB),
          mTlsCaCallback(caCB)
    {}

    ~OOBSSLServer() override = default;

    void RunInThread() override;

    void DealConnectInThread(int fd, struct sockaddr_in addressIn) override;

    inline UBSHcomNetCipherSuite GetCipherSuite() const
    {
        return mCipherSuite;
    }

    inline void SetTlsOptions(UBSHcomNetCipherSuite cipherSuite, UBSHcomTlsVersion tlsVersion)
    {
        mCipherSuite = cipherSuite;
        mTlsVersion = tlsVersion;
    }

    inline void SetPSKCallback(const UBSHcomPskFindSessionCb &pskFindSessionCb,
        const UBSHcomPskUseSessionCb &pskUseSessionCb)
    {
        mPskFindSessionCb = pskFindSessionCb;
        mPskUseSessionCb = pskUseSessionCb;
    }

    inline uint32_t GetTLSVersion()
    {
        return mTlsVersion;
    }

private:
    UBSHcomTLSPrivateKeyCallback mTlsPrivateKeyCb = nullptr;
    UBSHcomTLSCertificationCallback mTlsCertCb = nullptr;
    UBSHcomTLSCaCallback mTlsCaCallback = nullptr;
    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;
    UBSHcomTlsVersion mTlsVersion = TLS_1_3;

    UBSHcomPskFindSessionCb mPskFindSessionCb = nullptr;
    UBSHcomPskUseSessionCb mPskUseSessionCb = nullptr;
};

class OOBSSLClient : public OOBTCPClient {
public:
    OOBSSLClient(NetDriverOobType t, std::string serverIpOrName, uint16_t serverPort,
        UBSHcomTLSPrivateKeyCallback &keyCB, UBSHcomTLSCertificationCallback &certCB, UBSHcomTLSCaCallback &caCB)
        : OOBTCPClient(t, serverIpOrName, serverPort), mTlsCaCallback(caCB), mTlsCertCb(certCB), mTlsPrivateKeyCb(keyCB)
    {}

    ~OOBSSLClient() override = default;

    /* for tcp */
    inline NResult Connect(OOBTCPConnection *&conn) override
    {
        if (mOobType == NET_OOB_TCP) {
            return Connect(mServerIP, mServerPort, conn);
        } else if (mOobType == NET_OOB_UDS) {
            return Connect(mServerUdsName, conn);
        }

        return NN_ERROR;
    }

    NResult Connect(const std::string &ip, uint32_t port, OOBTCPConnection *&conn) override;

    NResult Connect(const std::string &udsName, OOBTCPConnection *&conn) override;

    inline void SetTlsOptions(UBSHcomNetDriverOptions options)
    {
        mCipherSuite = options.cipherSuite;
        mTlsVersion = options.tlsVersion;
    }

    inline void SetPSKCallback(const UBSHcomPskFindSessionCb &pskFindSessionCb,
        const UBSHcomPskUseSessionCb &pskUseSessionCb)
    {
        mPskFindSessionCb = pskFindSessionCb;
        mPskUseSessionCb = pskUseSessionCb;
    }

private:
    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;
    UBSHcomTlsVersion mTlsVersion = TLS_1_3;
    UBSHcomTLSCaCallback mTlsCaCallback = nullptr;
    UBSHcomTLSCertificationCallback mTlsCertCb = nullptr;
    UBSHcomTLSPrivateKeyCallback mTlsPrivateKeyCb = nullptr;

    UBSHcomPskFindSessionCb mPskFindSessionCb = nullptr;
    UBSHcomPskUseSessionCb mPskUseSessionCb = nullptr;
    OOBSSLConnection *mOobConn {};
};

class TlsConnectCbTask : public ConnectCbTask {
public:
    using NewConnectionHandler = std::function<int(OOBTCPConnection &)>;

    TlsConnectCbTask(NewConnectionHandler cb, int fd, NetWorkerLBPtr workerLb) : ConnectCbTask(cb, fd, workerLb) {}

    ~TlsConnectCbTask() override
    {
        NetFunc::NN_SafeCloseFd(mFd);
    }

    void SetTlsCb(UBSHcomTLSCertificationCallback certCb, UBSHcomTLSPrivateKeyCallback privateKeyCb,
        UBSHcomTLSCaCallback caCb)
    {
        mTlsCertCb = certCb;
        mTlsPrivateKeyCb = privateKeyCb;
        mTlsCaCallback = caCb;
    };

    void SetTlsOptions(UBSHcomNetCipherSuite cipherSuite, UBSHcomTlsVersion tlsVersion)
    {
        mCipherSuite = cipherSuite;
        mTlsVersion = tlsVersion;
    }

    void SetPSKCallback(const UBSHcomPskFindSessionCb &pskFindSessionCb, const UBSHcomPskUseSessionCb &pskUseSessionCb)
    {
        mPskFindSessionCb = pskFindSessionCb;
        mPskUseSessionCb = pskUseSessionCb;
    }

    void Run() override;

private:
    UBSHcomTLSPrivateKeyCallback mTlsPrivateKeyCb = nullptr;
    UBSHcomTLSCertificationCallback mTlsCertCb = nullptr;
    UBSHcomTLSCaCallback mTlsCaCallback = nullptr;
    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;
    UBSHcomTlsVersion mTlsVersion = TLS_1_3;

    UBSHcomPskFindSessionCb mPskFindSessionCb = nullptr;
    UBSHcomPskUseSessionCb mPskUseSessionCb = nullptr;
};
}
}

#endif // OCK_HCOM_OOB_SSL_12334324233_H
