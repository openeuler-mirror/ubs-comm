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
#include "sock_wrapper.h"

namespace ock {
namespace hcom {
SResult Sock::Initialize(const SockWorkerOptions &workerOptions)
{
    std::lock_guard<std::mutex> guard(mInitMutex);
    if (mInited) {
        return SS_OK;
    }

    if (NN_UNLIKELY(mType != SOCK_UDS && mType != SOCK_TCP)) {
        NN_LOG_ERROR("Failed to initialize sock as type is invalid");
        return SS_PARAM_INVALID;
    }

    SResult result = SS_OK;
    /* validate options */
    if (NN_UNLIKELY((result = ValidateOptions()) != SS_OK)) {
        NN_LOG_ERROR("Failed to validate options in sock initialize");
        return result;
    }

    /* validate fd and set sock option */
    if (NN_UNLIKELY((result = SetSockOption(workerOptions)) != SS_OK)) {
        NN_LOG_ERROR("Failed to set sock options in sock initialize");
        return result;
    }

    /* allocate receive buf and send list */
    if (NN_UNLIKELY(!mReceiveBuff.ExpandIfNeed(mOptions.receiveBufSizeKB * NN_NO1024))) {
        NN_LOG_ERROR("Failed to allocate receive buffer for sock " << mId << ", probably of memory");
        return SS_MEMORY_ALLOCATE_FAILED;
    }

    mReceiveState.ResetHeader();
    mCtxMap.reserve(SOCK_CTX_MAP_RESERVATION);
    mSendQueue.Initialize();
    mInited = true;
    return SS_OK;
}

void Sock::UnInitialize()
{
    std::lock_guard<std::mutex> guard(mInitMutex);
    if (mSsl != nullptr) {
        HcomSsl::SslShutdown(mSsl);
        HcomSsl::SslFree(mSsl);
        mSsl = nullptr;
    }
    NetFunc::NN_SafeCloseFd(mFd);
    mCtxMap.clear();
}

void Sock::Close()
{
    std::lock_guard<std::mutex> guard(mInitMutex);
    if (mSsl != nullptr) {
        HcomSsl::SslShutdown(mSsl);
        HcomSsl::SslFree(mSsl);
        mSsl = nullptr;
    }
    NetFunc::NN_SafeCloseFd(mFd);
}

SResult Sock::ValidateOptions()
{
    if (NN_UNLIKELY(mOptions.receiveBufSizeKB == 0)) {
        mOptions.receiveBufSizeKB = 1; /* min 1kB */
    }
    return SS_OK;
}

SResult Sock::SetSockOption(const SockWorkerOptions &workerOptions)
{
    /* fd is invalid */
    if (NN_UNLIKELY(mFd == -1)) {
        NN_LOG_ERROR("Failed to initialize sock " << mId << " as mFd is invalid");
        return SS_PARAM_INVALID;
    }

    mOptions.sendQueueSize = workerOptions.sendQueueSize;
    mOptions.sendZCopy = workerOptions.tcpSendZCopy;

    if (workerOptions.sockReceiveBufKB > 0) {
        /* set the size of receive buffer, which would compromise the performance of tcp */
        mOptions.receiveBufSizeKB = workerOptions.sockReceiveBufKB;
        auto value = workerOptions.sockReceiveBufKB * NN_NO1024;
        if (NN_UNLIKELY(setsockopt(mFd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to set receive buffer for sock " << mId << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SS_TCP_SET_OPTION_FAILED;
        }
    }

    if (workerOptions.sockSendBufKB > 0) {
        /* set the size of send buffer, which would compromise the performance of tcp */
        mOptions.sendBufSizeKB = workerOptions.sockSendBufKB;
        auto value = workerOptions.sockSendBufKB * NN_NO1024;
        if (NN_UNLIKELY(setsockopt(mFd, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to set send buffer for sock " << mId << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SS_TCP_SET_OPTION_FAILED;
        }
    } else {
        int sendBufSize = 0;
        socklen_t buffTypeSize = sizeof(sendBufSize);
        if (NN_UNLIKELY(getsockopt(mFd, SOL_SOCKET, SO_SNDBUF, &sendBufSize, &buffTypeSize) < 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to get send buffer for sock " << mId << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SS_TCP_GET_OPTION_FAILED;
        }
        if (NN_UNLIKELY(sendBufSize <= 0)) {
            NN_LOG_ERROR("send buffer size should be greater than 0 for sock" << mId);
            return SS_TCP_GET_OPTION_FAILED;
        }
        mOptions.sendBufSizeKB = sendBufSize / NN_NO1024;
    }

    /* stop here if uds */
    if (mType == SockType::SOCK_UDS) {
        return SS_OK;
    }

    /* following only for tcp */
    /* set keep alive */
    int value = 1;
    auto optSize = sizeof(workerOptions.keepaliveIdleTime);
    if (NN_UNLIKELY(setsockopt(mFd, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value)) < 0 ||
        setsockopt(mFd, IPPROTO_TCP, TCP_KEEPIDLE, &workerOptions.keepaliveIdleTime, optSize) < 0 ||
        setsockopt(mFd, IPPROTO_TCP, TCP_KEEPINTVL, &workerOptions.keepaliveProbeInterval, optSize) < 0 ||
        setsockopt(mFd, IPPROTO_TCP, TCP_KEEPCNT, &workerOptions.keepaliveProbeTimes, optSize) < 0)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set keepalive options for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    /* set no delay */
    if (workerOptions.tcpEnableNoDelay &&
        NN_UNLIKELY(setsockopt(mFd, SOL_TCP, TCP_NODELAY, reinterpret_cast<void *>(&value), sizeof(value)) != 0)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set no-delay for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    if (workerOptions.tcpUserTimeout < -1 || workerOptions.tcpUserTimeout > static_cast<int>(NN_NO1024)) {
        NN_LOG_ERROR(
            "tcpUserTimeout is invalid, it should be [-1, 1024], -1 means do not set, 0 means never timeout during io");
        return SS_PARAM_INVALID;
    }

    /* set timeout during io (ms) */
    if (workerOptions.tcpUserTimeout >= 0) {
        auto timeout = workerOptions.tcpUserTimeout * NN_NO1000;
        if (NN_UNLIKELY(setsockopt(mFd, SOL_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout)) != 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to set timeout for sock " << mId << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SS_TCP_SET_OPTION_FAILED;
        }
    }
    return SS_OK;
}

/* in order to have same function as other protocols which take -1 as blocking forever and 0 as return immediately,
 * while tcp acts contrarily, we switch it manually when setting. */
SResult Sock::SetBlockingSendTimeout(int32_t sendTimeout)
{
    if (sendTimeout == mSendTimeoutSecond) {
        return SS_OK;
    }
    mSendTimeoutSecond = sendTimeout;

    sendTimeout = sendTimeout > 0 ? sendTimeout : sendTimeout == 0 ? -1 : 0;
    struct timeval timeval {
        sendTimeout, 0
    };
    if (NN_UNLIKELY(setsockopt(mFd, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(timeval)) < 0)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set send timeout for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    return SS_OK;
}

SResult Sock::SetBlockingIo()
{
    int32_t value = NN_NO1;
    /* set blocking, fcntl result is 0 or -1 */
    if (NN_UNLIKELY((value = fcntl(mFd, F_GETFL, 0)) == -1)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to get control value for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    if (NN_UNLIKELY((value = fcntl(mFd, F_SETFL, static_cast<uint32_t>(value) & ~O_NONBLOCK)) == -1)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set control value for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }
    mTcpBlockingMode = true;

    return SS_OK;
}

SResult Sock::SetNonBlockingIo()
{
    int value = 1;
    /* set no-blocking */
    if (NN_UNLIKELY((value = fcntl(mFd, F_GETFL, 0)) == -1)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to get control value for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    if (NN_UNLIKELY((value = fcntl(mFd, F_SETFL, static_cast<uint32_t>(value) | O_NONBLOCK)) == -1)) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set control value for sock " << mId << ", errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SS_TCP_SET_OPTION_FAILED;
    }

    mTcpBlockingMode = false;

    return SS_OK;
}

SResult Sock::SetBlockingIo(UBSHcomEpOptions &epOptions)
{
    if (NN_UNLIKELY(SetBlockingIo() != SS_OK)) {
        return SS_TCP_SET_OPTION_FAILED;
    }
    mCbByWorkerInBlocking = epOptions.cbByWorkerInBlocking;
    if (NN_UNLIKELY(SetBlockingSendTimeout(epOptions.sendTimeout) != SS_OK)) {
        return SS_TCP_SET_OPTION_FAILED;
    }
    return SS_OK;
}

uint32_t Sock::GetSendQueueCount()
{
    return mSendQueue.Size();
}
}
}
