/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-28
 *Note:
 *History: 2025-07-28
*/

#ifndef CONFIGURE_SETTINGS
#define CONFIGURE_SETTINGS

#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include "securec.h"
#include "rpc_adpt_vlog.h"
#include "umq_api.h"
#include "umq_types.h"

#define LOG_LEVEL_STR_LEN_MAX     (64)
#define IP_ADDR_STR_LEN_MAX       (64)
#define DEV_NAME_STR_LEN_MAX      (64)
#define TRANS_MODE_STR_LEN_MAX    (64)
#define BLOCK_TYPE_STR_LEN_MAX    (64)
#define DEV_SCHEDULE_POLICY_LEN_MAX (64)
#define BOOL_STR_LEN_MAX          (8)
#define UB_TRANS_MODE_STR_LEN_MAX    (8)
#define DEFAULT_EID_IDX           (0)
#define DEFAULT_TX_DEPTH          (1024)
#define DEFAULT_RX_DEPTH          (1024)
#define DEFAULT_IO_TOTAL_SIZE     (1024)    // MB
#define IO_SIZE_MB                (1024 * 1024)
#define UBSOCKET_TRACE_TIME_DEFAULT (10)
#define UBSOCKET_TRACE_FILE_SIZE_DEFAULT (10)
#define UBSOCKET_TRACE_TIME_MIN     (1)
#define UBSOCKET_TRACE_TIME_MAX     (300)
#define UBSOCKET_TRACE_FILE_PATH_MIN     (1)
#define UBSOCKET_TRACE_FILE_PATH_MAX     (300)
#define UBSOCKET_TRACE_FILE_PATH_LEN_MAX (512)
#define DEFAULT_QBUF_BLOCK_TYPE   "default" // 8k
#define SMALL_QBUF_BLOCK_TYPE     "small"   // 16k
#define MEDIUM_QBUF_BLOCK_TYPE    "medium"  // 32k
#define LARGE_QBUF_BLOCK_TYPE     "large"   // 64k
#define DEV_SCHEDULE_POLICY_CPU_AFFINITY  "affinity"  // cpu_affinity
#define DEV_SCHEDULE_POLICY_ROUND_ROBIN   "rr"  // round_robin
#define ENV_VAR_LOG_LEVEL         "UBSOCKET_LOG_LEVEL"
#define ENV_VAR_TRANS_MODE        "UBSOCKET_TRANS_MODE"
#define ENV_VAR_DEV_IP            "UBSOCKET_DEV_IP"
#define ENV_VAR_DEV_DEV_NAME      "UBSOCKET_DEV_NAME"
#define ENV_VAR_EID_IDX           "UBSOCKET_EID_IDX"
#define ENV_VAR_TX_DEPTH          "UBSOCKET_TX_DEPTH"
#define ENV_VAR_DEV_SRC_EID       "UBSOCKET_SRC_EID"
#define ENV_VAR_RX_DEPTH          "UBSOCKET_RX_DEPTH"
#define ENV_VAR_STATS             "UBSOCKET_STATS_CLI"
#define ENV_VAR_BLOCK_TYPE        "UBSOCKET_BLOCK_TYPE"        // default, small, medium, large
#define ENV_VAR_POOL_INITIAL_SIZE "UBSOCKET_POOL_INITIAL_SIZE" // MB
#define ENV_VAR_USE_ZCOPY         "UBSOCKET_USE_BRPC_ZCOPY"
#define ENV_LOG_USE_PRINTF        "UBSOCKET_LOG_USE_PRINTF" // default 0, 0 false; 1 true
#define ENV_SCHEDULE_POLICY       "UBSOCKET_SCHEDULE_POLICY" // affinity, rr
#define ENV_TRACE_ENABLE          "UBSOCKET_TRACE_ENABLE"
#define ENV_TRACE_TIME            "UBSOCKET_TRACE_TIME"
#define ENV_TRACE_FILE_PATH       "UBSOCKET_TRACE_FILE_PATH"
#define ENV_TRACE_FILE_SIZE       "UBSOCKET_TRACE_FILE_SIZE"
#define ENV_UB_TRANS_MODE         "UBSOCKET_UB_TRANS_MODE"

enum dev_schedule_policy {
    ROUND_ROBIN = 1,
    CPU_AFFINITY
};

enum ub_trans_mode {
    RC_TP,
    RM_TP,
    RM_CTP,
    RC_CTP
};

template <typename T>
class EnvStrConverter {
public:
    struct EnvStrDef {
        T env_val;
        const char *output_name;
        const char **alias_names;
    };

    // The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
    EnvStrConverter(const EnvStrDef *definition_list, uint32_t definition_list_size) :
       m_definition_list(definition_list), m_definition_list_size(definition_list_size) {}
    
    T EnvStrConvert(const char *str, T default_value)
    {
       if(str == nullptr){
         return default_value;
       }

       for(uint32_t i = 0; i < m_definition_list_size; ++i){
        for(const char **name_def = m_definition_list[i].alias_names; *name_def!=nullptr;++name_def){
            if(strcasecmp(str, *name_def) == 0){
                return m_definition_list[i].env_val;
            }
          }
       }

       return default_value;
    }
    
    const char *EnvStrConvert(T env_val)
    {
        for(uint32_t i=0; i<m_definition_list_size;++i){
            if(m_definition_list[i].env_val == env_val){
                return m_definition_list[i].output_name;
            }
        }

        return nullptr;
    }

private:
    const EnvStrDef *m_definition_list = nullptr;
    uint32_t m_definition_list_size = 0;
};

namespace TransMode {

umq_trans_mode_t TransModeConverter(const char *str, umq_trans_mode_t default_trans_mode = UMQ_TRANS_MODE_UB);
const char *TransModeConverter(umq_trans_mode_t trans_mode);

}

namespace BoolVal {

bool BoolConverter(const char *str, bool default_bool_val = false);
const char *BoolConverter(bool bool_val);

}

class ConfigSettings {
public:
    enum socket_fd_trans_mode {
        SOCKET_FD_TRANS_MODE_UNSET = -1,
        SOCKET_FD_TRANS_MODE_TCP,
        SOCKET_FD_TRANS_MODE_UMQ,
        SOCKET_FD_TRANS_MODE_UMQ_ZERO_COPY,
        SOCKET_FD_TRANS_MODE_SHM,
        SOCKET_FD_TRANS_MODE_MAX
    };

    ConfigSettings() {}
    
    ~ConfigSettings() {}

    virtual int Init()
    {
        LoadEnvVars();

        if(ParseEnvVars() !=0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to parse enviornment variables\n");
            return -1;
        }

        return 0;
    }

    uint32_t GetTxDepth()
    {
        return m_tx_depth;
    }

    uint32_t GetEidIdx()
    {
        return m_eid_idx;
    }

    uint32_t GetRxDepth()
    {
        return m_rx_depth;
    }

    ubsocket::util_vlog_level_t GetLogLevel()
    {
        return m_log_level;
    }

    const char *GetDevIpStr()
    {
        return strlen(m_dev_ip_str) > 0 ? m_dev_ip_str : nullptr;
    }

    const char *GetDevNameStr()
    {
        return strlen(m_dev_name_str) > 0 ? m_dev_name_str : nullptr;
    }

    bool IsDevIpv6()
    {
        return m_is_ipv6;
    }

    umq_trans_mode_t GetTransMode()
    {
        return m_trans_mode;
    }

    static socket_fd_trans_mode GetSocketFdTransMode()
    {
        return m_socket_fd_trans_mode;
    }

    bool GetStatsEnable()
    {
        return m_stats_enable;
    }

    uint64_t GetIOTotalSize()
    {
        return m_io_total_size;
    }

    const char *GetIOBlockTypeStr()
    {
        return strlen(m_block_type_str) > 0 ? m_block_type_str : DEFAULT_QBUF_BLOCK_TYPE;
    }

    umq_buf_block_size_t GetIOBlockType()
    {
        return m_block_type;
    }

    umq_eid_t GetDevSrcEid()
    {
        return m_src_eid;
    }

    bool GetLogUse()
    {
        return m_log_use_printf;
    }

    dev_schedule_policy GetDevSchedulePolicy()
    {
        return m_dev_schedule_policy;
    }

    bool GetTraceEnable()
    {
        return m_trace_enable;
    }

    uint64_t GetUbsocketTraceTime()
    {
        return m_ubsocket_trace_time;
    }

    const char *GetUbsocketTraceFilePath()
    {
        return strlen(m_ubsocket_trace_file_path) > 0 ? m_ubsocket_trace_file_path : nullptr;
    }

    uint64_t GetUbsocketTraceFileSize()
    {
        return m_ubsocket_trace_file_size;
    }

    ub_trans_mode GetUbTransMode()
    {
        return m_ub_trans_mode;
    }

    void SetUbTransMode(ub_trans_mode trans_mode)
    {
        static const char *trans_mode_str[RC_CTP + 1] = {
            "RC_TP",
            "RM_TP",
            "RM_CTP",
            "RC_CTP"
        };
        m_ub_trans_mode = trans_mode;
        RPC_ADPT_VLOG_INFO("urma transport mode: %s\n", trans_mode_str[m_ub_trans_mode]);
    }

protected:
    static void ReadEnvVar(char *env_ptr, char *output_str, uint32_t output_str_len)
    {
        if(NULL == env_ptr || NULL == output_str){
            return;
        }

        int n = snprintf_s(output_str,output_str_len,output_str_len-1,"%s",env_ptr);
        if((((int)output_str_len-1)<n) || (n<0)){
            (void)memset_s(output_str,output_str_len,0,output_str_len);
        }
    } 
    
    virtual int ParseEnvVars();
    virtual void LoadEnvVars()
    {
        char *env_ptr;
        if ((env_ptr = getenv(ENV_LOG_USE_PRINTF)) != NULL) {
            ReadEnvVar(env_ptr, m_log_use_printf_str, sizeof(m_stats_str));
        }

        if ((env_ptr = getenv(ENV_VAR_USE_ZCOPY)) != nullptr) {
            ReadEnvVar(env_ptr, m_use_brpc_zcopy_str, sizeof(m_use_brpc_zcopy_str));
        }

        if((env_ptr = getenv(ENV_VAR_LOG_LEVEL)) != NULL){
            ReadEnvVar(env_ptr,m_log_level_str,sizeof(m_log_level_str));
        }

        if((env_ptr = getenv(ENV_VAR_TRANS_MODE)) != NULL){
            ReadEnvVar(env_ptr,m_trans_mode_str,sizeof(m_trans_mode_str));
        }

        if((env_ptr = getenv(ENV_VAR_DEV_IP)) != NULL){
            ReadEnvVar(env_ptr,m_dev_ip_str,sizeof(m_dev_ip_str));
        }

        if((env_ptr = getenv(ENV_VAR_DEV_DEV_NAME)) != NULL){
            ReadEnvVar(env_ptr,m_dev_name_str,sizeof(m_dev_name_str));
        }

        if((env_ptr = getenv(ENV_VAR_EID_IDX))!=NULL){
            uint32_t input_eid_idx = static_cast<uint32_t>(std::stoi(env_ptr));
            m_eid_idx = input_eid_idx == 0 ? DEFAULT_EID_IDX : input_eid_idx;
        }

        if((env_ptr = getenv(ENV_VAR_DEV_SRC_EID)) != NULL){
            ReadEnvVar(env_ptr,m_src_eid_str,sizeof(m_src_eid_str));
        }

        if((env_ptr = getenv(ENV_VAR_TX_DEPTH))!=NULL){
            uint32_t input_tx_depth = static_cast<uint32_t>(std::stoi(env_ptr));
            m_tx_depth = input_tx_depth == 0 ? DEFAULT_TX_DEPTH : input_tx_depth;
        }

        if((env_ptr = getenv(ENV_VAR_RX_DEPTH))!=NULL){
            uint32_t input_rx_depth = static_cast<uint32_t>(std::stoi(env_ptr));
            m_rx_depth = input_rx_depth == 0 ? DEFAULT_RX_DEPTH : input_rx_depth;
        }

        if((env_ptr = getenv(ENV_VAR_STATS)) != NULL){
            ReadEnvVar(env_ptr,m_stats_str,sizeof(m_stats_str));
        }

        if ((env_ptr = getenv(ENV_VAR_POOL_INITIAL_SIZE)) != NULL) {
            uint64_t total_size = static_cast<uint64_t>(std::stoull(env_ptr));
            m_io_total_size = total_size == 0 ? DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB : total_size * IO_SIZE_MB;
        }
        if ((env_ptr = getenv(ENV_VAR_BLOCK_TYPE)) != NULL) {
            ReadEnvVar(env_ptr, m_block_type_str, sizeof(m_block_type_str));
            if (memcmp(m_block_type_str, DEFAULT_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_8K;
            } else if (memcmp(m_block_type_str, SMALL_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_16K;
            } else if (memcmp(m_block_type_str, MEDIUM_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_32K;
            } else if (memcmp(m_block_type_str, LARGE_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_64K;
            } else {
                (void)strcpy_s(m_block_type_str, sizeof(m_block_type_str), DEFAULT_QBUF_BLOCK_TYPE);
                m_block_type = BLOCK_SIZE_8K;
            }
        }

        if ((env_ptr = getenv(ENV_SCHEDULE_POLICY)) != NULL) {
            ReadEnvVar(env_ptr, m_dev_schedule_policy_str, sizeof(m_dev_schedule_policy_str));
            if (memcmp(m_dev_schedule_policy_str, DEV_SCHEDULE_POLICY_CPU_AFFINITY, strlen(m_dev_schedule_policy_str)) == 0) {
                m_dev_schedule_policy = dev_schedule_policy::CPU_AFFINITY;
            } else if (memcmp(m_dev_schedule_policy_str, DEV_SCHEDULE_POLICY_ROUND_ROBIN, strlen(m_dev_schedule_policy_str)) == 0) {
                m_dev_schedule_policy = dev_schedule_policy::ROUND_ROBIN;
            } else {
                (void)strcpy_s(m_dev_schedule_policy_str, sizeof(m_dev_schedule_policy_str),
                    DEV_SCHEDULE_POLICY_CPU_AFFINITY);
                m_dev_schedule_policy = dev_schedule_policy::CPU_AFFINITY;
            }
        }

        if ((env_ptr = getenv(ENV_TRACE_ENABLE)) != NULL) {
            m_trace_enable = BoolVal::BoolConverter(env_ptr);
        }

        if ((env_ptr = getenv(ENV_TRACE_TIME)) != NULL) {
            uint64_t ubsocket_trace_time = static_cast<uint64_t>(std::stoull(env_ptr));
            if (ubsocket_trace_time >= UBSOCKET_TRACE_TIME_MIN
                && ubsocket_trace_time <= UBSOCKET_TRACE_TIME_MAX) {
                m_ubsocket_trace_time = ubsocket_trace_time;
            }
        }

        char default_path[] = "/tmp/ubsocket/log";
        env_ptr = getenv(ENV_TRACE_FILE_PATH);
        if (env_ptr != nullptr && env_ptr[0] == '/' && strlen(env_ptr) < UBSOCKET_TRACE_FILE_PATH_LEN_MAX &&
            strcspn(env_ptr, " \t\n\r;\"'`&|<>()[]{}$\\") == strlen(env_ptr)) {
            ReadEnvVar(env_ptr, m_ubsocket_trace_file_path, sizeof(m_ubsocket_trace_file_path));
        } else {
            ReadEnvVar(default_path, m_ubsocket_trace_file_path, sizeof(m_ubsocket_trace_file_path));
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid TRACE_FILE_PATH, using default.\n");
        }

        if ((env_ptr = getenv(ENV_TRACE_FILE_SIZE)) != NULL) {
            uint64_t ubsocket_trace_file_size = static_cast<uint64_t>(std::stoull(env_ptr));
            if (ubsocket_trace_file_size >= UBSOCKET_TRACE_FILE_PATH_MIN
                && ubsocket_trace_file_size <= UBSOCKET_TRACE_FILE_PATH_MAX) {
                m_ubsocket_trace_file_size = ubsocket_trace_file_size;
            }
        }
        GetEnvUbTransMode();
    }

    void GetEnvUbTransMode()
    {
        char *env_ptr;
        if ((env_ptr = getenv(ENV_UB_TRANS_MODE)) != NULL) {
            ReadEnvVar(env_ptr, m_ub_trans_mode_str, sizeof(m_ub_trans_mode_str));
            if (memcmp(m_ub_trans_mode_str, "RM_TP", strlen(m_ub_trans_mode_str)) == 0) {
                m_ub_trans_mode = ub_trans_mode::RM_TP;
            } else if (memcmp(m_ub_trans_mode_str, "RM_CTP", strlen(m_ub_trans_mode_str)) == 0) {
                m_ub_trans_mode = ub_trans_mode::RM_CTP;
            } else if (memcmp(m_ub_trans_mode_str, "RC_TP", strlen(m_ub_trans_mode_str)) == 0) {
                m_ub_trans_mode = ub_trans_mode::RC_TP;
            } else if (memcmp(m_ub_trans_mode_str, "RC_CTP", strlen(m_ub_trans_mode_str)) == 0) {
                m_ub_trans_mode = ub_trans_mode::RC_CTP;
            } else {
                (void)strcpy_s(m_ub_trans_mode_str, sizeof(m_ub_trans_mode_str), "RC_TP");
                m_ub_trans_mode = ub_trans_mode::RC_TP;
            }
        }
    }

    void SetSocketFdTransMode(socket_fd_trans_mode trans_mode)
    {
        static const char *socket_fd_trans_mode_str[SOCKET_FD_TRANS_MODE_MAX] = {
            "TCP/IP",
            "UMQ-based Acceleration",
            "UMQ-based Acceleration with Zero-copy",
            "UB_SHM-based Acceleration"
        };
        m_socket_fd_trans_mode = trans_mode;
        RPC_ADPT_VLOG_INFO(
            "Socket fd transport mode: %s\n", socket_fd_trans_mode_str[m_socket_fd_trans_mode]);
    }

    char m_log_level_str[LOG_LEVEL_STR_LEN_MAX] = "";
    char m_trans_mode_str[TRANS_MODE_STR_LEN_MAX] = "";
    char m_dev_ip_str[IP_ADDR_STR_LEN_MAX] = "";
    char m_dev_name_str[DEV_NAME_STR_LEN_MAX] = "";
    char m_log_use_printf_str[BOOL_STR_LEN_MAX] = "";
    char m_stats_str[BOOL_STR_LEN_MAX] = "";
    char m_use_brpc_zcopy_str[BOOL_STR_LEN_MAX] = "";
    char m_block_type_str[BLOCK_TYPE_STR_LEN_MAX] = "";
    uint32_t m_eid_idx = DEFAULT_EID_IDX;
    char m_src_eid_str[BLOCK_TYPE_STR_LEN_MAX] = "";
    char m_dev_schedule_policy_str[DEV_SCHEDULE_POLICY_LEN_MAX] = "";
    char m_ubsocket_trace_file_path[UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = "";
    uint64_t m_ubsocket_trace_time = UBSOCKET_TRACE_TIME_DEFAULT;
    uint64_t m_ubsocket_trace_file_size = UBSOCKET_TRACE_FILE_SIZE_DEFAULT;
    char m_ub_trans_mode_str[UB_TRANS_MODE_STR_LEN_MAX] = "";
    umq_eid_t m_src_eid;
    uint32_t m_tx_depth = DEFAULT_TX_DEPTH;
    uint32_t m_rx_depth = DEFAULT_RX_DEPTH;
    uint64_t m_io_total_size = DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB;
    ubsocket::util_vlog_level_t m_log_level;
    umq_trans_mode_t m_trans_mode;
    umq_buf_block_size_t m_block_type = BLOCK_SIZE_8K;
    struct sockaddr_in m_addr;
    struct sockaddr_in6 m_addr6;
    bool m_is_ipv6 = false;
    static socket_fd_trans_mode m_socket_fd_trans_mode;
    bool m_stats_enable = false;
    bool m_trace_enable = false;
    bool m_log_use_printf = false;
    bool m_use_brpc_zcopy = true;
    dev_schedule_policy m_dev_schedule_policy = dev_schedule_policy::CPU_AFFINITY;
    ub_trans_mode m_ub_trans_mode = ub_trans_mode::RC_TP;
};

#endif