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

#ifndef HCOM_UB_URMA_WRAPPER_CTX_H
#define HCOM_UB_URMA_WRAPPER_CTX_H
#ifdef UB_BUILD_ENABLED

#include "ub_common.h"
#include "ub_device_helper.h"

namespace ock {
namespace hcom {

extern std::atomic<uint32_t> g_jetty_id;
extern uint64_t g_connection_count;

class UBContext {
public:
    static UResult Create(const std::string &name, const UBEId &eid, UBContext *&ctx);

public:
    UBContext(const std::string &name, const UBEId &eid) : mName(name), mDevIndex(eid.devIndex),
        mEidIndex(eid.eidIndex), mBestEid(eid)
    {
        OBJ_GC_INCREASE(UBContext);
    }

    ~UBContext()
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBContext);
    }

    UResult Initialize();
    UResult UnInitialize();

    void UpdateGid(const std::string &matchIp);

    UBContext() = delete;
    UBContext(const UBContext &) = delete;
    UBContext &operator = (const UBContext &) = delete;
    UBContext(UBContext &&) = delete;
    UBContext &operator = (UBContext &&) = delete;

    std::string ToString();

    inline urma_context_t *GetContext()
    {
        return mUrmaContext;
    }

    inline uint32_t GetMaxJfs()
    {
        return mMaxJfs;
    }

    inline uint32_t GetMaxJfr()
    {
        return mMaxJfr;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

    UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::UBC;

private:
    std::string mName;
    urma_context_t *mUrmaContext = nullptr;
    urma_device_attr_t *mDevAttr = nullptr;
    uint8_t mPortNumber = 1;
    uint16_t mDevIndex = 0;
    uint16_t mEidIndex = 0;
    uint32_t mMaxJfs = 0;
    uint32_t mMaxJfr = 0;
    int mMaxSge = NN_NO16;
    UBEId mBestEid{};

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend UBJetty;
    friend UBJfc;
    friend UBMemoryRegion;
    friend UBWorker;
    friend NetDriverUB;
    friend class UBPublicJetty;
};
}
}
#endif
#endif // HCOM_UB_URMA_WRAPPER_CTX_H