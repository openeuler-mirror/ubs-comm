/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-28
 *Note:
 *History: 2025-07-28
*/

#ifndef BRPC_CONFIGURE_SETTINGS
#define BRPC_CONFIGURE_SETTINGS

#include <sys/socket.h>
#include "configure_settings.h"
#include "../util_vlog.h"

#define BRPC_SYM_STR_LEN_MAX       (128)
#define DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH          (1024)
#define ENV_VAR_BRPC_ALLOC_SYM     "UBSOCKET_BRPC_ALLOC_SYM"
#define ENV_VAR_BRPC_DEALLOC_SYM   "UBSOCKET_BRPC_DEALLOC_SYM"
#define ENV_VAR_READV_UNLIMITED    "UBSOCKET_READV_UNLIMITED"
#define ENV_VAR_USE_POLLING        "UBSOCKET_USE_POLLING"
#define ENV_VAR_ENABLE_SHARE_JFR   "UBSOCKET_ENABLE_SHARE_JFR"
#define ENV_VAR_SHARE_JFR_RX_QUEUE_DEPTH   "UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH"
#define ENV_VAR_AUTO_FALLBACK_TCP  "UBSOCKET_AUTO_FALLBACK_TCP"
#define ENV_VAR_USE_UB_FORCE       "UBSOCKET_USE_UB_FORCE"

namespace Brpc{

class ConfigSettings : public :: ConfigSettings{
public:
      ConfigSettings() : ::ConfigSettings() {}
      
      ~ConfigSettings() {}

      virtual int Init() override
      {
          ::ConfigSettings::LoadEnvVars();
          Brpc::ConfigSettings::LoadEnvVars();

          if(::ConfigSettings::ParseEnvVars() != 0 || Brpc::ConfigSettings::ParseEnvVars() != 0){
              RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,  "Failed to parse enviornment variables\n");
              return -1;
          }

          return 0 ;
      }

      const char *GetBrpcAllocSymStr()
      {
         return strlen(m_alloc_sym_str) > 0 ? m_alloc_sym_str : nullptr;
      }

      const char *GetBrpcDeallocSymStr()
      {
         return strlen(m_dealloc_sym_str) > 0 ? m_dealloc_sym_str : nullptr;
      }

      bool GetReadvUnlimited()
      {
         return m_readv_unlimited;
      }

      bool GetUsePolling()
      {
          return m_use_polling;
      }

      bool EnableShareJfr()
      {
          return m_enable_share_jfr;
      }

      uint64_t GetShareJfrRxQueueDepth()
      {
          return m_share_jfr_rx_queue_depth;
      }
 
    bool AutoFallbackTCP()
    {
        return m_auto_fallback_tcp;
    }

    bool UseUB(int domain, int type)
    {
        bool isTCP = ((domain == AF_INET) || (domain == AF_INET6)) && (type == SOCK_STREAM);
        if ((domain == AF_SMC) || (m_use_ub_force && isTCP)) {
            return true;
        }
        return false;
    }

      protected:
      int ParseEnvVars() override
      {
         size_t alloc_sym_str_len = strlen(m_alloc_sym_str);
         size_t dealloc_sym_str_len = strlen(m_dealloc_sym_str);
         if(alloc_sym_str_len > 0 && dealloc_sym_str_len > 0){
            RPC_ADPT_VLOG_INFO("%s: %s\n", ENV_VAR_BRPC_ALLOC_SYM, m_alloc_sym_str);
            RPC_ADPT_VLOG_INFO("%s: %s\n", ENV_VAR_BRPC_DEALLOC_SYM, m_dealloc_sym_str);
            m_modify_allocator = true;
         } else if (alloc_sym_str_len > 0 || dealloc_sym_str_len > 0){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,  "Both %s & %s need to be configured simultaneously\n",
            ENV_VAR_BRPC_ALLOC_SYM, ENV_VAR_BRPC_DEALLOC_SYM);
            return -1;
         }

         if (strlen(m_readv_unlimited_str) > 0) {
             m_readv_unlimited = BoolVal::BoolConverter(m_readv_unlimited_str);
             RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_READV_UNLIMITED,
                                BoolVal::BoolConverter(m_readv_unlimited), m_readv_unlimited_str);
         }

         if (strlen(m_use_polling_str) > 0) {
            m_use_polling = BoolVal::BoolConverter(m_use_polling_str);
            RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_USE_POLLING, BoolVal::BoolConverter(m_use_polling),
            m_use_polling_str);
         }

        if (strlen(m_auto_fallback_tcp_str) > 0) {
            m_auto_fallback_tcp = BoolVal::BoolConverter(m_auto_fallback_tcp_str);
        }
        RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_AUTO_FALLBACK_TCP,
            BoolVal::BoolConverter(m_auto_fallback_tcp),
            strlen(m_auto_fallback_tcp_str) > 0 ? m_auto_fallback_tcp_str: "(null)");
        if (strlen(m_use_ub_force_str) > 0) {
            m_use_ub_force = BoolVal::BoolConverter(m_use_ub_force_str);
        }      
        RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_USE_UB_FORCE, BoolVal::BoolConverter(m_use_ub_force),
            strlen(m_use_ub_force_str) > 0 ? m_use_ub_force_str:"(null)");
#ifdef UBS_SHM_BUILD_ENABLED
             m_use_polling = true;
#endif

         if (strlen(m_enable_share_jfr_str) > 0) {
            m_enable_share_jfr = BoolVal::BoolConverter(m_enable_share_jfr_str);
            RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_ENABLE_SHARE_JFR,
                BoolVal::BoolConverter(m_enable_share_jfr), m_enable_share_jfr_str);
         }
        return 0;
      }

      void LoadEnvVars() override
      {
        char *env_ptr;
        if((env_ptr = getenv(ENV_VAR_BRPC_ALLOC_SYM)) != NULL){
            ReadEnvVar(env_ptr, m_alloc_sym_str, sizeof(m_alloc_sym_str));
        }

        if((env_ptr = getenv(ENV_VAR_BRPC_DEALLOC_SYM)) != NULL){
            ReadEnvVar(env_ptr, m_dealloc_sym_str, sizeof(m_dealloc_sym_str));
        }

        if((env_ptr = getenv(ENV_VAR_READV_UNLIMITED)) != NULL){
            ReadEnvVar(env_ptr, m_readv_unlimited_str, sizeof(m_readv_unlimited_str));
        }

        if ((env_ptr = getenv(ENV_VAR_USE_POLLING)) != NULL) {
            ReadEnvVar(env_ptr, m_use_polling_str, sizeof(m_use_polling_str));
        }

        if ((env_ptr = getenv(ENV_VAR_AUTO_FALLBACK_TCP)) != nullptr) {
            ReadEnvVar(env_ptr, m_auto_fallback_tcp_str, sizeof(m_auto_fallback_tcp_str));
        }

        if ((env_ptr = getenv(ENV_VAR_USE_UB_FORCE)) != nullptr) {
            ReadEnvVar(env_ptr, m_use_ub_force_str, sizeof(m_use_ub_force_str));
        }

        if ((env_ptr = getenv(ENV_VAR_ENABLE_SHARE_JFR)) != nullptr) {
            ReadEnvVar(env_ptr, m_enable_share_jfr_str, sizeof(m_enable_share_jfr_str));
        }

        if ((env_ptr = getenv(ENV_VAR_SHARE_JFR_RX_QUEUE_DEPTH)) != nullptr) {
            uint64_t share_jfr_rx_queue_depth = static_cast<uint64_t>(atoi(env_ptr));
            m_share_jfr_rx_queue_depth = share_jfr_rx_queue_depth == 0 ? DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH :
                                                                         share_jfr_rx_queue_depth;
        }
      }
       
      char m_alloc_sym_str[BRPC_SYM_STR_LEN_MAX] = "";
      char m_dealloc_sym_str[BRPC_SYM_STR_LEN_MAX] = "";
      bool m_modify_allocator = false;
      char m_readv_unlimited_str[BOOL_STR_LEN_MAX] = "";
      bool m_readv_unlimited = true;
      char m_use_polling_str[BOOL_STR_LEN_MAX] = "";
      bool m_use_polling = false;
      char m_auto_fallback_tcp_str[BOOL_STR_LEN_MAX] = "";
      bool m_auto_fallback_tcp = true;
      char m_use_ub_force_str[BOOL_STR_LEN_MAX] = "";
      bool m_use_ub_force = false;
      char m_enable_share_jfr_str[BOOL_STR_LEN_MAX] = "";
      bool m_enable_share_jfr = false;
      uint64_t m_share_jfr_rx_queue_depth = DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH;
}; 
   
}

#endif