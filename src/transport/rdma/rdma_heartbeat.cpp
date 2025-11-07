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
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <unordered_set>

#include "rdma_heartbeat.h"
namespace ock {
namespace hcom {
constexpr uint32_t MAX_EPOLL_SIZE = 4096 * 4; // 4096 hosts, 4 card per host
constexpr uint32_t MAX_EPOLL_WAIT_EVENTS = 16;
constexpr uint32_t EPOLL_WAIT_TIMEOUT = 1000; // 1 second

RIPDeviceHeartbeatManager::RIPDeviceHeartbeatManager(const std::string &name) : mName(name) {}

NResult RIPDeviceHeartbeatManager::Initialize()
{
    if (mEpollHandle > 0) {
        return NN_OK;
    }

    if (mConnBrokenCheckHandler == nullptr) {
        NN_LOG_ERROR("ConnBrokenCheckHandler is not set in RIPDeviceHeartbeatManager " << mName);
        return NN_PARAM_INVALID;
    }

    if (mConnBrokenPostHandler == nullptr) {
        NN_LOG_ERROR("ConnBrokenPostHandler is not set in RIPDeviceHeartbeatManager " << mName);
        return NN_PARAM_INVALID;
    }

    int epollHandle = epoll_create(MAX_EPOLL_SIZE);
    if (epollHandle < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create epoll in RIPDeviceHeartbeatManager " << mName << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_HEARTBEAT_CREATE_EPOLL_FAILED;
    }

    mEpollHandle = epollHandle;
    mStarted.store(false);
    return NN_OK;
}

void RIPDeviceHeartbeatManager::UnInitialize()
{
    if (mEpollHandle == -1) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(mMutex);
        mIpFdMap.clear();
        mFdIpMap.clear();
    }
    close(mEpollHandle);
    mEpollHandle = -1;
}

NResult RIPDeviceHeartbeatManager::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted.load()) {
        NN_LOG_INFO("RIPDeviceHeartbeatManager " << mName << " already started");
        return NN_OK;
    }

    std::thread tmpThread(&RIPDeviceHeartbeatManager::RunInThread, this);
    mWorkingThread = std::move(tmpThread);
    std::string threadName = "IpHeartbeat";
    if (pthread_setname_np(mWorkingThread.native_handle(), threadName.c_str()) != 0) {
        NN_LOG_WARN("Failed to set name of RIPDeviceHeartbeatManager working thread");
    }

    while (!mStarted.load()) {
        usleep(NN_NO10);
    }

    return NN_OK;
}

void RIPDeviceHeartbeatManager::Stop()
{
    mNeedStop = true;
    if (mWorkingThread.native_handle()) {
        mWorkingThread.join();
    }
}

void RIPDeviceHeartbeatManager::RunInThread()
{
    mStarted.store(true);
    NN_LOG_INFO("RIPDeviceHeartbeatManager " << mName << " working thread started");
    struct epoll_event ev[MAX_EPOLL_WAIT_EVENTS];
    while (!mNeedStop) {
        try {
            // do epoll wait
            int count = epoll_wait(mEpollHandle, ev, MAX_EPOLL_WAIT_EVENTS, EPOLL_WAIT_TIMEOUT);
            if (count <= 0) {
                continue;
            }

            HandleEpollEvent(count, ev);
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime error in RIPDeviceHeartbeatManager::RunInThread '" << ex.what() <<
                "', ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown error in RIPDeviceHeartbeatManager::RunInThread, ignore and continue");
        }
    }
    NN_LOG_INFO("RIPDeviceHeartbeatManager " << mName << " working thread exiting");
}

void RIPDeviceHeartbeatManager::HandleEpollEvent(uint32_t eventCount, struct epoll_event *events)
{
    if (events == nullptr) {
        return;
    }

    std::unordered_set<int> fds;
    fds.reserve(eventCount);
    for (uint32_t i = 0; i < eventCount; i++) {
        if (!(events[i].events & EPOLLIN)) {
            continue;
        }

        try {
            if (!mConnBrokenCheckHandler(events[i].data.fd)) {
                fds.emplace(static_cast<int>(events[i].data.fd));
            }
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime error in mConnBrokenCheckHandler " << ex.what() << ", ignored");
        } catch (...) {
            NN_LOG_WARN("Got unknown error in mConnBrokenCheckHandler , ignored");
        }
    }

    if (fds.empty()) {
        return;
    }

    // remove related fd and ip from maps
    for (auto item : fds) {
        RemoveByFD(item);
    }

    // call post handler function
    for (auto item : fds) {
        try {
            mConnBrokenPostHandler(item);
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime error in mConnBrokenPostHandler " << ex.what() << ", ignored");
        } catch (...) {
            NN_LOG_WARN("Got unknown error in mConnBrokenPostHandler , ignored");
        }
    }
}

NResult RIPDeviceHeartbeatManager::AddNewIP(const std::string &ip, int fd)
{
    if (fd < 0) {
        NN_LOG_ERROR("Failed to add new IP and fd to RIPDeviceHeartbeatManager " << mName << " as fd is invalid");
        return NN_PARAM_INVALID;
    }

    // set keep alive params
    int value = 1;
    RKeepaliveConfig tmpConfig = mKeepaliveConfig;
    size_t optSize = sizeof(tmpConfig.probeTimes);
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value)) < 0 ||
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &tmpConfig.idleTime, optSize) < 0 ||
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &tmpConfig.probeInterval, optSize) < 0 ||
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &tmpConfig.probeTimes, optSize) < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set keepalive option for " << ip << "-" << fd << " in RIPDeviceHeartbeatManager " <<
            mName << ", errno:" << errno << " error:" << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_HEARTBEAT_SET_SOCKET_OPT_FAILED;
    }

    {
        std::lock_guard<std::mutex> guard(mMutex);
        auto iter = mIpFdMap.find(ip);
        if (iter != mIpFdMap.end()) {
            NN_LOG_ERROR("Failed to add " << ip << " into RIPDeviceHeartbeatManager " << mName <<
                " as already existed, remove it firstly.");
            return NN_HEARTBEAT_IP_ALREADY_EXISTED;
        }

        struct epoll_event ev {};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, fd, &ev) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to add " << ip << " into RIPDeviceHeartbeatManager " << mName <<
                " as epoll add failed, error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return NN_HEARTBEAT_IP_ADD_EPOLL_FAILED;
        }

        if (!mIpFdMap.emplace(ip, fd).second || !mFdIpMap.emplace(fd, ip).second) {
            NN_LOG_ERROR("Failed to add " << ip << " into RIPDeviceHeartbeatManager " << mName);
            return NN_HEARTBEAT_IP_ADD_FAILED;
        }
    }

    return NN_OK;
}

NResult RIPDeviceHeartbeatManager::GetFdByIP(const std::string &ip, int &fd)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mIpFdMap.find(ip);
    if (iter == mIpFdMap.end()) {
        NN_LOG_ERROR("No ip " << ip << " found from RIPDeviceHeartbeatManager " << mName);
        return NN_HEARTBEAT_IP_NO_FOUND;
    }

    fd = iter->second;
    return NN_OK;
}

NResult RIPDeviceHeartbeatManager::RemoveIP(const std::string &ip)
{
    int fd = -1;
    {
        std::lock_guard<std::mutex> guard(mMutex);
        auto iter = mIpFdMap.find(ip);
        if (iter == mIpFdMap.end()) {
            NN_LOG_ERROR("No ip " << ip << " found from RIPDeviceHeartbeatManager " << mName);
            return NN_HEARTBEAT_IP_NO_FOUND;
        }

        fd = iter->second;
        mIpFdMap.erase(iter);
        mFdIpMap.erase(fd);
    }

    if (epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, fd, nullptr) != 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to delete from epoll handle for " << ip << "-" << fd << " in RIPDeviceHeartbeatManager " <<
            mName << ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_HEARTBEAT_IP_REMOVE_EPOLL_FAILED;
    }
    return NN_OK;
}

NResult RIPDeviceHeartbeatManager::RemoveByFD(int fd)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> guard(mMutex);
        auto iter = mFdIpMap.find(fd);
        if (iter == mFdIpMap.end()) {
            NN_LOG_ERROR("No fd " << fd << " found from RIPDeviceHeartbeatManager " << mName);
            return NN_HEARTBEAT_IP_NO_FOUND;
        }

        ip = iter->second;
        mFdIpMap.erase(iter);
        mIpFdMap.erase(ip);
    }

    if (epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, fd, nullptr) != 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to delete from epoll handle for " << ip << "-" << fd << " in RIPDeviceHeartbeatManager " <<
            mName << ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return NN_HEARTBEAT_IP_REMOVE_EPOLL_FAILED;
    }

    return NN_OK;
}

bool RIPDeviceHeartbeatManager::DefaultConnBrokenCheckCB(int fd)
{
    char data[1];
    auto result = recv(fd, data, 1, MSG_DONTWAIT);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // connection is still ok
            return true;
        }

        // connection is wrong
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_INFO("DefaultConnBrokenCheckCB connection is wrong, fd " << fd << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return false;
    } else if (result == 0) {
        NN_LOG_INFO("DefaultConnBrokenCheckCB connection is broken, fd " << fd);
        return false; // connection really broken
    } else {
        return true;
    }
}

void RIPDeviceHeartbeatManager::DefaultConnBrokenPostCB(int fd)
{
    NN_LOG_INFO("DefaultConnBrokenPostCB close fd");
    close(fd);
}
}
}