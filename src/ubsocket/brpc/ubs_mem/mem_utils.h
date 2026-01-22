/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2026-01-10
*/

#ifndef MEM_UTILS_H
#define MEM_UTILS_H

#include <memory>
#include <utility>
#include <poll.h>
#include <signal.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "rpc_adpt_vlog.h"
#include "mem_def.h"

#define EXPOSE_C_DEFINE extern "C" __attribute__((visibility("default")))

typedef int (*ubsmem_init_attributes_api) (ubsmem_options_t *ubsm_shmem_opts);
typedef int (*ubsmem_initialize_api) (const ubsmem_options_t *ubsm_shmem_opts);
typedef int (*ubsmem_finalize_api) (void);
typedef int (*ubsmem_set_logger_level_api) (int level);
typedef int (*ubsmem_set_extern_logger_api) (void (*func)(int level, const char *msg));
typedef int (*ubsmem_lookup_regions_api) (ubsmem_regions_t *regions);
typedef int (*ubsmem_create_region_api) (const char *region_name, size_t size,
    const ubsmem_region_attributes_t *reg_attr);
typedef int (*ubsmem_destroy_region_api) (const char *region_name);
typedef int (*ubsmem_shmem_allocate_api) (const char *region_name, const char *name, size_t size, mode_t mode,
    uint64_t flags);
typedef int (*ubsmem_shmem_deallocate_api) (const char *name);
typedef int (*ubsmem_shmem_map_api) (void *addr, size_t length, int prot, int flags, const char *name, off_t offset,
    void **local_ptr);
typedef int (*ubsmem_shmem_unmap_api) (void *local_ptr, size_t length);
typedef int (*ubsmem_shmem_faults_register_api) (shmem_faults_func registerFunc);
typedef int (*ubsmem_local_nid_query_api) (uint32_t* nid);


#define UBSMEM_API_DEFINE(__api_name) __api_name##_api __api_name = nullptr
#define UBSMEM_API_INITIALIZER(__api_name) UbsMemRecordApi(this->ubs_mem_handle_, #__api_name, this->__api_name)

/** 
 * Record the address where the symbol is loaded into memory from libubsm_sdk.so
 * The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
 * @param[in] handle: handle for libubsm_sdk.so;
 * @param[in] symbol_name: symbol name;
 * @param[out] symbol: variable to record the address;
 * Return: Void
 */
template <typename ApiType>
void UbsMemRecordApi(void *handle, const char *symbol_name, ApiType &symbol)
{
    (void)dlerror();
    symbol = reinterpret_cast<ApiType>(dlsym(handle, symbol_name));
    char *err = dlerror();
    if (!symbol || err) {
        RPC_ADPT_VLOG_ERR("Failed to find symbol '%s' in libubsm_sdk.so: %s\n",
            symbol_name, (err ? err : "unknown error"));
    } else {
        RPC_ADPT_VLOG_DEBUG("Found symbol '%s()' in libubsm_sdk.so\n", symbol_name);
    }
}

class UbsMemAPiMgr {
public:
    static ALWAYS_INLINE UbsMemAPiMgr *GetUbsMemAPiApi()
    {
        static UbsMemAPiMgr mgr;
        return &mgr;
    }

    UBSMEM_API_DEFINE(ubsmem_init_attributes);
    UBSMEM_API_DEFINE(ubsmem_initialize);
    UBSMEM_API_DEFINE(ubsmem_finalize);
    UBSMEM_API_DEFINE(ubsmem_set_logger_level);
    UBSMEM_API_DEFINE(ubsmem_set_extern_logger);
    UBSMEM_API_DEFINE(ubsmem_lookup_regions);
    UBSMEM_API_DEFINE(ubsmem_create_region);
    UBSMEM_API_DEFINE(ubsmem_destroy_region);
    UBSMEM_API_DEFINE(ubsmem_shmem_allocate);
    UBSMEM_API_DEFINE(ubsmem_shmem_deallocate);
    UBSMEM_API_DEFINE(ubsmem_shmem_map);
    UBSMEM_API_DEFINE(ubsmem_shmem_unmap);
    UBSMEM_API_DEFINE(ubsmem_shmem_faults_register);
    UBSMEM_API_DEFINE(ubsmem_local_nid_query);

private:
    void *ubs_mem_handle_ = nullptr;

    UbsMemAPiMgr(void)
    {
        (void)dlerror();
        const char *ubsm_sdk_path = "/usr/local/ubs_mem/lib/libubsm_sdk.so";
        ubs_mem_handle_ = dlopen(ubsm_sdk_path, RTLD_LAZY);
        if (!ubs_mem_handle_) {
            RPC_ADPT_VLOG_ERR("dlopen %s failed: %s\n", ubsm_sdk_path, dlerror());
        }
        UBSMEM_API_INITIALIZER(ubsmem_init_attributes);
        UBSMEM_API_INITIALIZER(ubsmem_initialize);
        UBSMEM_API_INITIALIZER(ubsmem_finalize);
        UBSMEM_API_INITIALIZER(ubsmem_set_logger_level);
        UBSMEM_API_INITIALIZER(ubsmem_set_extern_logger);
        UBSMEM_API_INITIALIZER(ubsmem_lookup_regions);
        UBSMEM_API_INITIALIZER(ubsmem_create_region);
        UBSMEM_API_INITIALIZER(ubsmem_destroy_region);
        UBSMEM_API_INITIALIZER(ubsmem_shmem_allocate);
        UBSMEM_API_INITIALIZER(ubsmem_shmem_deallocate);
        UBSMEM_API_INITIALIZER(ubsmem_shmem_map);
        UBSMEM_API_INITIALIZER(ubsmem_shmem_unmap);
        UBSMEM_API_INITIALIZER(ubsmem_shmem_faults_register);
        UBSMEM_API_INITIALIZER(ubsmem_local_nid_query);
    }
 };

#endif
