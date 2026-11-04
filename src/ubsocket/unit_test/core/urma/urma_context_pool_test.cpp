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
#include <gtest/gtest.h>
#include <cstdlib>

#include "urma_context_pool.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
urma_status_t MockDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}
} // namespace

class UrmaContextPoolTest : public testing::Test {
protected:
    void SetUp() override
    {
        savedDevName_ = UrmaSetting::UBS_DEV_NAME;
        savedEidIndex_ = UrmaSetting::UBS_DEV_EID_INDEX;
        savedPollerCount_ = UrmaSetting::UBS_POLLER_COUNT;
        savedCqDepth_ = UrmaSetting::UBS_POLLER_CQ_DEPTH;
        savedMode_ = UrmaSetting::UBS_EPOLL_MODE;
        UrmaSetting::UBS_DEV_NAME = "configured";
        UrmaDevice::ALL_DEVICES.clear();
        UrmaDevice::LOADED = true;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;

        device_ = MakeRef<UrmaDevice>("udmac0", "", urma_device_attr_t{});
        urma_eid_info_t eid1{};
        eid1.eid_index = 1;
        urma_eid_info_t eid2{};
        eid2.eid_index = 2;
        device_->eid_list_ = {eid1, eid2};
        context1_ = MakeRef<UrmaContext>(&raw_contexts_[0], device_, eid1);
        context2_ = MakeRef<UrmaContext>(&raw_contexts_[1], device_, eid2);
        context1_->context_id_ = 1;
        context2_->context_id_ = 2;
    }

    void TearDown() override
    {
        UrmaDevice::ALL_DEVICES.clear();
        context2_ = nullptr;
        context1_ = nullptr;
        device_ = nullptr;
        UrmaSetting::UBS_DEV_NAME = savedDevName_;
        UrmaSetting::UBS_DEV_EID_INDEX = savedEidIndex_;
        UrmaSetting::UBS_POLLER_COUNT = savedPollerCount_;
        UrmaSetting::UBS_POLLER_CQ_DEPTH = savedCqDepth_;
        UrmaSetting::UBS_EPOLL_MODE = savedMode_;
    }

    urma_context_t raw_contexts_[2]{};
    UrmaDevicePtr device_{nullptr};
    UrmaContextPtr context1_{nullptr};
    UrmaContextPtr context2_{nullptr};
    std::string savedDevName_;
    uint32_t savedEidIndex_ = 0;
    uint16_t savedPollerCount_ = 0;
    uint32_t savedCqDepth_ = 0;
    EpollHandleMode savedMode_ = THREAD_PER_CONNECTION;
};

TEST_F(UrmaContextPoolTest, AddGetClearAndLifecycle)
{
    UrmaContextPool pool;
    EXPECT_EQ(UBS_OK, pool.AddEidIndex("dev", 1));
    EXPECT_EQ(UBS_ERROR, pool.AddEidIndex("dev", 1));
    EXPECT_EQ(1u, pool.Contexts().size());
    pool.ClearEidIndex();
    EXPECT_TRUE(pool.Contexts().empty());

    pool.contexts_[{"dev", 1}] = context1_;
    EXPECT_EQ(UBS_OK, pool.Init());
    EXPECT_EQ(UBS_OK, pool.Init());
    EXPECT_EQ(UBS_ERROR, pool.AddEidIndex("dev", 2));
    UrmaContextPtr found;
    EXPECT_EQ(UBS_OK, pool.GetContext("dev", 1, found));
    EXPECT_EQ(context1_.Get(), found.Get());
    EXPECT_EQ(UBS_ERROR, pool.GetContext("missing", 1, found));
    pool.UnInit();
    pool.UnInit();
    EXPECT_EQ(UBS_ERROR, pool.GetContext("dev", 1, found));
}

TEST_F(UrmaContextPoolTest, AutoAddEidIndexFiltersDeviceKinds)
{
    UrmaContextPool pool;
    UrmaDevice::ALL_DEVICES.clear();
    EXPECT_EQ(UBS_ERROR, pool.AutoAddEidIndex());

    UrmaDevice::ALL_DEVICES["udmac0"] = device_;
    auto bonding = MakeRef<UrmaDevice>("bonding0", "", urma_device_attr_t{});
    bonding->eid_list_ = device_->eid_list_;
    UrmaDevice::ALL_DEVICES["bonding0"] = bonding;
    EXPECT_EQ(UBS_OK, pool.AutoAddEidIndex(true));
    EXPECT_EQ(2u, pool.Contexts().size());
    pool.ClearEidIndex();
    EXPECT_EQ(UBS_OK, pool.AutoAddEidIndex(false));
    EXPECT_EQ(2u, pool.Contexts().size());
    EXPECT_EQ(UBS_ERROR, pool.AutoAddEidIndex(false));
}

TEST_F(UrmaContextPoolTest, InitSortedContextsPrioritizesAffinityAndSkipsBadPorts)
{
    UrmaContextPool pool;
    pool.my_chip_id_ = 0;
    pool.contexts_[{"other0", 1}] = context2_;
    pool.contexts_[{"udmac0", 2}] = context1_;
    pool.InitSortedContexts();
    ASSERT_EQ(2u, pool.SortedContexts().size());
    EXPECT_EQ(context1_.Get(), pool.SortedContexts()[0].Get());
    EXPECT_TRUE(context1_->IsAffinity());
    EXPECT_FALSE(context2_->IsAffinity());

    context1_->RecordPortError(1);
    pool.InitSortedContexts();
    ASSERT_EQ(1u, pool.SortedContexts().size());
    EXPECT_EQ(context2_.Get(), pool.SortedContexts()[0].Get());
}

TEST_F(UrmaContextPoolTest, SelectContextHandlesHealthPolicyAndIndex)
{
    UrmaContextPool pool;
    context1_->SetAffinity(true);
    context2_->SetAffinity(false);
    pool.sorted_contexts_ = {context1_, context2_};
    UrmaContextHealthEntry health[2]{};
    health[0].eid_index = 1;
    health[1].eid_index = 2;
    int32_t selected = -1;
    EXPECT_EQ(context1_.Get(), pool.SelectContext(PSP_ROUND_ROBIN, &selected, health, 2).Get());
    EXPECT_EQ(0, selected);

    health[0].is_bad = true;
    EXPECT_EQ(context2_.Get(), pool.SelectContext(PSP_RANDOM, &selected, health, 2).Get());
    EXPECT_EQ(1, selected);
    health[1].is_bad = true;
    EXPECT_EQ(nullptr, pool.SelectContext(PSP_ROUND_ROBIN, &selected, health, 2).Get());
}

TEST_F(UrmaContextPoolTest, SelectContextRoundRobinAndInvalidPolicy)
{
    UrmaContextPool pool;
    context1_->SetAffinity(true);
    context2_->SetAffinity(true);
    pool.sorted_contexts_ = {context1_, context2_};
    UrmaContextHealthEntry health[2]{};
    int32_t selected = -1;
    EXPECT_EQ(context1_.Get(), pool.SelectContext(PSP_ROUND_ROBIN, &selected, health, 2).Get());
    EXPECT_EQ(context2_.Get(), pool.SelectContext(PSP_ROUND_ROBIN, &selected, health, 2).Get());
    EXPECT_EQ(nullptr, pool.SelectContext(PSP_COUNT, &selected, health, 2).Get());
}

TEST_F(UrmaContextPoolTest, SettingLoadEnvAppliesSupportedUrmaOptions)
{
    const char *names[] = {"UBS_DEV_NAME", "UBS_DEV_EID_INDEX", "UBS_POLLER_COUNT", "UBS_POLLER_CQ_DEPTH",
                           "UBS_EPOLL_MODE"};
    const char *values[] = {"ut_dev", "17", "3", "8192", "1"};
    std::string saved[5];
    bool existed[5]{};
    for (size_t i = 0; i < 5; ++i) {
        const char *value = getenv(names[i]);
        existed[i] = value != nullptr;
        saved[i] = value == nullptr ? "" : value;
        ASSERT_EQ(0, setenv(names[i], values[i], 1));
    }

    UrmaSetting::AddRules();
    EXPECT_EQ(UBS_OK, UrmaSetting::LoadEnv());
    EXPECT_EQ("ut_dev", UrmaSetting::UBS_DEV_NAME);
    EXPECT_EQ(17u, UrmaSetting::UBS_DEV_EID_INDEX);
    EXPECT_EQ(3u, UrmaSetting::UBS_POLLER_COUNT);
    EXPECT_EQ(8192u, UrmaSetting::UBS_POLLER_CQ_DEPTH);
    EXPECT_EQ(DISPATCHER_THREAD_POOL, UrmaSetting::UBS_EPOLL_MODE);

    for (size_t i = 0; i < 5; ++i) {
        if (existed[i]) {
            setenv(names[i], saved[i].c_str(), 1);
        } else {
            unsetenv(names[i]);
        }
    }
}
