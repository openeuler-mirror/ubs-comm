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

#include "net_oob.h"
#include "net_oob_openssl.h"
#include "openssl_api_wrapper.h"
#include "net_oob_ssl.h"

namespace ock {
namespace hcom {
void OOBSSLServer::DealConnectInThread(int fd, const sockaddr_storage &peerAddr, socklen_t peerLen)
{
    ConnectResp resp = ConnectResp::OK;

    char ipStr[INET6_ADDRSTRLEN] = {0};
    uint16_t peerPort = 0;
    int family = peerAddr.ss_family;

    if (family == AF_INET6) {
        const auto *a6 = reinterpret_cast<const sockaddr_in6*>(&peerAddr);
        if (inet_ntop(AF_INET6, &(a6->sin6_addr), ipStr, sizeof(ipStr)) == nullptr) {
            NN_LOG_ERROR("Failed to convert ipv6 number to string");
            resp = SERVER_INTERNAL_ERROR;
        } else {
            peerPort = ntohs(a6->sin6_port);
        }
    } else {
        const auto *a4 = reinterpret_cast<const sockaddr_in*>(&peerAddr);
        if (inet_ntop(AF_INET, &(a4->sin_addr), ipStr, sizeof(ipStr)) == nullptr) {
            NN_LOG_ERROR("Failed to convert ipv4 number to string");
            resp = SERVER_INTERNAL_ERROR;
        } else {
            peerPort = ntohs(a4->sin_port);
        }
    }

    TlsConnectCbTask *tlsConnectCbTask =  nullptr;
    if (resp == ConnectResp::OK) {
        tlsConnectCbTask = new (std::nothrow) TlsConnectCbTask(mNewConnectionHandler, fd, mWorkerLb);
        if (NN_UNLIKELY(tlsConnectCbTask == nullptr)) {
            resp = ConnectResp::CONN_ACCEPT_NEW_TASK_FAIL;
        }
    }

    if (resp == ConnectResp::OK) {
        tlsConnectCbTask->SetIpPort(std::string(ipStr), peerPort, mListenPort);
        tlsConnectCbTask->SetTlsCb(mTlsCertCb, mTlsPrivateKeyCb, mTlsCaCallback);
        tlsConnectCbTask->SetTlsOptions(mCipherSuite, mTlsVersion);
        tlsConnectCbTask->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
        if (mOobType == NET_OOB_UDS) {
            tlsConnectCbTask->SetUdsName(mUdsName);
        }
        if (NN_UNLIKELY(!mEs->Execute(tlsConnectCbTask))) {
            delete tlsConnectCbTask;
            resp = ConnectResp::CONN_ACCEPT_QUEUE_FULL;
            NN_LOG_WARN("Invalid to execute task may be queue is full please retry it");
        }
    }

    if (resp != ConnectResp::OK) {
        // if accept success but execute task failed, should notify client connect fail and client will retry
        if (::send(fd, &resp, sizeof(ConnectResp), 0) <= 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to send connect status to peer on oob @ " << ipStr << ":" <<
                peerPort << ", as " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
    }
}

void OOBSSLServer::RunInThread()
{
    if (mOobType == NET_OOB_TCP) {
        NN_LOG_INFO("OOB ssl server accept thread for " << mListenIP << ":" << mListenPort <<
            " started, load balancer " << (mWorkerLb == nullptr ? "null" : mWorkerLb->ToString()));
    } else if (mOobType == NET_OOB_UDS) {
        NN_LOG_TRACE_INFO("OOB ssl server accept thread for " << mUdsName << " started, load balancer " <<
            (mWorkerLb == nullptr ? "null" : mWorkerLb->ToString()));
    } else {
        NN_LOG_ERROR("Un-reachable");
    }

    mThreadStarted.store(true);

    int flags = 1;
    auto maxRecvTimeout = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_RECV_TIMEOUT_SEC", NN_NO1, NN_NO7200, NN_NO0);
    auto maxSendTimeout = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_SEND_TIMEOUT_SEC", NN_NO1, NN_NO7200, NN_NO0);

    while (NN_UNLIKELY(mEs == nullptr || !mEs->IsStart())) {
        usleep(NN_NO100);
    }
    while (true) {
        try {
            if (NN_UNLIKELY(mNeedStop)) {
                NN_LOG_INFO("Got stop signal, stop listening in oob ssl server");
                break;
            }

            struct pollfd pollEventFd = {};
            pollEventFd.fd = mListenFD;
            pollEventFd.events = POLLIN;
            pollEventFd.revents = 0;

            int rc = poll(&pollEventFd, 1, NN_NO500);
            if (rc < 0 && errno != EINTR) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Get poll event failed in oob ssl server, errno " <<
                    NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                break;
            }

            if (rc == 0) {
                continue;
            }

            sockaddr_storage peerAddr {};
            socklen_t peerLen = sizeof(peerAddr);

            auto fd = ::accept(mListenFD, reinterpret_cast<struct sockaddr *>(&peerAddr), &peerLen);
            if (fd < 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_WARN("Invalid to accept in oob ssl server on new socket with " <<
                    NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << ", ignore and continue");
                continue;
            }

            /* set no delay */
            setsockopt(fd, SOL_TCP, TCP_NODELAY, reinterpret_cast<void *>(&flags), sizeof(flags));

            /* set recv or send timeout */
            if (maxRecvTimeout != NN_NO0) {
                struct timeval recvTimeout = { maxRecvTimeout, 0 };
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
            }
            if (maxSendTimeout != NN_NO0) {
                struct timeval sendTimeout = { maxSendTimeout, 0 };
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
            }

            DealConnectInThread(fd, peerAddr, peerLen);
        } catch (std::exception &ex) {
            NN_LOG_WARN("Got exception in OOBSSLServer::RunInThread, exception " << ex.what() <<
                ", ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown error in OOBSSLServer::RunInThread, ignore and continue");
        }
    }

    NN_LOG_INFO("Working thread for OOBSSLServer exiting");
}


/* OOBSSLConnection */
OOBSSLConnection::~OOBSSLConnection()
{
    NetFunc::NN_SafeCloseFd(mFD);
}

NResult OOBSSLConnection::SendSecret()
{
    if (NN_UNLIKELY(!mSecret.Init(mCipherSuite))) {
        NN_LOG_ERROR("Failed to init secret");
        return NN_ERROR;
    }

    size_t len = mSecret.GetSerializeLen();
    char *serializedData = new (std::nothrow) char[len + NN_NO1];
    if (NN_UNLIKELY(serializedData == nullptr)) {
        NN_LOG_ERROR("Failed to new a serializedData array, probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }
    serializedData[len] = '\0';
    NetLocalAutoFreePtr<char> autoFreeData(serializedData, true);

    bool ret = mSecret.Serialize(serializedData, len);
    if (!ret) {
        NN_LOG_ERROR("Failed to serialize TLS exchange info");
        return NN_OOB_SSL_INIT_ERROR;
    }

    NN_LOG_TRACE_INFO("Server update the secrets Len: " << len);

    auto result = Send(serializedData, len);
    if (result != NN_OK) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to send info for TLS peer, error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return result;
    }

    return NN_OK;
}

NResult OOBSSLConnection::RecvSecret()
{
    if (NN_UNLIKELY(!mSecret.Init(mCipherSuite))) {
        NN_LOG_ERROR("Failed to init secret");
        return NN_ERROR;
    }

    size_t len = mSecret.GetSerializeLen();
    void *serializedData = malloc(len);
    if (serializedData == nullptr) {
        return NN_MALLOC_FAILED;
    }

    auto result = Receive(serializedData, len);
    if (result != NN_OK) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to receive info for TLS from peer, error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        free(serializedData);
        serializedData = nullptr;
        return result;
    }

    bool ret = mSecret.Deserialize(static_cast<const char *>(serializedData), len);
    if (!ret) {
        NN_LOG_ERROR("Failed to deserialize TLS exchange info");
        free(serializedData);
        serializedData = nullptr;
        return NN_OOB_SSL_INIT_ERROR;
    }

    NN_LOG_TRACE_INFO("Client update the secrets.");
    free(serializedData);
    serializedData = nullptr;
    return NN_OK;
}

NResult OOBSSLConnection::SSLClientRecvHandler(int tmpFD)
{
    if (tmpFD <= 0) {
        return NN_ERROR;
    }

    return RecvSecret();
}

/* OOBSSLClient */
NResult OOBSSLClient::Connect(const std::string &ip, uint32_t port, OOBTCPConnection *&conn)
{
    int fd = -1;
    auto result = ConnectWithFd(ip, port, fd);
    if (result != NN_OK) {
        return result;
    }

    mOobConn = new (std::nothrow) OOBOpenSSLConnection(fd);
    if (NN_UNLIKELY(mOobConn == nullptr)) {
        NN_LOG_ERROR("Failed to new oob connection, probably out of memory");
        NetFunc::NN_SafeCloseFd(fd);
        return NN_NEW_OBJECT_FAILED;
    }

    mOobConn->SetTlsOptions(mCipherSuite, mTlsVersion);
    mOobConn->SetTLSCallback(mTlsCertCb, mTlsPrivateKeyCb, mTlsCaCallback);
    mOobConn->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);

    if (mOobConn->InitSSL(false) != NN_OK) {
        delete mOobConn;
        mOobConn = nullptr;
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    if (mOobConn->SSLClientRecvHandler(fd) != NN_OK) {
        NN_LOG_ERROR("Failed to receive secret from server to TLS");
        delete mOobConn;
        mOobConn = nullptr;
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    mOobConn->ListenPort(port);
    conn = mOobConn;

    return NN_OK;
}

NResult OOBSSLClient::Connect(const std::string &udsName, OOBTCPConnection *&conn)
{
    NN_LOG_INFO("SSL CONNECT");
    int fd = -1;
    auto result = ConnectWithFd(udsName, fd);
    if (result != NN_OK) {
        return result;
    }

    mOobConn = new (std::nothrow) OOBOpenSSLConnection(fd);
    if (NN_UNLIKELY(mOobConn == nullptr)) {
        NN_LOG_ERROR("Failed to new oob uds connection, probably out of memory");
        NetFunc::NN_SafeCloseFd(fd);
        return NN_NEW_OBJECT_FAILED;
    }

    mOobConn->SetTLSCallback(mTlsCertCb, mTlsPrivateKeyCb, mTlsCaCallback);
    mOobConn->SetTlsOptions(mCipherSuite, mTlsVersion);
    mOobConn->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);

    if (mOobConn->InitSSL(false) != NN_OK) {
        delete mOobConn;
        mOobConn = nullptr;
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    if (mOobConn->SSLClientRecvHandler(fd) != NN_OK) {
        NN_LOG_ERROR("Failed to receive secret from uds server to TLS");
        delete mOobConn;
        mOobConn = nullptr;
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    conn = mOobConn;
    conn->mIsUds = true;

    return NN_OK;
}

void TlsConnectCbTask::Run()
{
    ConnectResp resp = ConnectResp::OK;
    if (::send(mFd, &resp, sizeof(ConnectResp), 0) <= 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to send connect status to peer on oob @ " << mClientIP << ":" << mClientIP << ", as " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return;
    }

    OOBSSLConnection *conn = nullptr;
    conn = new (std::nothrow) OOBOpenSSLConnection(mFd);
    if (NN_UNLIKELY(conn == nullptr)) {
        NN_LOG_ERROR("Failed to new connection");
        return;
    }

    conn->SetIpAndPort(mClientIP, mClientPort);
    conn->ListenPort(mListenPort);
    conn->LoadBalancer(mWorkerLb);
    conn->SetTLSCallback(mTlsCertCb, mTlsPrivateKeyCb, mTlsCaCallback);
    conn->SetTlsOptions(mCipherSuite, mTlsVersion);
    conn->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
    conn->SetUdsName(mUdsName);

    if (NN_UNLIKELY(mNewConnectionHandler == nullptr)) {
        NN_LOG_ERROR("Failed to handshake and exchange address as new connection handler is null");
        delete conn;
        conn = nullptr;
        return;
    }

    if (conn->InitSSL(true) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize TLS context for new connection from " << conn->GetIpAndPort());
        delete conn;
        conn = nullptr;
        return;
    }

    /* Update the secret first */
    if (conn->SendSecret() != NN_OK) {
        NN_LOG_ERROR("Failed to send TLS info to send new connection from " << conn->GetIpAndPort());
        delete conn;
        conn = nullptr;
        return;
    }

    auto startConnCb = NetMonotonic::TimeUs();
    if (mNewConnectionHandler(*conn) != 0) {
        NN_LOG_ERROR("Failed to handshake and exchange address with client " << conn->GetIpAndPort() <<
            ", continue to accept future connection");
        mFd = conn->TransferFd();
        delete conn;
        conn = nullptr;
        return;
    }
    auto endConnCb = NetMonotonic::TimeUs();
    auto cbTime = endConnCb - startConnCb;
    if (NN_UNLIKELY(cbTime > MAX_CB_TIME_US)) {
        NN_LOG_WARN("Call new Connection Cb time is too long: " << cbTime << " us.");
    }
    /* the socket could be transfer to real connection when type is socket */
    mFd = conn->TransferFd();
    delete conn;
    conn = nullptr;
}
}
}