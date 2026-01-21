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
#include "file_descriptor.h"
#include "brpc_configure_settings.h"
#include "brpc_iobuf_adapter.h"
#include "brpc_dynsym_scanner.h"
#include "statistics.h"
#include "umq_types.h"

namespace Brpc {

class Context : public Brpc::ConfigSettings {
    public:
    static const uint32_t RECLAIM_INTERVAL = 100;

    static ALWAYS_INLINE Context *GetContext()
    {
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
            case SOCKET_FD_TRANS_MODE_TCP:
            default:
                break;    
        }
    }

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
        *m_alloc_addr = m_alloc_addr_origin;
        *m_dealloc_addr = m_dealloc_addr_origin;
    }

    Context() : Brpc::ConfigSettings()
    {
        if(Brpc::ConfigSettings::Init() != 0){
            RPC_ADPT_VLOG_ERR("Failed to initialize configure settings for brpc\n");
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }

        char *env_ptr = getenv(ENV_VAR_USE_ZCOPY);
        if (env_ptr == NULL) {
            if(GetBrpcAllocSymStr() != nullptr && GetBrpcDeallocSymStr() != nullptr){
                RecordApi(RTLD_DEFAULT, GetBrpcAllocSymStr(), m_alloc_addr);
                RecordApi(RTLD_DEFAULT, GetBrpcDeallocSymStr(), m_dealloc_addr);
                if(m_alloc_addr == nullptr || m_dealloc_addr == nullptr){
                    RPC_ADPT_VLOG_WARN("Failed to load and replace allocate(%s)/deallocate(%s) "
                        "function for brpc, try to scan ELF\n", GetBrpcAllocSymStr(), GetBrpcDeallocSymStr());
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

        umq_init_cfg_t umq_config;
        memset_s(&umq_config, sizeof(umq_config), 0, sizeof(umq_config));
        umq_config.feature = UMQ_FEATURE_API_PRO | UMQ_FEATURE_ENABLE_FLOW_CONTROL;
        umq_config.buf_mode = UMQ_BUF_SPLIT;
        umq_config.io_lock_free = true;
        umq_config.trans_info_num = 1;
        umq_config.flow_control.use_atomic_window = true;
        umq_config.block_cfg.small_block_size = GetIOBlockType();

        const char *dev_info = nullptr;
        int ret = -1;
        if ((dev_info = GetDevIpStr()) != nullptr){
            if(IsDevIpv6()){
                umq_config.trans_info[0].mem_cfg.total_size = GetIOTotalSize();
                umq_config.trans_info[0].trans_mode = GetTransMode();
                umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
                ret = sprintf_s(umq_config.trans_info[0].dev_info.ipv6.ip_addr, UMQ_IPV6_SIZE, "%s", dev_info);
                if(ret < 0 || ret >= UMQ_IPV6_SIZE){
                    RPC_ADPT_VLOG_ERR("Failed to sprintf_s ipv6 address\n");
                    ResetBrpcAllocator();
                    SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                    return;
                }
            } else {
                umq_config.trans_info[0].mem_cfg.total_size = GetIOTotalSize();
                umq_config.trans_info[0].trans_mode = GetTransMode();
                umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
                ret = sprintf_s(umq_config.trans_info[0].dev_info.ipv4.ip_addr, UMQ_IPV4_SIZE, "%s", dev_info);
                if (ret < 0 || ret >= UMQ_IPV4_SIZE) {
                    RPC_ADPT_VLOG_ERR("Failed to sprintf_s ipv4 address\n");
                    ResetBrpcAllocator();
                    SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                    return;
                }
            }
        } else if ((dev_info = GetDevNameStr()) != nullptr) {
            umq_config.trans_info[0].mem_cfg.total_size = GetIOTotalSize();
            umq_config.trans_info[0].trans_mode = GetTransMode();
            ret = sprintf_s(umq_config.trans_info[0].dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "%s", dev_info);
            if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
                RPC_ADPT_VLOG_ERR("Failed to sprintf_s device name\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
            }
            if (strstr(umq_config.trans_info[0].dev_info.dev.dev_name, "bonding_dev") == nullptr) {
                umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
                umq_config.trans_info[0].dev_info.dev.eid_idx = GetEidIdx();
            }else{
                umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
                umq_config.trans_info[0].dev_info.eid.eid = GetDevSrcEid();
                isBonding = true;
            }
        }

        if (GetDevIpStr() == nullptr && GetDevNameStr() == nullptr) {
            umq_config.trans_info[0].mem_cfg.total_size = GetIOTotalSize();
            umq_config.trans_info[0].trans_mode = GetTransMode();
            ret = sprintf_s(umq_config.trans_info[0].dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "%s", "bonding_dev_0");
            if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
                RPC_ADPT_VLOG_ERR("Failed to sprintf_s device name\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
            }
            umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            umq_config.trans_info[0].dev_info.dev.eid_idx = GetEidIdx();
            isBonding = true;
        }

        if (GetDevSchedulePolicy() == dev_schedule_policy::CPU_AFFINITY) {
            m_process_socket_id = GetCurrentProcessSocketId();
            m_socket_ids = GetSocketIdsViaNumaSysfs();
            if (m_socket_ids.empty() || m_process_socket_id==-1) {
                RPC_ADPT_VLOG_ERR("Failed get socket id in cpu affinity policy.\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
            }
        }

        ret = umq_init(&umq_config);
        if(ret != 0){
            RPC_ADPT_VLOG_ERR("Failed to execute umq init\n");
            ResetBrpcAllocator();
            SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
            return;
        }

        if (GetDevIpStr() == nullptr && GetDevNameStr() == nullptr) {
            umq_dev_info_t umq_dev_info = {};
            char devName[] = "bonding_dev_0";
            int infoGetRet = umq_dev_info_get(devName, UMQ_TRANS_MODE_UB, &umq_dev_info);
            if (infoGetRet != 0) {
                RPC_ADPT_VLOG_ERR("Failed to get bonding device information\n");
                ResetBrpcAllocator();
                SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
                return;
            }
            uint32_t i = 0;
            for (; i < umq_dev_info.ub.eid_cnt; ++i) {
                if (umq_dev_info.ub.eid_list[i].eid_index == GetEidIdx()) {
                    m_src_eid = umq_dev_info.ub.eid_list[i].eid;
                    break;
                }
            }
            if (i == umq_dev_info.ub.eid_cnt) {
                RPC_ADPT_VLOG_ERR("Failed to find bonding eid\n");
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
    }

    virtual ~Context()
    {
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

        RPC_ADPT_VLOG_INFO("Context reclaimed successfully.\n");
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
};     

}

#endif