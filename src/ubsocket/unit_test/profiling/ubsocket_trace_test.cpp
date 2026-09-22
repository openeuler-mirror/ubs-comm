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

#include "profiling/trace/ubsocket_trace.h"

#include <gtest/gtest.h>

#include "common/ubsocket_global_setting.h"

using namespace ock::ubs;

namespace {

void *FakeGetRpcId()
{
    return nullptr;
}

void *FakeGetRpcCallTimestamp()
{
    return nullptr;
}

} // namespace

TEST(TraceRegistryTest, RegisterRpcIdOps_NullOps_ReturnsInvalidParam)
{
    const ock::ubs::Result ret = TraceRegistry::RegisterRpcIdOps(nullptr);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
}

TEST(TraceRegistryTest, RegisterRpcIdOps_GetRpcIdNull_ReturnsInvalidParam)
{
    u_external_rpc_id_ops_t ops;
    ops.get_rpc_id = nullptr;
    ops.get_rpc_call_timestamp = FakeGetRpcCallTimestamp;
    const ock::ubs::Result ret = TraceRegistry::RegisterRpcIdOps(&ops);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
}

TEST(TraceRegistryTest, RegisterRpcIdOps_GetRpcCallTimestampNull_ReturnsInvalidParam)
{
    u_external_rpc_id_ops_t ops;
    ops.get_rpc_id = FakeGetRpcId;
    ops.get_rpc_call_timestamp = nullptr;
    const ock::ubs::Result ret = TraceRegistry::RegisterRpcIdOps(&ops);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
}

TEST(TraceRegistryTest, RegisterRpcIdOps_ValidOps_StoresAndReturnsOk)
{
    u_external_rpc_id_ops_t ops;
    ops.get_rpc_id = FakeGetRpcId;
    ops.get_rpc_call_timestamp = FakeGetRpcCallTimestamp;
    const ock::ubs::Result ret = TraceRegistry::RegisterRpcIdOps(&ops);
    EXPECT_EQ(ret, static_cast<ock::ubs::Result>(0));
    EXPECT_EQ(TraceRegistry::RPC_ID_OPS.get_rpc_id, FakeGetRpcId);
    EXPECT_EQ(TraceRegistry::RPC_ID_OPS.get_rpc_call_timestamp, FakeGetRpcCallTimestamp);
}

TEST(GlobalTracePoolTest, LazyInit_AllocatesAndIsReady)
{
    auto &pool = GlobalTracePool::Instance();
    EXPECT_TRUE(pool.LazyInit());
    EXPECT_TRUE(pool.IsReady());
}

TEST(GlobalTracePoolTest, LazyInit_Idempotent_SecondCallReturnsTrue)
{
    auto &pool = GlobalTracePool::Instance();
    EXPECT_TRUE(pool.LazyInit());
    EXPECT_TRUE(pool.LazyInit());
    EXPECT_TRUE(pool.IsReady());
}

TEST(GlobalTracePoolTest, AllocSlot_LazyInitNotCalled_ReturnsMinusOne)
{
    GlobalTracePool::Instance().DestroyPool();
    EXPECT_FALSE(GlobalTracePool::Instance().IsReady());
    int16_t idx = GlobalTracePool::Instance().AllocSlot(100, 5, PATH_TX_WRITEV);
    EXPECT_LT(idx, 0);
}

TEST(GlobalTracePoolTest, AllocSlot_AfterLazyInit_ReturnsValidSlot)
{
    auto &pool = GlobalTracePool::Instance();
    ASSERT_TRUE(pool.LazyInit());
    int16_t idx = pool.AllocSlot(100, 5, PATH_TX_WRITEV);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, GlobalTracePool::MAX_SLOTS);
    GlobalTracePool::Instance().DestroyPool();
}

TEST(GlobalTracePoolTest, MixLookupKey_DifferentFds_ProduceDifferentKeys)
{
    uint32_t key1 = GlobalTracePool::MixLookupKey(1000, 5);
    uint32_t key2 = GlobalTracePool::MixLookupKey(1000, 12);
    EXPECT_NE(key1, key2);
}

TEST(GlobalTracePoolTest, PackUnpackLookup_RoundTrip)
{
    uint64_t packed = GlobalTracePool::PackLookup(12345, 42, 7);
    uint16_t outSlot = 0xFFFF;
    EXPECT_TRUE(GlobalTracePool::UnpackLookup(packed, 12345, 42, outSlot));
    EXPECT_EQ(outSlot, 7u);
}

TEST(GlobalTracePoolTest, UnpackLookup_ZeroPacked_ReturnsFalse)
{
    uint16_t outSlot = 0xFFFF;
    EXPECT_FALSE(GlobalTracePool::UnpackLookup(0, 12345, 42, outSlot));
}

TEST(GlobalTracePoolTest, UnpackLookup_SeqNoMismatch_ReturnsFalse)
{
    uint64_t packed = GlobalTracePool::PackLookup(12345, 42, 7);
    uint16_t outSlot = 0xFFFF;
    EXPECT_FALSE(GlobalTracePool::UnpackLookup(packed, 99999, 42, outSlot));
}

TEST(GlobalTracePoolTest, UnpackLookup_FdMismatch_ReturnsFalse)
{
    uint64_t packed = GlobalTracePool::PackLookup(12345, 42, 7);
    uint16_t outSlot = 0xFFFF;
    EXPECT_FALSE(GlobalTracePool::UnpackLookup(packed, 12345, 99, outSlot));
}
