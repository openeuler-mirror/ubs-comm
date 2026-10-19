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
#ifndef UBS_COMM_UBSOCKET_CONN_REACTOR_H
#define UBS_COMM_UBSOCKET_CONN_REACTOR_H

#include <poll.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"
#include "common/ubsocket_mpsc_ring_queue.h"
#include "ubsocket_conn_ops_base.h"
#include "ubsocket_conn_types.h"

namespace ock {
namespace ubs {

/*
 * 全局 event fd 注册表：维护线程池每个 thread 的 event fd -> 负载（在途建链连接数）的映射，
 * 供分发器按"负载最轻"做负载均衡。load 即该线程 poll 监听中的活跃建链连接数，直接反映 poll 遍历开销。
 */
class ConnEventFdTable : public LeakySingleton<ConnEventFdTable> {
    friend LeakySingleton<ConnEventFdTable>;

public:
    static ConnEventFdTable &Instance()
    {
        return LeakySingleton<ConnEventFdTable>::Instance();
    }

    void Register(int event_fd, int thread_index);
    void Unregister(int event_fd);
    int GetLeastLoadedIndex(); /* 负载最轻线程的 thread_index；空表返回 -1 */
    void IncLoad(int event_fd);
    void DecLoad(int event_fd);

private:
    struct Entry {
        int event_fd = -1;
        int thread_index = 0;
        uint32_t load = 0;
    };

    ConnEventFdTable() = default;
    ~ConnEventFdTable() = default;

    std::mutex mutex_;
    std::vector<Entry> entries_;
};

/*
 * 全异步建链线程池中的单个线程（Server/Client 共用）：进程启动时由分发器创建，先为本线程创建专属
 * event fd 并注册进 ConnEventFdTable，随后进入 poll 循环。
 * 初始化时 poll 只监听专属 event fd；分发器把新连接任务投递到本线程 event_queue_ 并写 event fd 唤醒 poll。
 * poll 被唤醒后遍历就绪 fd：event fd 可读则 read 消费计数、取出 event_queue_ 中的 socket fd、加入监听数组
 * 监听 POLLIN 并逐个执行建链任务；socket fd 可读/可写则取 UBConnOpsBase 执行 Execute，按返回值决定 fd 去留：
 * - kPending 保留监听（EAGAIN 续发时追加 POLLOUT）；kDone/kFailed 从监听数组移除。
 * 连接一经分配固定在本线程（fd 亲和性），状态机在该 poll 线程单线程推进、无锁；
 * conn_ops_map_ 由分发器线程插入、本线程读取/移除，需加锁保护。
 * 重活内联在 poll 线程执行会占用线程；空闲超时用"有效时钟"（墙钟扣除 busy_ms_）避免误杀等待中的连接。
 */
class ConnReactorThread {
public:
    using DoneCallback = std::function<void(int fd, const struct sockaddr &addr, socklen_t addr_len)>;

    /* 两个 MPSCRingQueue 成员容量恒为编译期 2 的幂（kEventQueueCapacity），构造不会抛异常；
     * 显式 noexcept 让编译器不再为该默认构造生成不可达的异常清理分支 */
    ConnReactorThread() noexcept {}
    ~ConnReactorThread()
    {
        Stop();
    }

    ConnReactorThread(const ConnReactorThread &) = delete;
    ConnReactorThread &operator=(const ConnReactorThread &) = delete;

    Result Init(int thread_index);
    Result AddConnection(int fd, const std::shared_ptr<UBConnOpsBase> &ops, const DoneCallback &done_cb);
    /* 取消建链中的连接：置 Abort()，异步让本线程移出监听与映射（fd 归调用方，不在此 close） */
    void Cancel(int fd);
    void Stop();

private:
    void RunLoop();
    void HandleEventFd(uint64_t effective_now_ms);
    void HandleCancelQueue();
    void HandleSocketFd(int fd, uint64_t effective_now_ms);
    void AddPollFd(int fd, short events);
    void RemovePollFd(int fd);
    void FinishConnection(int fd, ConnStageResult result);
    void ScanTimeoutConnections();
    uint64_t EffectiveNow() const; /* 有效时钟 = GetTimeMs() - busy_ms_，扣除本线程内联重活耗时 */

private:
    static constexpr int kPollTimeoutMs = 100;
    static constexpr uint64_t kEventQueueCapacity = 8192; /* 对齐 RPC_ADPT_FD_MAX */
    static constexpr uint64_t kScanIntervalMs = 1000;

    int event_fd_ = -1;
    std::thread thread_;
    std::vector<struct pollfd> poll_fds_; /* 槽0 固定 event fd，后续槽为 socket fd（惰性删除置 fd=-1） */

    struct ConnEntry {
        std::shared_ptr<UBConnOpsBase> ops;
        DoneCallback done_cb;
    };
    std::unordered_map<int, ConnEntry> conn_ops_map_;
    std::mutex conn_map_mutex_;

    MPSCRingQueue<int> event_queue_{kEventQueueCapacity};
    MPSCRingQueue<int> cancel_queue_{kEventQueueCapacity}; /* Cancel(fd) 跨线程投递，poll 线程消费 */

    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
    uint64_t last_scan_ms_ = 0;
    uint64_t busy_ms_ = 0; /* 累计"处理就绪 fd"耗时（含内联重活），仅 poll 线程访问 */
};

/*
 * UB 建链任务分发器（Server/Client 共用）：进程启动时启动全异步建链线程池（每 thread 一个 event fd，
 * 注册进 ConnEventFdTable）；新连接由调用方经工厂创建 UBConnOpsBase 后 Submit，按负载最轻选线程
 * （GetLeastLoadedIndex），把任务投递到该线程 event_queue_ 并写 event fd 唤醒其 poll。
 * 重活内联在 poll 线程，不使用共享 ExecutorService。
 */
class ConnDispatcher : public LeakySingleton<ConnDispatcher> {
    friend LeakySingleton<ConnDispatcher>;

public:
    static ConnDispatcher &Instance()
    {
        return LeakySingleton<ConnDispatcher>::Instance();
    }

    Result Start(uint32_t num_threads);
    void Stop();
    Result Submit(int fd, const struct sockaddr &addr, socklen_t addr_len,
                  const ConnReactorThread::DoneCallback &done_cb, const std::shared_ptr<UBConnOpsBase> &ops);
    /* 取消建链中的连接（遍历线程，命中者处理；fd 归调用方，不 close） */
    void Cancel(int fd);

private:
    ConnDispatcher() = default;
    ~ConnDispatcher() = default;

    std::mutex threads_mutex_;
    std::vector<std::shared_ptr<ConnReactorThread>> threads_;
    std::atomic<bool> started_{false};
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONN_REACTOR_H
