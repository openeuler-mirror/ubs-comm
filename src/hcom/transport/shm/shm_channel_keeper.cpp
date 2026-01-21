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

#include "shm_channel.h"
#include "shm_handle_fds.h"
#include "shm_channel_keeper.h"

namespace ock {
namespace hcom {
constexpr uint32_t MAX_EPOLL_SIZE = 4096 * 4; // 4096 hosts, 4 card per host
constexpr uint32_t MAX_EPOLL_WAIT_EVENTS = 16;
constexpr uint32_t EPOLL_WAIT_TIMEOUT = 1000; // 1 second

HResult ShmChannelKeeper::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        return SH_OK;
    }

    if (mMsgHandler == nullptr) {
        NN_LOG_ERROR("Message handler is not set in ShmChannelKeeper " << mName);
        return SH_PARAM_INVALID;
    }

    mEpollHandle = epoll_create(MAX_EPOLL_SIZE);
    if (mEpollHandle < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create epoll in ShmChannelKeeper " << mName << ", error "
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return SH_CREATE_KEEPER_EPOLL_FAILURE;
    }

    std::thread tmpThread(&ShmChannelKeeper::RunInThread, this);
    mEPollThread = std::move(tmpThread);
    std::string threadName = "ShmChKeeper" + std::to_string(mDriverIndex);
    if (pthread_setname_np(mEPollThread.native_handle(), threadName.c_str()) != 0) {
        NN_LOG_WARN("Failed to set name of ShmChannelKeeper working thread to " << threadName);
    }

    while (!mThreadStarted.load()) {
        usleep(NN_NO10);
    }

    mStarted = true;
    return SH_OK;
}

void ShmChannelKeeper::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        NN_LOG_WARN("ShmChannelKeeper " << mName << " has not been started");
        return;
    }

    StopInner();

    mStarted = false;
}

void ShmChannelKeeper::StopInner()
{
    mNeedStop = true;
    if (mEPollThread.native_handle()) {
        mEPollThread.join();
    }

    if (mEpollHandle != -1) {
        NetFunc::NN_SafeCloseFd(mEpollHandle);
        mEpollHandle = -1;
    }
}

HResult ShmChannelKeeper::AddShmChannel(const ShmChannelPtr &ch)
{
    NN_ASSERT_LOG_RETURN(ch.Get() != nullptr, SH_PARAM_INVALID)
    NN_ASSERT_LOG_RETURN(ch->UdsFD() != -1, SH_PARAM_INVALID)

    std::lock_guard<std::mutex> guard(mChMapMutex);
    auto iter = mShmChannels.find(ch->Id());
    if (iter != mShmChannels.end()) {
        NN_LOG_ERROR("Failed to add channel " << ch->Id() << " into ShmChannelKeeper " << mName <<
            " as already existed, remove it firstly.");
        return SH_DUP_CH_IN_KEEPER;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.ptr = ch.Get();
    if (epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, ch->UdsFD(), &ev) != 0) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to add channel " << ch->Id() << " into ShmChannelKeeper " << mName <<
            " as epoll add failed, errno:" << errno << " error:" <<
            NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SH_CH_ADD_FAILURE_IN_KEEPER;
    }

    if (!mShmChannels.emplace(ch->Id(), ch).second) {
        NN_LOG_ERROR("Failed to add channel " << ch->Id() << " into ShmChannelKeeper " << mName);
        epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, ch->UdsFD(), &ev);
        return SH_CH_ADD_FAILURE_IN_KEEPER;
    }

    return SH_OK;
}

HResult ShmChannelKeeper::RemoveShmChannel(uint64_t id)
{
    std::lock_guard<std::mutex> guard(mChMapMutex);
    auto iter = mShmChannels.find(id);
    if (iter == mShmChannels.end()) {
        NN_LOG_ERROR("No channel with " << id << " found in ShmChannelKeeper " << mName);
        return SH_CH_REMOVE_FAILURE_IN_KEEPER;
    }

    if (epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, iter->second->UdsFD(), nullptr) != 0) {
        char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to delete from epoll handle for channel " << id << " in ShmChannelKeeper " << mName <<
            ", errno:" << errno << " error:" << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        return SH_CH_REMOVE_FAILURE_IN_KEEPER;
    }

    iter->second->State().CAS(CH_NEW, CH_BROKEN);
    mShmChannels.erase(iter);

    return SH_OK;
}

void ShmChannelKeeper::RunInThread()
{
    mThreadStarted.store(true);
    NN_LOG_INFO("Shm channelKeeper " << mName << " working thread started");

    struct epoll_event ev[MAX_EPOLL_WAIT_EVENTS];
    while (!mNeedStop) {
        try {
            // do epoll wait
            int count = epoll_wait(mEpollHandle, ev, MAX_EPOLL_WAIT_EVENTS, EPOLL_WAIT_TIMEOUT);
            if (count > 0) {
                /* there are events, handle it */
                TRACE_DELAY_BEGIN(SHM_THREAD_CHANNEL_KEEPER);
                HandleEpollEvent(count, ev);
                TRACE_DELAY_END(SHM_THREAD_CHANNEL_KEEPER, 0);
            } else if (count == 0) {
                continue;
            } else if (errno == EINTR) {
                NN_LOG_WARN("Got errno EINTR in channelKeeper " << mName);
                continue;
            } else {
                /* error happens */
                char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to do epoll_wait in channelKeeper " << mName << ", errno:" << errno << " error:" <<
                    NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
                continue;
            }
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime error in ShmChannelKeeper::RunInThread '" << ex.what() <<
                "', ignore and continue");
        } catch (...) {
            NN_LOG_WARN("Got unknown error in ShmChannelKeeper::RunInThread, ignore and continue");
        }
    }

    NN_LOG_INFO("Shm channelKeeper " << mName << " working thread exiting");
}

HResult ShmChannelKeeper::ExchangeFdProcess(ShmChKeeperMsgHeader &header, const ShmChannelPtr &ch)
{
    HResult result = SH_OK;
    if (header.msgType == SEND_MR_FD) {
        int fds[NN_NO4] = {0};
        result = ShmHandleFds::ReceiveMsgFds(ch->UdsFD(), fds, NN_NO4);
        if (NN_UNLIKELY(result != SH_OK)) {
            NN_LOG_ERROR("Failed to receive the peer fd from the channel" << ch->Id());
            return result;
        }

        result = ch->AddMrFd(fds[NN_NO0]);
        if (NN_UNLIKELY(result != SH_OK)) {
            NN_LOG_ERROR("Successfully received mr to peer fd:" << fds[NN_NO0] << ", but the channel " << ch->Id() <<
                " cannot add peer fd to the fd queue, result is " << result);
            return result;
        }
    } else if (header.msgType == EXCHANGE_USER_FD) {
        int fds[NN_NO4] = {0};
        result = ShmHandleFds::ReceiveMsgFds(ch->UdsFD(), fds, NN_NO4);
        if (NN_UNLIKELY(result != SH_OK)) {
            NN_LOG_ERROR("Failed to receive the peer fds from the channel" << ch->Id());
            return result;
        }

        if (header.dataSize > NN_NO4) {
            NN_LOG_ERROR("Fd length " << header.dataSize << " is invalid ");
            return SH_PARAM_INVALID;
        }

        result = ch->AddUserFds(fds, header.dataSize);
        if (NN_UNLIKELY(result != SH_OK)) {
            NN_LOG_ERROR("Failed to add fds to channel " << ch->Id() << " fd queue, result is " << result);
            return result;
        }
    }
    return result;
}

void ShmChannelKeeper::HandleEpollEvent(uint32_t eventCount, struct epoll_event *events)
{
    if (NN_UNLIKELY(events == nullptr)) {
        return;
    }

    ShmChKeeperMsgHeader header {};
    ShmChannelPtr ch;

    for (uint32_t i = 0; i < eventCount; ++i) {
        if (!(events[i].events & EPOLLIN)) {
            continue;
        }

        ch = static_cast<ShmChannel *>(events[i].data.ptr);
        /* read header in blocking */
        auto result = ::read(ch->UdsFD(), &header, sizeof(ShmChKeeperMsgHeader));
        if (result <= 0) {
            /* reset by peer */
            header.msgType = ShmChKeeperMsgType::RESET_BY_PEER;
            header.dataSize = 0;

            (void)RemoveShmChannel(ch->Id());
        } else if (static_cast<uint32_t>(result) != sizeof(ShmChKeeperMsgHeader)) {
            NN_LOG_WARN("Un-reachable path");
            continue;
        } else {
            if (header.msgType < GET_MR_FD || header.msgType > EXCHANGE_USER_FD) {
                NN_LOG_WARN("Un-reachable path, msgType is incorrect");
                continue;
            }
        }

        if (header.msgType == SEND_MR_FD || header.msgType == EXCHANGE_USER_FD) {
            if (NN_LIKELY(ExchangeFdProcess(header, ch) != SH_PEER_FD_ERROR)) {
                continue;
            }
            // peer fd error should process ep error
            header.msgType = ShmChKeeperMsgType::RESET_BY_PEER;
            header.dataSize = 0;
            (void)RemoveShmChannel(ch->Id());
        }

        try {
            mMsgHandler(header, ch);
        } catch (std::runtime_error &ex) {
            NN_LOG_WARN("Got runtime incorrect signal in mMsgHandler " << ex.what() << " in , ignored");
        } catch (...) {
            NN_LOG_WARN("Got unknown signal in mMsgHandler , ignored");
        }
    }
}
}
}