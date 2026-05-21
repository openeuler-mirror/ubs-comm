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
#ifndef UBS_COMM_URMA_WRAPPER_H
#define UBS_COMM_URMA_WRAPPER_H

#include "under_api/urma/urma_api_dl.h"
#include "under_api/urma/urma_types.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {
struct UBEId {
    uint16_t dev_index = 0;
    uint16_t eid_index = 0;
    urma_eid_t urma_eid{};
    uint8_t bandwidth = 0;
} __attribute__((packed));

class UrmaContext;
class UrmaJetty;
class UrmaJfs;
class UrmaJfr;
class UrmaJfc;
class UrmaSegment;
using UrmaContextPtr = Ref<UrmaContext>;
using UrmaJettyPtr = Ref<UrmaJetty>;
using UrmaJfsPtr = Ref<UrmaJfs>;
using UrmaJfrPtr = Ref<UrmaJfr>;
using UrmaJfcPtr = Ref<UrmaJfc>;
using UrmaSegmentPtr = Ref<UrmaSegment>;

class UrmaMisc {
public:
    
};

class UrmaContext : public Referable {
public:
private:
    urma_context_t *context_ = nullptr;
    urma_device_attr_t *dev_attr_ = nullptr;
    uint8_t port_count_ = 1;
    uint32_t max_jfs_ = 0;
    uint32_t max_jfr = 0;
    int cpt_priority_ = 0;
    int rtp_priority_ = 0;
    int max_sge_ = 16;
    UBEId ub_eid_{};
};

class UrmaJetty : public Referable {};
class UrmaJfs : public Referable {};
class UrmaJfr : public Referable {};
class UrmaJfc : public Referable {};
class UrmaSegment : public Referable {};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_WRAPPER_H
