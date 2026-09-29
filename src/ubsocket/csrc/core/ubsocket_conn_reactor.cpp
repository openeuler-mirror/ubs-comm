/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket_conn_reactor.h"

#include <pthread.h>
#include <sys/eventfd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>

#include "common/ubsocket_common_includes.h"
#include "ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {

/* C++11 static constexpr ODR-use 防护：类外定义 */
constexpr int ConnReactorThread::kPollTimeoutMs;
constexpr uint64_t ConnReactorThread::kEventQueueCapacity;
constexpr uint64_t ConnReactorThread::kScanIntervalMs;

/* ======================== ConnEventFdTable ======================== */

void ConnEventFdTable::Register(int event_fd, int thread_index)
{
    std::lock_guard<std::mutex> locker(mutex_);
    for (auto &entry : entries_) {
        if (entry.event_fd == event_fd) {
            entry.thread_index = thread_index;
            return;
        }
    }
    Entry entry;
    entry.event_fd = event_fd;
    entry.thread_index = thread_index;
    entries_.push_back(entry);
}

void ConnEventFdTable::Unregister(int event_fd)
{
    std::lock_guard<std::mutex> locker(mutex_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->event_fd == event_fd) {
            entries_.erase(it);
            return;
        }
    }
}

int ConnEventFdTable::GetLeastLoadedIndex()
{
    std::lock_guard<std::mutex> locker(mutex_);
    int best_index = -1;
    uint32_t best_load = UINT32_MAX;
    for (auto &entry : entries_) {
        if (entry.load < best_load) {
            best_load = entry.load;
            best_index = entry.thread_index;
        }
    }
    return best_index;
}

void ConnEventFdTable::IncLoad(int event_fd)
{
    std::lock_guard<std::mutex> locker(mutex_);
    for (auto &entry : entries_) {
        if (entry.event_fd == event_fd) {
            entry.load++;
            return;
        }
    }
}

void ConnEventFdTable::DecLoad(int event_fd)
{
    std::lock_guard<std::mutex> locker(mutex_);
    for (auto &entry : entries_) {
        if (entry.event_fd == event_fd) {
            if (entry.load > 0) {
                entry.load--;
            }
            return;
        }
    }
}

/* ======================== ConnReactorThread ======================== */

Result ConnReactorThread::Init(int thread_index)
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return UBS_OK; /* 幂等 */
    }

    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        UBS_VLOG_ERR("ConnReactorThread eventfd() failed, errno: %d, errmsg: %s\n", errno, Func::Error2Str(errno));
        started_.store(false);
        return UBS_ERROR;
    }
    event_fd_ = event_fd;

    poll_fds_.clear();
    struct pollfd pfd = {event_fd_, POLLIN, 0};
    poll_fds_.push_back(pfd);
    ConnEventFdTable::Instance().Register(event_fd_, thread_index);
    stopped_.store(false);
    last_scan_ms_ = SocketConnHelper::GetTimeMs();

    try {
        thread_ = std::thread(&ConnReactorThread::RunLoop, this);
    } catch (...) {
        UBS_VLOG_ERR("ConnReactorThread create thread failed, errno: %d\n", errno);
        ConnEventFdTable::Instance().Unregister(event_fd_);
        poll_fds_.clear();
        LibcApi::close(event_fd_);
        event_fd_ = -1;
        started_.store(false);
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result ConnReactorThread::AddConnection(int fd, const std::shared_ptr<UBConnOpsBase> &ops, const DoneCallback &done_cb)
{
    if (!started_.load(std::memory_order_acquire) || stopped_.load(std::memory_order_acquire)) {
        UBS_VLOG_ERR("ConnReactorThread not running, fd: %d\n", fd);
        return UBS_ERROR;
    }

    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        if (stopped_.load(std::memory_order_acquire)) {
            return UBS_ERROR;
        }
        if (conn_ops_map_.find(fd) != conn_ops_map_.end()) {
            UBS_VLOG_ERR("ConnReactorThread fd(%d) already in map\n", fd);
            return UBS_ERROR;
        }
        ConnEntry entry;
        entry.ops = ops;
        entry.done_cb = done_cb;
        conn_ops_map_.emplace(fd, std::move(entry));
    }

    ConnEventFdTable::Instance().IncLoad(event_fd_);

    /* 本线程在 fd 上做非阻塞 recv（首帧可能尚未到达），须在唤醒本线程前切换为非阻塞。
     * 阻塞态恢复由 ops 在收尾阶段按需完成。 */
    if (SocketConnHelper::SetNonBlocking(fd) != 0) {
        UBS_VLOG_ERR("ConnReactorThread set nonblocking failed, fd: %d, errno: %d\n", fd, errno);
        {
            std::lock_guard<std::mutex> locker(conn_map_mutex_);
            conn_ops_map_.erase(fd);
        }
        ConnEventFdTable::Instance().DecLoad(event_fd_);
        return UBS_ERROR;
    }

    if (!event_queue_.Push(fd)) {
        UBS_VLOG_ERR("ConnReactorThread event queue full, fd: %d\n", fd);
        {
            std::lock_guard<std::mutex> locker(conn_map_mutex_);
            conn_ops_map_.erase(fd);
        }
        ConnEventFdTable::Instance().DecLoad(event_fd_);
        return UBS_ERROR;
    }

    uint64_t notification = 1;
    if (eventfd_write(event_fd_, notification) < 0) {
        UBS_VLOG_ERR("ConnReactorThread wakeup eventfd_write failed, fd: %d, errno: %d\n", fd, errno);
    }
    return UBS_OK;
}

void ConnReactorThread::Cancel(int fd)
{
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }
    /* 先同步置 Abort()，让 poll 线程当前/后续 Execute 立即停止推进 */
    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        auto it = conn_ops_map_.find(fd);
        if (it != conn_ops_map_.end() && it->second.ops != nullptr) {
            it->second.ops->Abort();
        }
    }
    /* 再异步投递，由本线程 poll 循环安全地移出监听数组与映射 */
    while (!cancel_queue_.Push(fd)) {
        if (stopped_.load(std::memory_order_acquire)) {
            return;
        }
        std::this_thread::yield();
    }
    uint64_t notification = 1;
    if (eventfd_write(event_fd_, notification) < 0) {
        UBS_VLOG_ERR("ConnReactorThread Cancel eventfd_write failed, fd: %d, errno: %d\n", fd, errno);
    }
}

void ConnReactorThread::RunLoop()
{
    pthread_setname_np(pthread_self(), "ubs_conn_poll");
    UBS_VLOG_DEBUG("ConnReactorThread running, event_fd: %d\n", event_fd_);

    while (!stopped_.load(std::memory_order_acquire)) {
        uint64_t now_ms = SocketConnHelper::GetTimeMs();
        if (now_ms - last_scan_ms_ >= kScanIntervalMs) {
            last_scan_ms_ = now_ms;
            ScanTimeoutConnections();
        }

        int poll_ret = LibcApi::poll(poll_fds_.data(), static_cast<nfds_t>(poll_fds_.size()), kPollTimeoutMs);
        if (poll_ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (stopped_.load(std::memory_order_acquire)) {
                break;
            }
            UBS_VLOG_ERR("ConnReactorThread poll failed, errno: %d, errmsg: %s\n", errno, Func::Error2Str(errno));
            continue;
        }
        if (poll_ret == 0) {
            continue;
        }

        uint64_t begin_ms = SocketConnHelper::GetTimeMs();
        uint64_t effective_now_ms = (begin_ms > busy_ms_) ? (begin_ms - busy_ms_) : 0;

        std::vector<int> ready_fds;
        ready_fds.reserve(poll_fds_.size());
        for (auto &pfd : poll_fds_) {
            if (pfd.fd < 0 || pfd.revents == 0) {
                continue;
            }
            ready_fds.push_back(pfd.fd);
        }

        for (int fd : ready_fds) {
            if (fd == event_fd_) {
                HandleEventFd(effective_now_ms);
            } else {
                HandleSocketFd(fd, effective_now_ms);
            }
        }

        /* 有效时钟记账：本段处理耗时（含内联重活）不计入任何连接的空闲；poll 等待段照常计入空闲 */
        busy_ms_ += SocketConnHelper::GetTimeMs() - begin_ms;
    }
    UBS_VLOG_DEBUG("ConnReactorThread exit, event_fd: %d\n", event_fd_);
}

void ConnReactorThread::HandleEventFd(uint64_t effective_now_ms)
{
    /* 消费计数（非阻塞 event fd，读到 EAGAIN 即空） */
    uint64_t count = 0;
    while (LibcApi::read(event_fd_, &count, sizeof(count)) == static_cast<ssize_t>(sizeof(count))) {
        if (count == 0) {
            break;
        }
    }

    /* 先处理取消（可能已 close 的 fd 不应再被推进），再处理新连接任务 */
    HandleCancelQueue();

    /* event_queue_：分发器投递的新连接任务（首帧已到则推进，否则保持监听等 POLLIN） */
    int fd = -1;
    while (event_queue_.Pop(fd)) {
        AddPollFd(fd, POLLIN);
        HandleSocketFd(fd, effective_now_ms);
    }
}

void ConnReactorThread::HandleCancelQueue()
{
    int fd = -1;
    while (cancel_queue_.Pop(fd)) {
        RemovePollFd(fd);
        bool erased = false;
        {
            std::lock_guard<std::mutex> locker(conn_map_mutex_);
            erased = conn_ops_map_.erase(fd) > 0;
        }
        if (erased) {
            ConnEventFdTable::Instance().DecLoad(event_fd_);
        }
    }
}

void ConnReactorThread::HandleSocketFd(int fd, uint64_t effective_now_ms)
{
    std::shared_ptr<UBConnOpsBase> ops;
    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        auto it = conn_ops_map_.find(fd);
        if (it == conn_ops_map_.end()) {
            return;
        }
        ops = it->second.ops;
    }

    ConnStageResult result = ConnStageResult::kFailed;
    try {
        result = ops->Execute(effective_now_ms);
    } catch (...) {
        UBS_VLOG_ERR("ConnReactorThread Execute exception, fd: %d\n", fd);
    }
    if (result == ConnStageResult::kPending) {
        /* EAGAIN 续发时追加 POLLOUT，可写后唤醒本线程重入续发；其余半包场景仅监听可读 */
        short events = ops->WantsWritable() ? static_cast<short>(POLLIN | POLLOUT) : static_cast<short>(POLLIN);
        AddPollFd(fd, events);
        return; /* fd 保留在监听数组 */
    }
    FinishConnection(fd, result); /* kDone / kFailed */
}

void ConnReactorThread::AddPollFd(int fd, short events)
{
    for (auto &pfd : poll_fds_) {
        if (pfd.fd == fd) {
            pfd.events = events; /* 状态推进时更新关注事件（如 EAGAIN 续发需追加 POLLOUT） */
            pfd.revents = 0;
            return;
        }
    }
    for (auto &pfd : poll_fds_) {
        if (pfd.fd < 0) { /* 复用惰性删除的槽位 */
            pfd.fd = fd;
            pfd.events = events;
            pfd.revents = 0;
            return;
        }
    }
    struct pollfd pfd = {fd, events, 0};
    poll_fds_.push_back(pfd);
}

void ConnReactorThread::RemovePollFd(int fd)
{
    for (auto &pfd : poll_fds_) {
        if (pfd.fd == fd) {
            pfd.fd = -1;
            pfd.events = 0;
            pfd.revents = 0;
            return;
        }
    }
}

void ConnReactorThread::FinishConnection(int fd, ConnStageResult result)
{
    RemovePollFd(fd);

    ConnEntry entry;
    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        auto it = conn_ops_map_.find(fd);
        if (it == conn_ops_map_.end()) {
            return;
        }
        entry = std::move(it->second);
        conn_ops_map_.erase(it);
    }
    ConnEventFdTable::Instance().DecLoad(event_fd_);

    if (result == ConnStageResult::kDone) {
        entry.ops->OnConnDone();
        if (entry.done_cb) {
            entry.done_cb(fd, entry.ops->Addr(), entry.ops->AddrLen());
        }
    } else {                /* kFailed */
        entry.ops->Abort(); /* 阻止后续阶段推进（重活已内联完成，无需等待在途重活） */
        entry.ops->OnConnFailed();
        if (entry.ops->CloseOnFailure()) {
            LibcApi::close(fd);
        }
        UBS_VLOG_WARN("ConnReactorThread connection failed, fd: %d\n", fd);
    }
    /* entry（含 ops/done_cb）在此析构 */
}

uint64_t ConnReactorThread::EffectiveNow() const
{
    uint64_t now_ms = SocketConnHelper::GetTimeMs();
    return (now_ms > busy_ms_) ? (now_ms - busy_ms_) : 0;
}

void ConnReactorThread::ScanTimeoutConnections()
{
    if (conn_ops_map_.empty()) {
        return;
    }

    /* 用有效时钟：扣除本线程处理耗时（含内联重活），避免线程忙于其它连接时误杀等待中的连接 */
    uint64_t effective_now_ms = EffectiveNow();
    struct TimeoutEntry {
        int fd;
        const char *state;
        uint64_t idle_ms;
    };
    std::vector<TimeoutEntry> timed_out;
    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        for (auto &kv : conn_ops_map_) {
            uint64_t last_active = kv.second.ops->LastActiveMs();
            /* last_active==0 表示刚提交尚未首帧驱动，跳过防御误杀 */
            if (last_active != 0 && (effective_now_ms - last_active) > kv.second.ops->IdleTimeoutMs()) {
                kv.second.ops->SetFailureErrno(ETIMEDOUT);
                TimeoutEntry entry;
                entry.fd = kv.first;
                entry.state = kv.second.ops->StateName();
                entry.idle_ms = effective_now_ms - last_active;
                timed_out.push_back(entry);
            }
        }
    }
    for (auto &entry : timed_out) {
        UBS_VLOG_WARN("ConnReactorThread connection idle timeout, fd: %d, state: %s, idle_ms: %llu\n", entry.fd,
                      entry.state, static_cast<unsigned long long>(entry.idle_ms));
        FinishConnection(entry.fd, ConnStageResult::kFailed);
    }
}

void ConnReactorThread::Stop()
{
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return; /* 幂等 */
    }
    stopped_.store(true, std::memory_order_release);

    uint64_t notification = 1;
    if (event_fd_ >= 0) {
        (void)eventfd_write(event_fd_, notification); /* 唤醒 poll 退出 */
    }
    if (thread_.joinable()) {
        thread_.join();
    }

    std::vector<int> pending_fds;
    {
        std::lock_guard<std::mutex> locker(conn_map_mutex_);
        for (auto &kv : conn_ops_map_) {
            kv.second.ops->Abort();
            kv.second.ops->OnConnFailed();
            if (kv.second.ops->CloseOnFailure()) {
                pending_fds.push_back(kv.first);
            }
        }
        conn_ops_map_.clear();
    }
    for (int fd : pending_fds) {
        LibcApi::close(fd);
    }
    ConnEventFdTable::Instance().Unregister(event_fd_);
    if (event_fd_ >= 0) {
        LibcApi::close(event_fd_);
        event_fd_ = -1;
    }
    poll_fds_.clear();
}

/* ======================== ConnDispatcher ======================== */

Result ConnDispatcher::Start(uint32_t num_threads)
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return UBS_OK; /* 幂等 */
    }
    if (num_threads == 0) {
        UBS_VLOG_ERR("ConnDispatcher thread count is zero\n");
        started_.store(false);
        return UBS_INVALID_PARAM;
    }

    /* 每 thread 创建专属 event fd 并注册进 ConnEventFdTable，随后进入 poll 循环等待分发器唤醒。
     * 重活内联在 poll 线程执行，不使用共享 ExecutorService；poll 线程数须满足吞吐约束
     * （单线程在途连接数 × 单次重活耗时 < 对端单阶段预算），高并发场景需调大 poll 线程数。 */
    for (uint32_t i = 0; i < num_threads; i++) {
        auto thread = std::make_shared<ConnReactorThread>();
        Result ret = thread->Init(static_cast<int>(i));
        if (ret != UBS_OK) {
            UBS_VLOG_ERR("ConnDispatcher init thread %u failed, ret: %d\n", i, ret);
            Stop();
            return ret;
        }
        std::lock_guard<std::mutex> locker(threads_mutex_);
        threads_.push_back(thread);
    }
    UBS_VLOG_DEBUG("ConnDispatcher started, poll threads: %u\n", num_threads);
    return UBS_OK;
}

void ConnDispatcher::Stop()
{
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return; /* 幂等 */
    }

    std::vector<std::shared_ptr<ConnReactorThread>> saved;
    {
        std::lock_guard<std::mutex> locker(threads_mutex_);
        saved.swap(threads_);
    }
    /* 逆序停止 poll 线程（join），确保退出后不再访问连接状态 */
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        (*it)->Stop();
    }
}

Result ConnDispatcher::Submit(int fd, const struct sockaddr &addr, socklen_t addr_len,
                              const ConnReactorThread::DoneCallback &done_cb, const std::shared_ptr<UBConnOpsBase> &ops)
{
    if (!started_.load(std::memory_order_acquire)) {
        UBS_VLOG_ERR("ConnDispatcher not started, fd: %d\n", fd);
        return UBS_ERROR;
    }
    if (ops == nullptr) {
        UBS_VLOG_ERR("ConnDispatcher submit null ops, fd: %d\n", fd);
        return UBS_INVALID_PARAM;
    }

    int thread_index = ConnEventFdTable::Instance().GetLeastLoadedIndex();
    std::shared_ptr<ConnReactorThread> thread;
    {
        std::lock_guard<std::mutex> locker(threads_mutex_);
        if (thread_index >= 0 && thread_index < static_cast<int>(threads_.size())) {
            thread = threads_[thread_index];
        }
    }
    if (thread == nullptr) {
        UBS_VLOG_ERR("ConnDispatcher find thread by index(%d) failed, fd: %d\n", thread_index, fd);
        return UBS_ERROR;
    }
    return thread->AddConnection(fd, ops, done_cb);
}

void ConnDispatcher::Cancel(int fd)
{
    if (!started_.load(std::memory_order_acquire)) {
        return;
    }
    std::vector<std::shared_ptr<ConnReactorThread>> saved;
    {
        std::lock_guard<std::mutex> locker(threads_mutex_);
        saved = threads_;
    }
    /* 连接固定在某一线程，遍历让其命中者处理；未命中的线程 Cancel 为 no-op */
    for (auto &thread : saved) {
        thread->Cancel(fd);
    }
}

} // namespace ubs
} // namespace ock
