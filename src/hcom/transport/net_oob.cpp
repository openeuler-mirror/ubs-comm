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

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "hcom_def.h"
#include "hcom_log.h"
#include "net_oob.h"

namespace ock {
namespace hcom {
NResult OOBTCPServer::EnableAutoPortSelection(uint16_t minPort, uint16_t maxPort)
{
    if (mStarted) {
        NN_LOG_ERROR("Failed to enable auto port selection! oob server already start.");
        return NN_ERROR;
    }

    if (mOobType != NET_OOB_TCP) {
        NN_LOG_ERROR("Failed to enable auto port selection! OOB_TYPE is not TCP.");
        return NN_ERROR;
    }

    if (minPort == 0 || maxPort == 0) {
        NN_LOG_ERROR("Failed to enable auto port selection!, port range is invalid!");
        return NN_ERROR;
    }

    if (minPort < NN_NO1024) {
        NN_LOG_ERROR("Failed to enable auto port selection! minPort is less than 1024.");
        return NN_ERROR;
    }

    if (maxPort < NN_NO1024) {
        NN_LOG_ERROR("Failed to enable auto port selection! maxPort is less than 1024.");
        return NN_ERROR;
    }

    if (minPort > maxPort) {
        NN_LOG_ERROR("Failed to enable auto port selection! minPort is bigger than maxPort.");
        return NN_ERROR;
    }

    if (mListenPort != 0) {
        NN_LOG_WARN("oobPort will be selected automatically!");
    }

    mMinListenPort = minPort;
    mMaxListenPort = maxPort;
    mListenPort = mMinListenPort;
    mIsAutoPortSelectionEnabled = true;
    return NN_OK;
}

NResult OOBTCPServer::GetListenPort(uint16_t &port)
{
    if (!mStarted) {
        NN_LOG_ERROR("Failed to get listen port, oob server is not start");
        return NN_ERROR;
    }

    port = mListenPort;
    return NN_OK;
}

NResult OOBTCPServer::GetListenIp(std::string &ip)
{
    if (!mStarted) {
        NN_LOG_ERROR("Failed to get listen ip, oob server is not start");
        return NN_ERROR;
    }

    ip = mListenIP;
    return NN_OK;
}

NResult OOBTCPServer::GetUdsName(std::string &udsName)
{
    if (!mStarted) {
        NN_LOG_ERROR("Failed to get uds name, oob server is not start");
        return NN_ERROR;
    }

    if (mOobType != NET_OOB_UDS) {
        NN_LOG_ERROR("Failed to get uds name, oob server is not uds");
        return NN_ERROR;
    }

    udsName = mUdsName;
    return NN_OK;
}

bool BuildSockAddr(const std::string &ip, uint16_t port,
                   sockaddr_storage &addrStorage, socklen_t &addrLen, int &family)
{
    addrStorage = {};
    addrLen = 0;
    family = AF_UNSPEC;
    sockaddr_in addr4 {};
    if (inet_pton(AF_INET, ip.c_str(), &addr4.sin_addr) == 1) {
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        std::memcpy(&addrStorage, &addr4, sizeof(addr4));
        addrLen = sizeof(addr4);
        family = AF_INET;
        return true;
    }
    sockaddr_in6 addr6 {};
    if (inet_pton(AF_INET6, ip.c_str(), &addr6.sin6_addr) == 1) {
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        std::memcpy(&addrStorage, &addr6, sizeof(addr6));
        addrLen = sizeof(addr6);
        family = AF_INET6;
        return true;
    }
    return false;
}

NResult OOBTCPServer::BindAndListenCommon(int socketFD)
{
    sockaddr_storage addrStorage {};
    socklen_t addrLen = 0;
    int family = AF_UNSPEC;
    if (!BuildSockAddr(mListenIP, mListenPort, addrStorage, addrLen, family)) {
        NN_LOG_ERROR("Invalid listen ip: " << mListenIP);
        NetFunc::NN_SafeCloseFd(socketFD);
        return NN_INVALID_IP;
    }

    auto ret = ::bind(socketFD, reinterpret_cast<struct sockaddr *>(&addrStorage), addrLen);
    if (NN_UNLIKELY(ret < 0)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to bind on " << mListenIP << ":" << mListenPort << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        NetFunc::NN_SafeCloseFd(socketFD);
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    // listen
    if (NN_UNLIKELY(::listen(socketFD, OOB_DEFAULT_LISTEN_BACKLOG) < 0)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to listen on " << mListenIP << ":" << mListenPort << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        NetFunc::NN_SafeCloseFd(socketFD);
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }
    return NN_OK;
}

NResult OOBTCPServer::BindAndListenAuto(int &socketFD)
{
    bool isBindAndListenSuccess = false;
    // mListenPort is set to mMinListenPort in EnableAutoPortSelection()
    auto tmpPort = mListenPort;
    while (tmpPort <= mMaxListenPort) {
        sockaddr_storage addrStorage {};
        socklen_t addrLen = 0;
        int family = AF_UNSPEC;
        if (!BuildSockAddr(mListenIP, tmpPort, addrStorage, addrLen, family)) {
            NN_LOG_ERROR("Invalid listen ip: " << mListenIP);
            NetFunc::NN_SafeCloseFd(socketFD);
            return NN_INVALID_IP;
        }
        auto ret = ::bind(socketFD, reinterpret_cast<struct sockaddr *>(&addrStorage), addrLen);
        if (NN_UNLIKELY(ret < 0)) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_DEBUG("Try to bind on " << mListenIP << ":" << tmpPort << " failed, error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            ++tmpPort;
            continue;
        }

        ret = ::listen(socketFD, OOB_DEFAULT_LISTEN_BACKLOG);
        if (NN_LIKELY(ret == 0)) {
            isBindAndListenSuccess = true;
            break;
        }
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_DEBUG("Try to listen on " << mListenIP << ":" << tmpPort << " failed, error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        // bind success but listen failed, reuse socketFD will case invalid argument error(22)
        NetFunc::NN_SafeCloseFd(socketFD);
        ret = CreateAndConfigSocket(socketFD);
        if (NN_UNLIKELY(ret != NN_OK)) {
            NN_LOG_ERROR("Recreate socket fd failed");
            return ret;
        }
        ++tmpPort;
    }

    if (!isBindAndListenSuccess) {
        NN_LOG_ERROR("Failed to bind and listen on port range [" << mMinListenPort << ", " << mMaxListenPort << "].");
        NetFunc::NN_SafeCloseFd(socketFD);
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }
    mListenPort = tmpPort;
    return NN_OK;
}

NResult OOBTCPServer::CreateAndConfigSocket(int &socketFD)
{
    sockaddr_storage addrStorage {};
    socklen_t addrLen = 0;
    int family = AF_UNSPEC;
    if (!BuildSockAddr(mListenIP, mListenPort, addrStorage, addrLen, family)) {
        NN_LOG_ERROR("Invalid listen ip: " << mListenIP);
        NetFunc::NN_SafeCloseFd(socketFD);
        return NN_INVALID_IP;
    }

    auto tmpFD = ::socket(family, SOCK_STREAM, 0);
    if (tmpFD < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create listen socket, error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
            ", please check if running of fd limit");
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }
    /* set no-blocking */
    int value = 1;
    if (NN_UNLIKELY((value = fcntl(tmpFD, F_GETFL, 0)) == -1)) {
        NetFunc::NN_SafeCloseFd(tmpFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to get control value for sock " << mIndex.oobSvrIdx << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    if (NN_UNLIKELY((value = fcntl(tmpFD, F_SETFL, uint32_t(value) | O_NONBLOCK)) == -1)) {
        NetFunc::NN_SafeCloseFd(tmpFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set control value for sock " << mIndex.oobSvrIdx << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    // set option
    int flags = 1;
    int ret = ::setsockopt(tmpFD, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<void *>(&flags), sizeof(flags));
    if (NN_UNLIKELY(ret < 0)) {
        NetFunc::NN_SafeCloseFd(tmpFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set option, error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }
    socketFD = tmpFD;
    return NN_OK;
}

NResult OOBTCPServer::CreateAndStartSocket()
{
    int socketFD = 0;
    int ret = NN_OK;

    ret = CreateAndConfigSocket(socketFD);
    if (NN_UNLIKELY(ret != NN_OK)) {
        return ret;
    }

    if (mIsAutoPortSelectionEnabled) {
        ret = BindAndListenAuto(socketFD);
    } else {
        ret = BindAndListenCommon(socketFD);
    }

    if (NN_LIKELY(ret == NN_OK)) {
        mListenFD = socketFD;
    }
    return ret;
}

NResult OOBTCPServer::Start()
{
    if (mStarted) {
        return NN_OK;
    }

    // check new connection cb
    if (mNewConnectionHandler == nullptr) {
        NN_LOG_ERROR("Failed to start oob server as new connection callback is not set");
        return NN_OOB_CONN_CB_NOT_SET;
    }

    // check lb
    if ((!enableMultiRail) && (mWorkerLb == nullptr)) {
        NN_LOG_ERROR("Failed to start oob server as load balancer is not set");
        return NN_INVALID_PARAM;
    }

    if (mOobType == NET_OOB_UDS) {
        return StartForUds();
    }

    if (mOobType != NET_OOB_TCP || mListenIP.empty() || mListenPort < NN_NO1024) {
        NN_LOG_ERROR("Failed to start oob server as invalid type or listen ip " << mListenIP << " or port " <<
            mListenPort << ", port range is 1024 ~ 65535)");
        return NN_INVALID_PARAM;
    }

    auto ret = CreateAndStartSocket();
    if (NN_UNLIKELY(ret != NN_OK)) {
        NN_LOG_ERROR("Failed to create and start oob tcp socket");
        return ret;
    }

    // start oob connection cb thread
    mEs = NetExecutorService::Create(mNewConnCbThreadNum, mNewConnCbQueueCap);
    if (NN_UNLIKELY(mEs == nullptr)) {
        NetFunc::NN_SafeCloseFd(mListenFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create oob connection cb thread in oob server, as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    }
    mEs->SetThreadName("OOBTcpConnHdl");
    if (NN_UNLIKELY(!mEs->Start())) {
        NetFunc::NN_SafeCloseFd(mListenFD);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to start oob connection cb thread in oob server, as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    }

    mThreadStarted.store(false);

    // start oob accept thread
    std::thread tmpThread(&OOBTCPServer::RunInThread, this);
    mAcceptThread = std::move(tmpThread);
    std::string thrName = "OOBTcpSvr" + mIndex.ToString();
    if (pthread_setname_np(mAcceptThread.native_handle(), thrName.c_str()) != 0) {
        NN_LOG_WARN("Invalid to set thread name of oob tcp server");
    }

    // set cpu id
    cpu_set_t cpuSet;
    if (mCpuId != -1) {
        CPU_ZERO(&cpuSet);
        CPU_SET(mCpuId, &cpuSet);
        if (pthread_setaffinity_np(mAcceptThread.native_handle(), sizeof(cpuSet), &cpuSet) != 0) {
            NN_LOG_WARN("Unable to bind net oob server " << thrName << " to cpu " << mCpuId);
        }
        NN_LOG_INFO("Bind net oob server " << thrName << " to cpu " << mCpuId);
    }

    while (!mThreadStarted.load()) {
        usleep(NN_NO128);
    }

    mStarted = true;
    return NN_OK;
}

NResult OOBTCPServer::Stop()
{
    if (!mStarted) {
        return NN_OK;
    }

    mNeedStop = true;

    if (mAcceptThread.joinable()) {
        mAcceptThread.join();
    }

    if (mOobType == NET_OOB_UDS && mUdsPerm != 0) {
        if (!CanonicalPath(mUdsName)) {
            NN_LOG_ERROR("Uds oob file path is invalid");
            return NN_INVALID_PARAM;
        }

        if (!NetFunc::NN_CheckFilePrefix(mUdsName)) {
            NN_LOG_ERROR("Uds oob file path is invalid as prefix invalid");
            return NN_INVALID_PARAM;
        }
        unlink(mUdsName.c_str());
    }

    NetFunc::NN_SafeCloseFd(mListenFD);

    mStarted = false;
    return NN_OK;
}

NResult OOBTCPServer::AssignUdsAddress(sockaddr_un &address, socklen_t &addressLen)
{
    if (mUdsPerm == 0) {
        address.sun_path[0] = '\0'; /* use abstract namespace */
        if (strcpy_s(address.sun_path + 1, sizeof(address.sun_path) - 1, mUdsName.c_str()) != EOK) {
            NN_LOG_ERROR("strcpy_s uds name error.");
            return NN_ERROR;
        }
        addressLen = sizeof(address.sun_family) + 1 + mUdsName.length();
    } else {
        size_t index = mUdsName.find_last_of('/');
        if (NN_UNLIKELY(index == std::string::npos)) {
            NN_LOG_ERROR("Uds oob file path is invalid");
            return NN_INVALID_PARAM;
        }

        std::string udsFilePrefix = mUdsName.substr(0, index + 1);
        std::string udsFileName = mUdsName.substr(index + 1, mUdsName.length());

        if (!NetFunc::NN_CheckFilePrefix(udsFilePrefix)) {
            NN_LOG_ERROR("Uds oob file path is invalid as prefix invalid");
            return NN_INVALID_PARAM;
        }
        if (!CanonicalPath(udsFilePrefix)) {
            NN_LOG_ERROR("Uds oob file path is invalid");
            return NN_INVALID_PARAM;
        }

        mUdsName = udsFilePrefix + "/" + udsFileName;

        if (::access(mUdsName.c_str(), 0) == 0) {
            if (unlink(mUdsName.c_str()) == -1) {
                NN_LOG_ERROR("Failed to unlink uds oob file");
                return NN_INVALID_PARAM;
            }
        }

        int result = 0;
        if ((result = strcpy_s(address.sun_path, sizeof(address.sun_path), mUdsName.c_str())) != EOK) {
            NN_LOG_ERROR("strcpy_s uds name error.  result :" << result);
            return NN_ERROR;
        }
        addressLen = sizeof(address);
    }
    return NN_OK;
}

NResult OOBTCPServer::StartForUds()
{
    if (mUdsName.empty()) {
        NN_LOG_ERROR("Failed to start oob server as invalid UDS file path");
        return NN_INVALID_PARAM;
    }

    if (mUdsName[0] == '/' && mUdsPerm == 0) {
        NN_LOG_ERROR(
            "Failed to start oob server as invalid UDS file path, first char cannot be '/' for abstract namespace");
        return NN_INVALID_PARAM;
    }

    struct sockaddr_un address {};
    socklen_t addressLen = 0;
    NN_ASSERT_LOG_RETURN(sizeof(address.sun_path) - 1 > mUdsName.length(), NN_INVALID_PARAM);
    bzero(&address, sizeof(address));
    address.sun_family = AF_UNIX;

    auto result = AssignUdsAddress(address, addressLen);
    if (NN_UNLIKELY(result != NN_OK)) {
        return result;
    }

    auto listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create listen socket, error "
            << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
            ", please check if fd is out of limit");
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    if (::bind(listenFd, reinterpret_cast<struct sockaddr *>(&address), addressLen) < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to bind uds, error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        NetFunc::NN_SafeCloseFd(listenFd);
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    /* To support communication between different users in two containers */
    if (NN_UNLIKELY(mCheckUdsPerm && (mUdsPerm != NN_NO0600) && (mUdsPerm != NN_NO0))) {
        NN_LOG_WARN("File permission is incorrect, The file permission must be set to 0600.");
        mUdsPerm = NN_NO0600;
    }

    chmod(mUdsName.c_str(), mUdsPerm);

    if (::listen(listenFd, NN_NO1024) < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to listen uds, error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        NetFunc::NN_SafeCloseFd(listenFd);
        return NN_OOB_LISTEN_SOCKET_ERROR;
    }

    mListenFD = listenFd;
    mThreadStarted.store(false);

    // start oob accept thread
    std::thread tmpThread(&OOBTCPServer::RunInThread, this);
    mAcceptThread = std::move(tmpThread);
    std::string thrName = "OOBUdsSvr" + mIndex.ToString();
    if (pthread_setname_np(mAcceptThread.native_handle(), thrName.c_str()) != 0) {
        NN_LOG_WARN("Invalid to set thread name of oob uds server");
    }

    while (!mThreadStarted.load()) {
        usleep(NN_NO128);
    }

    // start oob connection cb thread
    mEs = NetExecutorService::Create(mNewConnCbThreadNum, mNewConnCbQueueCap);
    if (NN_UNLIKELY(mEs == nullptr)) {
        return NN_ERROR;
    }
    mEs->SetThreadName("OOBUdsConnHdl");
    if (NN_UNLIKELY(!mEs->Start())) {
        return NN_ERROR;
    }

    mStarted = true;
    return NN_OK;
}

void OOBTCPServer::DealConnectInThread(int fd, const sockaddr_storage &peerAddr, socklen_t peerLen)
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

    ConnectCbTask *newConnTask = nullptr;
    if (resp == ConnectResp::OK) {
        newConnTask = new (std::nothrow) ConnectCbTask(mNewConnectionHandler, fd, mWorkerLb);
        if (NN_UNLIKELY(newConnTask == nullptr)) {
            resp = ConnectResp::CONN_ACCEPT_NEW_TASK_FAIL;
        }
    }

    if (resp == ConnectResp::OK) {
        newConnTask->SetIpPort(std::string(ipStr), peerPort, mListenPort);
        if (mOobType == NET_OOB_UDS) {
            newConnTask->SetUdsName(mUdsName);
        }
        if (NN_UNLIKELY(!mEs->Execute(newConnTask))) {
            delete newConnTask;
            resp = ConnectResp::CONN_ACCEPT_QUEUE_FULL;
            NN_LOG_WARN("Failed to execute task, queue may be full, please retry");
        }
    }

    if (resp != ConnectResp::OK) {
        // if accept success but execute task failed, should notify client connect fail and client will retry
        if (::send(fd, &resp, sizeof(ConnectResp), 0) <= 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to send connect resp to peer on oob @ "
                << ipStr << ":" << peerPort
                << ", errno:" << errno << " error:"
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
    }
}

void OOBTCPServer::RunInThread()
{
    if (mOobType == NET_OOB_TCP) {
        NN_LOG_INFO("OOB server accept thread for " << mListenIP << ":" << mListenPort << " started, load balancer " <<
            (mWorkerLb == nullptr ? "null" : mWorkerLb->ToString()));
    } else if (mOobType == NET_OOB_UDS) {
        NN_LOG_TRACE_INFO("OOB server accept thread for " << mUdsName << " started, load balancer " <<
            (mWorkerLb == nullptr ? "null" : mWorkerLb->ToString()));
    } else {
        NN_LOG_ERROR("Un-reachable path");
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
                NN_LOG_INFO("Got stop signal, stop listening");
                break;
            }
            struct pollfd pollEventFd = {};
            pollEventFd.fd = mListenFD;
            pollEventFd.events = POLLIN;
            pollEventFd.revents = 0;

            int rc = poll(&pollEventFd, 1, NN_NO500);
            if (rc < 0 && errno != EINTR) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Get poll event failed, errno "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                break;
            }

            if (rc == 0) {
                continue;
            }

            sockaddr_storage peerAddr {};
            socklen_t peerLen = sizeof(peerAddr);

            int fd = ::accept(mListenFD, reinterpret_cast<sockaddr *>(&peerAddr), &peerLen);
            if (fd < 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_WARN("Invalid to accept on new socket with "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE)
                    << ", ignore and continue");
                continue;
            }

            /* set no delay */
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&flags), sizeof(flags));

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
            NN_LOG_WARN("Got exception in OOBTCPServer::RunInThread, exception "
                << ex.what() << ", ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown exception in OOBTCPServer::RunInThread, ignore and continue");
        }
    }

    NN_LOG_INFO("Working thread for OOBTCPServer at " << mListenIP << ":" << mListenPort << " exiting");
}

/* OOBTCPConnection */
OOBTCPConnection::~OOBTCPConnection()
{
    NetFunc::NN_SafeCloseFd(mFD);
}

NResult OOBTCPConnection::Send(void *buf, uint32_t size) const
{
    if (NN_UNLIKELY(buf == nullptr)) {
        NN_LOG_ERROR("Failed to send as buf is nullptr");
        return NN_PARAM_INVALID;
    }

    const unsigned char *p = static_cast<const unsigned char *>(buf);
    while (size > 0) {
        const ssize_t result = ::send(mFD, p, size, 0);
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                // Since mFD is blocking, EAGAIN/EWOULDBLOCK won't be there.
                char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR(
                    "Failed to send data to peer on oob @ "
                    << mIpAndPort << ", as errno:" << errno << " error:"
                    << NetFunc::NN_GetStrError(errno, errBuf,
                                               NET_STR_ERROR_BUF_SIZE));
                return NN_OOB_CONN_SEND_ERROR;
            }
        } else if (result == 0) {
            NN_LOG_ERROR("Failed to send data to peer on oob @ "
                         << mIpAndPort << ", reset by peer");
            return NN_OOB_CONN_SEND_ERROR;
        }

        p += result;
        size -= static_cast<uint32_t>(result);
    }

    return NN_OK;
}

NResult OOBTCPConnection::Receive(void *buf, uint32_t size) const
{
    if (NN_UNLIKELY(buf == nullptr)) {
        NN_LOG_ERROR("Failed to recv as buf is nullptr");
        return NN_PARAM_INVALID;
    }

    unsigned char *p = static_cast<unsigned char *>(buf);
    while (size > 0) {
        const ssize_t result = ::recv(mFD, p, size, 0);
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                // Since mFD is blocking, EAGAIN/EWOULDBLOCK won't be there.
                char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR(
                    "Failed to receive data from peer on oob @ "
                    << mIpAndPort << ", as errno:" << errno << " error:"
                    << NetFunc::NN_GetStrError(errno, errBuf,
                                               NET_STR_ERROR_BUF_SIZE));
                return NN_OOB_CONN_RECEIVE_ERROR;
            }
        } else if (result == 0) {
            NN_LOG_ERROR("Failed to receive data from peer on oob @ " << mIpAndPort << ", peer fd closed");
            return NN_OOB_CONN_RECEIVE_ERROR;
        }

        p += result;
        size -= static_cast<uint32_t>(result);
    }

    return NN_OK;
}

NResult OOBTCPConnection::SendMsg(msghdr msg, uint32_t size) const
{
    auto result = ::sendmsg(mFD, &msg, 0);
    if (NN_LIKELY(result == size)) {
        return NN_OK;
    } else if (result <= 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to send msg to peer " << mIpAndPort << " result:" << result << ", as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    } else {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed send msg to pee, the size is un-matched required size " << sizeof(msg) << ", send size " <<
            result << ", or connection error, errno " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    }
    return NN_OK;
}


NResult OOBTCPConnection::ReceiveMsg(msghdr msg, uint32_t size) const
{
    auto result = ::recvmsg(mFD, &msg, 0);
    if (NN_LIKELY(result == size)) {
        return NN_OK;
    } else if (result <= 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to receive msg from peer on oob" << mIpAndPort << ", as "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    } else {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to receive data from peer, the size is un-matched required size " << sizeof(msg) <<
            ", recv size " << result << ", or connection error, errno "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_ERROR;
    }
    return NN_OK;
}

/* OOBTCPClient */
NResult OOBTCPClient::Connect(const std::string &ip, uint32_t port, OOBTCPConnection *&conn)
{
    int fd = -1;
    auto result = ConnectWithFd(ip, port, fd);
    if (result != NN_OK) {
        return result;
    }

    conn = new (std::nothrow) OOBTCPConnection(fd);
    if (NN_UNLIKELY(conn == nullptr)) {
        NN_LOG_ERROR("Failed to new oob connection, probably out of memory");
        NetFunc::NN_SafeCloseFd(fd);
        return NN_NEW_OBJECT_FAILED;
    }

    conn->ListenPort(port);
    return NN_OK;
}

NResult OOBTCPClient::ConnectWithFd(const std::string &ip, uint32_t port, int &fd)
{
    sockaddr_storage addrStorage {};
    socklen_t addrLen = 0;
    int family = AF_UNSPEC;

    {
        sockaddr_in addr4 {};
        if (inet_pton(AF_INET, ip.c_str(), &addr4.sin_addr) == 1) {
            addr4.sin_family = AF_INET;
            addr4.sin_port = htons(port);
            std::memcpy(&addrStorage, &addr4, sizeof(addr4));
            addrLen = sizeof(addr4);
            family = AF_INET;
        }
    }

    if (family == AF_UNSPEC) {
        sockaddr_in6 addr6 {};
        if (inet_pton(AF_INET6, ip.c_str(), &addr6.sin6_addr) == 1) {
            addr6.sin6_family = AF_INET6;
            addr6.sin6_port = htons(port);
            std::memcpy(&addrStorage, &addr6, sizeof(addr6));
            addrLen = sizeof(addr6);
            family = AF_INET6;
        }
    }

    if (family == AF_UNSPEC) {
        NN_LOG_ERROR("Failed to connect because ip is invalid: " << ip);
        return NN_INVALID_IP;
    }

    auto tmpFD = ::socket(family, SOCK_STREAM, 0);
    if (tmpFD < 0) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create socket, errno:" << errno << " error:"
            << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE)
            << ", please check if fd is out of limit");
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    int flags = 1;
    setsockopt(tmpFD, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&flags), sizeof(flags));
    int synCnt = 1; /* Set connect() retry time for quick connect */
    setsockopt(tmpFD, IPPROTO_TCP, TCP_SYNCNT, &synCnt, sizeof(synCnt));

    uint32_t timesRetried = 0;
    long maxConnRetryTimes = NN_NO5;
    long maxConnRetryInterval = NN_NO20;
    ConfigureSocketTimeouts(tmpFD, maxConnRetryTimes, maxConnRetryInterval);

    ssize_t result = -1;
    ConnectState state = ConnectState::DISCONNECTED;
    ConnectResp connectStatus = ConnectResp::OK;

    if (family == AF_INET6) {
        sockaddr_storage localAddrStorage {};
        socklen_t localAddrLen = 0;
        sockaddr_in6 localAddr6 {};

        inet_pton(AF_INET6, mLocalEid.c_str(), &localAddr6.sin6_addr);
        localAddr6.sin6_family = AF_INET6;
        std::memcpy(&localAddrStorage, &localAddr6, sizeof(localAddr6));
        localAddrLen = sizeof(localAddr6);

        auto ret = ::bind(tmpFD, reinterpret_cast<struct sockaddr *>(&localAddrStorage), localAddrLen);
    }

    while (timesRetried < maxConnRetryTimes) {
        switch (state) {
            case ConnectState::DISCONNECTED:
                NN_LOG_INFO("Trying to connect to " << ip << ":" << port);

                if (timesRetried != 0) {
                    sleep((1 << timesRetried) > maxConnRetryInterval ? maxConnRetryInterval : (1 << timesRetried));
                }

                if (::connect(tmpFD, reinterpret_cast<struct sockaddr *>(&addrStorage), addrLen) == 0) {
                    state = ConnectState::CONNECTED;
                    continue;
                } else {
                    char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Trying to connect to "
                                 << ip << ":" << port << " errno:" << errno
                                 << " error:" << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE)
                                 << " retry times:" << timesRetried);
                }
                break;

            case ConnectState::CONNECTED:
                result = ::recv(tmpFD, &connectStatus, sizeof(ConnectResp), 0);
                if (result <= 0 || connectStatus != ConnectResp::OK) {
                    char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to receive connection status from peer on oob, as result:"
                                 << result << " errno:" << errno
                                 << " error:" << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE)
                                 << " connTaskStatus:" << connectStatus);
                } else {
                    fd = tmpFD;
                    NN_LOG_INFO("Connect to " << ip << ":" << port << " successfully");
                    return NN_OK;
                }
                break;
        }

        timesRetried++;
    }

    NetFunc::NN_SafeCloseFd(tmpFD);
    NN_LOG_ERROR("Failed to connect to " << ip << ":" << port << " after tried " << timesRetried << " times");
    return NN_OOB_CLIENT_SOCKET_ERROR;
}

NResult OOBTCPClient::Connect(const std::string &udsName, OOBTCPConnection *&conn)
{
    int fd = -1;
    auto result = ConnectWithFd(udsName, fd);
    if (result != NN_OK) {
        return result;
    }

    conn = new (std::nothrow) OOBTCPConnection(fd);
    if (NN_UNLIKELY(conn == nullptr)) {
        NN_LOG_ERROR("Failed to new oob connection, probably out of memory");
        NetFunc::NN_SafeCloseFd(fd);
        return NN_NEW_OBJECT_FAILED;
    }

    conn->mIsUds = true;

    return NN_OK;
}

NResult OOBTCPClient::ConnectWithFd(const std::string &filename, int &fd)
{
    if (filename.empty()) {
        NN_LOG_ERROR("Invalid name or file path to connect for uds, which is empty");
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }

    struct sockaddr_un address {};
    socklen_t addressLen = 0;
    NN_ASSERT_LOG_RETURN(sizeof(address.sun_path) - 1 > filename.length(), NN_INVALID_PARAM);

    bzero(&address, sizeof(address));
    address.sun_family = AF_UNIX;

    bool abstractNs = (filename[0] != '/');
    if (abstractNs) {
        address.sun_path[0] = '\0'; /* use abstract namespace */
        if (strcpy_s(address.sun_path + 1, sizeof(address.sun_path) - 1, filename.c_str()) != EOK) {
            NN_LOG_ERROR("strcpy_s filename error.");
            return NN_ERROR;
        }
        addressLen = sizeof(address.sun_family) + 1 + filename.length();
    } else {
        if (!CanonicalPath(const_cast<std::string &>(filename))) {
            NN_LOG_ERROR("Uds oob file path is invalid");
            return NN_INVALID_PARAM;
        }

        if (strcpy_s(address.sun_path, sizeof(address.sun_path), filename.c_str()) != EOK) {
            NN_LOG_ERROR("strcpy_s filename error.");
            return NN_ERROR;
        }
        addressLen = sizeof(address);
    }

    auto tmpFD = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (tmpFD < 0) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create listen socket, errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE) << ", please check if fd is out of limit");
        return NN_OOB_CLIENT_SOCKET_ERROR;
    }
    int synCnt = 1; /* Set connect() retry time for quick connect */
    setsockopt(tmpFD, IPPROTO_TCP, TCP_SYNCNT, &synCnt, sizeof(synCnt));

    uint32_t timesRetried = 0;
    long maxConnRetryTimes = NN_NO5;
    long maxConnRetryInterval = NN_NO20;
    ConfigureSocketTimeouts(tmpFD, maxConnRetryTimes, maxConnRetryInterval);

    while (timesRetried < maxConnRetryTimes) {
        NN_LOG_INFO("Trying to connect to " << filename);
        if (::connect(tmpFD, reinterpret_cast<struct sockaddr *>(&address), addressLen) == 0) {
            ConnectResp connectStatus = ConnectResp::OK;
            ssize_t result = ::recv(tmpFD, &connectStatus, sizeof(ConnectResp), 0);
            if (result <= 0 || connectStatus != ConnectResp::OK) {
                char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to receive connection status from peer on oob, as result:" << result <<
                    " errno:" << errno << " error:" << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE) <<
                    " connTaskStatus:" << connectStatus);
            } else {
                fd = tmpFD;
                NN_LOG_INFO("Connect to " << filename << " successfully");
                return NN_OK;
            }
        }

        if (errno == EINTR) {
            continue;
        }

        sleep(1 << timesRetried > maxConnRetryInterval ? maxConnRetryInterval : 1 << timesRetried);
        timesRetried++;

        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Trying to connect to " << filename << " errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE) << " retry times:" << timesRetried);
    }

    NetFunc::NN_SafeCloseFd(tmpFD);
    NN_LOG_ERROR("Failed to connect to " << filename << " after tried " << timesRetried << " times");
    return NN_OOB_CLIENT_SOCKET_ERROR;
}

void OOBTCPClient::ConfigureSocketTimeouts(int &tmpFD, long &maxConnRetryTimes, long &maxConnRetryInterval)
{
    maxConnRetryTimes = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_RETRY_TIMES", NN_NO1, NN_NO10, NN_NO5);
    maxConnRetryInterval = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_RETRY_INTERVAL_SEC", NN_NO1, NN_NO60, NN_NO20);
    auto maxRecvTimeout = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_RECV_TIMEOUT_SEC", NN_NO1, NN_NO7200, NN_NO0);
    auto maxSendTimeout = NetFunc::NN_GetLongEnv("HCOM_CONNECTION_SEND_TIMEOUT_SEC", NN_NO1, NN_NO7200, NN_NO0);
    if (maxRecvTimeout != NN_NO0) {
        struct timeval recvTimeout = { maxRecvTimeout, 0 };
        setsockopt(tmpFD, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(timeval));
    }
    if (maxSendTimeout != NN_NO0) {
        struct timeval sendTimeout = { maxSendTimeout, 0 };
        setsockopt(tmpFD, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(timeval));
    }
}
}
}
