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
#include "net_mem_pool_fixed.h"
#include "service_ctx_store.h"
#include "hcom_service.h"
#include "net_service_default_imp.h"
#include "net_monotonic.h"
#include "test_service_ctx_store.h"

using namespace ock::hcom;
TestCaseCtxStore::TestCaseCtxStore() {}

void TestCaseCtxStore::SetUp() {}

void TestCaseCtxStore::TearDown() {}

TEST_F(TestCaseCtxStore, BASIC)
{
    NetMemPoolFixedPtr ctxMemPool;
    NetMemPoolFixedOptions options = {};
    options.superBlkSizeMB = NN_NO4;
    options.minBlkSize = NN_NO64;
    options.tcExpandBlkCnt = NN_NO64;
    ctxMemPool = new (std::nothrow) NetMemPoolFixed("test", options);
    ASSERT_NE(ctxMemPool.Get(), nullptr);

    auto ret = ctxMemPool->Initialize();
    ASSERT_EQ(ret, 0);

    uint32_t flatSize = NN_NO128;
    NetServiceCtxStorePtr ctxStore = new (std::nothrow) NetServiceCtxStore(flatSize, ctxMemPool);
    ASSERT_NE(ctxStore.Get(), nullptr);

    ret = ctxStore->Initialize();
    ASSERT_EQ(ret, 0);

    NetSeqNo dumpNetSeq(0);
    dumpNetSeq.SetValue(1, 7, 16777214);
    NN_LOG_INFO(dumpNetSeq.ToString());

    uint32_t seqNoFlat[NN_NO128];
    /* set flat full */
    for (uint32_t i = 0; i < flatSize - 1; i++) {
        uint32_t seqNoId = 0;
        auto result = ctxStore->PutAndGetSeqNo<uint32_t>(&seqNoFlat[i], seqNoId);
        ASSERT_EQ(result, 0);

        NetSeqNo netSeq(seqNoId);
        NN_LOG_TRACE_INFO("flag set i = " << i << ", realSeq = " << netSeq.realSeq << ", value " << &seqNoFlat[i]);
        ASSERT_EQ(netSeq.realSeq, (i + 1) % flatSize);

        ASSERT_EQ(netSeq.version, 0u);
        ASSERT_EQ(netSeq.fromFlat, 1u);
        seqNoFlat[i] = seqNoId;
    }

    uint32_t seqNoMap[NN_NO128];
    /* set map, every time seq += 3 */
    for (uint32_t i = 0; i < flatSize / 3; i++) {
        uint32_t seqNoId = 0;
        auto result = ctxStore->PutAndGetSeqNo<uint32_t>(&seqNoMap[i], seqNoId);
        ASSERT_EQ(result, 0);

        NetSeqNo netSeq(seqNoId);
        ASSERT_EQ(netSeq.realSeq / 3u, i + 1);

        ASSERT_EQ(netSeq.version, 1u);
        ASSERT_EQ(netSeq.fromFlat, 0u);
        seqNoMap[i] = seqNoId;
    }

    for (uint32_t i = 0; i < flatSize - 1; i++) {
        uint32_t *seqFlatAdd = nullptr;

        NetSeqNo logNetSeq(seqNoFlat[i]);
        NN_LOG_TRACE_INFO("flag get i = " << i << ", realSeq = " << logNetSeq.realSeq << ", value " << &seqNoFlat[i]);
        auto result = ctxStore->GetSeqNoAndRemove<uint32_t>(seqNoFlat[i], seqFlatAdd);
        ASSERT_EQ(result, 0);
        ASSERT_EQ(seqFlatAdd, &seqNoFlat[i]);
    }

    for (uint32_t i = 0; i < flatSize / 3; i++) {
        uint32_t *seqMapAdd = nullptr;
        auto result = ctxStore->GetSeqNoAndRemove<uint32_t>(seqNoMap[i], seqMapAdd);
        ASSERT_EQ(result, 0);
        ASSERT_EQ(seqMapAdd, &seqNoMap[i]);
    }
}


TEST_F(TestCaseCtxStore, PERF)
{
    NetMemPoolFixedPtr ctxMemPool;
    NetMemPoolFixedOptions options = {};
    options.superBlkSizeMB = NN_NO4;
    options.minBlkSize = NN_NO64;
    options.tcExpandBlkCnt = NN_NO256;
    ctxMemPool = new (std::nothrow) NetMemPoolFixed("test", options);
    ASSERT_NE(ctxMemPool.Get(), nullptr);

    auto ret = ctxMemPool->Initialize();
    ASSERT_EQ(ret, 0);

    uint32_t flatSize = NN_NO1048576;
    NetServiceCtxStorePtr ctxStore = new (std::nothrow) NetServiceCtxStore(flatSize, ctxMemPool);
    ASSERT_NE(ctxStore.Get(), nullptr);

    ret = ctxStore->Initialize();
    ASSERT_EQ(ret, 0);

    uint32_t seqNoFlat;
    uint64_t start = NetMonotonic::TimeUs();
    /* set flat full */
    for (uint32_t i = 0; i < flatSize - 1; i++) {
        uint32_t seqNoId = 0;
        auto result = ctxStore->PutAndGetSeqNo<uint32_t>(&seqNoFlat, seqNoId);
        ASSERT_EQ(result, 0);
    }
    NN_LOG_INFO("Put flat seq no " << flatSize << " cost " << (NetMonotonic::TimeUs() - start) << "us");
}