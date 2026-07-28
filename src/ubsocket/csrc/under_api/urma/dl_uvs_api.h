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
#ifndef UBS_COMM_DL_UVS_API_H
#define UBS_COMM_DL_UVS_API_H

#include "under_api/dl_api.h"
#include "uvs_api_types.h"

namespace ock {
namespace ubs {
using uvs_create_agg_dev_api = int (*)(uvs_eid_t *, const char *);
using uvs_delete_agg_dev_api = int (*)(uvs_eid_t *);
using uvs_get_device_name_by_eid_api = int (*)(uvs_eid_t *, char *, size_t);
using uvs_set_topo_info_api = int (*)(void *, uint32_t, uint32_t);
using uvs_set_share_topo_info_api = int (*)(void *, uint32_t, uint32_t);
using uvs_insert_main_ue_eid_api = int (*)(const uvs_main_ue_eid_entry_t *);
using uvs_insert_main_ue_eid_batch_api = int (*)(const uvs_main_ue_eid_batch_entry_t *);
using uvs_delete_main_ue_eid_api = int (*)(const uvs_eid_t *);
using uvs_lookup_main_ue_eid_api = int (*)(const uvs_eid_t *, uvs_eid_t *);
using uvs_flush_main_ue_eid_api = int (*)();
using uvs_get_topo_info_api = int (*)(void *);
using uvs_get_path_set_api = int (*)(const uvs_eid_t *, const uvs_eid_t *, enum uvs_tp_type, bool, uvs_path_set_t *);

class UvsApi {
public:
    static Result Load() noexcept;
    static void UnLoad() noexcept;

    static int uvs_create_agg_dev(uvs_eid_t *agg_eid, const char *dev_name) noexcept
    {
        return uvs_create_agg_dev_ptr(agg_eid, dev_name);
    }

    static int uvs_delete_agg_dev(uvs_eid_t *agg_eid) noexcept
    {
        return uvs_delete_agg_dev_ptr(agg_eid);
    }

    static int uvs_get_device_name_by_eid(uvs_eid_t *eid, char *buf, size_t len) noexcept
    {
        return uvs_get_device_name_by_eid_ptr(eid, buf, len);
    }

    static int uvs_set_topo_info(void *topo_buf, uint32_t node_size, uint32_t node_num) noexcept
    {
        return uvs_set_topo_info_ptr(topo_buf, node_size, node_num);
    }

    static int uvs_set_share_topo_info(void *topo_buf, uint32_t node_size, uint32_t node_num) noexcept
    {
        return uvs_set_share_topo_info_ptr(topo_buf, node_size, node_num);
    }

    static int uvs_insert_main_ue_eid(const uvs_main_ue_eid_entry_t *entry) noexcept
    {
        return uvs_insert_main_ue_eid_ptr(entry);
    }

    static int uvs_insert_main_ue_eid_batch(const uvs_main_ue_eid_batch_entry_t *entry) noexcept
    {
        return uvs_insert_main_ue_eid_batch_ptr(entry);
    }

    static int uvs_delete_main_ue_eid(const uvs_eid_t *eid) noexcept
    {
        return uvs_delete_main_ue_eid_ptr(eid);
    }

    static int uvs_lookup_main_ue_eid(const uvs_eid_t *eid, uvs_eid_t *main_ue_eid) noexcept
    {
        return uvs_lookup_main_ue_eid_ptr(eid, main_ue_eid);
    }

    static int uvs_flush_main_ue_eid(void) noexcept
    {
        return uvs_flush_main_ue_eid_ptr();
    }

    static int uvs_get_topo_info(void *topo) noexcept
    {
        return uvs_get_topo_info_ptr(topo);
    }

    static int uvs_get_path_set(const uvs_eid_t *src_bondind_eid, const uvs_eid_t *dst_bonding_eid,
                                enum uvs_tp_type tp_type, bool iodie_level, uvs_path_set_t *uvs_path_set) noexcept
    {
        return uvs_get_path_set_ptr(src_bondind_eid, dst_bonding_eid, tp_type, iodie_level, uvs_path_set);
    }

private:
    DL_API_DECLARE(uvs_create_agg_dev);
    DL_API_DECLARE(uvs_delete_agg_dev);
    DL_API_DECLARE(uvs_get_device_name_by_eid);
    DL_API_DECLARE(uvs_set_topo_info);
    DL_API_DECLARE(uvs_set_share_topo_info);
    DL_API_DECLARE(uvs_insert_main_ue_eid);
    DL_API_DECLARE(uvs_insert_main_ue_eid_batch);
    DL_API_DECLARE(uvs_delete_main_ue_eid);
    DL_API_DECLARE(uvs_lookup_main_ue_eid);
    DL_API_DECLARE(uvs_flush_main_ue_eid);
    DL_API_DECLARE(uvs_get_topo_info);
    DL_API_DECLARE(uvs_get_path_set);

private:
    static void UnLoadInner() noexcept;
    static std::mutex LOAD_MUTEX;
    static bool LOADED;
};
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_DL_UVS_API_H
