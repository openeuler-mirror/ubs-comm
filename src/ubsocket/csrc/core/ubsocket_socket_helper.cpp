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
#include "ubsocket_socket_helper.h"

#include "core/ubsocket_event_epoll.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "common/ubsocket_lock.h"
#include "common/ubsocket_thread_errno.h"

namespace ock {
namespace ubs {

/* ============================================================================
 * issue #15 治本：握手期事件等待器（HandshakeWaitPoller）
 *
 * 背景：握手期 fd 未注册任何 epoll，原实现（下方 LegacyPollWait 保留为回退）
 * 用 PollerYield + poll(fd, ≤10ms 切片) 自旋——poll 把 brpc worker *pthread*
 * 钉住最长 10ms；EventDispatcher epoll_wait(-1) 占住 worker 的场景下，被
 * yield 的握手 bthread 可能饿死（本 issue 的 8 连接超时）；每条消息每腿还要
 * 付一次调度往返 + 切片量化的时延税（issue #11 的"薄摊膨胀"）。
 *
 * 机制（按可用性自动降级，三档）：
 *  1) 外部 sem（LockRegistry::ExternalSemOpsRegistered，如 brpc 注册的
 *     bthread 信号量）：waiter 直接 sem.wait 被正确 park，poller 事件/超时
 *     到达 post 唤醒——最优路径；
 *  2) 外部 lock（现网 brpc 已注册 bthread::Mutex）：闸门交接——waiter 入队
 *     并 yield 等 poller 把闸门锁上（arm，一个 poller 循环内完成，eventfd
 *     催促），随后 waiter 加锁即 park 在 butex 上；poller 事件/超时开闸。
 *     bthread::Mutex 的 unlock 从 poller pthread 调用仅做唤醒，不阻塞；
 *  3) 都不可用（纯工具/UT 环境）：LegacyPollWait 原样回退。
 *
 * 并发契约：
 *  - poller 单线程处理事件与超时；对每个 waiter 恰好动作一次
 *    （done CAS 兜底）；动作序 = 写结果 → 唤醒；唤醒后 poller 不再触碰 waiter。
 *  - waiter 被唤醒后自行清理：先在 timers 表中摘除自己（防过期扫描摸到已释放
 *    栈对象），再 epoll DEL（超时路径 poller 已 DEL，重复 DEL 的 ENOENT 无害），
 *    最后销毁同步原语。
 * ========================================================================== */
namespace {

class HandshakeWaitPoller {
public:
    enum class Mech { NONE, EXT_SEM, GATE };

    static HandshakeWaitPoller *Instance()
    {
        static std::atomic<HandshakeWaitPoller *> inst{nullptr};
        HandshakeWaitPoller *p = inst.load(std::memory_order_acquire);
        if (p != nullptr) {
            return p;
        }
        auto *cand = new (std::nothrow) HandshakeWaitPoller();
        if (cand == nullptr || !cand->Init()) {
            delete cand;
            /* 初始化失败：缓存一个哨兵，避免每次重试。nullptr = 回退 legacy */
            return nullptr;
        }
        if (!inst.compare_exchange_strong(p, cand, std::memory_order_release, std::memory_order_acquire)) {
            cand->Shutdown();
            delete cand;
            return p;
        }
        return cand;
    }

    Mech PickMech() const
    {
        if (LockRegistry::ExternalSemOpsRegistered()) {
            return Mech::EXT_SEM;
        }
        if (LockRegistry::ExternalLockOpsRegistered()) {
            return Mech::GATE;
        }
        return Mech::NONE;
    }

    /* 等待 fd 就绪（events: EPOLLIN/EPOLLOUT）或超时。
     * 返回 true = 已被唤醒（就绪或超时，调用方重试 IO 并自查全局超时）；
     * false = 本等待器不可用，调用方走 legacy 回退。 */
    bool Wait(int fd, uint32_t events, uint32_t timeout_ms)
    {
        const Mech mech = PickMech();
        if (mech == Mech::NONE) {
            return false;
        }

        Waiter w;
        w.mech = mech;
        if (mech == Mech::EXT_SEM) {
            w.sem = LockRegistry::SEM_OPS.create();
            if (w.sem == nullptr || LockRegistry::SEM_OPS.init(w.sem, 0, 0) != 0) {
                if (w.sem != nullptr) {
                    LockRegistry::SEM_OPS.destroy(w.sem);
                }
                return false;
            }
        } else {
            w.gate = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
            if (w.gate == nullptr) {
                return false;
            }
        }

        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = events | EPOLLONESHOT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        ev.data.ptr = &w;
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
            DestroyPrimitives(w);
            return false;
        }

        const uint64_t deadline = SocketConnHelper::GetTimeMs() + timeout_ms;
        w.fd = fd;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            timers_.emplace(deadline, &w);
            if (mech == Mech::GATE) {
                arm_queue_.push_back(&w);
            }
        }
        Nudge();

        if (mech == Mech::EXT_SEM) {
            LockRegistry::SEM_OPS.wait(w.sem); /* bthread 感知 sem：正确 park */
        } else {
            /* 等 poller 把闸门锁上（一个循环内，eventfd 已催促）；
             * 短自旋 + yield，随后加锁即 park 到事件/超时开闸 */
            while (!w.armed.load(std::memory_order_acquire)) {
                if (w.done.load(std::memory_order_acquire)) {
                    break; /* 极短等待即完成（poller 抢先跑完了事件） */
                }
                PollerYield();
            }
            if (!w.done.load(std::memory_order_acquire)) {
                LockRegistry::LOCK_OPS.lock(w.gate); /* park until 开闸 */
                LockRegistry::LOCK_OPS.unlock(w.gate);
            }
        }

        /* 析构竞态防护：等 poller 的 WakeOne 彻底走完（post/unlock 的第二步
         * butex_wake 仍在触碰原语内存），wake_done 是它的完成回执。唤醒来源
         * 只有 WakeOne（事件/超时/关停三路皆是），故必有人置位；窗口纳秒级。 */
        while (!w.wake_done.load(std::memory_order_acquire)) {
            PollerYield();
        }

        /* 醒来自清理（并发契约见文件头注释）：timers 与 arm_queue 都要摘，
         * 否则"事件先于 arm 到达"时 poller 的 arm 阶段会摸到已释放的栈对象 */
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto it = timers_.begin(); it != timers_.end(); ++it) {
                if (it->second == &w) {
                    timers_.erase(it);
                    break;
                }
            }
            arm_queue_.erase(std::remove(arm_queue_.begin(), arm_queue_.end(), &w), arm_queue_.end());
        }
        epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr); /* 超时路径已 DEL，ENOENT 无害 */
        DestroyPrimitives(w);
        return true;
    }

private:
    struct Waiter {
        Mech mech = Mech::NONE;
        u_semaphore_t *sem = nullptr;
        u_mutex_t *gate = nullptr;
        int fd = -1;
        std::atomic<bool> armed{false};
        std::atomic<bool> done{false};
        /* 析构竞态防护（fork#21 现场 core）：bthread 的 sem_post / mutex_unlock
         * 内部是两步（计数/状态位落地 → butex_wake），waiter 可在第一步后就从
         * wait 返回并销毁原语，poller 的第二步随即摸到已释放内存（core 中
         * butex=0x100000000 即被复写的尸体）。契约：poller 在原语操作全部完成后
         * 才置 wake_done（这是它对本 waiter 内存的最后一触）；waiter 醒来后必须
         * 自旋等到 wake_done 才允许销毁原语/解栈。窗口为 post 尾段，纳秒~微秒级。 */
        std::atomic<bool> wake_done{false};
    };

    static void DestroyPrimitives(Waiter &w)
    {
        if (w.sem != nullptr) {
            LockRegistry::SEM_OPS.destroy(w.sem);
            w.sem = nullptr;
        }
        if (w.gate != nullptr) {
            LockRegistry::LOCK_OPS.destroy(w.gate);
            w.gate = nullptr;
        }
    }

    bool Init()
    {
        epfd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epfd_ < 0) {
            return false;
        }
        evfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (evfd_ < 0) {
            close(epfd_);
            return false;
        }
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.ptr = nullptr; /* nullptr = nudge 标记 */
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) != 0 ||
            pthread_create(&thr_, nullptr, &HandshakeWaitPoller::ThreadMain, this) != 0) {
            close(evfd_);
            close(epfd_);
            return false;
        }
        pthread_setname_np(thr_, "ubs_hs_poller");
        return true;
    }

    /* 仅 Instance() CAS 败者调用：其 Init 已启动线程，必须停线程再释放 */
    void Shutdown()
    {
        stop_.store(true, std::memory_order_release);
        Nudge();
        pthread_join(thr_, nullptr);
        close(evfd_);
        close(epfd_);
    }

    void Nudge()
    {
        uint64_t one = 1;
        ssize_t wr = write(evfd_, &one, sizeof(one));
        (void)wr;
    }

    /* 唤醒协议拆成两半（fork#21 第二洞的修复）：
     *  Claim   —— done 的 exchange 认领唤醒责任。调用前提：W 可证明存活
     *             （事件路径：done 未置则 waiter 必睡在 wait 里；
     *              过期路径：必须持 mtx_ 且 W 仍在 timers_ ——自摘需持锁）。
     *             认领失败 = 别人已负责唤醒，此后不得再触碰该 waiter。
     *  Deliver —— post/unlock + wake_done 盖章。只对认领成功者调用；该
     *             waiter 仍睡在 wait 里（无人 post 过）或自旋等章，必活。
     * 旧实现把两半合在 WakeOne 里、过期批在锁外整批调用——事件路径先完整
     * 唤醒某 waiter 后，锁外批里的 done.exchange 就是对已解栈内存写 0x01
     * （0x01@+4 正是三份 core 里 0x100000000 的来源）。 */
    static bool Claim(Waiter *w)
    {
        return !w->done.exchange(true, std::memory_order_acq_rel);
    }

    void DeliverWake(Waiter *w)
    {
        if (w->mech == Mech::EXT_SEM) {
            LockRegistry::SEM_OPS.post(w->sem);
        } else {
            /* GATE：若已 arm，开闸；若尚未 arm（事件先到），done 已置位，
             * waiter 的 arm 自旋会看到 done 直接返回，无需开闸 */
            if (w->armed.load(std::memory_order_acquire)) {
                LockRegistry::LOCK_OPS.unlock(w->gate);
            }
        }
        w->wake_done.store(true, std::memory_order_release);
    }

    void WakeOne(Waiter *w)
    {
        if (Claim(w)) {
            DeliverWake(w);
        }
    }

    static void *ThreadMain(void *arg)
    {
        static_cast<HandshakeWaitPoller *>(arg)->Loop();
        return nullptr;
    }

    void Loop()
    {
        constexpr int MAX_EVENTS = 64;
        struct epoll_event evs[MAX_EVENTS];
        for (;;) {
            int timeout = -1;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                /* 先处理 GATE arm 队列：poller 持闸（锁上），waiter 随后加锁被 park */
                for (Waiter *w : arm_queue_) {
                    if (!w->done.load(std::memory_order_acquire)) {
                        LockRegistry::LOCK_OPS.lock(w->gate); /* bthread::Mutex 无竞争加锁，瞬时 */
                        w->armed.store(true, std::memory_order_release);
                    } else {
                        w->armed.store(true, std::memory_order_release);
                    }
                }
                arm_queue_.clear();
                if (!timers_.empty()) {
                    const uint64_t now = SocketConnHelper::GetTimeMs();
                    const uint64_t next = timers_.begin()->first;
                    timeout = next <= now ? 0 : static_cast<int>(std::min<uint64_t>(next - now, 60000));
                }
            }
            int n = epoll_wait(epfd_, evs, MAX_EVENTS, timeout);
            if (stop_.load(std::memory_order_acquire)) {
                return;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                UBS_VLOG_ERR("hs poller epoll_wait failed, errno: %d\n", errno);
                usleep(1000);
                continue;
            }
            for (int i = 0; i < n; ++i) {
                if (evs[i].data.ptr == nullptr) { /* nudge */
                    uint64_t drain;
                    while (read(evfd_, &drain, sizeof(drain)) > 0) {
                    }
                    continue;
                }
                WakeOne(static_cast<Waiter *>(evs[i].data.ptr));
            }
            /* 过期扫描（两阶段，fork#21 第二洞）：
             * 阶段1 锁内认领 —— W 仍在 timers_ 且 mtx_ 在手 ⇒ waiter 尚未
             * 自摘 ⇒ 必未销毁，touch 安全。认领失败（事件路径已赢）从此不再
             * 触碰该 waiter——旧实现在锁外对整批 WakeOne，正是对这类已解栈
             * waiter 的 UAF 写（三份现场 core 的 0x100000000 来源）。
             * 阶段2 锁外投递 —— 认领成功者仍睡在 wait 里（无人 post 过），
             * 必活到 post + wake_done 盖章完成。post 保持在锁外，不与建链
             * 热路径争 mtx_。 */
            std::vector<Waiter *> claimed;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                const uint64_t now = SocketConnHelper::GetTimeMs();
                while (!timers_.empty() && timers_.begin()->first <= now) {
                    Waiter *w = timers_.begin()->second;
                    timers_.erase(timers_.begin());
                    if (Claim(w)) {
                        epoll_ctl(epfd_, EPOLL_CTL_DEL, w->fd, nullptr);
                        claimed.push_back(w);
                    }
                }
            }
            for (Waiter *w : claimed) {
                DeliverWake(w);
            }
        }
    }

    int epfd_ = -1;
    int evfd_ = -1;
    std::atomic<bool> stop_{false};
    pthread_t thr_{};
    std::mutex mtx_;
    std::multimap<uint64_t, Waiter *> timers_;
    std::vector<Waiter *> arm_queue_;
};

/* 事件化等待入口：true = 已等待（就绪或超时），false = 回退 legacy */
bool EventWait(int fd, uint32_t epoll_events, uint32_t timeout_ms)
{
    HandshakeWaitPoller *poller = HandshakeWaitPoller::Instance();
    if (poller == nullptr) {
        return false;
    }
    return poller->Wait(fd, epoll_events, timeout_ms);
}

} // namespace

bool SocketConnHelper::IsUbsConnection(const int &fd)
{
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::TFO) {
        tcp_info info{};
        socklen_t len = sizeof(info);
        bool is_tfo_connection = false;
        if (LibcApi::getsockopt(fd, SOL_TCP, TCP_INFO, &info, &len) == 0) {
            // check TCPI_OPT_SYN_DATA
            is_tfo_connection = (info.tcpi_options & TCPI_OPT_SYN_DATA) != 0;
        }
        UBS_VLOG_DEBUG("Current tcpi_options: 0x%x, tfo connection: %s \n", info.tcpi_options,
                       is_tfo_connection ? "true" : "false");
        return is_tfo_connection;
    }
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::UB_SOCK_OPT) {
        int opt = -1;
        socklen_t len = sizeof(opt);
        if (LibcApi::getsockopt(fd, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, &len) == 0) {
            UBS_VLOG_DEBUG("UB handshake socket option is %s. \n", opt == 1 ? "enabled" : "disabled");
            return opt == 1;
        }
        return false;
    }
    UBS_VLOG_WARN("Unsupported handshake mode: 0x%x\n", static_cast<unsigned int>(GlobalSetting::UBS_HAND_SHAKE_MODE));
    return false;
}

void SocketConnHelper::ExtractIpFromSockAddr(const struct sockaddr *address, char *buf, size_t buf_len)
{
    if (buf == nullptr || buf_len == 0) {
        return;
    }
    buf[0] = '\0';
    if (address == nullptr) {
        return;
    }
    if (address->sa_family == AF_INET && buf_len >= INET_ADDRSTRLEN) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        static_cast<void>(inet_ntop(AF_INET, &addr_in->sin_addr, buf, buf_len));
    } else if (address->sa_family == AF_INET6 && buf_len >= INET6_ADDRSTRLEN) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        static_cast<void>(inet_ntop(AF_INET6, &addr_in6->sin6_addr, buf, buf_len));
    }
}

std::string SocketConnHelper::ExtractIpFromSockAddr(const struct sockaddr *address)
{
    if (address == nullptr) {
        return "";
    }
    char ip_str[INET6_ADDRSTRLEN] = {0};
    const char *result = nullptr;
    if (address->sa_family == AF_INET) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        result = inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN);
    } else if (address->sa_family == AF_INET6) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        result = inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
    }
    return (result != nullptr) ? std::string(ip_str) : "";
}

uint16_t SocketConnHelper::ExtractPortFromSockAddr(const struct sockaddr *address)
{
    uint16_t port = 0;
    if (address == nullptr) {
        return port;
    }
    if (address->sa_family == AF_INET) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        port = ntohs(addr_in->sin_port);
    } else if (address->sa_family == AF_INET6) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        port = ntohs(addr_in6->sin6_port);
    }
    return port;
}

ssize_t SocketConnHelper::SendSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t sent = 0;
    size_t total = size;
    auto start_ms = GetTimeMs();

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;

    while (total != 0) {
        sent = LibcApi::send(fd, cur, total, MSG_NOSIGNAL);
        /* issue#33：本循环含 bthread 停泊点（EventWait / PollerYield），停泊后可能
         * 迁移 pthread，而高优化构建会把 __errno_location() 按函数缓存——迁移后
         * errno 宏读写的全是旧 pthread 的 TLS（现网 753×753：真实 ECONNREFUSED
         * 被读成陈值 0）。故本循环所有 errno 访问一律经 ThreadErrno/SetThreadErrno
         * （独立编译单元、禁内联）现取现写，紧贴各自的系统调用。 */
        const int send_err = ThreadErrno();
        if (sent >= 0) {
            total -= sent;
            cur += sent;
            continue;
        }

        if (send_err == EAGAIN || send_err == EWOULDBLOCK) {
            uint64_t now_ms = GetTimeMs();
            uint64_t elapsed_ms = now_ms - start_ms;
            if (elapsed_ms >= timeout_ms) {
                SetThreadErrno(ETIMEDOUT);
                return sent;
            }

            /* issue #15 治本：同 RecvSocketData——事件化等待优先，回退旧路径 */
            if (EventWait(fd, EPOLLOUT, static_cast<uint32_t>(timeout_ms - elapsed_ms))) {
                continue;
            }

            // 回退：先尝试 yield, 不再占着 brpc worker 不放
            PollerYield();
            uint32_t remaining_ms = std::min(timeout_ms - elapsed_ms, static_cast<uint64_t>(SEND_RECV_POLL_SLICE_MS));
            int poll_ret = poll(&pfd, 1, remaining_ms);
            const int poll_err = ThreadErrno();
            if (poll_ret > 0) {
                continue;
            }
            if (poll_ret == 0) {
                PollerYield();
            } else if (poll_err == EINTR) {
                continue;
            } else {
                UBS_VLOG_ERR("poll() failed, ret: %d, errno: %d, errmsg: %s, fd: %d\n", poll_ret, poll_err,
                             Func::Error2Str(poll_err), fd);
                SetThreadErrno(poll_err);
                return sent;
            }
        } else {
            /* Snapshot before anything (the log gate included) can clobber
             * errno — issue #30 client logs showed "ret:-1, errno:0/EROFS"
             * where the true cause was ECONNREFUSED/EPIPE. */
            if (send_err == EINTR) {
                continue;
            }

            UBS_VLOG_ERR("send() failed, ret: %zd, errno: %d, errmsg: %s, sent: %zd\n", sent, send_err,
                         Func::Error2Str(send_err), sent);
            SetThreadErrno(send_err);
            return sent;
        }
    }
    return size;
}

ssize_t SocketConnHelper::RecvSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    // reset errno to 0
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t received = 0;
    size_t total = size;
    uint64_t start_ms = GetTimeMs();

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (total != 0) {
        received = LibcApi::recv(fd, cur, total, MSG_NOSIGNAL);
        /* issue#33：errno 访问纪律同 SendSocketData——停泊点后经不透明访问器现取现写 */
        const int recv_err = ThreadErrno();
        if (received > 0) {
            total -= received;
            cur += received;
            continue;
        }

        if (received == 0) {
            UBS_VLOG_DEBUG("The connection has been closed by peer.\n");
            return 0;
        }

        if (recv_err == EAGAIN || recv_err == EWOULDBLOCK) {
            uint64_t now_ms = GetTimeMs();
            uint64_t elapsed_ms = now_ms - start_ms;
            if (elapsed_ms >= timeout_ms) {
                SetThreadErrno(ETIMEDOUT);
                return received;
            }

            /* issue #15 治本：优先事件化等待——fd 注册进专职握手 poller，
             * 调用方（bthread）被正确 park，brpc worker 不再被 poll 切片
             * 钉住，也不再依赖 yield 自旋推进（饿死场景消失）。唤醒即重试
             * recv；全局超时由循环头 elapsed 判定。等待器不可用（外部
             * lock/sem ops 均未注册的工具/UT 环境）回退旧路径。 */
            if (EventWait(fd, EPOLLIN, static_cast<uint32_t>(timeout_ms - elapsed_ms))) {
                continue;
            }

            // 回退：先尝试 yield，不再占着 brpc worker 不放
            PollerYield();
            uint32_t remaining_ms = std::min(timeout_ms - elapsed_ms, static_cast<uint64_t>(SEND_RECV_POLL_SLICE_MS));
            int poll_ret = poll(&pfd, 1, remaining_ms);
            const int poll_err = ThreadErrno();
            if (poll_ret > 0) {
                continue;
            }
            if (poll_ret == 0) {
                PollerYield();
            } else if (poll_err == EINTR) {
                continue;
            } else {
                UBS_VLOG_ERR("poll() failed, ret: %d, errno: %d, errmsg: %s, fd: %d\n", poll_ret, poll_err,
                             Func::Error2Str(poll_err), fd);
                SetThreadErrno(poll_err);
                return received;
            }
        } else {
            if (recv_err == EINTR) {
                continue;
            }
            UBS_VLOG_ERR("recv() failed, ret: %zd, errno: %d, errmsg: %s, received: %zd, fd: %d\n", received, recv_err,
                         Func::Error2Str(recv_err), received, fd);
            SetThreadErrno(recv_err);
            return received;
        }
    }
    return size;
}

Result SocketConnHelper::SendLengthPrefixed(int fd, const void *body, uint32_t obj_size, uint32_t timeout_ms)
{
    uint32_t wire_size = sizeof(uint32_t) + obj_size;
    std::vector<uint8_t> buf(wire_size);
    memcpy(buf.data(), &obj_size, sizeof(obj_size));
    memcpy(buf.data() + sizeof(obj_size), body, obj_size);
    ssize_t ret = SendSocketData(fd, buf.data(), wire_size, timeout_ms);
    if (ret != static_cast<ssize_t>(wire_size)) {
        UBS_VLOG_ERR("SendLengthPrefixed failed, fd=%d, wire_size=%u, send_ret=%zd, errno=%d(%s)", fd, wire_size, ret,
                     errno, Func::Error2Str(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result SocketConnHelper::RecvLengthPrefixed(int fd, void *body, uint32_t obj_size, uint32_t timeout_ms)
{
    uint32_t body_len = 0;
    if (RecvSocketData(fd, &body_len, sizeof(body_len), timeout_ms) != static_cast<int>(sizeof(body_len))) {
        UBS_VLOG_ERR("RecvLengthPrefixed failed to read prefix, fd=%d, errno=%d(%s)", fd, errno,
                     Func::Error2Str(errno));
        return UBS_ERROR;
    }
    /* 越界必须在读 body 之前判：控制面存在裸 4 字节 ack 与前缀帧同流的场景，错位时
     * ack 值会被当成前缀读到——若先读 body 再判，会在死流上阻塞满 timeout_ms 才失败。
     * 硬上限独立于 obj_size：协议 desync 时 body_len 可能被垃圾数据填充（如对端 ack
     * 被误读为长度前缀），值远超任何合法控制面消息。即便 excess 刚好不超 64KB，
     * 一个 3.7GB 的 body_len 也绝不可能是合法值——立即拒绝，避免无谓 read/discard。 */
    constexpr uint32_t kMaxDiscardBytes = 64 * 1024;
    constexpr uint32_t kMaxBodyLen = 1U * 1024 * 1024; // 1MB, far above any control-plane struct
    if (body_len > kMaxBodyLen) {
        UBS_VLOG_WARN("RecvLengthPrefixed body_len exceeds hard limit, fd=%d, body_len=%u, kMaxBodyLen=%u", fd,
                     body_len, kMaxBodyLen);
        return UBS_ERROR;
    }
    if (body_len > obj_size && body_len - obj_size > kMaxDiscardBytes) {
        UBS_VLOG_WARN("RecvLengthPrefixed body_len overflow, fd=%d, body_len=%u, obj_size=%u, excess=%u", fd,
                     body_len, obj_size, body_len - obj_size);
        return UBS_ERROR;
    }
    uint32_t read_len = std::min(body_len, obj_size);
    std::vector<uint8_t> buf(read_len);
    if (RecvSocketData(fd, buf.data(), read_len, timeout_ms) != static_cast<int>(read_len)) {
        UBS_VLOG_ERR("RecvLengthPrefixed failed to read body, fd=%d, body_len=%u, read_len=%u, errno=%d(%s)", fd,
                     body_len, read_len, errno, Func::Error2Str(errno));
        return UBS_ERROR;
    }
    memcpy(body, buf.data(), read_len);
    if (read_len < obj_size) {
        memset(static_cast<uint8_t *>(body) + read_len, 0, obj_size - read_len);
    }
    if (body_len > obj_size) {
        uint32_t discard_len = body_len - obj_size;
        std::vector<uint8_t> discard(discard_len);
        if (RecvSocketData(fd, discard.data(), discard_len, timeout_ms) != static_cast<int>(discard_len)) {
            UBS_VLOG_ERR("RecvLengthPrefixed failed to discard excess, fd=%d, discard_len=%u, errno=%d(%s)", fd,
                         discard_len, errno, Func::Error2Str(errno));
            return UBS_ERROR;
        }
    }
    return UBS_OK;
}

void SocketConnHelper::FlushSocketMsg(int fd)
{
    // reset errno to 0
    errno = 0;
    char tmp_buf[FLUSH_SOCKET_MSG_BUFFER_LEN];
    ssize_t received = 0;
    do {
        received = LibcApi::recv(fd, tmp_buf, FLUSH_SOCKET_MSG_BUFFER_LEN, MSG_NOSIGNAL);
        if (errno == EAGAIN || errno == EINTR) {
            // reset errno to 0
            errno = 0;
            continue;
        }

        if (received < 0 || errno != 0) {
            return;
        }
    } while (received > 0);
}

int SocketConnHelper::GetCurrentProcessSocketId()
{
    // 获取当前进程主线程所在的 CPU
    int cpu = sched_getcpu();
    if (cpu < 0) {
        UBS_VLOG_ERR("sched_getcpu() failed, ret: %d, errno: %d, errmsg: %s\n", cpu, errno, Func::Error2Str(errno));
        return -1;
    }
    return SocketConnHelper::GetSocketIdOfCpu(cpu);
}

// 从 CPU ID 获取其 Socket ID（physical_package_id）
int SocketConnHelper::GetSocketIdOfCpu(int cpu)
{
    // CPU 号范围检查：拦截负数与垃圾值，避免构造无意义路径
    if (cpu < 0 || cpu > UBSOCKET_CPU_ID_MAX) {
        UBS_VLOG_ERR("GetSocketIdOfCpu invalid cpu: %d, range [0, %d]\n", cpu, UBSOCKET_CPU_ID_MAX);
        return -1;
    }
    std::string cpuStr = "cpu" + std::to_string(cpu);
    std::string path = std::string(SOCKET_ID_PERFIX_PATH) + cpuStr + std::string(SOCKET_ID_SUFFIX_PATH);
    // 符号链接逃逸检查：realpath 解析全路径中的符号链接，若结果与预期路径不一致则拒绝
    char resolvedBuf[PATH_MAX] = {0};
    char *resolvedPtr = realpath(path.c_str(), resolvedBuf);
    if (resolvedPtr == nullptr) {
        UBS_VLOG_ERR("GetSocketIdOfCpu realpath failed, cpu: %d, path: %s\n", cpu, path.c_str());
        return -1;
    }
    std::string resolvedPath(resolvedPtr);
    std::string expectedPath = std::string(SOCKET_ID_PERFIX_PATH) + cpuStr + std::string(SOCKET_ID_SUFFIX_PATH);
    if (resolvedPath != expectedPath) {
        UBS_VLOG_ERR("GetSocketIdOfCpu symlink escape detected, cpu: %d, path: %s, resolved: %s\n", cpu, path.c_str(),
                     resolvedPath.c_str());
        return -1;
    }
    std::ifstream file(resolvedPath);
    int socketId;
    if (file >> socketId) {
        return socketId;
    }
    UBS_VLOG_ERR("GetSocketIdOfCpu failed, cpu: %d, path: %s\n", cpu, resolvedPath.c_str());
    return -1; // 读取失败
}

std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaNumaSysfs()
{
    // 尝试 NUMA 方式获取
    std::vector<uint32_t> numaResult = SocketConnHelper::GetSocketIdsViaNuma();
    if (!numaResult.empty()) {
        return numaResult;
    }

    // NUMA 不可用，回退到 CPU 扫描
    UBS_VLOG_WARN("NUMA not available. Direct use CPU topology.\n");
    return SocketConnHelper::GetSocketIdsViaCpuScan();
}

std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaNuma()
{
    std::set<int> socketIds;

    DIR *nodeDir = opendir(CPU_LIST_PREFIX_PATH);
    if (!nodeDir) {
        return {}; // NUMA 不可用
    }

    struct dirent *entry;
    while ((entry = readdir(nodeDir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, NODE_STR_SIZE) == "node" && name.size() > NODE_STR_SIZE) {
            char *end;
            std::string nodeIdStr = name.substr(NODE_STR_SIZE);
            long nodeId = std::strtol(nodeIdStr.c_str(), &end, 10);
            if (*end == '\0' && nodeId >= 0) {
                // 读取该 NUMA 节点的 CPU 列表
                std::string cpuListPath = std::string(CPU_LIST_PREFIX_PATH) + name + std::string(CPU_LIST_SUFFIX_PATH);
                std::ifstream cpuListFile(cpuListPath);
                std::string cpuListStr;
                if (std::getline(cpuListFile, cpuListStr)) {
                    // 解析 CPU 列表，获得第一个 CPU ID
                    int cpu = SocketConnHelper::GetFirstCpuFromCpulist(cpuListStr);
                    if (cpu != -1) {
                        // 根据 CPU ID 获取其 Socket ID
                        int socketId = SocketConnHelper::GetSocketIdOfCpu(cpu);
                        if (socketId >= 0) {
                            socketIds.insert(socketId);
                        }
                    }
                }
            }
        }
    }

    closedir(nodeDir);
    return std::vector<uint32_t>(socketIds.begin(), socketIds.end());
}

// CPU 扫描方式获取 Socket IDs
std::vector<uint32_t> SocketConnHelper::GetSocketIdsViaCpuScan()
{
    std::set<int> socketIds;

    DIR *cpu_dir = opendir(SOCKET_ID_PERFIX_PATH);
    if (!cpu_dir) {
        return {};
    }

    struct dirent *entry;
    while ((entry = readdir(cpu_dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.substr(0, CPU_STR_SIZE) == "cpu" && name.size() > CPU_STR_SIZE) {
            char *end;
            std::string cpuIdStr = name.substr(CPU_STR_SIZE);
            long cpuId = std::strtol(cpuIdStr.c_str(), &end, 10);
            if (*end == '\0' && cpuId >= 0) {
                int socketId = SocketConnHelper::GetSocketIdOfCpu(static_cast<int>(cpuId));
                if (socketId >= 0) {
                    socketIds.insert(socketId);
                }
            }
        }
    }

    closedir(cpu_dir);
    return std::vector<uint32_t>(socketIds.begin(), socketIds.end());
}

// 解析cpulist字符串
int SocketConnHelper::GetFirstCpuFromCpulist(const std::string &cpuListStr)
{
    if (cpuListStr.empty()) {
        // 表示无效输入
        UBS_VLOG_WARN("GetFirstCpuFromCpulist empty, empty cpulist string\n");
        return -1;
    }

    std::stringstream ss(cpuListStr);
    std::string token;

    // 只取第一个逗号分隔的 token
    if (std::getline(ss, token, ',')) {
        size_t dash = token.find('-');
        if (dash != std::string::npos) {
            uint32_t dashStart = 0;
            try {
                dashStart = static_cast<uint32_t>(std::stoi(token.substr(0, dash)));
            } catch (const std::exception &e) {
                UBS_VLOG_ERR("No valid CPU detected.\n");
                dashStart = 0;
                return -1;
            }
            // 范围形式：如 "0-3"，返回开始的数字
            return dashStart;
        } else {
            // 单个 CPU：如 "5"，直接返回
            uint32_t tokenCPU = 0;
            try {
                tokenCPU = static_cast<uint32_t>(std::stoi(token));
            } catch (const std::exception &e) {
                UBS_VLOG_ERR("No valid CPU detected.\n");
                tokenCPU = 0;
                return -1;
            }
            return tokenCPU;
        }
    }

    UBS_VLOG_ERR("GetFirstCpuFromCpulist failed, no valid cpu token\n");
    return -1; // 没有找到有效 CPU
}

} // namespace ubs
} // namespace ock
