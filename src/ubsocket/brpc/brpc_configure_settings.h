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

#include "configure_settings.h"

#define BRPC_SYM_STR_LEN_MAX       (128)
#define ENV_VAR_BRPC_ALLOC_SYM     "RPC_ADPT_BRPC_ALLOC_SYM"
#define ENV_VAR_BRPC_DEALLOC_SYM   "RPC_ADPT_BRPC_DEALLOC_SYM"
#define ENV_VAR_READV_UNLIMITED    "RPC_ADPT_READV_UNLIMITED"
#define ENV_VAR_USE_POLLING        "RPC_ADPT_USE_POLLING"

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
              RPC_ADPT_VLOG_ERR("Failed to parse enviornment variables\n");
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
            RPC_ADPT_VLOG_ERR("Both %s & %s need to be configured simultaneously\n",
            ENV_VAR_BRPC_ALLOC_SYM, ENV_VAR_BRPC_DEALLOC_SYM);
            return -1;
         }

         if(strlen(m_readv_unlimited_str) > 0){
            m_readv_unlimited = BoolVal::BoolConverter(m_readv_unlimited_str);
            RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n",ENV_VAR_READV_UNLIMITED, 
            BoolVal::BoolConverter(m_readv_unlimited), m_readv_unlimited_str);
         }

         if (strlen(m_use_polling_str) > 0) {
             m_use_polling = BoolVal::BoolConverter(m_use_polling_str);
             RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_USE_POLLING, BoolVal::BoolConverter(m_use_polling),
                                m_use_polling_str);
         }
#ifdef UBS_SHM_BUILD_ENABLED
             m_use_polling = true;
#endif
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
      }
       
      char m_alloc_sym_str[BRPC_SYM_STR_LEN_MAX] = "";
      char m_dealloc_sym_str[BRPC_SYM_STR_LEN_MAX] = "";
      bool m_modify_allocator = false;
      char m_readv_unlimited_str[BOOL_STR_LEN_MAX] = "";
      bool m_readv_unlimited = false;
      char m_use_polling_str[BOOL_STR_LEN_MAX] = "";
      bool m_use_polling = false;
}; 
   
}

#endif