/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "umq_transport_pool.h"
#include "core/ubsocket_event_epoll.h"
#include "umq_conn_helper.h"
#include "umq_eid_table.h"
#include "umq_errno_converter.h"
#include "umq_tp_event_epoll_runner_ops.h"
#include "umq_tp_tx_epoll_runner_ops.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

Result UmqTransportPool::WarmUp(uint64_t main_umqh)
{
    // tx事件线程（jetty资源池tx/流控tx）
    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    uint64_t result = epoll_runner.Start();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to start tx epoll runner.");
        return result;
    }
    // RM模式 + 池化模式才开启
    if (UmqSetting::UMQ_UB_TP_MODE != UMQ_TM_RM || UmqSetting::UMQ_TP_TYPE != POOL) {
        return UBS_OK;
    }

    if (umq_tp_pool.count(main_umqh)) {
        UBS_VLOG_WARN("Umq transport pool is already warmed up, size: %zu.\n", PoolSize(main_umqh));
        return UBS_OK;
    }

    umq_eid_t bonding_eid = UmqSetting::UMQ_LOCAL_EID;
    if (UmqConnHelper::GetRouteList(route_list_tp_, bonding_eid, bonding_eid) != UBS_OK) {
        UBS_VLOG_ERR("Failed to get urma route info.\n");
        return UBS_ERROR;
    }

    int ret = CreatePool(main_umqh, static_cast<int>(UmqSetting::UMQ_TP_POOL_SIZE));
    if (ret != UBS_OK) {
        Clean();
        return ret;
    }
    UBS_VLOG_INFO("Umq transport pool finished to warm up, size: %zu.\n", PoolSize(main_umqh));

    // 为每个fd注册到epoll_wait监听
    if (AddPollTxEvent(main_umqh) != UBS_OK) {
        UBS_VLOG_ERR("Failed to add tx epoll event for fd %llu\n", main_umqh);
        Clean();
        return UBS_ERROR;
    }

    // 注册transport pool事件
    if (AddTransportEpollEvent(main_umqh) != UBS_OK) {
        UBS_VLOG_ERR("Failed to add transport event for fd %llu\n", main_umqh);
        Clean();
        return UBS_ERROR;
    }

    return UBS_OK;
}

Result UmqTransportPool::CreatePool(uint64_t main_umqh, int pool_size)
{
    Locker slock(mutex_);
    for (int i = 0; i < pool_size; ++i) {
        if (CreateOneTp(main_umqh)) {
            UBS_VLOG_ERR("Failed to create tp resources, umq: %llu, pool_size: %d, success_cnt: %d\n", main_umqh,
                         pool_size, i);
        }
    }
    return UBS_OK;
}

Result UmqTransportPool::Clean()
{
    if (umq_tp_pool.empty()) {
        UBS_VLOG_DEBUG("Umq transport pool is already empty.\n");
        return UBS_OK;
    }
    Locker lock(mutex_);
    for (const auto &umq_pair : umq_tp_pool) {
        uint64_t umq_handle = umq_pair.first;
        const auto &tp_map = umq_pair.second;
        for (const auto &tp_pair : tp_map) {
            uint32_t tp_idx = tp_pair.first;
            int ret = UmqApi::umq_transport_pool_resource_modify(umq_handle, tp_idx);
            if (ret < 0) {
                UBS_VLOG_ERR("Failed to modify transport node state to err.\n");
                continue;
            }
            if (UmqApi::umq_transport_pool_resource_destroy(umq_handle, tp_idx) < 0) {
                UBS_VLOG_ERR("Failed to destroy tp (idx: %u) of umq %llu\n", tp_idx, umq_handle);
            }
        }
    }
    umq_tp_pool.clear();
    return UBS_OK;
}

Result UmqTransportPool::CreateOneTp(uint64_t main_umqh)
{
    umq_tp_resource_create_option_t tp_create_cfg;
    memset(&tp_create_cfg, 0, sizeof(tp_create_cfg));

    // 声明在分支外，避免悬垂指针导致 umq 创建时，取不到正确的port信息
    std::vector<umq_port_id_t> used_ports{};

    if (GlobalSetting::LINK_SELECTION_POLICY == LinkSelectionPolicy::BONDING_BACKUP) {
        tp_create_cfg.create_flag |= UMQ_TP_CREATE_FLAG_USED_PORTS;
        uint32_t targetChipId;
        bool use_round_robin = true;

        if (UmqSetting::UMQ_DEV_SCHEDULE_POLICY != dev_schedule_policy::ROUND_ROBIN) {
            std::set<uint32_t> unique_chip_ids;
            for (uint32_t i = 0; i < route_list_tp_.route_num; ++i) {
                unique_chip_ids.insert(route_list_tp_.routes[i].src_port.bs.chip_id);
            }
            std::vector<uint32_t> chipId_list(unique_chip_ids.begin(), unique_chip_ids.end());
            targetChipId = UmqConnHelper::GetTargetChipId(UmqSetting::UMQ_ALL_SOCKET_IDS, chipId_list,
                                                          UmqSetting::UMQ_PROCESS_SOCKET_ID);
            if (targetChipId != UINT32_MAX) {
                use_round_robin = false;
            }
        }
        if (use_round_robin == false) {
            std::vector<umq_port_id_t> aff_ports, non_aff_ports;
            for (uint32_t i = 0; i < route_list_tp_.route_num; ++i)
                if (route_list_tp_.routes[i].src_port.bs.chip_id == targetChipId) {
                    aff_ports.push_back(route_list_tp_.routes[i].src_port);
                } else {
                    non_aff_ports.push_back(route_list_tp_.routes[i].src_port);
                }
            aff_rr_num_ %= aff_ports.size();
            used_ports.insert(used_ports.end(), aff_ports.begin() + aff_rr_num_, aff_ports.end());
            used_ports.insert(used_ports.end(), aff_ports.begin(), aff_ports.begin() + aff_rr_num_);
            used_ports.insert(used_ports.end(), non_aff_ports.begin(), non_aff_ports.end());
            aff_rr_num_ += 1;
        } else {
            std::vector<umq_port_id_t> all_ports;
            for (uint32_t i = 0; i < route_list_tp_.route_num; ++i) {
                all_ports.push_back(route_list_tp_.routes[i].src_port);
            }
            rr_num_ %= all_ports.size();
            used_ports.insert(used_ports.end(), all_ports[rr_num_]);
            all_ports.erase(all_ports.begin() + rr_num_);
            used_ports.insert(used_ports.end(), all_ports.begin(), all_ports.end());
            rr_num_ += 1;
        }

        std::sort(used_ports.begin(), used_ports.end(), [](const umq_port_id_t &a, const umq_port_id_t &b) {
            if (a.bs.chip_id != b.bs.chip_id) {
                return a.bs.chip_id < b.bs.chip_id;
            }
            if (a.bs.die_id != b.bs.die_id) {
                return a.bs.die_id < b.bs.die_id;
            }
            return a.bs.port_idx < b.bs.port_idx;
        });

        // 1主3备-DEBUG
        UBS_VLOG_DEBUG("[1m3b-SORT] CreateOneTp BEFORE sort, num=%zu\n", used_ports.size());
        for (size_t i = 0; i < used_ports.size(); ++i) {
            UBS_VLOG_DEBUG("[1m3b-SORT]   before[%zu]: chip=%u, die=%u, port=%u, value=0x%lx\n", i,
                           used_ports[i].bs.chip_id, used_ports[i].bs.die_id, used_ports[i].bs.port_idx,
                           (unsigned long)used_ports[i].value);
        }
        auto last = std::unique(used_ports.begin(), used_ports.end(),
                                [](const umq_port_id_t &a, const umq_port_id_t &b) { return a.value == b.value; });
        used_ports.erase(last, used_ports.end());
        // 1主3备-DEBUG
        UBS_VLOG_DEBUG("[1m3b-SORT] CreateOneTp AFTER unique, num=%zu (final order passed to umq)\n",
                       used_ports.size());
        for (size_t i = 0; i < used_ports.size(); ++i) {
            UBS_VLOG_DEBUG("[1m3b-SORT]   final[%zu]: chip=%u, die=%u, port=%u, value=0x%lx\n", i,
                           used_ports[i].bs.chip_id, used_ports[i].bs.die_id, used_ports[i].bs.port_idx,
                           (unsigned long)used_ports[i].value);
        }

        UBS_VLOG_DEBUG("CreateOneTp: used_ports.num=%u (expect 1 main + up to 3 backup)\n", used_ports.size());
        for (uint32_t i = 0; i < used_ports.size(); ++i) {
            UBS_VLOG_DEBUG("  used_ports[%u]: src_port(chip=%u,die=%u,port=%u)\n", i, used_ports[i].bs.chip_id,
                           used_ports[i].bs.die_id, used_ports[i].bs.port_idx);
        }

        tp_create_cfg.used_ports = {.port = used_ports.data(), .num = static_cast<uint8_t>(used_ports.size())};
    }

    // 调用创建接口，返回tp_idx
    uint32_t tp_idx = umq_transport_pool_resource_create(main_umqh, &tp_create_cfg);
    if (tp_idx == UINT32_MAX) {
        return UBS_ERROR;
    }

    std::vector<int> &fd_vec = umq_tp_pool[main_umqh][tp_idx];
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TP_HANDLE_IDX, UMQ_IO_TX,
                                        UMQ_FD_IO, tp_idx};
    // optimize: 待urma支持——umq_interrupt_fd_list_t fd_list = {};
    // optimize: 待urma支持——int ret = UmqApi::umq_interrupt_fd_list_get(main_umqh, &tx_option, &fd_list);
    int fd = UmqApi::umq_interrupt_fd_get(main_umqh, &tx_option);
    if (fd < UBS_OK) {
        UBS_VLOG_ERR("Failed to get fd list, umq: %llu\n", main_umqh);
        return UBS_ERROR;
    }
    // optimize: 待urma支持——fd_vec.insert(fd_vec.end(), fd_list.fd, fd_list.fd + fd_list.fd_num);
    fd_vec.push_back(fd);
    return UBS_OK;
}

Result UmqTransportPool::RebuildTp(uint64_t main_umqh, uint32_t old_tp_idx)
{
    Locker lock(mutex_);
    if (umq_tp_pool.empty()) {
        UBS_VLOG_DEBUG("Failed to rebuild tp, caused by umq transport pool is already empty.\n");
        return UBS_ERROR;
    }
    auto umq_pair = umq_tp_pool.find(main_umqh);
    if (umq_pair == umq_tp_pool.end()) {
        UBS_VLOG_ERR("Failed to rebuild tp, umq %llu not exist.\n", main_umqh);
        return UBS_ERROR;
    }
    auto &tp_map = umq_pair->second;
    auto tp_pair = tp_map.find(old_tp_idx);
    if (tp_pair == tp_map.end()) {
        UBS_VLOG_ERR("Failed to rebuild tp, tp_idx %u not exist.\n", old_tp_idx);
        return UBS_ERROR;
    }
    int ret = UmqApi::umq_transport_pool_resource_modify(main_umqh, old_tp_idx);
    if (ret < 0) {
        UBS_VLOG_ERR("Failed to modify transport node state to err.\n");
        return UBS_ERROR;
    }
    if (UmqApi::umq_transport_pool_resource_destroy(main_umqh, old_tp_idx) < 0) {
        UBS_VLOG_ERR("Failed to destroy tp (idx: %u) of umq %llu\n", old_tp_idx, main_umqh);
        return UBS_ERROR;
    }
    const auto fd_vec = tp_pair->second;
    EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER).DelEpollEvent(fd_vec[0]);
    tp_map.erase(tp_pair);
    CreateOneTp(main_umqh);
    return UBS_OK;
}

size_t UmqTransportPool::PoolSize(uint64_t main_umqh) const
{
    Locker slock(mutex_);
    auto pos = umq_tp_pool.find(main_umqh);
    if (pos != umq_tp_pool.end()) {
        return pos->second.size();
    }
    return 0;
}

Result UmqTransportPool::AddPollTxEvent(uint64_t umq_handle)
{
    Locker lock(mutex_);
    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    for (const auto &tp_pair : umq_tp_pool[umq_handle]) {
        uint32_t tp_idx = tp_pair.first;
        const auto &fd_vec = tp_pair.second;

        // 当前Jetty与fd是一对一关系，取第一个即可
        UmqTpTxEpollRunnerOps::TxEpollEvent *tx_epoll_event =
            new UmqTpTxEpollRunnerOps::TxEpollEvent{RUNNER_EVENT_TYPE_TP_TX, umq_handle, tp_idx};
        struct epoll_event umq_tx_event {
        };
        umq_tx_event.events = EPOLLIN | EPOLLET;
        umq_tx_event.data.u64 = reinterpret_cast<uintptr_t>(tx_epoll_event);

        UmqTpTxEpollRunnerOps::TpTxExtContext ctx;
        ctx.umq_handle = umq_handle;
        ctx.tp_idx = tp_idx;
        if (UNLIKELY(epoll_runner.AddEpollEvent(fd_vec[0], &umq_tx_event, &ctx))) {
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) tp tx event failed: %d : %s\n", errno, strerror(errno));
            return UBS_ERROR;
        }
    }
    return UBS_OK;
}

Result UmqTransportPool::AddTransportEpollEvent(uint64_t umq_handle)
{
    Locker lock(mutex_);
    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER);
    uint64_t result = epoll_runner.Start();
    if (result != UBS_OK) {
        return result;
    }
    int event_fd = UmqApi::umq_transport_pool_eventfd_get(umq_handle);
    RunnerEventData runner_event{};
    runner_event.event_data.type = RUNNER_EVENT_TYPE_TP_EVENT;
    runner_event.event_data.data = event_fd;

    struct epoll_event event {
    };
    event.events = EPOLLIN | EPOLLET;
    event.data.u64 = runner_event.u64;
    UmqTpEventEpollRunnerOps::ExtContext ctx;
    ctx.umq_handle = umq_handle;
    if (UNLIKELY(epoll_runner.AddEpollEvent(event_fd, &event, &ctx))) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) transport event fd failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock