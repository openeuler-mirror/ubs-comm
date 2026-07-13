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
#include "umq_backend.h"

#include <algorithm>
#include <cstring>

#include "core/ubsocket_event_epoll.h"
#include "umq_conn_helper.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_setting.h"
#include "umq_share_jfr_epoll_runner_ops.h"
#include "umq_socket.h"
#include "umq_transport_pool.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
std::mutex UmqBackend::UMQ_MUTEX;
bool UmqBackend::UMQ_INITED = false;

Result UmqBackend::Init() noexcept
{
    //UBS_VLOG_DEBUG("enter");
    Result ret = UBS_OK;
    std::lock_guard<std::mutex> guard(UMQ_MUTEX);
    if (UMQ_INITED) {
        //UBS_VLOG_DEBUG("umq already initialized");
        return UBS_OK;
    }

    /* step1: initialize umq settting */
    ret = UmqSetting::Init();
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("UmqSetting::Init() failed, ret: %d\n", ret);
        return ret;
    }

    /* step2: init umq init config */
    umq_init_cfg_t umq_config;
    bzero(&umq_config, sizeof(umq_config));
    umq_config.feature = UMQ_FEATURE_API_PRO |
                         (UmqSetting::UMQ_FLOW_CONTROL_ENABLE ? UMQ_FEATURE_ENABLE_FLOW_CONTROL : 0);
    umq_config.buf_mode = UMQ_BUF_SPLIT;
    umq_config.io_lock_free = true;
    umq_config.trans_info_num = 1;
    umq_config.flow_control.use_atomic_window = true;
    umq_config.flow_control.initial_credit = UmqSetting::UMQ_FC_DEFAULT_CREDIT;
    umq_config.flow_control.max_credits_request = UmqSetting::UMQ_FC_MAX_CREDIT;
    umq_config.flow_control.min_reserved_credit = UmqSetting::UMQ_FC_MIN_CREDIT;
    umq_config.buf_pool_cfg.small_block_size = UmqSetting::IO_BLOCK_TYPE;
    umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DUMMY;
    umq_config.trans_info[0].trans_mode = UmqSetting::UMQ_TRANS_MODE;
    umq_config.buf_pool_cfg.umq_mem_pool_init_size = UmqSetting::UMQ_MEM_POOL_INIT_SIZE_MB * IO_SIZE_MB;
    umq_config.buf_pool_cfg.normal_pool_block_count =
        static_cast<uint32_t>(4ULL * GlobalSetting::UBS_RX_DEPTH + UmqSetting::UMQ_BUF_POOL_DEPTH);
    umq_config.buf_pool_cfg.umq_buf_pool_max_size = UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB * IO_SIZE_MB;
    umq_config.buf_pool_cfg.tls_qbuf_pool_depth = 4ULL * GlobalSetting::UBS_RX_DEPTH + UmqSetting::UMQ_BUF_POOL_DEPTH;
    umq_config.buf_pool_cfg.enable_tiny_pool = UmqSetting::UMQ_TINY_POOL_ENABLE;
    umq_config.buf_pool_cfg.tiny_pool_block_size = UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE;
    umq_config.buf_pool_cfg.tiny_pool_block_count = UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT;
    umq_config.buf_pool_cfg.tls_tiny_pool_depth = UmqSetting::UMQ_TLS_TINY_POOL_DEPTH;
    umq_config.io_lock_free = false;
    umq_config.rq_lock_free = true;

    if (UmqSetting::UMQ_TP_TYPE == POOL) {
        umq_config.tp_pool_cfg.notify_threshold = 1;
    }

    /* step3: init umq */
    ret = UmqApi::umq_init(&umq_config);
    if (ret != 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_init() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return UBS_ERROR;
    }

    switch (UmqSetting::UMQ_TRANS_MODE) {
        case UMQ_TRANS_MODE_IB:
            // ret = AddIbDev(umq_config.trans_info[0]);
            UBS_VLOG_ERR("Un-supported IB protocol.\n");
            break;
        case UMQ_TRANS_MODE_UB:
            ret = AddUbDev(umq_config.trans_info[0]);
            break;
        default:
            UBS_VLOG_ERR("Un-supported protocol.\n");
            return UBS_ERROR;
    }
    if (ret != 0) {
        UBS_VLOG_ERR("AddIbDev()/AddUbDev() failed, ret: %d\n", ret);
        return UBS_ERROR;
    }

    GlobalSetting::LINK_SELECTION_POLICY =
        UmqSetting::UMQ_IS_BONDING && GlobalSetting::UBS_BACKUP_LINK_ENABLED  ? LinkSelectionPolicy::BONDING_BACKUP :
        UmqSetting::UMQ_IS_BONDING && !GlobalSetting::UBS_BACKUP_LINK_ENABLED ? LinkSelectionPolicy::BONDING_ROUTE :
                                                                                LinkSelectionPolicy::RAW_DEVICE;

    UmqSetting::UMQ_PROCESS_SOCKET_ID = SocketConnHelper::GetCurrentProcessSocketId();
    UmqSetting::UMQ_ALL_SOCKET_IDS = SocketConnHelper::GetSocketIdsViaNumaSysfs();
    if (UmqSetting::UMQ_ALL_SOCKET_IDS.empty() || UmqSetting::UMQ_PROCESS_SOCKET_ID == -1) {
        UBS_VLOG_ERR("Failed get socket id in cpu affinity policy.\n");
        return UBS_ERROR;
    }

    /* step4: umq perf start */
    if (GlobalSetting::UBS_PROF_ENABLE) {
        // umq perf start
        ret = UmqApi::umq_stats_perf_start();
        if (ret != UBS_OK) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_stats_perf_start() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            return UBS_UMQ_CREATE;
        }

        // TODO: add UMQ_PERF_QUANTILE_MAX_NUM, 控制统计的精度
        umq_perf_stats_cfg_t perf_stats_cfg;
        ret = UmqApi::umq_stats_perf_reset(&perf_stats_cfg);
        if (ret != UBS_OK) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_stats_perf_reset() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            return UBS_UMQ_CREATE;
        }

        // umq tp perf start (urma)
        ret = UmqApi::umq_stats_tp_perf_start(UmqSetting::UMQ_TRANS_MODE);
        if (ret != UBS_OK) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR(
                "[UMQ_API] umq_stats_tp_perf_start() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret,
                errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            return UBS_UMQ_CREATE;
        }
    }

    // 直接使用 bonding 设备通信，预创建主 umq、jetty 池
    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
        umq_eid_t local_eid;
        uint64_t main_umq_handle = UMQ_INVALID_HANDLE;
        if ((main_umq_handle = CreateShareMainUmq(local_eid)) == UMQ_INVALID_HANDLE) {
            UBS_VLOG_ERR("Failed to init main umq.");
            UmqCleanup();
            return UBS_UMQ_CREATE;
        }
        if (PrefillShareMainUmq(local_eid) != UBS_OK) {
            UBS_VLOG_ERR("Failed to prefill main umq rx.");
            UmqCleanup();
            return UBS_PREFILL_RX;
        }
        if (InitShareJfrMonitering(main_umq_handle)) {
            UBS_VLOG_ERR("Failed to init main umq share jfr event runner.");
            UmqCleanup();
            return UBS_ERROR;
        }
        if (UmqTransportPool::Instance().WarmUp(main_umq_handle) != UBS_OK) {
            UBS_VLOG_ERR("Failed to warm up umq shared pool.");
            UmqCleanup();
            return UBS_UMQ_CREATE;
        }
    }
    UMQ_INITED = true;

    //UBS_VLOG_DEBUG("leave, inited = %d", UMQ_INITED);
    return UBS_OK;
}

void UmqBackend::UmqCleanup() noexcept
{
    UmqApi::umq_uninit();
    UMQ_INITED = false;

    if (GlobalSetting::UBS_PROF_ENABLE) {
        UmqApi::umq_stats_perf_stop();
        UmqApi::umq_stats_tp_perf_stop(UmqSetting::UMQ_TRANS_MODE);
    }
}

void UmqBackend::UnInit() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(UMQ_MUTEX);
    if (!UMQ_INITED) {
        UBS_VLOG_DEBUG("umq not initialized");
        return;
    }

    UmqApi::umq_uninit();
    UMQ_INITED = false;

    if (GlobalSetting::UBS_PROF_ENABLE) {
        UmqApi::umq_stats_perf_stop();
        UmqApi::umq_stats_tp_perf_stop(UmqSetting::UMQ_TRANS_MODE);
    }

    UBS_VLOG_DEBUG("leave, inited = %d", UMQ_INITED);
}

Result UmqBackend::AddUbDev(umq_trans_info_t &trans_info)
{
    // 如果用户没有显式指定设备，则优先使用 bonding_dev_0，如不存在再尝试其他 bonding_dev_ 前缀的设备.
    // 如果用户指定了裸设备则辅助查找 local_eid.
    if (UmqSetting::UMQ_DEV_NAME.empty()) {
        if (FindDevName() != UBS_OK) {
            UBS_VLOG_ERR("Failed to find bonding dev, need active input.\n");
            return UBS_ERROR;
        }
    } else if (std::strncmp(UmqSetting::UMQ_DEV_NAME.c_str(), "udma", 4) == 0) {
        if (FindDevEid(UmqSetting::UMQ_DEV_NAME.c_str(), UmqSetting::UMQ_EID_INDEX) != UBS_OK) {
            UBS_VLOG_ERR("Failed to find udma dev.\n");
            return UBS_ERROR;
        }
    }

    if (UmqSetting::UMQ_DEV_NAME.length() >= DEV_NAME_STR_LEN_MAX) {
        UBS_VLOG_ERR("Device name too long.\n");
        return UBS_ERROR;
    }

    char dev_info[DEV_NAME_STR_LEN_MAX];
    strncpy(dev_info, UmqSetting::UMQ_DEV_NAME.c_str(), DEV_NAME_STR_LEN_MAX - 1);
    dev_info[DEV_NAME_STR_LEN_MAX - 1] = '\0';

    trans_info.trans_mode = UMQ_TRANS_MODE_UB;
    int ret = sprintf(trans_info.dev_info.dev.dev_name, "%s", dev_info);
    if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
        UBS_VLOG_ERR("Failed to sprintf_s device name\n");
        return UBS_ERROR;
    }

    if (strstr(trans_info.dev_info.dev.dev_name, "bonding_dev") == nullptr) {
        trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
        trans_info.dev_info.dev.eid_idx = UmqSetting::UMQ_EID_INDEX;
    } else {
        trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
        trans_info.dev_info.eid.eid = UmqSetting::UMQ_LOCAL_EID;
        UmqSetting::UMQ_IS_BONDING = true;
    }

    ret = UmqApi::umq_dev_add(&trans_info);
    if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_dev_add() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return -1;
    }

    // TODO RegisterAsyncEvent AE事件上报
    // return RegisterAsyncEvent(trans_info);
    return UBS_OK;
}

Result UmqBackend::FindDevName()
{
    umq_trans_mode_t transMode = UMQ_TRANS_MODE_UB;
    int devCount = 0;
    umq_dev_info_t *umqDevInfo = UmqApi::umq_dev_info_list_get(transMode, &devCount);
    if (umqDevInfo == nullptr || devCount <= 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_dev_info_list_get() failed, ret: %p, dev count: %d, "
                     "mapped errno: %d(%s), original errno: %d\n",
                     umqDevInfo, devCount, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
        return UBS_ERROR;
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
        UBS_VLOG_ERR("Failed to find bonding dev in the environment.\n");
        return UBS_ERROR;
    }

    UmqSetting::UMQ_DEV_NAME = umqDevInfo[bondingIndex].dev_name;
    if (UmqSetting::UMQ_DEV_NAME.size() >= UMQ_DEV_NAME_SIZE) {
        UBS_VLOG_ERR("Failed to set device name, name size: %zu\n", UmqSetting::UMQ_DEV_NAME.size());
        return UBS_ERROR;
    }

    UmqSetting::UMQ_LOCAL_EID = umqDevInfo[bondingIndex].ub.eid_list[0].eid;
    UmqApi::umq_dev_info_list_free(transMode, umqDevInfo);
    return UBS_OK;
}

Result UmqBackend::FindDevEid(const char *dev, uint32_t eid_idx)
{
    const umq_trans_mode_t mode = UMQ_TRANS_MODE_UB;
    int count = 0;
    umq_dev_info_t *dev_info_list = UmqApi::umq_dev_info_list_get(mode, &count);
    if (dev_info_list == nullptr || count <= 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_dev_info_list_get() failed, ret: %p, dev count: %d, "
                     "mapped errno: %d(%s), original errno: %d\n",
                     dev_info_list, count, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::BIND_INFO_GET, UMQ_FAIL), savedErrno);
        return UBS_ERROR;
    }
    auto free_on_exit = MakeScopeExit([mode, dev_info_list]() { UmqApi::umq_dev_info_list_free(mode, dev_info_list); });

    // 找到对应的裸设备
    auto di = std::find_if(dev_info_list, dev_info_list + count,
                           [dev](const umq_dev_info_t &info) { return std::strcmp(info.dev_name, dev) == 0; });
    if (di == dev_info_list + count) {
        UBS_VLOG_ERR("There is no such device named \"%s\"\n", dev);
        return UBS_ERROR;
    }

    // 查找 eid_idx
    auto ei = std::find_if(di->ub.eid_list, di->ub.eid_list + di->ub.eid_cnt,
                           [eid_idx](const umq_eid_info_t &info) { return info.eid_index == eid_idx; });
    if (ei == di->ub.eid_list + di->ub.eid_cnt) {
        UBS_VLOG_ERR("The device \"%s\" has no eid_idx numbered %u\n", dev, eid_idx);
        return UBS_ERROR;
    }

    UmqSetting::UMQ_LOCAL_EID = ei->eid;
    return UBS_OK;
}

uint64_t UmqBackend::CreateShareMainUmq(umq_eid_t &local_eid)
{
    umq_create_option_t share_main_umq_cfg;
    memset(&share_main_umq_cfg, 0, sizeof(share_main_umq_cfg));
    UmqConnHelper::NewBaseUmqCreateOptions(share_main_umq_cfg);

    // 声明在分支外，避免悬垂指针导致 umq 创建时，取不到正确的port信息
    std::vector<umq_port_id_t> used_ports;

    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
        share_main_umq_cfg.create_flag |= UMQ_CREATE_FLAG_USED_PORTS;
        umq_eid_t bonding_eid = UmqSetting::UMQ_LOCAL_EID;
        umq_route_list_t route_list;
        if (UmqConnHelper::GetRouteList(route_list, bonding_eid, bonding_eid) != UBS_OK) {
            UBS_VLOG_ERR("Failed to get urma route info.\n");
            return UMQ_INVALID_HANDLE;
        }
        used_ports.reserve(route_list.route_num);
        for (uint32_t i = 0; i < route_list.route_num; ++i) {
            used_ports.push_back(route_list.routes[i].src_port);
        }
        std::sort(used_ports.begin(), used_ports.end(),
                  [](const umq_port_id_t &a, const umq_port_id_t &b) { return a.value < b.value; });
        auto last = std::unique(used_ports.begin(), used_ports.end(),
                                [](const umq_port_id_t &a, const umq_port_id_t &b) { return a.value == b.value; });
        used_ports.erase(last, used_ports.end());
        share_main_umq_cfg.used_ports = {.port = used_ports.data(), .num = static_cast<uint8_t>(used_ports.size())};
    }

    if (std::strncpy(share_main_umq_cfg.name, "ubsocket_main_umq", UMQ_NAME_MAX_LEN) == nullptr) {
        UBS_VLOG_ERR("Failed to set main umq name\n");
        return UMQ_INVALID_HANDLE;
    }

    // init阶段 UMQ_DEV_NAME必定有值（FindDevName)
    if (std::strncpy(share_main_umq_cfg.dev_info.dev.dev_name, UmqSetting::UMQ_DEV_NAME.c_str(), UMQ_NAME_MAX_LEN) ==
        nullptr) {
        UBS_VLOG_ERR("Failed to set device name\n");
        return UMQ_INVALID_HANDLE;
    }

    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
        share_main_umq_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
        share_main_umq_cfg.dev_info.dev.eid_idx = UmqSetting::UMQ_EID_INDEX;

        if (UmqConnHelper::GetDevEid(share_main_umq_cfg.dev_info.dev.dev_name, UmqSetting::UMQ_EID_INDEX, &local_eid) !=
            0) {
            UBS_VLOG_ERR("Failed to get eid by dev name:%s and eid index:%d \n", UmqSetting::UMQ_DEV_NAME.c_str(),
                         UmqSetting::UMQ_EID_INDEX);
            return UMQ_INVALID_HANDLE;
        }
        UBS_VLOG_INFO("Use Bonding: " EID_FMT ".\n", EID_ARGS(local_eid));
    } else {
        // init use bonding dev
        share_main_umq_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
        share_main_umq_cfg.dev_info.eid.eid = UmqSetting::UMQ_LOCAL_EID;
        local_eid = UmqSetting::UMQ_LOCAL_EID;
        UBS_VLOG_INFO("Use UDMA: " EID_FMT ".\n", EID_ARGS(local_eid));
    }

    Locker sLock(UmqEidTable::Instance().GetMainMutex());
    std::vector<std::shared_ptr<MainUmqState>> main_umq_list;
    uint64_t main_umq_handle = UMQ_INVALID_HANDLE;
    if (!UmqEidTable::Instance().Get(local_eid, UmqSetting::UMQ_UB_TRANS_MODE, main_umq_list)) {
        share_main_umq_cfg.create_flag |= UMQ_CREATE_FLAG_MAIN_UMQ;
        main_umq_handle = UmqApi::umq_create(&share_main_umq_cfg);
        UmqEidTable::Instance().Add(local_eid, UmqSetting::UMQ_UB_TRANS_MODE, main_umq_handle);
    }
    return main_umq_handle;
}

Result UmqBackend::PrefillShareMainUmq(umq_eid_t &local_eid)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        return UBS_OK;
    }
    // 强依赖当前实现，一个 eid 对应多 UB 传输模式不同的 umq. 如果后续逻辑有变更，需同步修改。
    auto main_umq = UmqEidTable::Instance().GetFirst(local_eid, UmqSetting::UMQ_UB_TRANS_MODE);
    if (main_umq == nullptr) {
        UBS_VLOG_ERR("Failed to prefill share main umq. The main umq is null.\n");
        return UBS_ERROR;
    }
    uint64_t main_umq_handle = main_umq->GetUmqHandle();
    return main_umq->EnsurePrefilled([main_umq_handle]() {
        if (UmqConnHelper::PrefillRx(main_umq_handle) != 0) {
            UBS_VLOG_ERR("Failed to fill rx buffer to share main umq.\n");
            return UBS_ERROR;
        }
        return UBS_OK;
    });
}

Result UmqBackend::InitShareJfrMonitering(uint64_t main_umq_handle)
{
    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    uint64_t result = epoll_runner.Start();
    if (result != UBS_OK) {
        return result;
    }
    umq_interrupt_option_t main_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto share_jfr_fd = UmqApi::umq_interrupt_fd_get(main_umq_handle, &main_option);
    if (UNLIKELY(share_jfr_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, share_jfr_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get share jfr RX interrupt fd, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq_handle), share_jfr_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, share_jfr_fd), savedErrno);
        return UBS_ERROR;
    }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = share_jfr_fd;

    struct epoll_event share_jfr_event {
    };
    share_jfr_event.events = EPOLLIN | EPOLLET;
    share_jfr_event.data.u64 = event_data.u64;

    UmqShareJfrEpollRunnerOps::ExtContext ctx;
    ctx.umq_handle = main_umq_handle;
    if (UNLIKELY(epoll_runner.AddEpollEvent(share_jfr_fd, &share_jfr_event, &ctx))) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock