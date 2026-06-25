/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
* ubs-comm is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*      http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/
#ifndef UBS_COMM_UMQ_CONN_HELPER_H
#define UBS_COMM_UMQ_CONN_HELPER_H

#include "common/ubsocket_common_includes.h"
#include "core/umq/umq_setting.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqConnHelper {
public:
    UmqConnHelper() = delete;

public:
    static Result GetDevEid(char *dev_name, uint32_t eid_idx, umq_eid_t *eid);
    static Result PrefillRx(uint64_t umq_handle);
    static uint32_t GetLeftPostRxNum(uint64_t umq_handle);
    static Result NewBaseUmqCreateOptions(umq_create_option_t &umq_create_option,
                                          ub_trans_mode trans_mode = UmqSetting::UMQ_UB_TRANS_MODE);
    static Result GetTpInfo(umq_tp_mode_t &tp_mode, umq_tp_type_t &tp_type,
                            ub_trans_mode trans_mode = UmqSetting::UMQ_UB_TRANS_MODE);
    static Result GetRouteList(umq_route_list_t &route_list, const umq_eid_t &src_eid, const umq_eid_t &dst_eid);
    static Result RegisterSharedJfrForRead(uint64_t main_umq_handle);
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_CONN_HELPER_H
