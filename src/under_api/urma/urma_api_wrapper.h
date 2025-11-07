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
#ifndef HCOM_URMA_API_WRAPPER_H
#define HCOM_URMA_API_WRAPPER_H
#ifdef UB_BUILD_ENABLED

#include "urma_api_dl.h"

namespace ock {
namespace hcom {
class HcomUrma {
public:
    static inline urma_status_t Init(urma_init_attr_t *conf)
    {
        return UrmaAPI::hcomInnerUrmaInit(conf);
    }

    static inline urma_status_t Uninit(void)
    {
        return UrmaAPI::hcomInnerUrmaUninit();
    }

    static inline urma_device_t **GetDeviceList(int *num_devices)
    {
        return UrmaAPI::hcomInnerUrmaGetDeviceList(num_devices);
    }

    static inline void FreeDeviceList(urma_device_t **device_list)
    {
        return UrmaAPI::hcomInnerUrmaFreeDeviceList(device_list);
    }

    static inline urma_eid_info_t *GetEidList(urma_device_t *dev, uint32_t *cnt)
    {
        return UrmaAPI::hcomInnerUrmaGetEidList(dev, cnt);
    }

    static inline void FreeEidList(urma_eid_info_t *eid_list)
    {
        return UrmaAPI::hcomInnerUrmaFreeEidList(eid_list);
    }

    static inline urma_device_t *GetDeviceByName(char *dev_name)
    {
        return UrmaAPI::hcomInnerUrmaGetDeviceByName(dev_name);
    }

    static inline urma_device_t *GetDeviceByEid(urma_eid_t eid, urma_transport_type_t type)
    {
        return UrmaAPI::hcomInnerUrmaGetDeviceByEid(eid, type);
    }

    static inline urma_status_t QueryDevice(urma_device_t *dev, urma_device_attr_t *dev_attr)
    {
        return UrmaAPI::hcomInnerUrmaQueryDevice(dev, dev_attr);
    }

    static inline urma_context_t *CreateContext(urma_device_t *dev, uint32_t eid_index)
    {
        return UrmaAPI::hcomInnerUrmaCreateContext(dev, eid_index);
    }

    static inline urma_status_t DeleteContext(urma_context_t *ctx)
    {
        return UrmaAPI::hcomInnerUrmaDeleteContext(ctx);
    }

    static inline urma_jfc_t *CreateJfc(urma_context_t *ctx, urma_jfc_cfg_t *jfc_cfg)
    {
        return UrmaAPI::hcomInnerUrmaCreateJfc(ctx, jfc_cfg);
    }

    static inline urma_status_t ModifyJfc(urma_jfc_t *jfc, urma_jfc_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaModifyJfc(jfc, attr);
    }

    static inline urma_status_t DeleteJfc(urma_jfc_t *jfc)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJfc(jfc);
    }

    static inline urma_jfs_t *CreateJfs(urma_context_t *ctx, urma_jfs_cfg_t *jfs_cfg)
    {
        return UrmaAPI::hcomInnerUrmaCreateJfs(ctx, jfs_cfg);
    }

    static inline urma_status_t ModifyJfs(urma_jfs_t *jfs, urma_jfs_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaModifyJfs(jfs, attr);
    }

    static inline urma_status_t QueryJfs(urma_jfs_t *jfs, urma_jfs_cfg_t *cfg, urma_jfs_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaQueryJfs(jfs, cfg, attr);
    }

    static inline urma_status_t DeleteJfs(urma_jfs_t *jfs)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJfs(jfs);
    }

    static inline int FlushJfs(urma_jfs_t *jfs, int cr_cnt, urma_cr_t *cr)
    {
        return UrmaAPI::hcomInnerUrmaFlushJfs(jfs, cr_cnt, cr);
    }

    static inline urma_jfr_t *CreateJfr(urma_context_t *ctx, urma_jfr_cfg_t *jfr_cfg)
    {
        return UrmaAPI::hcomInnerUrmaCreateJfr(ctx, jfr_cfg);
    }

    static inline urma_status_t ModifyJfr(urma_jfr_t *jfr, urma_jfr_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaModifyJfr(jfr, attr);
    }

    static inline urma_status_t QueryJfr(urma_jfr_t *jfr, urma_jfr_cfg_t *cfg, urma_jfr_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaQueryJfr(jfr, cfg, attr);
    }

    static inline urma_status_t DeleteJfr(urma_jfr_t *jfr)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJfr(jfr);
    }

    static inline urma_target_jetty_t *ImportJfr(urma_context_t *ctx, urma_rjfr_t *rjfr, urma_token_t *token_value)
    {
        return UrmaAPI::hcomInnerUrmaImportJfr(ctx, rjfr, token_value);
    }

    static inline urma_status_t UnimportJfr(urma_target_jetty_t *target_jfr)
    {
        return UrmaAPI::hcomInnerUrmaUnimportJfr(target_jfr);
    }

    static inline urma_status_t AdviseJfr(urma_jfs_t *jfs, urma_target_jetty_t *tjfr)
    {
        return UrmaAPI::hcomInnerUrmaAdviseJfr(jfs, tjfr);
    }

    static inline urma_status_t UnadviseJfr(urma_jfs_t *jfs, urma_target_jetty_t *tjfr)
    {
        return UrmaAPI::hcomInnerUrmaUnadviseJfr(jfs, tjfr);
    }

    static inline urma_jetty_t *CreateJetty(urma_context_t *ctx, urma_jetty_cfg_t *jetty_cfg)
    {
        return UrmaAPI::hcomInnerUrmaCreateJetty(ctx, jetty_cfg);
    }

    static inline urma_status_t ModifyJetty(urma_jetty_t *jetty, urma_jetty_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaModifyJetty(jetty, attr);
    }

    static inline urma_status_t QueryJetty(urma_jetty_t *jetty, urma_jetty_cfg_t *cfg, urma_jetty_attr_t *attr)
    {
        return UrmaAPI::hcomInnerUrmaQueryJetty(jetty, cfg, attr);
    }

    static inline urma_status_t DeleteJetty(urma_jetty_t *jetty)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJetty(jetty);
    }

    static inline urma_target_jetty_t *ImportJetty(urma_context_t *ctx, urma_rjetty_t *rjetty,
        urma_token_t *token_value)
    {
        return UrmaAPI::hcomInnerUrmaImportJetty(ctx, rjetty, token_value);
    }

    static inline urma_status_t UnimportJetty(urma_target_jetty_t *tjetty)
    {
        return UrmaAPI::hcomInnerUrmaUnimportJetty(tjetty);
    }

    static inline urma_status_t AdviseJetty(urma_jetty_t *jetty, urma_target_jetty_t *tjetty)
    {
        return UrmaAPI::hcomInnerUrmaAdviseJetty(jetty, tjetty);
    }

    static inline urma_status_t UnadviseJetty(urma_jetty_t *jetty, urma_target_jetty_t *tjetty)
    {
        return UrmaAPI::hcomInnerUrmaUnadviseJetty(jetty, tjetty);
    }

    static inline urma_status_t BindJetty(urma_jetty_t *jetty, urma_target_jetty_t *tjetty)
    {
        return UrmaAPI::hcomInnerUrmaBindJetty(jetty, tjetty);
    }

    static inline urma_status_t UnbindJetty(urma_jetty_t *jetty)
    {
        return UrmaAPI::hcomInnerUrmaUnbindJetty(jetty);
    }

    static inline int FlushJetty(urma_jetty_t *jetty, int cr_cnt, urma_cr_t *cr)
    {
        return UrmaAPI::hcomInnerUrmaFlushJetty(jetty, cr_cnt, cr);
    }

    static inline urma_jetty_grp_t *CreateJettyGrp(urma_context_t *ctx, urma_jetty_grp_cfg_t *cfg)
    {
        return UrmaAPI::hcomInnerUrmaCreateJettyGrp(ctx, cfg);
    }

    static inline urma_status_t DeleteJettyGrp(urma_jetty_grp_t *jetty_grp)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJettyGrp(jetty_grp);
    }

    static inline urma_jfce_t *CreateJfce(urma_context_t *ctx)
    {
        return UrmaAPI::hcomInnerUrmaCreateJfce(ctx);
    }

    static inline urma_status_t DeleteJfce(urma_jfce_t *jfce)
    {
        return UrmaAPI::hcomInnerUrmaDeleteJfce(jfce);
    }

    static inline urma_status_t GetAsyncEvent(urma_context_t *ctx, urma_async_event_t *event)
    {
        return UrmaAPI::hcomInnerUrmaGetAsyncEvent(ctx, event);
    }

    static inline void AckAsyncEvent(urma_async_event_t *event)
    {
        return UrmaAPI::hcomInnerUrmaAckAsyncEvent(event);
    }

    static inline urma_token_id_t *AllocTokenId(urma_context_t *ctx)
    {
        return UrmaAPI::hcomInnerUrmaAllocTokenId(ctx);
    }

    static inline urma_status_t FreeTokenId(urma_token_id_t *token_id)
    {
        return UrmaAPI::hcomInnerUrmaFreeTokenId(token_id);
    }

    static inline urma_target_seg_t *RegisterSeg(urma_context_t *ctx, urma_seg_cfg_t *seg_cfg)
    {
        return UrmaAPI::hcomInnerUrmaRegisterSeg(ctx, seg_cfg);
    }

    static inline urma_status_t UnregisterSeg(urma_target_seg_t *target_seg)
    {
        return UrmaAPI::hcomInnerUrmaUnregisterSeg(target_seg);
    }

    static inline urma_target_seg_t *ImportSeg(urma_context_t *ctx, urma_seg_t *seg, urma_token_t *token_value,
        uint64_t addr, urma_import_seg_flag_t flag)
    {
        return UrmaAPI::hcomInnerUrmaImportSeg(ctx, seg, token_value, addr, flag);
    }

    static inline urma_status_t UnimportSeg(urma_target_seg_t *tseg)
    {
        return UrmaAPI::hcomInnerUrmaUnimportSeg(tseg);
    }

    static inline urma_status_t PostJfsWr(urma_jfs_t *jfs, urma_jfs_wr_t *wr, urma_jfs_wr_t **bad_wr)
    {
        return UrmaAPI::hcomInnerUrmaPostJfsWr(jfs, wr, bad_wr);
    }

    static inline urma_status_t PostJfrWr(urma_jfr_t *jfr, urma_jfr_wr_t *wr, urma_jfr_wr_t **bad_wr)
    {
        return UrmaAPI::hcomInnerUrmaPostJfrWr(jfr, wr, bad_wr);
    }

    static inline urma_status_t PostJettySendWr(urma_jetty_t *jetty, urma_jfs_wr_t *wr, urma_jfs_wr_t **bad_wr)
    {
        return UrmaAPI::hcomInnerUrmaPostJettySendWr(jetty, wr, bad_wr);
    }

    static inline urma_status_t PostJettySendWr(
        urma_jetty_t *jetty, urma_jfs_wr_t *wr, uint32_t wrCnt, urma_jfs_wr_t **bad_wr)
    {
        return UrmaAPI::hcomInnerUrmaPostJettySendWr(jetty, wr, bad_wr);
    }

    static inline urma_status_t PostJettyRecvWr(urma_jetty_t *jetty, urma_jfr_wr_t *wr, urma_jfr_wr_t **bad_wr)
    {
        return UrmaAPI::hcomInnerUrmaPostJettyRecvWr(jetty, wr, bad_wr);
    }

    static inline urma_status_t Write(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *dst_tseg,
        urma_target_seg_t *src_tseg, uint64_t dst, uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag,
        uint64_t user_ctx)
    {
        return UrmaAPI::hcomInnerUrmaWrite(jfs, target_jfr, dst_tseg, src_tseg, dst, src, len, flag, user_ctx);
    }

    static inline urma_status_t Read(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *dst_tseg,
        urma_target_seg_t *src_tseg, uint64_t dst, uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag,
        uint64_t user_ctx)
    {
        return UrmaAPI::hcomInnerUrmaRead(jfs, target_jfr, dst_tseg, src_tseg, dst, src, len, flag, user_ctx);
    }

    static inline urma_status_t Send(urma_jfs_t *jfs, urma_target_jetty_t *target_jfr, urma_target_seg_t *src_tseg,
        uint64_t src, uint32_t len, urma_jfs_wr_flag_t flag, uint64_t user_ctx)
    {
        return UrmaAPI::hcomInnerUrmaSend(jfs, target_jfr, src_tseg, src, len, flag, user_ctx);
    }

    static inline urma_status_t Recv(urma_jfr_t *jfr, urma_target_seg_t *recv_tseg, uint64_t buf, uint32_t len,
        uint64_t user_ctx)
    {
        return UrmaAPI::hcomInnerUrmaRecv(jfr, recv_tseg, buf, len, user_ctx);
    }

    static inline int PollJfc(urma_jfc_t *jfc, int cr_cnt, urma_cr_t *cr)
    {
        return UrmaAPI::hcomInnerUrmaPollJfc(jfc, cr_cnt, cr);
    }

    static inline urma_status_t RearmJfc(urma_jfc_t *jfc, bool solicited_only)
    {
        return UrmaAPI::hcomInnerUrmaRearmJfc(jfc, solicited_only);
    }

    static inline int WaitJfc(urma_jfce_t *jfce, uint32_t jfc_cnt, int time_out, urma_jfc_t *jfc[])
    {
        return UrmaAPI::hcomInnerUrmaWaitJfc(jfce, jfc_cnt, time_out, jfc);
    }

    static inline void AckJfc(urma_jfc_t *jfc[], uint32_t nevents[], uint32_t jfc_cnt)
    {
        return UrmaAPI::hcomInnerUrmaAckJfc(jfc, nevents, jfc_cnt);
    }

    static inline urma_status_t UserCtl(urma_context_t *ctx, urma_user_ctl_in_t *in, urma_user_ctl_out_t *out)
    {
        return UrmaAPI::hcomInnerUrmaUserCtl(ctx, in, out);
    }

    static inline urma_status_t RegisterLogFunc(urma_log_cb_t func)
    {
        return UrmaAPI::hcomInnerUrmaRegisterLogFunc(func);
    }

    static inline urma_status_t UnregisterLogFunc(void)
    {
        return UrmaAPI::hcomInnerUrmaUnregisterLogFunc();
    }

    static inline urma_vlog_level_t LogGetLevel(void)
    {
        return UrmaAPI::hcomInnerUrmaLogGetLevel();
    }

    static inline void LogSetLevel(urma_vlog_level_t level)
    {
        return UrmaAPI::hcomInnerUrmaLogSetLevel(level);
    }

    static inline int StrToEid(const char *buf, urma_eid_t *eid)
    {
        return UrmaAPI::hcomInnerUrmaStrToEid(buf, eid);
    }

    static inline void LogSetThreadTag(const char *tag)
    {
        return UrmaAPI::hcomInnerUrmaLogSetThreadTag(tag);
    }

    static inline bool IsLoaded()
    {
        return UrmaAPI::IsLoaded();
    }

    static inline int Load()
    {
#if !defined(TEST_LLT) || !defined(MOCK_VERBS)
        return UrmaAPI::LoadUrmaAPI();
#else
        return UrmaAPI::LoadUrmaAPI();
#endif
    }
};
}
}

#endif
#endif // HCOM_URMA_API_WRAPPER_H