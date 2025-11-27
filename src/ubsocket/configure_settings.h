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

#include "securec.h"
#include "rpc_adpt_vlog.h"
#include "umq_api.h"
#include "umq_types.h"

#define LOG_LEVEL_STR_LEN_MAX     (64)
#define IP_ADDR_STR_LEN_MAX       (64)
#define DEV_NAME_STR_LEN_MAX      (64)
#define TRANS_MODE_STR_LEN_MAX    (64)
#define BLOCK_TYPE_STR_LEN_MAX    (64)
#define BOOL_STR_LEN_MAX          (8)
#define DEFAULT_TX_DEPTH          (128)
#define DEFAULT_RX_DEPTH          (128)
#define DEFAULT_IO_TOTAL_SIZE     (1024)    // MB
#define IO_SIZE_MB                (1024 * 1024)
#define DEFAULT_QBUF_BLOCK_TYPE   "default" // 8k
#define LARGE_QBUF_BLOCK_TYPE     "large"   // 64k
#define ENV_VAR_LOG_LEVEL         "RPC_ADPT_LOG_LEVEL"
#define ENV_VAR_TRANS_MODE        "RPC_ADPT_TRANS_MODE"
#define ENV_VAR_DEV_IP            "RPC_ADPT_DEV_IP"
#define ENV_VAR_DEV_DEV_NAME      "RPC_ADPT_DEV_NAME"
#define ENV_VAR_TX_DEPTH          "RPC_ADPT_TX_DEPTH"
#define ENV_VAR_RX_DEPTH          "RPC_ADPT_RX_DEPTH"
#define ENV_VAR_STATS             "RPC_ADPT_STATS"
#define ENV_VAR_BLOCK_TYPE        "RPC_ADPT_BLOCK_TYPE"        // default, large
#define ENV_VAR_POOL_INITIAL_SIZE "RPC_ADPT_POOL_INITIAL_SIZE" // MB

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

umq_trans_mode_t TransModeConverter(const char *str, umq_trans_mode_t default_trans_mode = UMQ_TRANS_MODE_IB);
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
        SOCKET_FD_TRANS_MODE_MAX
    };

    ConfigSettings() {}
    
    ~ConfigSettings() {}

    virtual int Init()
    {
        LoadEnvVars();

        if(ParseEnvVars() !=0){
            RPC_ADPT_VLOG_ERR("Failed to parse enviornment variables\n");
            return -1;
        }

        return 0;
    }

    uint32_t GetTxDepth()
    {
        return m_tx_depth;
    }

    uint32_t GetRxDepth()
    {
        return m_rx_depth;
    }

    util_vlog_level_t GetLogLevel()
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

        if((env_ptr = getenv(ENV_VAR_TX_DEPTH))!=NULL){
            /* atoi return 0 means (1) failed to transfer input to int; (2) user set 0;
            both of them use default value directly*/
            uint32_t input_tx_depth = static_cast<uint32_t>(atoi(env_ptr));
            m_tx_depth = input_tx_depth == 0 ? DEFAULT_TX_DEPTH : input_tx_depth;
        }

        if((env_ptr = getenv(ENV_VAR_RX_DEPTH))!=NULL){
            /* atoi return 0 means (1) failed to transfer input to int; (2) user set 0;
            both of them use default value directly*/
            uint32_t input_rx_depth = static_cast<uint32_t>(atoi(env_ptr));
            m_rx_depth = input_rx_depth == 0 ? DEFAULT_RX_DEPTH : input_rx_depth;
        }

        if((env_ptr = getenv(ENV_VAR_STATS)) != NULL){
            ReadEnvVar(env_ptr,m_stats_str,sizeof(m_stats_str));
        }

        if ((env_ptr = getenv(ENV_VAR_POOL_INITIAL_SIZE)) != NULL) {
            uint64_t total_size = static_cast<uint64_t>(atoi(env_ptr));
            m_io_total_size = total_size == 0 ? DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB : total_size * IO_SIZE_MB;
        }
        if ((env_ptr = getenv(ENV_VAR_BLOCK_TYPE)) != NULL) {
            ReadEnvVar(env_ptr, m_block_type_str, sizeof(m_block_type_str));
            if (memcmp(m_block_type_str, DEFAULT_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_8K;
            } else if (memcmp(m_block_type_str, LARGE_QBUF_BLOCK_TYPE, strlen(m_block_type_str)) == 0) {
                m_block_type = BLOCK_SIZE_64K;
            } else {
                (void)strcpy_s(m_block_type_str, sizeof(m_block_type_str), DEFAULT_QBUF_BLOCK_TYPE);
                m_block_type = BLOCK_SIZE_8K;
            }
        }
    }

    void SetSocketFdTransMode(socket_fd_trans_mode trans_mode)
    {
        static const char *socket_fd_trans_mode_str[SOCKET_FD_TRANS_MODE_MAX] = {
            "TCP/IP",
            "UMQ-based Acceleration",
            "UMQ-based Acceleration with Zero-copy"
        };
        m_socket_fd_trans_mode = trans_mode;
        RPC_ADPT_VLOG_INFO("Socket fd transport mode: %s\n", socket_fd_trans_mode_str[m_socket_fd_trans_mode]);
    }

    char m_log_level_str[LOG_LEVEL_STR_LEN_MAX] = "";
    char m_trans_mode_str[TRANS_MODE_STR_LEN_MAX] = "";
    char m_dev_ip_str[IP_ADDR_STR_LEN_MAX] = "";
    char m_dev_name_str[DEV_NAME_STR_LEN_MAX] = "";
    char m_stats_str[BOOL_STR_LEN_MAX] = "";
    char m_block_type_str[BLOCK_TYPE_STR_LEN_MAX] = "";
    uint32_t m_tx_depth = DEFAULT_TX_DEPTH;
    uint32_t m_rx_depth = DEFAULT_RX_DEPTH;
    uint64_t m_io_total_size = DEFAULT_IO_TOTAL_SIZE * IO_SIZE_MB;
    util_vlog_level_t m_log_level;
    umq_trans_mode_t m_trans_mode;
    umq_buf_block_size_t m_block_type = BLOCK_SIZE_8K;
    struct sockaddr_in m_addr;
    struct sockaddr_in6 m_addr6;
    bool m_is_ipv6 = false;
    static socket_fd_trans_mode m_socket_fd_trans_mode;
    bool m_stats_enable = false;
};

#endif