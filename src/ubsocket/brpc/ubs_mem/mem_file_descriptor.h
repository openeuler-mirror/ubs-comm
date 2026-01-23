/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-01-10
*/

#ifndef MEM_FILE_DESCRIPTOR_H
#define MEM_FILE_DESCRIPTOR_H

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "mem_def.h"
#include "mem_utils.h"
#include "rpc_adpt_vlog.h"
#include "file_descriptor.h"
#include "ring_buffer.h"

#ifdef UBS_SHM_BUILD_ENABLED

#define BIND_SYNC_MSG   "SYNC_DONE"
#define MB_TO_BYTE (1024 * 1024)

namespace Brpc {

enum class SocketState {
    SOCKET_STATE_NONE,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_CLOSING,
    SOCKET_STATE_CLOSED
};

constexpr uint8_t POLLING_NONE = 0;
constexpr uint8_t POLLING_USING = 1;
constexpr uint8_t POLLING_CLOSING = 2;
constexpr uint8_t POLLING_CLOSED = 3;
constexpr uint32_t WAIT_LOCAL_END_SLEEP_INTERVAL = 100;

class FallbackTcp {
public:
    ALWAYS_INLINE bool TxUseTcp(void)
    {
        return m_tx_use_tcp;
    }
    
    ALWAYS_INLINE bool RxUseTcp(void)
    {
        return m_rx_use_tcp;
    }

    ALWAYS_INLINE bool UseTcp(void)
    {
        return m_rx_use_tcp || m_tx_use_tcp;
    }
    
protected:
    
    bool m_tx_use_tcp = false;
    bool m_rx_use_tcp = false;
};


class MemSocketFd : public ::SocketFd, public FallbackTcp {
public:
    MemSocketFd(int fd, int event_fd) : ::SocketFd(fd)
    {
        m_tx_window_capacity = Context::GetContext()->GetTxDepth();
        m_rx_window_capacity = Context::GetContext()->GetRxDepth();
        m_event_fd = event_fd;
        Context::FetchAdd();
    }

    MemSocketFd(int fd, uint64_t magic_number, uint32_t magic_number_recv_size) : ::SocketFd(fd)
    {
        m_magic_number = magic_number;
        m_magic_number_recv_size = magic_number_recv_size;
        m_tx_use_tcp = true;
        Context::FetchAdd();
    }

    ~MemSocketFd()
    {
        RPC_ADPT_VLOG_ERR("MemSocketFd deconstructor, fd: %d\n", m_fd);
        // 【可重入方案】设置为 CLOSING，阻止新的线程进入
        int oldState = m_pollingWriteState.exchange(POLLING_CLOSING);
        if (oldState == POLLING_USING) {
            // 有线程正在使用，等待它们全部退出（状态变为 CLOSED）
            auto start = std::chrono::high_resolution_clock::now();
            while (m_pollingWriteState.load() != POLLING_CLOSED) {
                if (IsTimeout(start, WAIT_LOCAL_END_SLEEP_INTERVAL)) {
                    /* The child threads are still accessing umq resources, so umq_uninit() is not actively
                     * executed here to avoid core dumps. Let the program automatically reclaim all resources. */
                    RPC_ADPT_VLOG_WARN("MemSocketFd reclamation exceeds the time limit and forcefully terminated\n");
                    break;
                }
                std::this_thread::yield();
            }
        } else {
            // 没有线程在使用，直接设为 CLOSED
            m_pollingWriteState.store(POLLING_CLOSED);
        }

        m_txState = SocketState::SOCKET_STATE_CLOSED;
        m_rxState = SocketState::SOCKET_STATE_CLOSED;
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_LOCAL_END_SLEEP_INTERVAL));
        
        auto* shm = ShmMgr::GetShmMgr();

        shm->Unmap(&m_remoteTrxShm);
        shm->Free(&m_remoteTrxShm);
        shm->Unmap(&m_localTrxShm);
        shm->Free(&m_localTrxShm);

        Context::FetchSub();

        if (m_event_fd >= 0) {
            OsAPiMgr::GetOriginApi()->close(m_event_fd);
            m_event_fd = -1;
        }
        RPC_ADPT_VLOG_ERR("MemSocketFd deconstructor finished.\n");
    }

    // magic number is the 0xff + ASCII of "R" + "P" + "C" + "A" + "D" + "P" + "T"
    static const uint64_t CONTROL_PLANE_MAGIC_NUMBER = 0xff52504341445055;
    static const uint8_t CONTROL_PLANE_MAGIC_NUMBER_PREFIX = 0xff;
    static const uint64_t CONTROL_PLANE_MAGIC_NUMBER_BODY = 0x52504341445055;
    static const uint32_t NEGOTIATE_TIMEOUT_MS = 1000000;
    static const uint32_t CONTROL_PLANE_TIMEOUT_MS = 1000000;

    static int ValidateMagicNumber(int fd, uint64_t &magic_number, ssize_t &magic_number_recv_size)
    {
        magic_number_recv_size = RecvSocketData(fd, &magic_number, sizeof(magic_number), NEGOTIATE_TIMEOUT_MS);
        if (magic_number_recv_size <= 0) {
            return -1;
        }
        
        if (magic_number_recv_size != sizeof(magic_number) || magic_number != CONTROL_PLANE_MAGIC_NUMBER) {
            return magic_number_recv_size;
        }

        return 0;
    }

    void OutputStats(std::ostringstream &oss)
    {
        return;
    }

    ALWAYS_INLINE int Accept(struct sockaddr *address, socklen_t *address_len)
    {
        int fd = OsAPiMgr::GetOriginApi()->accept(m_fd, address, address_len);
        RPC_ADPT_VLOG_WARN("[DEBUG] Origin accept, fd: %d\n", fd);
        if (fd < 0 || m_tx_use_tcp || m_rx_use_tcp) {
            return fd;
        }

        bool is_blocking = IsBlocking(fd);
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(fd);
        }

        uint64_t magic_number = 0;
        ssize_t magic_number_recv_size = 0;
        int ret = ValidateMagicNumber(fd, magic_number, magic_number_recv_size);
        if (ret > 0) {
            /* IF the magic number verification fails, it is still necessary to create a socket fd object
             * to store the magic number information, so that the received information can be reported to
             * the user when readv is called. */
            MemSocketFd *socket_fd_obj = nullptr;
            try {
                socket_fd_obj = new MemSocketFd(fd, magic_number, (uint32_t)magic_number_recv_size);
                Fd<::SocketFd>::OverrideFdObj(fd, socket_fd_obj);
            } catch (std::exception& e) {
                RPC_ADPT_VLOG_ERR("%s\n", e.what());
                OsAPiMgr::GetOriginApi()->close(fd);
                return -1;
            }
        } else if (ret == 0) {
            if (DoAccept(fd) != 0) {
                RPC_ADPT_VLOG_WARN("Fatal error occurred, fd: %d fallback to TCP/IP\n", fd);
                /* Clear messages that already exist on the TCP link to prevent
                 * dirty messages from affecting user data transmission */
                FlushSocketMsg(fd);
            }
        }

        if (is_blocking) {
            // reset
            SetBlocking(fd);
        }

        return fd;
    }

    ALWAYS_INLINE int Connect(const struct sockaddr *address, socklen_t address_len)
    {
        int ret = OsAPiMgr::GetOriginApi()->connect(m_fd, address, address_len);
        if (ret < 0 || m_tx_use_tcp || m_rx_use_tcp) {
            return ret;
        }
        RPC_ADPT_VLOG_INFO("Begin Connect, m_fd %d.\n", m_fd);

        bool is_blocking = IsBlocking(m_fd);
        if (is_blocking) {
            // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
            SetNonBlocking(m_fd);
        }

        ret = DoConnect();
        if (ret != 0) {
            RPC_ADPT_VLOG_WARN("Fatal error occurred, fd: %d fallback to TCP/IP\n", m_fd);
            Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
            /* Clear messages that already exist on the TCP link to prevent
             * dirty messages from affecting user data transmission */
            FlushSocketMsg(m_fd);
        }

        if (is_blocking) {
            // reset
            SetBlocking(m_fd);
        }

        return ret;
    }

    ALWAYS_INLINE ssize_t ReadV(const struct iovec *iov, int iovcnt)
    {
        if (m_rx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->readv(m_fd, iov, iovcnt);
        }

        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            return -1;
        }

        if (m_txState != SocketState::SOCKET_STATE_CONNECTED || m_rxState != SocketState::SOCKET_STATE_CONNECTED) {
            return 0;
        }

        ssize_t rx_total_len = m_ringbuffer->ReadV(iov, iovcnt);
        if (rx_total_len == 0) {
            if (m_txState != SocketState::SOCKET_STATE_CONNECTED || m_rxState != SocketState::SOCKET_STATE_CONNECTED) {
                return 0;
            }
            errno = EAGAIN;
            return -1;
        }
        return rx_total_len;
    }

    ALWAYS_INLINE ssize_t WriteV(const struct iovec *iov, int iovcnt)
    {
        if (m_tx_use_tcp) {
            return OsAPiMgr::GetOriginApi()->writev(m_fd, iov, iovcnt);
        }

        if (iov == nullptr || iovcnt == 0) {
            errno = EINVAL;
            return -1;
        }

        if (m_txState != SocketState::SOCKET_STATE_CONNECTED || m_rxState != SocketState::SOCKET_STATE_CONNECTED) {
            errno = EPIPE;
            return -1;
        }
        
        ssize_t tx_total_len = m_ringbuffer->WriteV(iov, iovcnt);
        return tx_total_len;
    }

    ALWAYS_INLINE void SetTxState(SocketState input)
    {
        m_txState = input;
        RPC_ADPT_VLOG_INFO("Accept, this: %p, m_txState addr: %p, m_txState: %d.\n", this,
            &m_txState, static_cast<int>(m_txState));
    }

    ALWAYS_INLINE void SetRxState(SocketState input)
    {
        m_rxState = input;
        RPC_ADPT_VLOG_INFO("Accept, this: %p, m_rxState addr: %p, m_rxState: %d.\n", this,
            &m_rxState, static_cast<int>(m_rxState));
    }

    void SetLocalTrxShm(Shm &input)
    {
        m_localTrxShm.addr = input.addr;
        m_localTrxShm.fd = input.fd;
        m_localTrxShm.len = input.len;
        m_localTrxShm.memid = input.memid;
        int ret = strncpy_s(m_localTrxShm.name, SHM_MAX_NAME_BUFF_LEN, input.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Set local trx info copy local shared memory name failed, ret %d.\n", ret);
        }
    }

    void SetRemoteTrxShm(Shm &input)
    {
        m_remoteTrxShm.addr = input.addr;
        m_remoteTrxShm.fd = input.fd;
        m_remoteTrxShm.len = input.len;
        m_remoteTrxShm.memid = input.memid;
        int ret = strncpy_s(m_remoteTrxShm.name, SHM_MAX_NAME_BUFF_LEN, input.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Set remote trx info copy remote shared memory name failed, ret %d.\n", ret);
        }
    }

    void InitRingBuffer(RingBufferOpt &option)
    {
        AddrInfo localAddInfo {m_localTrxShm.addr, m_localTrxShm.len};
        AddrInfo remoteAddInfo {m_remoteTrxShm.addr, m_remoteTrxShm.len};
        RPC_ADPT_VLOG_ERR("[InitRingBuffer] localAddInfo, addr %p, m_remoteTrxShm %p.\n",
            m_localTrxShm.addr, m_remoteTrxShm.addr);
        m_ringbuffer = std::make_unique<RingBuffer>(option, localAddInfo, remoteAddInfo);
    }

    void *operator new(std::size_t size)
    {
        void *ptr = nullptr;
        if (posix_memalign(&ptr, CACHE_LINE_ALIGNMENT, size) != 0) {
            throw std::bad_alloc();
        }

        return ptr;
    }

    void operator delete(void* ptr) noexcept
    {
        free(ptr);
    }

    int DoConnect()
    {
        ExchangeData local_msg;
        ExchangeData remote_msg;
        char send_sync_msg[] = BIND_SYNC_MSG;
        char recv_sync_msg[] = BIND_SYNC_MSG;

        // Use magic number to check TCP link
        if (SendSocketData(m_fd, &local_msg.magic_number, sizeof(uint64_t), NEGOTIATE_TIMEOUT_MS) != sizeof(uint64_t)) {
            RPC_ADPT_VLOG_ERR("Add shmem connect failed to send magic number, fd: %d.\n", m_fd);
            return -1;
        }

        auto* shm = ShmMgr::GetShmMgr();
        size_t localShmLen = DATA_QUEUE_MAX_SIZE * MB_TO_BYTE;
        m_localTrxShm.len = localShmLen;
        m_localTrxShm.fd = m_fd;

        if (!Context::GetContext()->GetShmName(m_localTrxShm, "connect")) {
            RPC_ADPT_VLOG_ERR("Add shmem connect failed to Get SHM name\n");
            return -1;
        }
        
        // Allocate local shared memory and map it to m_localTrxShm.addr
        shm->Malloc(&m_localTrxShm);
        shm->Map(&m_localTrxShm);

        memset(reinterpret_cast<char *>(m_localTrxShm.addr), 0, localShmLen);

        // Send local Shm info to remote
        local_msg.shmem_length = m_localTrxShm.len;
        int ret = strncpy_s(local_msg.name, SHM_MAX_NAME_BUFF_LEN, m_localTrxShm.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Add shmem connect copy local shared memory name failed, ret %d.\n", ret);
            shm->Unmap(&m_localTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }
        if (SendSocketData(m_fd, &local_msg, sizeof(ExchangeData), NEGOTIATE_TIMEOUT_MS) != sizeof(ExchangeData)) {
            RPC_ADPT_VLOG_ERR("Add shmem connect failed to send exchange data, fd: %d.\n", m_fd);
            shm->Unmap(&m_localTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }

        // Recv remote Shm info from remote
        if (RecvSocketData(m_fd, &remote_msg, sizeof(ExchangeData), NEGOTIATE_TIMEOUT_MS) != sizeof(ExchangeData)) {
            RPC_ADPT_VLOG_ERR("Add shmem connect failed to recv remote exchange data, fd: %d.\n", m_fd);
            shm->Unmap(&m_localTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }

        m_remoteTrxShm.len = remote_msg.shmem_length;
        m_remoteTrxShm.fd = m_fd;
        ret = strncpy_s(m_remoteTrxShm.name, SHM_MAX_NAME_BUFF_LEN, remote_msg.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Add shmem connect copy remote shared memory name failed, ret %d.\n", ret);
            shm->Unmap(&m_localTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }

        // Map remote shared memory to m_remoteTrxShm.addr
        shm->Map(&m_remoteTrxShm);

        AddrInfo localAddInfo {m_localTrxShm.addr, m_localTrxShm.len};
        AddrInfo remoteAddInfo {m_remoteTrxShm.addr, m_remoteTrxShm.len};
        RingBufferOpt ringBufferOpt {};
        m_ringbuffer = std::make_unique<RingBuffer>(ringBufferOpt, localAddInfo, remoteAddInfo);
        RPC_ADPT_VLOG_WARN("[InitRingBuffer] localAddInfo, addr %p, m_remoteTrxShm %p.\n",
            m_localTrxShm.addr, m_remoteTrxShm.addr);

        m_txState = SocketState::SOCKET_STATE_CONNECTED;
        m_rxState = SocketState::SOCKET_STATE_CONNECTED;

        // Send sever sync done message
        if (SendSocketData(
            m_fd, &send_sync_msg, sizeof(send_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(send_sync_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to send sync done message, fd: %d\n", m_fd);
            shm->Unmap(&m_localTrxShm);
            shm->Unmap(&m_remoteTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }

        if (RecvSocketData(
            m_fd, &recv_sync_msg, sizeof(recv_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(recv_sync_msg) ||
            strcmp(recv_sync_msg, UMQ_BIND_SYNC_MSG) != 0) {
            RPC_ADPT_VLOG_ERR("Failed to receive sync done message, fd: %d\n", m_fd);
            shm->Unmap(&m_localTrxShm);
            shm->Unmap(&m_remoteTrxShm);
            shm->Free(&m_localTrxShm);
            return -1;
        }

        Brpc::Context *context = Brpc::Context::GetContext();
        if (context && context->GetUsePolling()) {
            Socket *sock = NULL;
            if (PollingEpoll::GetInstance().SocketCreate(&sock, m_fd, SocketType::SOCKET_TYPE_TCP_CLIENT,
                                                         0) != 0) {
                RPC_ADPT_VLOG_ERR("SocketCreate failed \n");
            } else {
                PollingEpoll::GetInstance().AddSocket(m_fd, sock);
            }
        }

        RPC_ADPT_VLOG_INFO("Client connection create sucess, local shmem name \"%s\", remote shmem name \"%s\".\n",
            m_localTrxShm.name, m_remoteTrxShm.name);
        return 0;
    }

    ALWAYS_INLINE void NewOriginEpollError()
    {
        m_closed.store(true, std::memory_order_relaxed);
    }

    ALWAYS_INLINE void NewOriginEpollIn(bool use_polling = false)
    {
        m_epoll_in_msg_recv_size =
            OsAPiMgr::GetOriginApi()->recv(m_fd, (void *)&m_epoll_in_msg, sizeof(uint8_t), MSG_NOSIGNAL);
        if (m_epoll_in_msg_recv_size == 0) {
            m_closed.store(true, std::memory_order_relaxed);
        } else if (!use_polling) {
            RPC_ADPT_VLOG_ERR("Unexpected EPOLLIN event through TCP\n");
        }
    }

    int ChangeWritePollingState(int new_state)
    {
        return m_pollingWriteState.exchange(new_state);
    }

    PollingErrCode IsShmReadable(uint32_t event) override
    {
        // 【可重入方案】读取当前状态
        int currentState = m_pollingWriteState.load(std::memory_order_acquire);
        // 如果正在关闭或已关闭，直接返回
        if (currentState >= POLLING_CLOSING) {
            return PollingErrCode::NOT_READY;
        }
        
        // 如果是 USING 状态，可重入执行（不修改状态）
        bool needResetState = false;
        if (currentState == POLLING_NONE) {
            // 尝试从 NONE 转到 USING
            int expected = POLLING_NONE;
            if (!m_pollingWriteState.compare_exchange_strong(expected, POLLING_USING,
                std::memory_order_acq_rel)) {

                // CAS 失败，检查是否进入了 CLOSING 状态
                if (expected >= POLLING_CLOSING) {
                    return PollingErrCode::NOT_READY;
                }
                // 否则是已经是 USING，可重入执行
            } else {
                // CAS 成功，标记需要恢复状态
                needResetState = true;
            }
        }
        // else: currentState == POLLING_USING，可重入，直接执行

        // 执行实际的读取操作
        int res = m_ringbuffer->GetReadEvent(event);

        // 如果是我们设置的 USING 状态，需要恢复
        if (needResetState) {
            // 尝试恢复为 NONE，如果失败说明析构函数已介入
            int expected = POLLING_USING;
            if (!m_pollingWriteState.compare_exchange_strong(expected, POLLING_NONE,
                std::memory_order_acq_rel)) {
                // 析构函数已设置为 CLOSING，通知它我们已完成
                m_pollingWriteState.store(POLLING_CLOSED, std::memory_order_release);
                return PollingErrCode::NOT_READY;
            }
        }

        if (res == 0) {
            return PollingErrCode::OK;
        }
        return PollingErrCode::ERR;
    }

    PollingErrCode IsShmWriteable(uint32_t event) override
    {
        // 【可重入方案】读取当前状态
        int currentState = m_pollingWriteState.load(std::memory_order_acquire);

        // 如果正在关闭或已关闭，直接返回
        if (currentState >= POLLING_CLOSING) {
            return PollingErrCode::NOT_READY;
        }

        // 如果是 USING 状态，可重入执行（不修改状态）
        bool needResetState = false;
        if (currentState == POLLING_NONE) {
            // 尝试从 NONE 转到 USING
            int expected = POLLING_NONE;
            if (!m_pollingWriteState.compare_exchange_strong(expected, POLLING_USING,
                std::memory_order_acq_rel)) {
                // CAS 失败，检查是否进入了 CLOSING 状态
                if (expected >= POLLING_CLOSING) {
                    return PollingErrCode::NOT_READY;
                }
                // 否则是已经是 USING，可重入执行
            } else {
                // CAS 成功，标记需要恢复状态
                needResetState = true;
            }
        }
        // else: currentState == POLLING_USING，可重入，直接执行

        // 执行实际的写入操作
        int res = m_ringbuffer->GetWriteEvent(event);

        // 如果是我们设置的 USING 状态，需要恢复
        if (needResetState) {
            // 尝试恢复为 NONE，如果失败说明析构函数已介入
            int expected = POLLING_USING;
            if (!m_pollingWriteState.compare_exchange_strong(expected, POLLING_NONE,
                std::memory_order_acq_rel)) {
                // 析构函数已设置为 CLOSING，通知它我们已完成
                m_pollingWriteState.store(POLLING_CLOSED, std::memory_order_release);
                return PollingErrCode::NOT_READY;
            }
        }

        if (res == 0) {
            return PollingErrCode::OK;
        }
        return PollingErrCode::ERR;
    }

private:
    struct ExchangeData {
        uint64_t magic_number = CONTROL_PLANE_MAGIC_NUMBER;
        size_t shmem_length;
        char name[SHM_MAX_NAME_BUFF_LEN] = {0};
    };

    int DoAccept(int new_fd)
    {
        ExchangeData local_msg;
        ExchangeData remote_msg;
        Shm localTrxShm = {};
        Shm remoteTrxShm = {};
        char send_sync_msg[] = BIND_SYNC_MSG;
        char recv_sync_msg[] = BIND_SYNC_MSG;

        MemSocketFd *socket_fd_obj = nullptr;
        int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        try {
            socket_fd_obj = new MemSocketFd(new_fd, event_fd);
        } catch (std::exception& e) {
            OsAPiMgr::GetOriginApi()->close(event_fd);
            RPC_ADPT_VLOG_ERR("%s\n", e.what());
            return -1;
        }

        auto* shm = ShmMgr::GetShmMgr();

        // Recv remote Shm info from remote
        if (RecvSocketData(new_fd, &remote_msg, sizeof(ExchangeData), CONTROL_PLANE_TIMEOUT_MS) !=
            sizeof(ExchangeData)) {
            RPC_ADPT_VLOG_ERR("Accept shmem connect failed to recv remote exchange data, fd: %d.\n", new_fd);
            return -1;
        }

        remoteTrxShm.len = remote_msg.shmem_length;
        remoteTrxShm.fd = new_fd;
        int ret = strncpy_s(remoteTrxShm.name, SHM_MAX_NAME_BUFF_LEN, remote_msg.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Accept shmem connect copy remote shared memory name failed, ret %d.\n", ret);
            return -1;
        }

        // Map remote shared memory to m_remoteTrxShm.addr
        shm->Map(&remoteTrxShm);

        // Use config to set connectName
        size_t localShmLen = DATA_QUEUE_MAX_SIZE * MB_TO_BYTE;
        localTrxShm.len = localShmLen;
        localTrxShm.fd = new_fd;
        
        if (!Context::GetContext()->GetShmName(localTrxShm, "accept")) {
            RPC_ADPT_VLOG_ERR("Failed to Get SHM name.\n");
            shm->Unmap(&remoteTrxShm);
            return -1;
        }

        // Allocate local shared memory and map it to m_localTrxShm.addr
        shm->Malloc(&localTrxShm);
        shm->Map(&localTrxShm);

        memset(reinterpret_cast<char *>(localTrxShm.addr), 0, localShmLen);

        // Send local Shm info to remote
        local_msg.shmem_length = localTrxShm.len;
        ret = strncpy_s(local_msg.name, SHM_MAX_NAME_BUFF_LEN, localTrxShm.name, SHM_MAX_NAME_BUFF_LEN - 1);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Accept shmem connect copy local shared memory name failed, ret %d.\n", ret);
            shm->Unmap(&localTrxShm);
            shm->Unmap(&remoteTrxShm);
            shm->Free(&localTrxShm);
            return -1;
        }

        if (SendSocketData(new_fd, &local_msg, sizeof(ExchangeData), NEGOTIATE_TIMEOUT_MS) != sizeof(ExchangeData)) {
            RPC_ADPT_VLOG_ERR("Accept shmem connect failed to send exchange data, fd: %d.\n", new_fd);
            shm->Unmap(&localTrxShm);
            shm->Unmap(&remoteTrxShm);
            shm->Free(&localTrxShm);
            return -1;
        }

        // Delete existing objects and record new objects in the list.
        Fd<::SocketFd>::OverrideFdObj(new_fd, socket_fd_obj);

        Brpc::Context *context = Brpc::Context::GetContext();
        if (context && context->GetUsePolling()) {
            Socket *sock = NULL;
            if (PollingEpoll::GetInstance().SocketCreate(&sock, new_fd, SocketType::SOCKET_TYPE_TCP_SERVER, 0) != 0) {
                RPC_ADPT_VLOG_ERR("SocketCreate failed \n");
            } else {
                PollingEpoll::GetInstance().AddSocket(new_fd, sock);
            }
        }

        socket_fd_obj->SetLocalTrxShm(localTrxShm);
        socket_fd_obj->SetRemoteTrxShm(remoteTrxShm);
        RingBufferOpt ringBufferOpt {};
        socket_fd_obj->InitRingBuffer(ringBufferOpt);

        socket_fd_obj->SetTxState(SocketState::SOCKET_STATE_CONNECTED);
        socket_fd_obj->SetRxState(SocketState::SOCKET_STATE_CONNECTED);

        // Send sever sync done message
        if (SendSocketData(
            new_fd, &send_sync_msg, sizeof(send_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(send_sync_msg)) {
            RPC_ADPT_VLOG_ERR("Failed to send sync done message, fd: %d\n", new_fd);
            shm->Unmap(&localTrxShm);
            shm->Unmap(&remoteTrxShm);
            shm->Free(&localTrxShm);
            return -1;
        }

        if (RecvSocketData(
            new_fd, &recv_sync_msg, sizeof(recv_sync_msg), CONTROL_PLANE_TIMEOUT_MS) != sizeof(recv_sync_msg) ||
            strcmp(recv_sync_msg, UMQ_BIND_SYNC_MSG) != 0) {
            RPC_ADPT_VLOG_ERR("Failed to receive sync done message, fd: %d\n", new_fd);
            shm->Unmap(&localTrxShm);
            shm->Unmap(&remoteTrxShm);
            shm->Free(&localTrxShm);
            return -1;
        }

        RPC_ADPT_VLOG_INFO("Server connection create sucess, local shmem name \"%s\", remote shmem name \"%s\".\n",
            localTrxShm.name, remoteTrxShm.name);
        return 0;
    }

    Shm m_localTrxShm = {};
    Shm m_remoteTrxShm = {};
    SocketState m_txState = SocketState::SOCKET_STATE_NONE;
    SocketState m_rxState = SocketState::SOCKET_STATE_NONE;
    uint16_t m_tx_window_capacity = 0; // the capacity of TX window size
    uint16_t m_rx_window_capacity = 0; // the capacity of RX window size
    int m_event_fd;
    uint64_t m_magic_number = 0;
    uint32_t m_magic_number_recv_size = 0;
    std::unique_ptr<RingBuffer> m_ringbuffer;
    std::atomic<bool> m_closed{false};
    uint8_t m_epoll_in_msg_recv_size = 0;
    uint8_t m_epoll_in_msg = 0;
    std::atomic<int> m_pollingWriteState{POLLING_NONE};
};

class MemEpollEvent : public ::EpollEvent {
public:
    void CleanUp()
    {
    }
    
    MemEpollEvent(int fd, struct epoll_event *event): ::EpollEvent(fd, event)
    {
        MemSocketFd *socket_fd_obj = (MemSocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr || socket_fd_obj->UseTcp()) {
            return;
        }
    }

    virtual ~MemEpollEvent()
    {
        CleanUp();
    }

    virtual int AddEpollEvent(int epoll_fd, bool use_polling = false) override
    {
        if (::EpollEvent::AddEpollEvent(epoll_fd, use_polling) < 0) {
            return -1;
        }

        return 0;
    }

    virtual int ModEpollEvent(int epoll_fd, struct epoll_event *event, bool use_polling = false) override
    {
        if (::EpollEvent::ModEpollEvent(epoll_fd, event, use_polling) < 0) {
            return -1;
        }

        MemSocketFd *socket_fd_obj = (MemSocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || socket_fd_obj->UseTcp()) {
            return 0;
        }

        return 0;
    }

    virtual int DelEpollEvent(int epoll_fd, bool use_polling = false) override
    {
        if (::EpollEvent::DelEpollEvent(epoll_fd, use_polling) < 0) {
            return -1;
        }

        MemSocketFd *socket_fd_obj = (MemSocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (socket_fd_obj == nullptr || socket_fd_obj->UseTcp()) {
            return 0;
        }

        return 0;
    }

    virtual int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                  bool use_polling = false) override
    {
        MemSocketFd *socket_fd_obj = (MemSocketFd *)Fd<::SocketFd>::GetFdObj(m_fd);
        if (input_event->events & EPOLLOUT) {
            if (socket_fd_obj != nullptr && !socket_fd_obj->UseTcp()) {
                if (socket_fd_obj->DoConnect() != 0) {
                    Fd<::SocketFd>::OverrideFdObj(m_fd, nullptr);
                    /* Clear messages that already exist on the TCP link to prevent
                     * dirty messages from affecting user data transmission. */
                    (void)MemSocketFd::FlushSocketMsg(m_fd);
                }
            } else {
                // EPOLLOUT events from original socket fd will be ignored
                return 0;
            }
        }

        if ((input_event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) &&
            socket_fd_obj != nullptr && !socket_fd_obj->RxUseTcp()) {
            socket_fd_obj->NewOriginEpollError();
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event, use_polling);
        }

        if ((input_event->events & EPOLLIN) &&
            socket_fd_obj != nullptr && !socket_fd_obj->RxUseTcp()) {
            socket_fd_obj->NewOriginEpollIn(use_polling);
            return ::EpollEvent::ProcessEpollEvent(input_event, output_event, use_polling);
        }

        return ::EpollEvent::ProcessEpollEvent(input_event, output_event, use_polling);
    }
};

class MemEpollFd : public ::EpollFd {
public:
    MemEpollFd(int epoll_fd) : ::EpollFd(epoll_fd) {}
    
    virtual ~MemEpollFd() {}

    virtual ALWAYS_INLINE int EpollCtlAdd(int fd, struct epoll_event *event, bool use_polling = false) override
    {
        if (event == nullptr) {
            RPC_ADPT_VLOG_ERR("Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }

        MemSocketFd *socket_fd_obj = (MemSocketFd *)Fd<::SocketFd>::GetFdObj(fd);
        if (socket_fd_obj == nullptr) {
            return ::EpollFd::EpollCtlAdd(fd, event, use_polling);
        }

        if (m_epoll_event_map.count(fd) > 0) {
            RPC_ADPT_VLOG_ERR("Origin epoll control add duplicated, epfd: %d, fd: %d\n", m_fd, fd);
            errno = EEXIST;
            return -1;
        }

        MemEpollEvent *epoll_event = nullptr;
        try {
            epoll_event = new MemEpollEvent(fd, event);
        } catch (std::exception& e) {
            RPC_ADPT_VLOG_ERR("%s\n", e.what());
            return -1;
        }

        if (epoll_event->AddEpollEvent(m_fd, use_polling) < 0) {
            delete epoll_event;
            return -1;
        }

        m_epoll_event_map[fd] = epoll_event;

        return 0;
    }
};

}
#endif  // UBSMEM_PROTOCOL_ENABLE

#endif  // MEM_FILE_DESCRIPTOR_H