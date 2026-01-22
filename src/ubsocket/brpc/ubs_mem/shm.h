/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2026-01-10
*/

#ifndef SHM_DEF_H
#define SHM_DEF_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include "mem_utils.h"

#define SHM_REGION_NAME_PREFIX "ShmRegion"
#define SHM_RIGHT_MODE 0666
// Shmem mmap rights
#define PORT_READ 0x1               /* Page can be read. */
#define PORT_WRTIE 0X2              /* Page can be written. */
// System mmap vlaue
#define MAP_SHARED 0x01             /* Share changes. */
#define MAP_PRIVATE 0x02            /* Private changes */
#define SHM_MAX_NAME_BUFF_LEN 48    /* byte, buffer size, ubsm_sdk need name to be below 48 byte */

// Config
#define DATA_QUEUE_MAX_SIZE 128
#define DATA_QUEUE_MIN_SIZE 4

struct Shm {
    uint8_t *addr = nullptr;
    size_t len = 0;
    uint64_t memid = 0;
    char name[MAX_REGION_NAME_DESC_LENGTH] = {0};
    uint32_t fd = 0;
};

enum class LogLevel {
    UBSM_LOG_DEBUG_LEVEL = 0,
    UBSM_LOG_INFO_LEVEL = 1,
    UBSM_LOG_WARN_LEVEL = 2,
    UBSM_LOG_ERROR_LEVEL = 3,
    UBSM_LOG_CLOSED_LEVEL = 4
};

struct ShmConfig {
    LogLevel shmLogLevel = LogLevel::UBSM_LOG_INFO_LEVEL;
    bool shmWrDelayComp = true;
};

class ShmMgr {
public:
    static ALWAYS_INLINE ShmMgr *GetShmMgr()
    {
        static ShmMgr instance;
        return &instance;
    }

    ShmMgr(const ShmMgr&) = delete;
    ShmMgr& operator=(const ShmMgr&) = delete;

    bool IsShmEnabled()
    {
        const char* env = std::getenv("UBS_ENABLE_SHM");
        if (env == nullptr) {
            return false;
        }

        std::string value(env);
        return (value == "1") || (value == "true");
    }

    bool IsInitialized()
    {
        return m_initialized;
    }

    int Initialize()
    {
        int ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_set_extern_logger(UbsMemeLoggerPrint);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ub shmem set logger function failed, ret %d.\n", ret);
            return -1;
        }

        ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_set_logger_level(static_cast<int>(m_config.shmLogLevel));
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ub shmem set logger level failed, ret %d.\n", ret);
            return -1;
        }

        if (UNLIKELY(UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_local_nid_query(&m_nodeLocation) != 0)) {
            RPC_ADPT_VLOG_ERR("Get local nid failed.\n");
            return -1;
        }

        // TODO: add fault callback ubsmem_shmem_faults_register

        ubsmem_options_t options = {};
        ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_init_attributes(&options);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem init attributes failed, ret %d.\n", ret);
            return -1;
        }

        ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_initialize(&options);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem initialize failed, ret %d.\n", ret);
            return -1;
        }


        ret = UbsMemShmRegionCreate();
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem shared region create failed, ret %d.\n", ret);
            return -1;
        }

        return 0;
    }

    int Finalize()
    {
        int ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_finalize();
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem finalize failed, ret %d.\n", ret);
            return -1;
        }

        return 0;
    }

    int Malloc(Shm *shm)
    {
        uint64_t flag = UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_MEM_ANONYMOUS;
        // Add flag means enable non-relay mode(direct mode)
        flag = m_config.shmWrDelayComp ? flag | UBSM_FLAG_WR_DELAY_COMP : flag;
        int ret  = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_allocate(m_regionName, shm->name, shm->len, SHM_RIGHT_MODE, flag);
        if (ret == UBSM_ERR_ALREADY_EXIST) {
            if (UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_deallocate(shm->name) != 0) {
                RPC_ADPT_VLOG_ERR("Ubsmem free origin shm name \"%s\" failed, ret %d.\n", shm->name, ret);
                return -1;
            }
            RPC_ADPT_VLOG_INFO("Ubsmem free origin shm name \"%s\" success, try to recreate.\n");
            ret  = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_allocate(m_regionName, shm->name, shm->len, SHM_RIGHT_MODE, flag);
            if (ret != 0) {
                RPC_ADPT_VLOG_ERR("Ubsmem recreate shm name \"%s\" failed, ret %d.\n", shm->name, ret);
                return -1;
            }
        } else if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem create shm name \"%s\" failed, ret %d.\n", shm->name, ret);
            return -1;
        }

        shm->memid = 1;
        RPC_ADPT_VLOG_INFO("Ubsmem malloc success, shm name \"%s\", length %ld, memid %lu, m_regionName \"%s\" success.\n", shm->name, shm->len, shm->memid, m_regionName);
        return 0;
    }
    
    int Free(Shm *shm)
    {
        if (shm->addr == nullptr) {
            RPC_ADPT_VLOG_ERR("Ubsmem free input params is invalid, shm->addr is nullptr.\n");
            return -1;
        }

        int ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_deallocate(shm->name);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem free shm name \"%s\" failed, ret %d.\n", shm->name, ret);
            // TODO : Check ret=UBSM_ERROR_IN_USING or ret=UBSM_ERR_NOT_FOUND
            return ret;
        }

        shm->addr = nullptr;
        RPC_ADPT_VLOG_INFO("Ubsmem free success, shm name \"%s\", length %ld success.\n", shm->name, shm->len);

        return 0;
    }
    
    int Map(Shm *shm)
    {
        if (shm == nullptr) {
            RPC_ADPT_VLOG_ERR("Ubsmem map input params is invalid, shm is nullptr.\n");
            return -1;
        }

        int ret  = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_map(NULL, shm->len, PORT_READ | PORT_WRTIE, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem map shm name \"%s\" failed, ret %d.\n", shm->name, ret);
            (void)UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_deallocate(shm->name);
            // TODO : Check ret=UBSM_ERR_NOT_FOUND
            return -1;
        }

        RPC_ADPT_VLOG_INFO("Ubsmem map success, shm name \"%s\", length %ld, nodeLocation %d addr %p success.\n", shm->name, shm->len, m_nodeLocation, shm->addr);
        return 0;
    }
    
    int Unmap(Shm *shm)
    {
        if (shm->addr == nullptr) {
            RPC_ADPT_VLOG_ERR("Ubsmem unmap input params is invalid, shm->addr is nullptr.\n");
            return -1;
        }

        int ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_shmem_unmap(shm->addr, shm->len);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem unmap shm name \"%s\" , addr %p, len %llu, failed, ret %d.\n", shm->name, shm->addr, shm->len, ret);
            // TODO : Check ret=UBSM_ERR_NET
            return -1;
        }

        RPC_ADPT_VLOG_INFO("Ubsmem unmap success, shm name \"%s\", length %ld success.\n", shm->name, shm->len);
        return 0;
    }

private:
    explicit ShmMgr()
    {
        m_config.shmLogLevel = LogLevel::UBSM_LOG_INFO_LEVEL;
        if (Initialize() == 0) {
            m_initialized = true;
        }
    }

    virtual ~ShmMgr() = default;

    static void UbsMemeLoggerPrint(int level, const char *msg)
    {
        if (level == static_cast<int>(LogLevel::UBSM_LOG_DEBUG_LEVEL)) {
            RPC_ADPT_VLOG_DEBUG("%s\n", msg);
        } else if (level == static_cast<int>(LogLevel::UBSM_LOG_INFO_LEVEL)) {
            RPC_ADPT_VLOG_INFO("%s\n", msg);
        } else if (level == static_cast<int>(LogLevel::UBSM_LOG_WARN_LEVEL)) {
            RPC_ADPT_VLOG_WARN("%s\n", msg);
        } else {
            RPC_ADPT_VLOG_ERR("%s\n", msg);
        }
    
        return;
    }

    int UbsMemShmRegionCreate()
    {
        std::ostringstream oss;
        oss << SHM_REGION_NAME_PREFIX << '_' << m_nodeLocation;
        std::string nameStr = oss.str();

        if (nameStr.length() >= MAX_REGION_NAME_DESC_LENGTH) {
            RPC_ADPT_VLOG_ERR("Create Region name: %s too loog.\n", nameStr.c_str());
            return -1;
        }

        int ret = strcpy_s(m_regionName, MAX_REGION_NAME_DESC_LENGTH, nameStr.c_str());
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Region name strcpy_s failed, ret %d.\n", ret);
            return -1;
        }

        ubsmem_regions_t regions = {0};
        ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_lookup_regions(&regions);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("ubsmem lookup share regions failed, ret %d.\n", ret);
            return -1;
        } else if (regions.region[0].host_num <= 0) {
            RPC_ADPT_VLOG_ERR("ubsmem lookup share regions sucess, but first region host num %d.\n", regions.region[0].host_num);
            return -1;           
        }

        // Get local hostname
        char hostname[MAX_HOST_NAME_DESC_LENGTH];
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            throw std::system_error(errno, std::generic_category(), "gethostname failed.\n");
        }
        hostname[sizeof(hostname) - 1] = '\0';

        ubsmem_region_attributes_t regionAttr = {0};
        regionAttr.host_num = regions.region[0].host_num;
        for (int i = 0; i < regionAttr.host_num && i < MAX_HOST_NUM; i++) {
            ret = strcpy_s(regionAttr.hosts[i].host_name, MAX_HOST_NAME_DESC_LENGTH, regions.region[0].hosts[i].host_name);
            if (ret != 0) {
                RPC_ADPT_VLOG_ERR("Region's hostname strcpy_s failed, ret %d.\n", ret);
            }

            regionAttr.hosts[i].affinity = (strcmp(regionAttr.hosts[i].host_name, hostname) == 0) ? true : false;
        }

        ret = UbsMemAPiMgr::GetUbsMemAPiApi()->ubsmem_create_region(m_regionName, 0, &regionAttr);
        if (ret == UBSM_ERR_ALREADY_EXIST) {
            RPC_ADPT_VLOG_WARN("Ubsmem region exists, region name \"%s\"", m_regionName);
        } else if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Ubsmem create region failed, ret %d.\n", ret);
            return -1;
        }

        return 0;
    }
    
    ShmConfig m_config = {};
    uint32_t m_nodeLocation = 0;
    char m_regionName[MAX_REGION_NAME_DESC_LENGTH] = {0};
    bool m_initialized = false;
};

#endif