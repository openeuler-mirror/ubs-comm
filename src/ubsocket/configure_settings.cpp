// Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
// Description: Provide the utility for umq buffer, iov, etc
// Author:
// Create: 2025-07-28
// Note:
// History: 2025-07-28

#include <arpa/inet.h>

#include "configure_settings.h"

namespace TransMode{

static const char *g_trans_mode_ub[] = {"ub", "0", nullptr};
static const char *g_trans_mode_ib[] = {"ib", "roce", "1",nullptr};
static const char *g_trans_mode_ubmm[] = {"ubmm" , "3", nullptr};
static const char *g_trans_mode_ub_plus[] = {"ub_plus", "4", nullptr};
static const char *g_trans_mode_ib_plus[] = {"ib_plus", "5", nullptr};
static const char *g_trans_mode_ubmm_plus[] = {"ubmm_plus", "6", nullptr};

static const EnvStrConverter<umq_trans_mode_t>::EnvStrDef g_trans_mode_def[] = {
    {UMQ_TRANS_MODE_UB, "UB", g_trans_mode_ub},
    {UMQ_TRANS_MODE_IB, "IB", g_trans_mode_ib},
    {UMQ_TRANS_MODE_UBMM, "UBMM", g_trans_mode_ubmm},
    {UMQ_TRANS_MODE_UB_PLUS, "UB_PLUS", g_trans_mode_ub_plus},
    {UMQ_TRANS_MODE_IB_PLUS, "IB_PLUS", g_trans_mode_ib_plus},
    {UMQ_TRANS_MODE_UBMM_PLUS, "UBMM_PLUS", g_trans_mode_ubmm_plus},
};

EnvStrConverter<umq_trans_mode_t>& GetTransModeConverter()
{
    static EnvStrConverter<umq_trans_mode_t> trans_mode_converter = {
        g_trans_mode_def,
        sizeof(g_trans_mode_def) / sizeof(g_trans_mode_def[0])
    };
    return trans_mode_converter;
}

umq_trans_mode_t TransModeConverter(const char *str, umq_trans_mode_t default_trans_mode)
{
    auto& trans_mode_converter = GetTransModeConverter();
    return trans_mode_converter.EnvStrConvert(str, default_trans_mode);
}

const char *TransModeConverter(umq_trans_mode_t trans_mode)
{
    auto& trans_mode_converter = GetTransModeConverter();
    return trans_mode_converter.EnvStrConvert(trans_mode);
}
}

namespace BoolVal{

static const char *g_bool_true[] = {"true", "1", nullptr};
static const char *g_bool_false[] = {"false", "0", nullptr};

static const EnvStrConverter<bool>::EnvStrDef g_bool_def[] = {
    {true,"True",g_bool_true},
    {false,"False",g_bool_false},
};

EnvStrConverter<bool>& GetBoolValConverter()
{
    static EnvStrConverter<bool> bool_converter = {
            g_bool_def,
            sizeof(g_bool_def) / sizeof(g_bool_def[0])
    };
    return bool_converter;
}

bool BoolConverter(const char *str,bool default_bool_val)
{
    auto& bool_converter = GetBoolValConverter();
    return bool_converter.EnvStrConvert(str, default_bool_val);
}

const char *BoolConverter(bool bool_val)
{
    auto& bool_converter = GetBoolValConverter();
    return bool_converter.EnvStrConvert(bool_val);
}
}

ConfigSettings::socket_fd_trans_mode ConfigSettings::m_socket_fd_trans_mode = SOCKET_FD_TRANS_MODE_UNSET;

int ConfigSettings::ParseEnvVars()
{
    m_log_level = ubsocket::util_vlog_level_converter_from_str(m_log_level_str, ubsocket::UTIL_VLOG_LEVEL_INFO);
    if(m_log_use_printf){
        if (RpcAdptSetLogCtx(m_log_level) != UMQ_SUCCESS) {
            RPC_ADPT_VLOG_WARN("Log output via printf is disabled; messages will be sent to syslog.\n");
        }
    }
    RpcAdptVlogCtxSet(m_log_level,nullptr);
    RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_LOG_LEVEL,
                       ubsocket::util_vlog_level_converter_to_str(m_log_level),
                       strlen(m_log_level_str) > 0 ? m_log_level_str : "(null)");

    m_trans_mode = TransMode::TransModeConverter(m_trans_mode_str);
     RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_TRANS_MODE, TransMode::TransModeConverter(m_trans_mode),
      strlen(m_trans_mode_str) > 0 ? m_trans_mode_str : "(null)");

    if(strlen(m_dev_ip_str) > 0){
        m_addr.sin_family = AF_INET;
        m_addr6.sin6_family = AF_INET6;
        if(inet_pton(AF_INET, m_dev_ip_str, &(m_addr.sin_addr)) == 1){
            m_is_ipv6 = false;
             RPC_ADPT_VLOG_INFO("%s: %s (ipv4)\n", ENV_VAR_DEV_IP, m_dev_ip_str);
        }else if(inet_pton(AF_INET6, m_dev_ip_str, &(m_addr6.sin6_addr)) == 1){
            m_is_ipv6 = true;
             RPC_ADPT_VLOG_INFO("%s: %s (ipv6)\n", ENV_VAR_DEV_IP, m_dev_ip_str);
        }else{
            RPC_ADPT_VLOG_ERR("IP address is invalid. Please double check your input(%s)\n", m_dev_ip_str);
            return -1;
        }
    }else if(strlen(m_dev_name_str) > 0){
        RPC_ADPT_VLOG_INFO("%s: %s\n", ENV_VAR_DEV_DEV_NAME, m_dev_name_str);
        if(strlen(m_src_eid_str) > 0){
            if(inet_pton(AF_INET6, m_src_eid_str, &(m_src_eid)) == 1){
                RPC_ADPT_VLOG_INFO("%s: %s (eid)\n", ENV_VAR_DEV_SRC_EID, m_src_eid_str);
            }else {
                RPC_ADPT_VLOG_ERR("Eid is invalid. Please double check your input(%s)\n", m_src_eid_str);
                return -1;
            }
        }
    } else {
        RPC_ADPT_VLOG_WARN("No device information is provided, such as device IP or device name\n");
    }

    RPC_ADPT_VLOG_INFO("%s: %d\n", ENV_VAR_EID_IDX, m_eid_idx);
    RPC_ADPT_VLOG_INFO("%s: %d\n", ENV_VAR_TX_DEPTH, m_tx_depth);

    RPC_ADPT_VLOG_INFO("%s: %d\n", ENV_VAR_RX_DEPTH, m_rx_depth);

    RPC_ADPT_VLOG_INFO("%s: %s\n", ENV_VAR_BLOCK_TYPE, GetIOBlockTypeStr());
    RPC_ADPT_VLOG_INFO("%s: %lu\n", ENV_VAR_POOL_INITIAL_SIZE, m_io_total_size);

    if(strlen(m_stats_str) > 0){
        m_stats_enable = BoolVal::BoolConverter(m_stats_str);
        RPC_ADPT_VLOG_INFO("%s: %s (input: %s)\n", ENV_VAR_STATS, BoolVal::BoolConverter(m_stats_enable),m_stats_str);
    }

    return 0;
}
