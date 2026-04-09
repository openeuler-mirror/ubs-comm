/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
*/

#ifndef BRPC_CONTEXT_H
#define BRPC_CONTEXT_H

#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>
#include "file_descriptor.h"
#include "brpc_configure_settings.h"
#include "brpc_iobuf_adapter.h"
#include "brpc_dynsym_scanner.h"
#include "statistics.h"
#include "share_jfr_common.h"
#include "umq_types.h"
#include "ubs_mem/shm.h"
#include "print_stats_mgr.h"
#include "utracer.h"

namespace Brpc {

class Context : public Brpc::ConfigSettings {
public:
    static const uint32_t RECLAIM_INTERVAL = 100;
    static const uint16_t DEFAULT_INITIAL_CREDIT = 1024;
    static const uint16_t DEFAULT_MAX_CREDITS_REQUEST = 1024;

    static ALWAYS_INLINE Context *GetContext()
    {
#ifdef UBSOCKET_ENABLE_INTERCEPT
        /* To avoid recursively constructing 'Brpc::Context'
         * .e.g., In the QEMU environment, the following call chain may lead to it.
         * Brpc::Context() -> umq_init() -> urma_init() -> epoll_creat() -> Brpc::Context() */
         static thread_local bool constructing = false;
         if (constructing || m_socket_fd_trans_mode == SOCKET_FD_TRANS_MODE_TCP) {
            return nullptr;
         }

         constructing = true;
         static Context context;
         constructing = false;
         return &context;
#else
         static Context context;
         return &context;
#endif
    }

    static ALWAYS_INLINE void SetUbEnable()
    {
        m_ubEnable = true;
    }

    static ALWAYS_INLINE bool GetUbEnableFlag()
    {
        return m_ubEnable;
    }

    ::SocketFd *CreateSocketFd(int fd);
    ::SocketFd *CreateSocketFd(int fd, int event_fd);
    ::EpollFd *CreateEpollFd(int fd);

    static ALWAYS_INLINE int FetchAdd(void)
    {
        return m_ref.fetch_add(1, std::memory_order_acq_rel);
    }

    static ALWAYS_INLINE int FetchSub(void)
    {
        return m_ref.fetch_sub(1, std::memory_order_acq_rel);
    }

    static void CleanContext()
    {
        switch (m_socket_fd_trans_mode) {
            case SOCKET_FD_TRANS_MODE_UMQ:
            case SOCKET_FD_TRANS_MODE_UMQ_ZERO_COPY:
                umq_uninit();
                break;
            case SOCKET_FD_TRANS_MODE_SHM:
                ShmMgr::GetShmMgr()->Finalize();
                break;
            case SOCKET_FD_TRANS_MODE_TCP:
            default:
                break;    
        }
    }

    // 判断是否ub链接
    static bool IsProtocolByUb(int fd);

    bool IsBonding()
    {
        return isBonding;
    }

    int GetProcessSocketId()
    {
        return m_process_socket_id;
    }

    std::vector<uint32_t> GetAllSocketId()
    {
        return m_socket_ids;
    }

    bool GetShmName(Shm &shm, const char *prefix)
    {
        if (!prefix) {
            return false;
        }

        uint64_t seq = m_shmNameSeq.fetch_add(1, std::memory_order_relaxed);
        pid_t pid = getpid();
        int written = std::snprintf(shm.name, MAX_REGION_NAME_DESC_LENGTH, "%s_%d_%llu", prefix, static_cast<int>(pid),
                                    static_cast<unsigned long long>(seq));
        if (written < 0 || static_cast<size_t>(written) >= MAX_REGION_NAME_DESC_LENGTH) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "SHM name too long: prefix=\"%s\", pid=%d, seq=%llu", prefix, pid,
                              seq);
            shm.name[0] = '\0';
            return false;
        }

        return true;
    }

#ifdef UBS_SHM_BUILD_ENABLED
    void InitShm()
    {
        if (!ShmMgr::GetShmMgr()->IsInitialized()) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to initialize shm for brpc\n");
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }
        SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_SHM);
    }
#endif

    /// 注册 info 对应设备的 AE 事件
    int RegisterAsyncEvent(umq_trans_info_t info);

private:
    void RecordAndSetBrpcAllocator()
    {
        m_alloc_addr_origin = *m_alloc_addr;
        m_dealloc_addr_origin = *m_dealloc_addr;
        *m_alloc_addr = Brpc::IOBuf::blockmem_allocate_zero_copy;
        *m_dealloc_addr = Brpc::IOBuf::blockmem_deallocate_zero_copy;
    }

    void ResetBrpcAllocator()
    {
        if (m_alloc_addr != nullptr) {
            *m_alloc_addr = m_alloc_addr_origin;
        }
        if (m_dealloc_addr != nullptr) {
            *m_dealloc_addr = m_dealloc_addr_origin;
        }
    }

    static int GlobalLockInit();

    Context() : Brpc::ConfigSettings()
    {
        m_asyncEventRegistryMutex = g_external_lock_ops.create(LT_EXCLUSIVE);
        if (GlobalLockInit() != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to initialize global lock for brpc\n");
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }
        if(Brpc::ConfigSettings::Init() != 0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to initialize configure settings for brpc\n");
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }

        if (Statistics::UTracerInit() != 0) {
            RPC_ADPT_VLOG_WARN("Init tracer instance failed. Delay trace will not in use!");
        }

        if (m_use_brpc_zcopy) {
            if(GetBrpcAllocSymStr() != nullptr && GetBrpcDeallocSymStr() != nullptr){
                RecordApi(RTLD_DEFAULT, GetBrpcAllocSymStr(), m_alloc_addr, ubsocket::UTIL_VLOG_LEVEL_WARN);
                RecordApi(RTLD_DEFAULT, GetBrpcDeallocSymStr(), m_dealloc_addr, ubsocket::UTIL_VLOG_LEVEL_WARN);
                if(m_alloc_addr == nullptr || m_dealloc_addr == nullptr) {
                    RPC_ADPT_VLOG_WARN("Failed to load and replace allocate(%s)/deallocate(%s) "
                                       "function for brpc, try to scan ELF\n",
                                       GetBrpcAllocSymStr(), GetBrpcDeallocSymStr());
                }
            }

            if(m_alloc_addr == nullptr || m_dealloc_addr == nullptr){
                DynSymScanner scanner;
                if(!scanner.ParseBrpcAllocator()){
                    SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                    return;
                }

                m_alloc_addr = scanner.GetBrpcAllocSymAddr();
                m_dealloc_addr = scanner.GetBrpcDeallocSymAddr();
            }
            RecordAndSetBrpcAllocator();
        }

#ifdef UBS_SHM_BUILD_ENABLED
        ResetBrpcAllocator();
        SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
        return;
#endif

        umq_init_cfg_t umq_config;
        memset_s(&umq_config, sizeof(umq_config), 0, sizeof(umq_config));
        umq_config.feature = UMQ_FEATURE_API_PRO | UMQ_FEATURE_ENABLE_FLOW_CONTROL;
        umq_config.buf_mode = UMQ_BUF_SPLIT;
        umq_config.io_lock_free = true;
        umq_config.trans_info_num = 1;
        umq_config.flow_control.use_atomic_window = true;
        umq_config.flow_control.initial_credit = DEFAULT_INITIAL_CREDIT;
        umq_config.flow_control.max_credits_request = DEFAULT_MAX_CREDITS_REQUEST;
        umq_config.flow_control.min_reserved_credit = GetMinReservedCredit();
        umq_config.block_cfg.small_block_size = GetIOBlockType();
        umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DUMMY;
        umq_config.trans_info[0].mem_cfg.total_size = GetIOTotalSize();
        umq_config.trans_info[0].trans_mode = GetTransMode();

        int ret = umq_init(&umq_config);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to execute umq init\n");
            ResetBrpcAllocator();
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }

        int epfd = OsAPiMgr::GetOriginApi()->epoll_create(1);
        if (epfd < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to create epoll for async events.\n");
            return;
        }

        m_asyncEventEpollFd = epfd;
        m_asyncEventThread = std::thread(&Context::AsyncEventProcess, this);

        umq_trans_mode_t transMode = GetTransMode();
        switch (transMode) {
            case UMQ_TRANS_MODE_IB:
                ret = AddIbDev(umq_config.trans_info[0]);
                break;
            case UMQ_TRANS_MODE_UB:
                ret = AddUbDev(umq_config.trans_info[0]);
                break;
            default:
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Un-supported protocol.\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
        }

        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to execute umq init\n");
            ResetBrpcAllocator();
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }

        if (GetDevSchedulePolicy() == dev_schedule_policy::CPU_AFFINITY ||
            GetDevSchedulePolicy() == dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
            m_process_socket_id = GetCurrentProcessSocketId();
            m_socket_ids = GetSocketIdsViaNumaSysfs();
            if (m_socket_ids.empty() || m_process_socket_id==-1) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed get socket id in cpu affinity policy.\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
            }
        }

        SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_UMQ);

        if (m_stats_enable) {
            // Get global statistics manager to invoke construction
            (void)Statistics::GlobalStatsMgr::GetGlobalStatsMgr();
        }

        if (m_trace_enable) {
            Statistics::PrintStatsMgr::StartStatsCollection(
                GetUbsocketTraceTime(), GetUbsocketTraceFilePath(), GetUbsocketTraceFileSize());
        }
    }

    virtual ~Context()
    {
        // 通知 AE 线程关闭
        m_asyncEventThreadStopFlag.store(true);
        if (m_asyncEventThread.joinable()) {
            m_asyncEventThread.join();
        }

        if (m_asyncEventEpollFd != -1) {
            OsAPiMgr::GetOriginApi()->close(m_asyncEventEpollFd);
        }

        m_socket_fd_trans_mode = SOCKET_FD_TRANS_MODE_UNSET;
        auto start = std::chrono::high_resolution_clock::now();
        while (m_ref.load() > 0) {
            if (IsTimeout(start, RECLAIM_INTERVAL)) {
                /* The child threads are still accessing umq resources, so umq_uninit() is not actively
                 * executed here to avoid core dumps. Let the program automatically reclaim all resources. */
                RPC_ADPT_VLOG_WARN("Context reclamation has exceeded the time limit and was forcefully terminated\n");
                return;
            }
        }

        CleanContext();
        MainSubUmqTable::Clean();

        if (m_trace_enable) {
            Statistics::PrintStatsMgr::StopStatsCollection();
        }
        g_external_lock_ops.destroy(m_asyncEventRegistryMutex);

        RPC_ADPT_VLOG_INFO("Context reclaimed successfully.\n");
    }

    int AddIbDev(umq_trans_info_t &trans_info)
    {
        const char *dev_info = GetDevIpStr();
        if (dev_info == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to use ib, beacuse ipv4/ipv6 address is null\n");
            return -1;
        }

        trans_info.mem_cfg.total_size = GetIOTotalSize();
        trans_info.trans_mode = UMQ_TRANS_MODE_IB;
        trans_info.dev_info.assign_mode = IsDevIpv6() ? UMQ_DEV_ASSIGN_MODE_IPV6 : UMQ_DEV_ASSIGN_MODE_IPV4;
        int addrSize = IsDevIpv6() ? UMQ_IPV6_SIZE : UMQ_IPV4_SIZE;
        int ret = sprintf_s(trans_info.dev_info.ipv4.ip_addr, addrSize, "%s", dev_info);
        if (ret < 0 || ret >= addrSize) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to sprintf_s ipv4/ipv6 address\n");
            return -1;
        }

        ret = umq_dev_add(&trans_info);
        if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to add umq dev, ret %d\n", ret);
            return -1;
        }
        return 0;
    }

    int AddUbDev(umq_trans_info_t &trans_info)
    {
        const char *dev_info = GetDevNameStr();
        if (dev_info == nullptr) {
            if (FindDevName() != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find bonding dev, need active input.\n");
                return -1;
            }
        }
        dev_info = GetDevNameStr();
        trans_info.mem_cfg.total_size = GetIOTotalSize();
        trans_info.trans_mode = UMQ_TRANS_MODE_UB;
        int ret = sprintf_s(trans_info.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "%s", dev_info);
        if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to sprintf_s device name\n");
            return -1;
        }

        if (strstr(trans_info.dev_info.dev.dev_name, "bonding_dev") == nullptr) {
            trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            trans_info.dev_info.dev.eid_idx = GetEidIdx();
        } else {
            trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            trans_info.dev_info.eid.eid = GetDevSrcEid();
            isBonding = true;
        }

        ret = umq_dev_add(&trans_info);
        if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to add umq dev, ret %d\n", ret);
            return -1;
        }

        return RegisterAsyncEvent(trans_info);
    }

    int FindDevName()
    {
        umq_trans_mode_t transMode = UMQ_TRANS_MODE_UB;
        int devCount = 0;
        umq_dev_info_t *umqDevInfo = umq_dev_info_list_get(transMode, &devCount);
        if (umqDevInfo == nullptr || devCount <= 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to add umq dev info list get.\n");
            return -1;
        }

        int index = 0;
        int bondingIndex = -1;
        for (; index < devCount; ++index) {
            const char *name = umqDevInfo[index].dev_name;
            if (strcmp(name, "bonding_dev_0") == 0) {
                bondingIndex = index;
                break;
            }
            if ((bondingIndex == -1) && (strstr(name, "bonding_dev_") != nullptr)) {
                bondingIndex = index;
            }
        }
        if ((bondingIndex == -1) || (bondingIndex > devCount) || (umqDevInfo[bondingIndex].ub.eid_cnt == 0)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to find bonding dev in the environment.\n");
            return -1;
        }
        int ret = sprintf_s(m_dev_name_str, UMQ_DEV_NAME_SIZE, "%s", umqDevInfo[bondingIndex].dev_name);
        if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to sprintf_s device name\n");
            return -1;
        }

        m_src_eid = umqDevInfo[bondingIndex].ub.eid_list[0].eid;
        umq_dev_info_list_free(transMode, umqDevInfo);
        return 0;
    }

    // 解析cpulist字符串，例如0~24，返回0
    int GetFirstCpuFromCpulist(const std::string& cpuListStr);

    // 从 CPU ID 获取其 Socket ID（physical_package_id）
    int GetSocketIdOfCpu(int cpu);

    // 获取所有 Socket ID
    std::vector<uint32_t> GetSocketIdsViaNumaSysfs();

    // 通过 NUMA 节点获取所有 Socket ID
    std::vector<uint32_t> GetSocketIdsViaNuma();

    // CPU 扫描方式获取 Socket IDs
    std::vector<uint32_t> GetSocketIdsViaCpuScan();

    // 获得当前进程socketID
    int GetCurrentProcessSocketId();

    void AsyncEventProcess();
    void AsyncEventHandle(const umq_async_event_t *av);

    static const char* CPU_LIST_PREFIX_PATH;
    static const char* CPU_LIST_SUFFIX_PATH;
    static const char* SOCKET_ID_PERFIX_PATH;
    static const char* SOCKET_ID_SUFFIX_PATH;

    static std::atomic<int> m_ref;
    IOBuf::blockmem_allocate_t *m_alloc_addr = nullptr; // store scanned address of alloc function
    IOBuf::blockmem_deallocate_t *m_dealloc_addr = nullptr; // store scanned address of dealloc function
    IOBuf::blockmem_allocate_t m_alloc_addr_origin = nullptr; // store original alloc function address
    IOBuf::blockmem_deallocate_t m_dealloc_addr_origin = nullptr; // store original dealloc function address
    bool isBonding = false;
    int m_process_socket_id = -1;
    std::vector<uint32_t> m_socket_ids;
    static std::atomic<int> m_shmNameSeq;
    static bool m_ubEnable;

    // AE 事件处理
    u_external_mutex_t* m_asyncEventRegistryMutex;
    std::map<int, umq_trans_info_t> m_asyncEventRegistry;
    int m_asyncEventEpollFd = -1;
    std::atomic<bool> m_asyncEventThreadStopFlag{false};
    std::thread m_asyncEventThread;
};

}  // namespace Brpc

#endif
