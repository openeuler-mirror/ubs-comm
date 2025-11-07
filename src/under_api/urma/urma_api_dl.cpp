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
#ifdef UB_BUILD_ENABLED
#include <dlfcn.h>

#if defined(TEST_LLT) && defined(MOCK_URMA)
#include "fake_urma.h"
#endif
#include "hcom_log.h"
#include "urma_api_dl.h"

using namespace ock::hcom;

URMA_INIT UrmaAPI::hcomInnerUrmaInit = nullptr;
URMA_UNINIT UrmaAPI::hcomInnerUrmaUninit = nullptr;
URMA_GET_DEVICE_LIST UrmaAPI::hcomInnerUrmaGetDeviceList = nullptr;
URMA_FREE_DEVICE_LIST UrmaAPI::hcomInnerUrmaFreeDeviceList = nullptr;
URMA_GET_EID_LIST UrmaAPI::hcomInnerUrmaGetEidList = nullptr;
URMA_FREE_EID_LIST UrmaAPI::hcomInnerUrmaFreeEidList = nullptr;
URMA_GET_DEVICE_BY_NAME UrmaAPI::hcomInnerUrmaGetDeviceByName = nullptr;
URMA_GET_DEVICE_BY_EID UrmaAPI::hcomInnerUrmaGetDeviceByEid = nullptr;
URMA_QUERY_DEVICE UrmaAPI::hcomInnerUrmaQueryDevice = nullptr;
URMA_CREATE_CONTEXT UrmaAPI::hcomInnerUrmaCreateContext = nullptr;
URMA_DELETE_CONTEXT UrmaAPI::hcomInnerUrmaDeleteContext = nullptr;
URMA_CREATE_JFC UrmaAPI::hcomInnerUrmaCreateJfc = nullptr;
URMA_MODIFY_JFC UrmaAPI::hcomInnerUrmaModifyJfc = nullptr;
URMA_DELETE_JFC UrmaAPI::hcomInnerUrmaDeleteJfc = nullptr;
URMA_CREATE_JFS UrmaAPI::hcomInnerUrmaCreateJfs = nullptr;
URMA_MODIFY_JFS UrmaAPI::hcomInnerUrmaModifyJfs = nullptr;
URMA_QUERY_JFS UrmaAPI::hcomInnerUrmaQueryJfs = nullptr;
URMA_DELETE_JFS UrmaAPI::hcomInnerUrmaDeleteJfs = nullptr;
URMA_FLUSH_JFS UrmaAPI::hcomInnerUrmaFlushJfs = nullptr;
URMA_CREATE_JFR UrmaAPI::hcomInnerUrmaCreateJfr = nullptr;
URMA_MODIFY_JFR UrmaAPI::hcomInnerUrmaModifyJfr = nullptr;
URMA_QUERY_JFR UrmaAPI::hcomInnerUrmaQueryJfr = nullptr;
URMA_DELETE_JFR UrmaAPI::hcomInnerUrmaDeleteJfr = nullptr;
URMA_IMPORT_JFR UrmaAPI::hcomInnerUrmaImportJfr = nullptr;
URMA_UNIMPORT_JFR UrmaAPI::hcomInnerUrmaUnimportJfr = nullptr;
URMA_ADVISE_JFR UrmaAPI::hcomInnerUrmaAdviseJfr = nullptr;
URMA_UNADVISE_JFR UrmaAPI::hcomInnerUrmaUnadviseJfr = nullptr;
URMA_CREATE_JETTY UrmaAPI::hcomInnerUrmaCreateJetty = nullptr;
URMA_MODIFY_JETTY UrmaAPI::hcomInnerUrmaModifyJetty = nullptr;
URMA_QUERY_JETTY UrmaAPI::hcomInnerUrmaQueryJetty = nullptr;
URMA_DELETE_JETTY UrmaAPI::hcomInnerUrmaDeleteJetty = nullptr;
URMA_IMPORT_JETTY UrmaAPI::hcomInnerUrmaImportJetty = nullptr;
URMA_UNIMPORT_JETTY UrmaAPI::hcomInnerUrmaUnimportJetty = nullptr;
URMA_ADVISE_JETTY UrmaAPI::hcomInnerUrmaAdviseJetty = nullptr;
URMA_UNADVISE_JETTY UrmaAPI::hcomInnerUrmaUnadviseJetty = nullptr;
URMA_BIND_JETTY UrmaAPI::hcomInnerUrmaBindJetty = nullptr;
URMA_UNBIND_JETTY UrmaAPI::hcomInnerUrmaUnbindJetty = nullptr;
URMA_FLUSH_JETTY UrmaAPI::hcomInnerUrmaFlushJetty = nullptr;
URMA_CREATE_JETTY_GRP UrmaAPI::hcomInnerUrmaCreateJettyGrp = nullptr;
URMA_DELETE_JETTY_GRP UrmaAPI::hcomInnerUrmaDeleteJettyGrp = nullptr;
URMA_CREATE_JFCE UrmaAPI::hcomInnerUrmaCreateJfce = nullptr;
URMA_DELETE_JFCE UrmaAPI::hcomInnerUrmaDeleteJfce = nullptr;
URMA_GET_ASYNC_EVENT UrmaAPI::hcomInnerUrmaGetAsyncEvent = nullptr;
URMA_ACK_ASYNC_EVENT UrmaAPI::hcomInnerUrmaAckAsyncEvent = nullptr;
URMA_ALLOC_TOKEN_ID UrmaAPI::hcomInnerUrmaAllocTokenId = nullptr;
URMA_FREE_TOKEN_ID UrmaAPI::hcomInnerUrmaFreeTokenId = nullptr;
URMA_REGISTER_SEG UrmaAPI::hcomInnerUrmaRegisterSeg = nullptr;
URMA_UNREGISTER_SEG UrmaAPI::hcomInnerUrmaUnregisterSeg = nullptr;
URMA_IMPORT_SEG UrmaAPI::hcomInnerUrmaImportSeg = nullptr;
URMA_UNIMPORT_SEG UrmaAPI::hcomInnerUrmaUnimportSeg = nullptr;
URMA_POST_JFS_WR UrmaAPI::hcomInnerUrmaPostJfsWr = nullptr;
URMA_POST_JFR_WR UrmaAPI::hcomInnerUrmaPostJfrWr = nullptr;
URMA_POST_JETTY_SEND_WR UrmaAPI::hcomInnerUrmaPostJettySendWr = nullptr;
URMA_POST_JETTY_RECV_WR UrmaAPI::hcomInnerUrmaPostJettyRecvWr = nullptr;
URMA_WRITE UrmaAPI::hcomInnerUrmaWrite = nullptr;
URMA_READ UrmaAPI::hcomInnerUrmaRead = nullptr;
URMA_SEND UrmaAPI::hcomInnerUrmaSend = nullptr;
URMA_RECV UrmaAPI::hcomInnerUrmaRecv = nullptr;
URMA_POLL_JFC UrmaAPI::hcomInnerUrmaPollJfc = nullptr;
URMA_REARM_JFC UrmaAPI::hcomInnerUrmaRearmJfc = nullptr;
URMA_WAIT_JFC UrmaAPI::hcomInnerUrmaWaitJfc = nullptr;
URMA_ACK_JFC UrmaAPI::hcomInnerUrmaAckJfc = nullptr;
URMA_USER_CTL UrmaAPI::hcomInnerUrmaUserCtl = nullptr;
URMA_REGISTER_LOG_FUNC UrmaAPI::hcomInnerUrmaRegisterLogFunc = nullptr;
URMA_UNREGISTER_LOG_FUNC UrmaAPI::hcomInnerUrmaUnregisterLogFunc = nullptr;
URMA_LOG_GET_LEVEL UrmaAPI::hcomInnerUrmaLogGetLevel = nullptr;
URMA_LOG_SET_LEVEL UrmaAPI::hcomInnerUrmaLogSetLevel = nullptr;
URMA_STR_TO_EID UrmaAPI::hcomInnerUrmaStrToEid = nullptr;
URMA_LOG_SET_THREAD_TAG UrmaAPI::hcomInnerUrmaLogSetThreadTag = nullptr;

bool UrmaAPI::gLoaded = false;

bool UrmaAPI::IsLoaded()
{
    return gLoaded;
}

#if !defined(TEST_LLT) || !defined(MOCK_URMA)
#define DLSYM(type, ptr, sym)                                                                 \
    do {                                                                                      \
        auto ptr1 = dlsym(handle, sym);                                                       \
        if (ptr1 == nullptr) {                                                                \
            NN_LOG_ERROR("Failed to load function " << sym << ", error " << dlerror()); \
            dlclose(handle);                                                                  \
            return -1;                                                                        \
        }                                                                                     \
        ptr = (type)ptr1;                                                                     \
    } while (0)

int UrmaAPI::LoadUrmaAPI()
{
    if (gLoaded) {
        return 0;
    }

    // UBC 多路径使用虚拟聚合设备，依赖 liburma_bond.so, 而它又会在 liburma.so 中隐式地打开，且依赖 liburma.so 中的符号。
    void *handle = dlopen(URMA_SO_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        NN_LOG_ERROR("Failed to load verbs so " << URMA_SO_PATH << ", error " << dlerror());
        return -1;
    }

    DLSYM(URMA_INIT, UrmaAPI::hcomInnerUrmaInit, "urma_init");
    DLSYM(URMA_UNINIT, UrmaAPI::hcomInnerUrmaUninit, "urma_uninit");
    DLSYM(URMA_GET_DEVICE_LIST, UrmaAPI::hcomInnerUrmaGetDeviceList, "urma_get_device_list");
    DLSYM(URMA_FREE_DEVICE_LIST, UrmaAPI::hcomInnerUrmaFreeDeviceList, "urma_free_device_list");
    DLSYM(URMA_GET_EID_LIST, UrmaAPI::hcomInnerUrmaGetEidList, "urma_get_eid_list");
    DLSYM(URMA_FREE_EID_LIST, UrmaAPI::hcomInnerUrmaFreeEidList, "urma_free_eid_list");
    DLSYM(URMA_GET_DEVICE_BY_NAME, UrmaAPI::hcomInnerUrmaGetDeviceByName, "urma_get_device_by_name");
    DLSYM(URMA_GET_DEVICE_BY_EID, UrmaAPI::hcomInnerUrmaGetDeviceByEid, "urma_get_device_by_eid");
    DLSYM(URMA_QUERY_DEVICE, UrmaAPI::hcomInnerUrmaQueryDevice, "urma_query_device");
    DLSYM(URMA_CREATE_CONTEXT, UrmaAPI::hcomInnerUrmaCreateContext, "urma_create_context");
    DLSYM(URMA_DELETE_CONTEXT, UrmaAPI::hcomInnerUrmaDeleteContext, "urma_delete_context");
    DLSYM(URMA_CREATE_JFC, UrmaAPI::hcomInnerUrmaCreateJfc, "urma_create_jfc");
    DLSYM(URMA_MODIFY_JFC, UrmaAPI::hcomInnerUrmaModifyJfc, "urma_modify_jfc");
    DLSYM(URMA_DELETE_JFC, UrmaAPI::hcomInnerUrmaDeleteJfc, "urma_delete_jfc");
    DLSYM(URMA_CREATE_JFS, UrmaAPI::hcomInnerUrmaCreateJfs, "urma_create_jfs");
    DLSYM(URMA_MODIFY_JFS, UrmaAPI::hcomInnerUrmaModifyJfs, "urma_modify_jfs");
    DLSYM(URMA_QUERY_JFS, UrmaAPI::hcomInnerUrmaQueryJfs, "urma_query_jfs");
    DLSYM(URMA_DELETE_JFS, UrmaAPI::hcomInnerUrmaDeleteJfs, "urma_delete_jfs");
    DLSYM(URMA_FLUSH_JFS, UrmaAPI::hcomInnerUrmaFlushJfs, "urma_flush_jfs");
    DLSYM(URMA_CREATE_JFR, UrmaAPI::hcomInnerUrmaCreateJfr, "urma_create_jfr");
    DLSYM(URMA_MODIFY_JFR, UrmaAPI::hcomInnerUrmaModifyJfr, "urma_modify_jfr");
    DLSYM(URMA_QUERY_JFR, UrmaAPI::hcomInnerUrmaQueryJfr, "urma_query_jfr");
    DLSYM(URMA_DELETE_JFR, UrmaAPI::hcomInnerUrmaDeleteJfr, "urma_delete_jfr");
    DLSYM(URMA_IMPORT_JFR, UrmaAPI::hcomInnerUrmaImportJfr, "urma_import_jfr");
    DLSYM(URMA_UNIMPORT_JFR, UrmaAPI::hcomInnerUrmaUnimportJfr, "urma_unimport_jfr");
    DLSYM(URMA_ADVISE_JFR, UrmaAPI::hcomInnerUrmaAdviseJfr, "urma_advise_jfr");
    DLSYM(URMA_UNADVISE_JFR, UrmaAPI::hcomInnerUrmaUnadviseJfr, "urma_unadvise_jfr");
    DLSYM(URMA_CREATE_JETTY, UrmaAPI::hcomInnerUrmaCreateJetty, "urma_create_jetty");
    DLSYM(URMA_MODIFY_JETTY, UrmaAPI::hcomInnerUrmaModifyJetty, "urma_modify_jetty");
    DLSYM(URMA_QUERY_JETTY, UrmaAPI::hcomInnerUrmaQueryJetty, "urma_query_jetty");
    DLSYM(URMA_DELETE_JETTY, UrmaAPI::hcomInnerUrmaDeleteJetty, "urma_delete_jetty");
    DLSYM(URMA_IMPORT_JETTY, UrmaAPI::hcomInnerUrmaImportJetty, "urma_import_jetty");
    DLSYM(URMA_UNIMPORT_JETTY, UrmaAPI::hcomInnerUrmaUnimportJetty, "urma_unimport_jetty");
    DLSYM(URMA_ADVISE_JETTY, UrmaAPI::hcomInnerUrmaAdviseJetty, "urma_advise_jetty");
    DLSYM(URMA_UNADVISE_JETTY, UrmaAPI::hcomInnerUrmaUnadviseJetty, "urma_unadvise_jetty");
    DLSYM(URMA_BIND_JETTY, UrmaAPI::hcomInnerUrmaBindJetty, "urma_bind_jetty");
    DLSYM(URMA_UNBIND_JETTY, UrmaAPI::hcomInnerUrmaUnbindJetty, "urma_unbind_jetty");
    DLSYM(URMA_FLUSH_JETTY, UrmaAPI::hcomInnerUrmaFlushJetty, "urma_flush_jetty");
    DLSYM(URMA_CREATE_JETTY_GRP, UrmaAPI::hcomInnerUrmaCreateJettyGrp, "urma_create_jetty_grp");
    DLSYM(URMA_DELETE_JETTY_GRP, UrmaAPI::hcomInnerUrmaDeleteJettyGrp, "urma_delete_jetty_grp");
    DLSYM(URMA_CREATE_JFCE, UrmaAPI::hcomInnerUrmaCreateJfce, "urma_create_jfce");
    DLSYM(URMA_DELETE_JFCE, UrmaAPI::hcomInnerUrmaDeleteJfce, "urma_delete_jfce");
    DLSYM(URMA_GET_ASYNC_EVENT, UrmaAPI::hcomInnerUrmaGetAsyncEvent, "urma_get_async_event");
    DLSYM(URMA_ACK_ASYNC_EVENT, UrmaAPI::hcomInnerUrmaAckAsyncEvent, "urma_ack_async_event");
    DLSYM(URMA_ALLOC_TOKEN_ID, UrmaAPI::hcomInnerUrmaAllocTokenId, "urma_alloc_token_id");
    DLSYM(URMA_FREE_TOKEN_ID, UrmaAPI::hcomInnerUrmaFreeTokenId, "urma_free_token_id");
    DLSYM(URMA_REGISTER_SEG, UrmaAPI::hcomInnerUrmaRegisterSeg, "urma_register_seg");
    DLSYM(URMA_UNREGISTER_SEG, UrmaAPI::hcomInnerUrmaUnregisterSeg, "urma_unregister_seg");
    DLSYM(URMA_IMPORT_SEG, UrmaAPI::hcomInnerUrmaImportSeg, "urma_import_seg");
    DLSYM(URMA_UNIMPORT_SEG, UrmaAPI::hcomInnerUrmaUnimportSeg, "urma_unimport_seg");
    DLSYM(URMA_POST_JFS_WR, UrmaAPI::hcomInnerUrmaPostJfsWr, "urma_post_jfs_wr");
    DLSYM(URMA_POST_JFR_WR, UrmaAPI::hcomInnerUrmaPostJfrWr, "urma_post_jfr_wr");
    DLSYM(URMA_POST_JETTY_SEND_WR, UrmaAPI::hcomInnerUrmaPostJettySendWr, "urma_post_jetty_send_wr");
    DLSYM(URMA_POST_JETTY_RECV_WR, UrmaAPI::hcomInnerUrmaPostJettyRecvWr, "urma_post_jetty_recv_wr");
    DLSYM(URMA_WRITE, UrmaAPI::hcomInnerUrmaWrite, "urma_write");
    DLSYM(URMA_READ, UrmaAPI::hcomInnerUrmaRead, "urma_read");
    DLSYM(URMA_SEND, UrmaAPI::hcomInnerUrmaSend, "urma_send");
    DLSYM(URMA_RECV, UrmaAPI::hcomInnerUrmaRecv, "urma_recv");
    DLSYM(URMA_POLL_JFC, UrmaAPI::hcomInnerUrmaPollJfc, "urma_poll_jfc");
    DLSYM(URMA_REARM_JFC, UrmaAPI::hcomInnerUrmaRearmJfc, "urma_rearm_jfc");
    DLSYM(URMA_WAIT_JFC, UrmaAPI::hcomInnerUrmaWaitJfc, "urma_wait_jfc");
    DLSYM(URMA_ACK_JFC, UrmaAPI::hcomInnerUrmaAckJfc, "urma_ack_jfc");
    DLSYM(URMA_USER_CTL, UrmaAPI::hcomInnerUrmaUserCtl, "urma_user_ctl");
    DLSYM(URMA_REGISTER_LOG_FUNC, UrmaAPI::hcomInnerUrmaRegisterLogFunc, "urma_register_log_func");
    DLSYM(URMA_UNREGISTER_LOG_FUNC, UrmaAPI::hcomInnerUrmaUnregisterLogFunc, "urma_unregister_log_func");
    DLSYM(URMA_LOG_GET_LEVEL, UrmaAPI::hcomInnerUrmaLogGetLevel, "urma_log_get_level");
    DLSYM(URMA_LOG_SET_LEVEL, UrmaAPI::hcomInnerUrmaLogSetLevel, "urma_log_set_level");
    DLSYM(URMA_STR_TO_EID, UrmaAPI::hcomInnerUrmaStrToEid, "urma_str_to_eid");
    DLSYM(URMA_LOG_SET_THREAD_TAG, UrmaAPI::hcomInnerUrmaLogSetThreadTag, "urma_log_set_thread_tag");

    NN_LOG_INFO("Success to load urma api");
    gLoaded = true;

    return 0;
}
#else
int UrmaAPI::LoadUrmaAPI()
{
    if (gLoaded) {
        return 0;
    }

    UrmaAPI::hcomInnerUrmaInit = urma_init;
    UrmaAPI::hcomInnerUrmaUninit = urma_uninit;
    UrmaAPI::hcomInnerUrmaGetDeviceList = urma_get_device_list;
    UrmaAPI::hcomInnerUrmaFreeDeviceList = urma_free_device_list;
    UrmaAPI::hcomInnerUrmaGetEidList = urma_get_eid_list;
    UrmaAPI::hcomInnerUrmaFreeEidList = urma_free_eid_list;
    UrmaAPI::hcomInnerUrmaGetDeviceByName = urma_get_device_by_name;
    UrmaAPI::hcomInnerUrmaGetDeviceByEid = urma_get_device_by_eid;
    UrmaAPI::hcomInnerUrmaQueryDevice = urma_query_device;
    UrmaAPI::hcomInnerUrmaCreateContext = urma_create_context;
    UrmaAPI::hcomInnerUrmaDeleteContext = urma_delete_context;
    UrmaAPI::hcomInnerUrmaCreateJfc = urma_create_jfc;
    UrmaAPI::hcomInnerUrmaModifyJfc = urma_modify_jfc;
    UrmaAPI::hcomInnerUrmaDeleteJfc = urma_delete_jfc;
    UrmaAPI::hcomInnerUrmaCreateJfs = urma_create_jfs;
    UrmaAPI::hcomInnerUrmaModifyJfs = urma_modify_jfs;
    UrmaAPI::hcomInnerUrmaQueryJfs = urma_query_jfs;
    UrmaAPI::hcomInnerUrmaDeleteJfs = urma_delete_jfs;
    UrmaAPI::hcomInnerUrmaFlushJfs = urma_flush_jfs;
    UrmaAPI::hcomInnerUrmaCreateJfr = urma_create_jfr;
    UrmaAPI::hcomInnerUrmaModifyJfr = urma_modify_jfr;
    UrmaAPI::hcomInnerUrmaQueryJfr = urma_query_jfr;
    UrmaAPI::hcomInnerUrmaDeleteJfr = urma_delete_jfr;
    UrmaAPI::hcomInnerUrmaImportJfr = urma_import_jfr;
    UrmaAPI::hcomInnerUrmaUnimportJfr = urma_unimport_jfr;
    UrmaAPI::hcomInnerUrmaAdviseJfr = urma_advise_jfr;
    UrmaAPI::hcomInnerUrmaUnadviseJfr = urma_unadvise_jfr;
    UrmaAPI::hcomInnerUrmaCreateJetty = urma_create_jetty;
    UrmaAPI::hcomInnerUrmaModifyJetty = urma_modify_jetty;
    UrmaAPI::hcomInnerUrmaQueryJetty = urma_query_jetty;
    UrmaAPI::hcomInnerUrmaDeleteJetty = urma_delete_jetty;
    UrmaAPI::hcomInnerUrmaImportJetty = urma_import_jetty;
    UrmaAPI::hcomInnerUrmaUnimportJetty = urma_unimport_jetty;
    UrmaAPI::hcomInnerUrmaAdviseJetty = urma_advise_jetty;
    UrmaAPI::hcomInnerUrmaUnadviseJetty = urma_unadvise_jetty;
    UrmaAPI::hcomInnerUrmaBindJetty = urma_bind_jetty;
    UrmaAPI::hcomInnerUrmaUnbindJetty = urma_unbind_jetty;
    UrmaAPI::hcomInnerUrmaFlushJetty = urma_flush_jetty;
    UrmaAPI::hcomInnerUrmaCreateJettyGrp = urma_create_jetty_grp;
    UrmaAPI::hcomInnerUrmaDeleteJettyGrp = urma_delete_jetty_grp;
    UrmaAPI::hcomInnerUrmaCreateJfce = urma_create_jfce;
    UrmaAPI::hcomInnerUrmaDeleteJfce = urma_delete_jfce;
    UrmaAPI::hcomInnerUrmaGetAsyncEvent = urma_get_async_event;
    UrmaAPI::hcomInnerUrmaAckAsyncEvent = urma_ack_async_event;
    UrmaAPI::hcomInnerUrmaAllocTokenId = urma_alloc_token_id;
    UrmaAPI::hcomInnerUrmaFreeTokenId = urma_free_token_id;
    UrmaAPI::hcomInnerUrmaRegisterSeg = urma_register_seg;
    UrmaAPI::hcomInnerUrmaUnregisterSeg = urma_unregister_seg;
    UrmaAPI::hcomInnerUrmaImportSeg = urma_import_seg;
    UrmaAPI::hcomInnerUrmaUnimportSeg = urma_unimport_seg;
    UrmaAPI::hcomInnerUrmaPostJfsWr = urma_post_jfs_wr;
    UrmaAPI::hcomInnerUrmaPostJfrWr = urma_post_jfr_wr;
    UrmaAPI::hcomInnerUrmaPostJettySendWr = urma_post_jetty_send_wr;
    UrmaAPI::hcomInnerUrmaPostJettyRecvWr = urma_post_jetty_recv_wr;
    UrmaAPI::hcomInnerUrmaWrite = urma_write;
    UrmaAPI::hcomInnerUrmaRead = urma_read;
    UrmaAPI::hcomInnerUrmaSend = urma_send;
    UrmaAPI::hcomInnerUrmaRecv = urma_recv;
    UrmaAPI::hcomInnerUrmaPollJfc = urma_poll_jfc;
    UrmaAPI::hcomInnerUrmaRearmJfc = urma_rearm_jfc;
    UrmaAPI::hcomInnerUrmaWaitJfc = urma_wait_jfc;
    UrmaAPI::hcomInnerUrmaAckJfc = urma_ack_jfc;
    UrmaAPI::hcomInnerUrmaUserCtl = urma_user_ctl;
    UrmaAPI::hcomInnerUrmaRegisterLogFunc = urma_register_log_func;
    UrmaAPI::hcomInnerUrmaUnregisterLogFunc = urma_unregister_log_func;
    UrmaAPI::hcomInnerUrmaLogGetLevel = urma_log_get_level;
    UrmaAPI::hcomInnerUrmaLogSetLevel = urma_log_set_level;
    UrmaAPI::hcomInnerUrmaStrToEid = urma_str_to_eid;
    UrmaAPI::hcomInnerUrmaLogSetThreadTag = urma_log_set_thread_tag;

    NN_LOG_INFO("Success to load fake iburma");
    gLoaded = true;

    return 0;
}
#endif

#endif
