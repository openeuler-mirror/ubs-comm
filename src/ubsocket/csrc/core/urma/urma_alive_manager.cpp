/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "urma_socket.h"
#include "urma_alive_manager.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaAliveManager::Init(const UrmaAliveConf &options_)
{
    if (mRunning.exchange(true)) {
        return UBS_OK;
    }
    if (options_.keepalive_time == 0) {
        UBS_VLOG_DEBUG("UrmaAlive is disabled (UBS_KEEP_ALIVE_TIME=0)\n");
        mRunning.store(false);
        return UBS_OK;
    }
    sem_init(&mSem, 0, 0);
    options = options_;
    mWorkerThread = std::thread(&UrmaAliveManager::RunInThread, this);

    UBS_VLOG_DEBUG("UrmaAliveManager started, keep_alive_time=%u s, intvl=%u s, probes=%u\n",
                  options.keepalive_time, options.keepalive_intvl, options.keepalive_probes);
    return UBS_OK;
}

void UrmaAliveManager::UnInit()
{
    if (!mRunning.exchange(false)) {
        return;
    }
    sem_post(&mSem);
    if (mWorkerThread.joinable()) {
        try {
            mWorkerThread.join();
        } catch (...) {
            UBS_VLOG_ERR("Exception caught during keep_alive thread join\n");
        }
    }
    sem_destroy(&mSem);

    UBS_VLOG_DEBUG("UrmaAliveManager is stopped\n");
}

void UrmaAliveManager::HandleAckPacket(Socket *socket)
{
    if (!KeepAliveEnable()) {
        return;
    }
    if (socket == nullptr) {
        UBS_VLOG_ERR("UrmaAlive handle ack packet error for socket is nullptr.");
        return;
    }
    if (!mRunning.load()) {
        UBS_VLOG_ERR("UrmaAlive handle ack packet error for not running.");
        return;
    }
    auto *urmaSocket = RefStaticCastToRaw<UrmaSocket, Socket>(socket);
    urmaSocket->GetAliveInfo()->ResetProbes();
}

Result UrmaAliveManager::SendProbePacket(Socket *socket)
{
    if (socket == nullptr) {
        UBS_VLOG_ERR("UrmaAlive send probe packet error for socket is nullptr.");
        return UBS_ERROR;
    }
    auto *urmaSocket = RefStaticCastToRaw<UrmaSocket, Socket>(socket);
    if (!urmaSocket->CanKeepAlive()) {
        return UBS_ERROR;
    }
    auto info = urmaSocket->GetAliveInfo();
    if (info == nullptr) {
        return UBS_ERROR;
    }
    info->IncProbesSent();
    return urmaSocket->PostCtrlOnlyNotification(IOOpcode::KEEP_ALIVE, OneSideSenderType::KEEP_ALIVE_PROBE);
}

Result UrmaAliveManager::SendResponsePacket(Socket *socket)
{
    if (socket == nullptr) {
        UBS_VLOG_ERR("UrmaAlive send response packet error for socket is nullptr.");
        return UBS_ERROR;
    }
    auto *urmaSocket = RefStaticCastToRaw<UrmaSocket, Socket>(socket);
    return urmaSocket->PostCtrlOnlyNotification(IOOpcode::KEEP_ALIVE_ACK, OneSideSenderType::KEEP_ALIVE_PROBE);
}

void UpdateTimespec(struct timespec *t, uint64_t ms)
{
    t->tv_nsec += ms * UBSOCKET_MS_TO_NS;
    t->tv_sec += t->tv_nsec / UBSOCKET_S_TO_NS;
    t->tv_nsec %= UBSOCKET_S_TO_NS;
}

void UrmaAliveManager::RunInThread()
{
    pthread_setname_np(pthread_self(), "ub_keep_alive");
    struct timespec exceptTime;
    // 初始化wait超时时间
    clock_gettime(CLOCK_REALTIME, &exceptTime);
    UpdateTimespec(&exceptTime, mIntervalMs);

    while (mRunning) {
        // sleep mIntervalMs
        sem_timedwait(&mSem, &exceptTime);
        if (!mRunning) {
            break;
        }
        ArraySet<Socket>::GetInstance().ForEach(
            [&](int fd, Socket *socket) { CheckSocket(socket, ubsocket_get_timeNs()); });

        // 刷新wait的超时时间
        clock_gettime(CLOCK_REALTIME, &exceptTime);
        UpdateTimespec(&exceptTime, mIntervalMs);
    }
}

void UrmaAliveManager::CheckSocket(Socket *socket, uint64_t now)
{
    if (socket == nullptr || socket->Type() != SocketType::SOCK_TYPE_URMA) {
        return;
    }
    auto *urmaSocket = RefStaticCastToRaw<UrmaSocket, Socket>(socket);
    // The acquire in CanKeepAlive observes resources published after peer import/ack.
    if (!urmaSocket->CanKeepAlive()) {
        return;
    }
    auto info = urmaSocket->GetAliveInfo();
    if (info == nullptr) {
        return;
    }
    if (info->GetProbesSent() >= options.keepalive_probes) {
        UBS_VLOG_WARN("UrmaAlive reaching the set threshold: %d, closing socket fd: %d\n", options.keepalive_probes,
                      urmaSocket->raw_socket_);
        urmaSocket->OnPeerReset();
        return;
    }
    const uint64_t lastActive = info->GetLastActiveTime();
    const uint64_t lastSend = info->GetProbesSendTime();
    const uint64_t aliveTimeNs = options.keepalive_time * UBSOCKET_S_TO_NS;
    const uint64_t aliveIntvlNs = options.keepalive_intvl * UBSOCKET_S_TO_NS;
    // New sockets and concurrent activity can have timestamps newer than this scan.
    if (now < lastActive || now < lastSend || now - lastActive < aliveTimeNs || now - lastSend < aliveIntvlNs) {
        return;
    }
    SendProbePacket(urmaSocket);
}

bool UrmaAliveManager::KeepAliveEnable()
{
    return options.keepalive_time > 0;
}

} // namespace urma
} // namespace ubs
} // namespace ock
