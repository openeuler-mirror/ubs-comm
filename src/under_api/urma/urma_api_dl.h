/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef HCOM_DYLOADER_IURMA_H
#define HCOM_DYLOADER_IURMA_H
#ifdef UB_BUILD_ENABLED

#include <errno.h>
#include <linux/types.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "ub/umdk/urma/urma_types.h"

#define URMA_SO_PATH "liburma.so.0"

using URMA_INIT = urma_status_t (*)(urma_init_attr_t *conf);
using URMA_UNINIT = urma_status_t (*)(void);
using URMA_GET_DEVICE_LIST = urma_device_t **(*)(int *num_devices);
using URMA_FREE_DEVICE_LIST = void (*)(urma_device_t **device_list);
using URMA_GET_EID_LIST = urma_eid_info_t *(*)(urma_device_t *dev, uint32_t *cnt);
using URMA_FREE_EID_LIST = void (*)(urma_eid_info_t *eid_list);
using URMA_GET_DEVICE_BY_NAME = urma_device_t *(*)(char *dev_name);
using URMA_GET_DEVICE_BY_EID = urma_device_t *(*)(urma_eid_t eid, urma_transport_type_t type);
using URMA_QUERY_DEVICE = urma_status_t (*)(urma_device_t *dev, urma_device_attr_t *dev_attr);
using URMA_CREATE_CONTEXT = urma_context_t *(*)(urma_device_t *dev, uint32_t eid_index);
using URMA_DELETE_CONTEXT = urma_status_t (*)(urma_context_t *ctx);
using URMA_CREATE_JFC = urma_jfc_t *(*)(urma_context_t *ctx, urma_jfc_cfg_t *jfc_cfg);
using URMA_MODIFY_JFC = urma_status_t (*)(urma_jfc_t *jfc, urma_jfc_attr_t *attr);
using URMA_DELETE_JFC = urma_status_t (*)(urma_jfc_t *jfc);
using URMA_CREATE_JFS = urma_jfs_t *(*)(urma_context_t *ctx, urma_jfs_cfg_t *jfs_cfg);
using URMA_MODIFY_JFS = urma_status_t (*)(urma_jfs_t *jfs, urma_jfs_attr_t *attr);
using URMA_QUERY_JFS = urma_status_t (*)(urma_jfs_t *jfs, urma_jfs_cfg_t *cfg, urma_jfs_attr_t *attr);
using URMA_DELETE_JFS = urma_status_t (*)(urma_jfs_t *jfs);
using URMA_FLUSH_JFS = int (*)(urma_jfs_t *jfs, int cr_cnt, urma_cr_t *cr);
using URMA_CREATE_JFR = urma_jfr_t *(*)(urma_context_t *ctx, urma_jfr_cfg_t *jfr_cfg);
using URMA_MODIFY_JFR = urma_status_t (*)(urma_jfr_t *jfr, urma_jfr_attr_t *attr);
using URMA_QUERY_JFR = urma_status_t (*)(urma_jfr_t *jfr, urma_jfr_cfg_t *cfg, urma_jfr_attr_t *attr);
using URMA_DELETE_JFR = urma_status_t (*)(urma_jfr_t *jfr);
using URMA_IMPORT_JFR = urma_target_jetty_t *(*)(urma_context_t *ctx, urma_rjfr_t *rjfr, urma_token_t *token_value);
using URMA_UNIMPORT_JFR = urma_status_t (*)(urma_target_jetty_t *target_jfr);
using URMA_ADVISE_JFR = urma_status_t (*)(urma_jfs_t *jfs, urma_target_jetty_t *tjfr);
using URMA_UNADVISE_JFR = urma_status_t (*)(urma_jfs_t *jfs, urma_target_jetty_t *tjfr);
using URMA_CREATE_JETTY = urma_jetty_t *(*)(urma_context_t *ctx, urma_jetty_cfg_t *jetty_cfg);
using URMA_MODIFY_JETTY = urma_status_t (*)(urma_jetty_t *jetty, urma_jetty_attr_t *attr);
using URMA_QUERY_JETTY = urma_status_t (*)(urma_jetty_t *jetty, urma_jetty_cfg_t *cfg, urma_jetty_attr_t *attr);
using URMA_DELETE_JETTY = urma_status_t (*)(urma_jetty_t *jetty);
using URMA_IMPORT_JETTY = urma_target_jetty_t *(*)(urma_context_t *ctx, urma_rjetty_t *rjetty,
    urma_token_t *token_value);
using URMA_UNIMPORT_JETTY = urma_status_t (*)(urma_target_jetty_t *tjetty);
using URMA_ADVISE_JETTY = urma_status_t (*)(urma_jetty_t *jetty, urma_target_jetty_t *tjetty);
using URMA_UNADVISE_JETTY = urma_status_t (*)(urma_jetty_t *jetty, urma_target_jetty_t *tjetty);
using URMA_BIND_JETTY = urma_status_t (*)(urma_jetty_t *jetty, urma_target_jetty_t *tjetty);
using URMA_UNBIND_JETTY = urma_status_t (*)(urma_jetty_t *jetty);
using URMA_FLUSH_JETTY = int (*)(urma_jetty_t *jetty, int cr_cnt, urma_cr_t *cr);
using URMA_CREATE_JETTY_GRP = urma_jetty_grp_t *(*)(urma_context_t *ctx, urma_jetty_grp_cfg_t *cfg);
using URMA_DELETE_JETTY_GRP = urma_status_t (*)(urma_jetty_grp_t *jetty_grp);
using URMA_CREATE_JFCE = urma_jfce_t *(*)(urma_context_t *ctx);
using URMA_DELETE_JFCE = urma_status_t (*)(urma_jfce_t *jfce);
using URMA_GET_ASYNC_EVENT = urma_status_t (*)(urma_context_t *ctx, urma_async_event_t *event);
using URMA_ACK_ASYNC_EVENT = void (*)(urma_async_event_t *event);
using URMA_ALLOC_TOKEN_ID = urma_token_id_t *(*)(urma_context_t *ctx);
using URMA_FREE_TOKEN_ID = urma_status_t (*)(urma_token_id_t *token_id);
using URMA_REGISTER_SEG = urma_target_seg_t *(*)(urma_context_t *ctx, urma_seg_cfg_t *seg_cfg);
using URMA_UNREGISTER_SEG = urma_status_t (*)(urma_target_seg_t *target_seg);
using URMA_IMPORT_SEG = urma_target_seg_t *(*)(urma_context_t *ctx, urma_seg_t *seg, urma_token_t *token_value,
    uint64_t addr, urma_import_seg_flag_t flag);
using URMA_UNIMPORT_SEG = urma_status_t (*)(urma_target_seg_t *tseg);
using URMA_POST_JFS_WR = urma_status_t (*)(urma_jfs_t *jfs, urma_jfs_wr_t *wr, urma_jfs_wr_t **bad_wr);
using URMA_POST_JFR_WR = urma_status_t (*)(urma_jfr_t *jfr, urma_jfr_wr_t *wr, urma_jfr_wr_t **bad_wr);
using URMA_POST_JETTY_SEND_WR = urma_status_t (*)(urma_jetty_t *jetty, urma_jfs_wr_t *wr, urma_jfs_wr_t **bad_wr);
using URMA_POST_JETTY_RECV_WR = urma_status_t (*)(urma_jetty_t *jetty, urma_jfr_wr_t *wr, urma_jfr_wr_t **bad_wr);
using URMA_WRITE = urma_status_t (*)(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *dst_tseg,
    urma_target_seg_t *src_tseg, uint64_t dst, uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag, uint64_t user_ctx);
using URMA_READ = urma_status_t (*)(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *dst_tseg,
    urma_target_seg_t *src_tseg, uint64_t dst, uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag, uint64_t user_ctx);
using URMA_SEND = urma_status_t (*)(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *src_tseg,
    uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag, uint64_t user_ctx);
using URMA_RECV = urma_status_t (*)(urma_jfr_t *jfr, urma_target_seg_t *recv_tseg, uint64_t buf, uint32_t len,
    uint64_t user_ctx);
using URMA_POLL_JFC = int (*)(urma_jfc_t *jfc, int cr_cnt, urma_cr_t *cr);
using URMA_REARM_JFC = urma_status_t (*)(urma_jfc_t *jfc, bool solicited_only);
using URMA_WAIT_JFC = int (*)(urma_jfce_t *jfce, uint32_t jfc_cnt, int time_out, urma_jfc_t *jfc[]);
using URMA_ACK_JFC = void (*)(urma_jfc_t *jfc[], uint32_t nevents[], uint32_t jfc_cnt);
using URMA_USER_CTL = urma_status_t (*)(urma_context_t *ctx, urma_user_ctl_in_t *in, urma_user_ctl_out_t *out);
using URMA_REGISTER_LOG_FUNC = urma_status_t (*)(urma_log_cb_t func);
using URMA_UNREGISTER_LOG_FUNC = urma_status_t (*)(void);
using URMA_LOG_GET_LEVEL = urma_vlog_level_t (*)(void);
using URMA_LOG_SET_LEVEL = void (*)(urma_vlog_level_t level);
using URMA_STR_TO_EID = int (*)(const char *buf, urma_eid_t *eid);
using URMA_LOG_SET_THREAD_TAG = void (*)(const char *tag);

class UrmaAPI {
public:
    static URMA_INIT hcomInnerUrmaInit;
    static URMA_UNINIT hcomInnerUrmaUninit;
    static URMA_GET_DEVICE_LIST hcomInnerUrmaGetDeviceList;
    static URMA_FREE_DEVICE_LIST hcomInnerUrmaFreeDeviceList;
    static URMA_GET_EID_LIST hcomInnerUrmaGetEidList;
    static URMA_FREE_EID_LIST hcomInnerUrmaFreeEidList;
    static URMA_GET_DEVICE_BY_NAME hcomInnerUrmaGetDeviceByName;
    static URMA_GET_DEVICE_BY_EID hcomInnerUrmaGetDeviceByEid;
    static URMA_QUERY_DEVICE hcomInnerUrmaQueryDevice;
    static URMA_CREATE_CONTEXT hcomInnerUrmaCreateContext;
    static URMA_DELETE_CONTEXT hcomInnerUrmaDeleteContext;
    static URMA_CREATE_JFC hcomInnerUrmaCreateJfc;
    static URMA_MODIFY_JFC hcomInnerUrmaModifyJfc;
    static URMA_DELETE_JFC hcomInnerUrmaDeleteJfc;
    static URMA_CREATE_JFS hcomInnerUrmaCreateJfs;
    static URMA_MODIFY_JFS hcomInnerUrmaModifyJfs;
    static URMA_QUERY_JFS hcomInnerUrmaQueryJfs;
    static URMA_DELETE_JFS hcomInnerUrmaDeleteJfs;
    static URMA_FLUSH_JFS hcomInnerUrmaFlushJfs;
    static URMA_CREATE_JFR hcomInnerUrmaCreateJfr;
    static URMA_MODIFY_JFR hcomInnerUrmaModifyJfr;
    static URMA_QUERY_JFR hcomInnerUrmaQueryJfr;
    static URMA_DELETE_JFR hcomInnerUrmaDeleteJfr;
    static URMA_IMPORT_JFR hcomInnerUrmaImportJfr;
    static URMA_UNIMPORT_JFR hcomInnerUrmaUnimportJfr;
    static URMA_ADVISE_JFR hcomInnerUrmaAdviseJfr;
    static URMA_UNADVISE_JFR hcomInnerUrmaUnadviseJfr;
    static URMA_CREATE_JETTY hcomInnerUrmaCreateJetty;
    static URMA_MODIFY_JETTY hcomInnerUrmaModifyJetty;
    static URMA_QUERY_JETTY hcomInnerUrmaQueryJetty;
    static URMA_DELETE_JETTY hcomInnerUrmaDeleteJetty;
    static URMA_IMPORT_JETTY hcomInnerUrmaImportJetty;
    static URMA_UNIMPORT_JETTY hcomInnerUrmaUnimportJetty;
    static URMA_ADVISE_JETTY hcomInnerUrmaAdviseJetty;
    static URMA_UNADVISE_JETTY hcomInnerUrmaUnadviseJetty;
    static URMA_BIND_JETTY hcomInnerUrmaBindJetty;
    static URMA_UNBIND_JETTY hcomInnerUrmaUnbindJetty;
    static URMA_FLUSH_JETTY hcomInnerUrmaFlushJetty;
    static URMA_CREATE_JETTY_GRP hcomInnerUrmaCreateJettyGrp;
    static URMA_DELETE_JETTY_GRP hcomInnerUrmaDeleteJettyGrp;
    static URMA_CREATE_JFCE hcomInnerUrmaCreateJfce;
    static URMA_DELETE_JFCE hcomInnerUrmaDeleteJfce;
    static URMA_GET_ASYNC_EVENT hcomInnerUrmaGetAsyncEvent;
    static URMA_ACK_ASYNC_EVENT hcomInnerUrmaAckAsyncEvent;
    static URMA_ALLOC_TOKEN_ID hcomInnerUrmaAllocTokenId;
    static URMA_FREE_TOKEN_ID hcomInnerUrmaFreeTokenId;
    static URMA_REGISTER_SEG hcomInnerUrmaRegisterSeg;
    static URMA_UNREGISTER_SEG hcomInnerUrmaUnregisterSeg;
    static URMA_IMPORT_SEG hcomInnerUrmaImportSeg;
    static URMA_UNIMPORT_SEG hcomInnerUrmaUnimportSeg;
    static URMA_POST_JFS_WR hcomInnerUrmaPostJfsWr;
    static URMA_POST_JFR_WR hcomInnerUrmaPostJfrWr;
    static URMA_POST_JETTY_SEND_WR hcomInnerUrmaPostJettySendWr;
    static URMA_POST_JETTY_RECV_WR hcomInnerUrmaPostJettyRecvWr;
    static URMA_WRITE hcomInnerUrmaWrite;
    static URMA_READ hcomInnerUrmaRead;
    static URMA_SEND hcomInnerUrmaSend;
    static URMA_RECV hcomInnerUrmaRecv;
    static URMA_POLL_JFC hcomInnerUrmaPollJfc;
    static URMA_REARM_JFC hcomInnerUrmaRearmJfc;
    static URMA_WAIT_JFC hcomInnerUrmaWaitJfc;
    static URMA_ACK_JFC hcomInnerUrmaAckJfc;
    static URMA_USER_CTL hcomInnerUrmaUserCtl;
    static URMA_REGISTER_LOG_FUNC hcomInnerUrmaRegisterLogFunc;
    static URMA_UNREGISTER_LOG_FUNC hcomInnerUrmaUnregisterLogFunc;
    static URMA_LOG_GET_LEVEL hcomInnerUrmaLogGetLevel;
    static URMA_LOG_SET_LEVEL hcomInnerUrmaLogSetLevel;
    static URMA_STR_TO_EID hcomInnerUrmaStrToEid;
    static URMA_LOG_SET_THREAD_TAG hcomInnerUrmaLogSetThreadTag;

    static bool IsLoaded();

#if defined(TEST_LLT) && defined(MOCK_VERBS)
    static int LoadUrmaAPI();
#else
    static int LoadUrmaAPI();
#endif

private:
    static bool gLoaded;
};

#endif
#endif // HCOM_DYLOADER_IURMA_H
