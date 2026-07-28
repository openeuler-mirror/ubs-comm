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
#include "dl_uvs_api.h"

namespace ock {
namespace ubs {
std::mutex UvsApi::LOAD_MUTEX;
bool UvsApi::LOADED = false;

DL_API_DEFINE(UvsApi, uvs_create_agg_dev);
DL_API_DEFINE(UvsApi, uvs_delete_agg_dev);
DL_API_DEFINE(UvsApi, uvs_get_device_name_by_eid);
DL_API_DEFINE(UvsApi, uvs_set_topo_info);
DL_API_DEFINE(UvsApi, uvs_set_share_topo_info);
DL_API_DEFINE(UvsApi, uvs_insert_main_ue_eid);
DL_API_DEFINE(UvsApi, uvs_insert_main_ue_eid_batch);
DL_API_DEFINE(UvsApi, uvs_delete_main_ue_eid);
DL_API_DEFINE(UvsApi, uvs_lookup_main_ue_eid);
DL_API_DEFINE(UvsApi, uvs_flush_main_ue_eid);
DL_API_DEFINE(UvsApi, uvs_get_topo_info);
DL_API_DEFINE(UvsApi, uvs_get_path_set);

Result UvsApi::Load() noexcept
{
    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (LOADED) {
        UBS_VLOG_DEBUG("uvs library already opened, skip");
        return UBS_OK;
    }

    /* step1: open library file */
    void *handle = dlopen("libtpsa.so", RTLD_NOW | RTLD_NODELETE | RTLD_GLOBAL);
    if (handle == nullptr) {
        UBS_VLOG_ERR("Open libtpsa.so failed, error: %s", dlerror());
        return UBS_DL_OPEN_LIB_FAILED;
    }

    /* step2: load api */
    DL_API_LOAD(uvs_create_agg_dev);
    DL_API_LOAD(uvs_delete_agg_dev);
    DL_API_LOAD(uvs_get_device_name_by_eid);
    DL_API_LOAD(uvs_set_topo_info);
    DL_API_LOAD(uvs_set_share_topo_info);
    DL_API_LOAD(uvs_insert_main_ue_eid);
    DL_API_LOAD(uvs_insert_main_ue_eid_batch);
    DL_API_LOAD(uvs_delete_main_ue_eid);
    DL_API_LOAD(uvs_lookup_main_ue_eid);
    DL_API_LOAD(uvs_flush_main_ue_eid);
    DL_API_LOAD(uvs_get_topo_info);
    DL_API_LOAD(uvs_get_path_set);

    /* step3: close handle */
    dlclose(handle);

    UBS_VLOG_DEBUG("uvs library libtpsa.so loaded");
    return UBS_OK;
}

void UvsApi::UnLoad() noexcept
{
    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (!LOADED) {
        UBS_VLOG_DEBUG("uvs library not loaded");
        return;
    }

    UnLoadInner();
    LOADED = false;
    UBS_VLOG_DEBUG("uvs library unloaded");
}

void UvsApi::UnLoadInner() noexcept
{
    DL_API_SET_NULL(uvs_create_agg_dev);
    DL_API_SET_NULL(uvs_delete_agg_dev);
    DL_API_SET_NULL(uvs_get_device_name_by_eid);
    DL_API_SET_NULL(uvs_set_topo_info);
    DL_API_SET_NULL(uvs_set_share_topo_info);
    DL_API_SET_NULL(uvs_insert_main_ue_eid);
    DL_API_SET_NULL(uvs_insert_main_ue_eid_batch);
    DL_API_SET_NULL(uvs_delete_main_ue_eid);
    DL_API_SET_NULL(uvs_lookup_main_ue_eid);
    DL_API_SET_NULL(uvs_flush_main_ue_eid);
    DL_API_SET_NULL(uvs_get_topo_info);
    DL_API_SET_NULL(uvs_get_path_set);
}
} // namespace ubs
} // namespace ock