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
#include <mockcpp/mockcpp.hpp>

#include <sstream>
#include <vector>

#include "urma_data_rx_ops.h"
#include "urma_socket.h"
#include "urma_wrapper.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
urma_jfce_t g_jfce{};
urma_jfc_t g_jfc{};
urma_jfs_t g_jfs{};
urma_jfr_t g_jfr{};
urma_jetty_t g_jetty{};
urma_target_jetty_t g_targetJetty{};
urma_target_seg_t g_targetSeg{};
urma_device_t g_rawDevice{};
urma_device_t *g_deviceList[2]{};
urma_eid_info_t g_eidList[2]{};
urma_context_t g_createdContext{};
bool g_failCreate = false;
bool g_failRearm = false;
bool g_failImport = false;
bool g_failBind = false;
bool g_failPost = false;
uint32_t g_deleteCount = 0;
uint32_t g_postCount = 0;
urma_jfs_wr_t g_postedWr{};
bool g_hasPostedWr = false;
int g_deviceCount = 1;
bool g_nullDeviceList = false;
bool g_failQueryDevice = false;
bool g_nullEidList = false;
bool g_nullNamedDevice = false;
bool g_nullContext = false;
bool g_failDelete = false;

urma_device_t **MockGetDeviceList(int *count)
{
    *count = g_deviceCount;
    return g_nullDeviceList ? nullptr : g_deviceList;
}

void MockFreeDeviceList(urma_device_t **) {}

urma_status_t MockQueryDevice(urma_device_t *, urma_device_attr_t *attr)
{
    *attr = urma_device_attr_t{};
    return g_failQueryDevice ? URMA_FAIL : URMA_SUCCESS;
}

urma_eid_info_t *MockGetEidList(urma_device_t *, uint32_t *count)
{
    *count = g_nullEidList ? 0 : 2;
    return g_nullEidList ? nullptr : g_eidList;
}

void MockFreeEidList(urma_eid_info_t *) {}

urma_device_t *MockGetDeviceByName(char *)
{
    return g_nullNamedDevice ? nullptr : &g_rawDevice;
}

urma_context_t *MockCreateContext(urma_device_t *, uint32_t)
{
    return g_nullContext ? nullptr : &g_createdContext;
}

urma_jfce_t *MockCreateJfce(urma_context_t *)
{
    return g_failCreate ? nullptr : &g_jfce;
}

urma_jfc_t *MockCreateJfc(urma_context_t *, urma_jfc_cfg_t *)
{
    return g_failCreate ? nullptr : &g_jfc;
}

urma_jfs_t *MockCreateJfs(urma_context_t *, urma_jfs_cfg_t *cfg)
{
    g_jfs.jfs_cfg = *cfg;
    return g_failCreate ? nullptr : &g_jfs;
}

urma_jfr_t *MockCreateJfr(urma_context_t *, urma_jfr_cfg_t *cfg)
{
    g_jfr.jfr_cfg = *cfg;
    return g_failCreate ? nullptr : &g_jfr;
}

urma_jetty_t *MockCreateJetty(urma_context_t *, urma_jetty_cfg_t *cfg)
{
    g_jetty.jetty_cfg = *cfg;
    g_jetty.jetty_id.id = 88;
    return g_failCreate ? nullptr : &g_jetty;
}

urma_status_t CountDelete()
{
    ++g_deleteCount;
    return g_failDelete ? URMA_FAIL : URMA_SUCCESS;
}

urma_status_t MockDeleteJfce(urma_jfce_t *)
{
    return CountDelete();
}

urma_status_t MockDeleteJfc(urma_jfc_t *)
{
    return CountDelete();
}

urma_status_t MockDeleteJfs(urma_jfs_t *)
{
    return CountDelete();
}

urma_status_t MockDeleteJfr(urma_jfr_t *)
{
    return CountDelete();
}

urma_status_t MockDeleteJetty(urma_jetty_t *)
{
    return CountDelete();
}

urma_status_t MockDeleteContext(urma_context_t *)
{
    return CountDelete();
}

urma_status_t MockUnimportJetty(urma_target_jetty_t *)
{
    return CountDelete();
}

urma_status_t MockUnbindJetty(urma_jetty_t *)
{
    return CountDelete();
}

urma_status_t MockRearm(urma_jfc_t *, bool)
{
    return g_failRearm ? URMA_FAIL : URMA_SUCCESS;
}

int MockPoll(urma_jfc_t *, int, urma_cr_t *)
{
    return 0;
}

int MockWait(urma_jfce_t *, uint32_t, int, urma_jfc_t *[])
{
    return 0;
}

void MockAck(urma_jfc_t *[], uint32_t[], uint32_t) {}

urma_target_jetty_t *MockImport(urma_context_t *, urma_rjetty_t *, urma_token_t *)
{
    return g_failImport ? nullptr : &g_targetJetty;
}

urma_status_t MockBind(urma_jetty_t *, urma_target_jetty_t *)
{
    return g_failBind ? URMA_FAIL : URMA_SUCCESS;
}

urma_status_t MockPostSend(urma_jetty_t *, urma_jfs_wr_t *wr, urma_jfs_wr_t **)
{
    ++g_postCount;
    g_postedWr = *wr;
    g_hasPostedWr = true;
    return g_failPost ? URMA_FAIL : URMA_SUCCESS;
}

urma_status_t MockPostRecv(urma_jetty_t *, urma_jfr_wr_t *, urma_jfr_wr_t **)
{
    ++g_postCount;
    return URMA_SUCCESS;
}
} // namespace

class UrmaWrapperTest : public testing::Test {
protected:
    void SetUp() override
    {
        savedApiLoaded_ = UrmaApi::LOADED;
        UrmaApi::LOADED = true;
        g_failCreate = false;
        g_failRearm = false;
        g_failImport = false;
        g_failBind = false;
        g_failPost = false;
        g_deleteCount = 0;
        g_postCount = 0;
        g_postedWr = urma_jfs_wr_t{};
        g_hasPostedWr = false;
        g_deviceCount = 1;
        g_nullDeviceList = false;
        g_failQueryDevice = false;
        g_nullEidList = false;
        g_nullNamedDevice = false;
        g_nullContext = false;
        g_failDelete = false;
        std::strncpy(g_rawDevice.name, "ut_dev0", sizeof(g_rawDevice.name) - 1);
        std::strncpy(g_rawDevice.path, "/sys/ut", sizeof(g_rawDevice.path) - 1);
        g_deviceList[0] = &g_rawDevice;
        g_deviceList[1] = nullptr;
        g_eidList[0] = urma_eid_info_t{};
        g_eidList[0].eid_index = 3;
        g_eidList[1] = urma_eid_info_t{};
        g_eidList[1].eid_index = 4;
        g_jfce.fd = 37;
        g_jfs.jfs_cfg = urma_jfs_cfg_t{};
        g_jfr.jfr_cfg = urma_jfr_cfg_t{};
        g_jetty.jetty_cfg = urma_jetty_cfg_t{};
        g_jetty.jetty_id = urma_jetty_id_t{};

        UrmaApi::urma_create_jfce_ptr = MockCreateJfce;
        UrmaApi::urma_create_jfc_ptr = MockCreateJfc;
        UrmaApi::urma_create_jfs_ptr = MockCreateJfs;
        UrmaApi::urma_create_jfr_ptr = MockCreateJfr;
        UrmaApi::urma_create_jetty_ptr = MockCreateJetty;
        UrmaApi::urma_delete_jfce_ptr = MockDeleteJfce;
        UrmaApi::urma_delete_jfc_ptr = MockDeleteJfc;
        UrmaApi::urma_delete_jfs_ptr = MockDeleteJfs;
        UrmaApi::urma_delete_jfr_ptr = MockDeleteJfr;
        UrmaApi::urma_delete_jetty_ptr = MockDeleteJetty;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;
        UrmaApi::urma_rearm_jfc_ptr = MockRearm;
        UrmaApi::urma_poll_jfc_ptr = MockPoll;
        UrmaApi::urma_wait_jfc_ptr = MockWait;
        UrmaApi::urma_ack_jfc_ptr = MockAck;
        UrmaApi::urma_import_jetty_ptr = MockImport;
        UrmaApi::urma_unimport_jetty_ptr = MockUnimportJetty;
        UrmaApi::urma_bind_jetty_ptr = MockBind;
        UrmaApi::urma_unbind_jetty_ptr = MockUnbindJetty;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSend;
        UrmaApi::urma_post_jetty_recv_wr_ptr = MockPostRecv;
        UrmaApi::urma_get_device_list_ptr = MockGetDeviceList;
        UrmaApi::urma_free_device_list_ptr = MockFreeDeviceList;
        UrmaApi::urma_query_device_ptr = MockQueryDevice;
        UrmaApi::urma_get_eid_list_ptr = MockGetEidList;
        UrmaApi::urma_free_eid_list_ptr = MockFreeEidList;
        UrmaApi::urma_get_device_by_name_ptr = MockGetDeviceByName;
        UrmaApi::urma_create_context_ptr = MockCreateContext;

        urma_device_attr_t attr{};
        attr.dev_cap.max_jfs_sge = 4;
        attr.dev_cap.max_jfs_rsge = 5;
        attr.dev_cap.max_jfs_inline_len = 64;
        attr.dev_cap.max_jfr_sge = 6;
        device_ = MakeRef<UrmaDevice>("ut_dev", "/sys/ut", attr);
        context_ = MakeRef<UrmaContext>(&raw_context_, device_, urma_eid_info_t{});
        context_->context_id_ = 9;
    }

    void TearDown() override
    {
        UrmaContext::ALL_CONTEXTS.clear();
        UrmaDevice::ALL_DEVICES.clear();
        UrmaDevice::LOADED = false;
        context_ = nullptr;
        device_ = nullptr;
        GlobalMockObject::verify();
        UrmaApi::LOADED = savedApiLoaded_;
    }

    UrmaJfcePtr MakeJfce()
    {
        return MakeRef<UrmaJfce>(&g_jfce, context_);
    }

    UrmaJfcPtr MakeJfc()
    {
        return MakeRef<UrmaJfc>(EVENT_POLLING, &g_jfc, &g_jfce, context_);
    }

    urma_context_t raw_context_{};
    UrmaDevicePtr device_{nullptr};
    UrmaContextPtr context_{nullptr};
    bool savedApiLoaded_{false};
};

TEST_F(UrmaWrapperTest, Device_FormatsAttributesAndEnumValues)
{
    EXPECT_EQ("down", UrmaDevice::DeviceState2Str(URMA_PORT_DOWN));
    EXPECT_EQ("active", UrmaDevice::DeviceState2Str(URMA_PORT_ACTIVE));
    EXPECT_EQ("unknown", UrmaDevice::DeviceState2Str(URMA_PORT_INIT));
    EXPECT_EQ("256bytes", UrmaDevice::DeviceMTU2Str(URMA_MTU_256));
    EXPECT_EQ("8192bytes", UrmaDevice::DeviceMTU2Str(URMA_MTU_8192));

    device_->attributes_.port_cnt = 1;
    device_->attributes_.port_attr[0].active_speed = URMA_SP_100G;
    device_->attributes_.port_attr[0].state = URMA_PORT_ACTIVE;
    device_->attributes_.port_attr[0].active_mtu = URMA_MTU_4096;
    device_->eid_list_.push_back(urma_eid_info_t{});
    auto text = device_->ToString(true, ">", "\n");
    EXPECT_NE(std::string::npos, text.find("100Gb"));
    EXPECT_NE(std::string::npos, text.find("4096bytes"));
    EXPECT_NE(std::string::npos, text.find("eid count:"));

    device_->attributes_.port_attr[0].active_speed = static_cast<urma_speed_t>(255);
    EXPECT_NE(std::string::npos, device_->ToString().find("bandwidth: invalid"));
}

TEST_F(UrmaWrapperTest, Device_InitHandlesEmptyInvalidAndValidDevices)
{
    UrmaDevice::ALL_DEVICES.clear();
    UrmaDevice::LOADED = false;
    g_nullDeviceList = true;
    UrmaDevice::Init();
    EXPECT_FALSE(UrmaDevice::LOADED);

    g_nullDeviceList = false;
    g_deviceCount = 2;
    g_deviceList[0] = nullptr;
    g_deviceList[1] = &g_rawDevice;
    g_failQueryDevice = true;
    UrmaDevice::Init();
    EXPECT_TRUE(UrmaDevice::ALL_DEVICES.empty());
    EXPECT_TRUE(UrmaDevice::LOADED);

    UrmaDevice::LOADED = false;
    g_failQueryDevice = false;
    g_nullEidList = true;
    UrmaDevice::Init();
    ASSERT_EQ(1u, UrmaDevice::ALL_DEVICES.size());
    EXPECT_TRUE(UrmaDevice::ALL_DEVICES.begin()->second->EidList().empty());

    UrmaDevice::ALL_DEVICES.clear();
    UrmaDevice::LOADED = false;
    g_nullEidList = false;
    UrmaDevice::Init();
    ASSERT_EQ(1u, UrmaDevice::ALL_DEVICES.size());
    EXPECT_EQ(2u, UrmaDevice::ALL_DEVICES.begin()->second->EidList().size());
    UrmaDevice::Init();
}

TEST_F(UrmaWrapperTest, Context_CreateContextCoversDriverFailuresAndCache)
{
    UrmaContextPtr out;
    EXPECT_EQ(UBS_INVALID_PARAM, UrmaContext::CreateContext("", 3, out));
    UrmaDevice::ALL_DEVICES.clear();
    UrmaDevice::ALL_DEVICES["ut_dev0"] = device_;
    UrmaDevice::LOADED = true;
    EXPECT_EQ(UBS_INVALID_PARAM, UrmaContext::CreateContext("missing", 3, out));

    g_nullNamedDevice = true;
    EXPECT_EQ(UBS_UB_DEV_ERROR, UrmaContext::CreateContext("ut_dev", 3, out));
    g_nullNamedDevice = false;
    g_nullContext = true;
    EXPECT_EQ(UBS_UB_DEV_ERROR, UrmaContext::CreateContext("ut_dev", 3, out));
    g_nullContext = false;
    g_nullEidList = true;
    EXPECT_EQ(UBS_UB_DEV_ERROR, UrmaContext::CreateContext("ut_dev", 3, out));
    g_nullEidList = false;
    EXPECT_EQ(UBS_OK, UrmaContext::CreateContext("ut_dev", 3, out));
    ASSERT_NE(nullptr, out.Get());
    EXPECT_EQ(3u, out->EidInfo().eid_index);
    UrmaContextPtr cached;
    EXPECT_EQ(UBS_OK, UrmaContext::CreateContext("ut_dev", 3, cached));
    EXPECT_EQ(out.Get(), cached.Get());
    EXPECT_EQ(UBS_ERROR, UrmaContext::LookupContext("ut_dev0", 99, cached));
}

TEST_F(UrmaWrapperTest, Context_CreatesConfigurationObjects)
{
    auto jfc = context_->CreateJfcCfg(32, 44);
    EXPECT_EQ(32u, jfc.depth);
    EXPECT_EQ(44u, jfc.user_ctx);
    EXPECT_EQ(1u, jfc.flag.bs.lock_free);

    auto jfs = context_->CreateJfsCfg(64, URMA_TM_RC, 45, 3);
    EXPECT_EQ(64u, jfs.depth);
    EXPECT_EQ(4u, jfs.max_sge);
    EXPECT_EQ(5u, jfs.max_rsge);
    EXPECT_EQ(3u, jfs.priority);
    auto jfr = context_->CreateJfrCfg(65, URMA_TM_RC, 123, 46);
    EXPECT_EQ(6u, jfr.max_sge);
    EXPECT_EQ(URMA_TOKEN_POLICY_PLAIN_TEXT, jfr.flag.bs.token_policy);
    EXPECT_EQ(123u, jfr.token_value.token);
    auto jetty = context_->CreateJettyCfg(47);
    EXPECT_EQ(1u, jetty.flag.bs.share_jfr);
    EXPECT_EQ(47u, jetty.user_ctx);
}

TEST_F(UrmaWrapperTest, Context_TracksIdentityPortHealthAndIds)
{
    EXPECT_EQ(UBS_OK, context_->InitLocalIdentifyNodeInfo());
    EXPECT_EQ(static_cast<uint32_t>(getpid()), context_->GetLocalIdentifyNode().pid);
    EXPECT_FALSE(context_->IsBadPort());
    context_->RecordPortError(5);
    EXPECT_TRUE(context_->IsBadPort());
    auto first = UrmaContext::NewJettyId();
    EXPECT_EQ(first + 1, UrmaContext::NewJettyId());
    context_->SetAffinity(true);
    EXPECT_TRUE(context_->IsAffinity());
}

TEST_F(UrmaWrapperTest, Context_CreateJfceCoversValidationFailureAndSuccess)
{
    UrmaJfcePtr jfce;
    context_->raw_context_ = nullptr;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfce(jfce));
    context_->raw_context_ = &raw_context_;
    g_failCreate = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfce(jfce));
    g_failCreate = false;
    ASSERT_EQ(UBS_OK, context_->CreateJfce(jfce));
    EXPECT_EQ(&g_jfce, jfce->GetRawJfce());
    EXPECT_EQ(37, jfce->Fd());
    jfce->Destroy();
    EXPECT_EQ(nullptr, jfce->GetRawJfce());
    EXPECT_EQ(-1, jfce->Fd());
}

TEST_F(UrmaWrapperTest, Context_CreateJfcCoversEventFailuresAndSuccess)
{
    urma_jfc_cfg_t cfg{};
    UrmaJfcPtr jfc;
    g_failCreate = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfc(cfg, EVENT_POLLING, jfc));
    g_failCreate = false;
    g_failRearm = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfc(cfg, EVENT_POLLING, jfc));
    g_failRearm = false;
    cfg.jfce = nullptr;
    ASSERT_EQ(UBS_OK, context_->CreateJfc(cfg, EVENT_POLLING, jfc));
    EXPECT_EQ(&g_jfc, jfc->Handle());
    EXPECT_EQ(37, jfc->GetInterruptFd());
    EXPECT_EQ(EVENT_POLLING, jfc->PollingType());
    jfc->Destroy();
    EXPECT_EQ(nullptr, jfc->Handle());
}

TEST_F(UrmaWrapperTest, Context_CreateJfcWithJfceValidatesAndCreates)
{
    urma_jfc_cfg_t cfg{};
    UrmaJfcPtr jfc;
    UrmaJfcePtr jfce;
    EXPECT_EQ(UBS_INVALID_PARAM, context_->CreateJfc(cfg, jfce, jfc));
    jfce = MakeJfce();
    g_failCreate = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfc(cfg, jfce, jfc));
    g_failCreate = false;
    ASSERT_EQ(UBS_OK, context_->CreateJfc(cfg, jfce, jfc));
    EXPECT_EQ(jfce->GetRawJfce(), jfc->JfceHandle());
}

TEST_F(UrmaWrapperTest, Context_CreateJfsAndJfrValidateAndCreate)
{
    urma_jfs_cfg_t jfsCfg{};
    urma_jfr_cfg_t jfrCfg{};
    UrmaJfsPtr jfs;
    UrmaJfrPtr jfr;
    UrmaJfcPtr empty;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfs(jfsCfg, empty, jfs));
    EXPECT_EQ(UBS_ERROR, context_->CreateJfr(jfrCfg, empty, jfr));
    auto jfc = MakeJfc();
    g_failCreate = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJfs(jfsCfg, jfc, jfs));
    EXPECT_EQ(UBS_ERROR, context_->CreateJfr(jfrCfg, jfc, jfr));
    g_failCreate = false;
    jfrCfg.token_value.token = 321;
    ASSERT_EQ(UBS_OK, context_->CreateJfs(jfsCfg, jfc, jfs));
    ASSERT_EQ(UBS_OK, context_->CreateJfr(jfrCfg, jfc, jfr));
    EXPECT_EQ(&g_jfs, jfs->Handle());
    EXPECT_EQ(&g_jfr, jfr->Handle());
    EXPECT_EQ(321u, jfr->Token());
}

TEST_F(UrmaWrapperTest, Context_CreateJettyValidatesAndCreates)
{
    auto jfc = MakeJfc();
    auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
    auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
    urma_jetty_cfg_t cfg{};
    UrmaJettyPtr jetty;
    UrmaJfsPtr emptyJfs;
    EXPECT_EQ(UBS_ERROR, context_->CreateJetty(cfg, URMA_UTP, emptyJfs, jfr, jetty));
    UrmaJfrPtr emptyJfr;
    EXPECT_EQ(UBS_ERROR, context_->CreateJetty(cfg, URMA_UTP, jfs, emptyJfr, jetty));
    g_failCreate = true;
    EXPECT_EQ(UBS_ERROR, context_->CreateJetty(cfg, URMA_UTP, jfs, jfr, jetty));
    g_failCreate = false;
    ASSERT_EQ(UBS_OK, context_->CreateJetty(cfg, URMA_UTP, jfs, jfr, jetty));
    EXPECT_EQ(88u, jetty->JettyId());
    EXPECT_EQ(11u, jetty->Token());
    EXPECT_EQ(&g_jetty, jetty->Handle());
}

TEST_F(UrmaWrapperTest, ContextNullHandleRejectsFactoriesAndConfigBuilders)
{
    auto *saved = context_->raw_context_;
    context_->raw_context_ = nullptr;
    UrmaJfcPtr jfc;
    UrmaJfcePtr jfce = MakeJfce();
    UrmaJfsPtr jfs;
    UrmaJfrPtr jfr;
    UrmaJettyPtr jetty;
    urma_jfc_cfg_t jfcCfg{};
    urma_jfs_cfg_t jfsCfg{};
    urma_jfr_cfg_t jfrCfg{};
    urma_jetty_cfg_t jettyCfg{};

    EXPECT_EQ(UBS_ERROR, context_->CreateJfc(jfcCfg, BUSY_POLLING, jfc));
    EXPECT_EQ(UBS_ERROR, context_->CreateJfc(jfcCfg, jfce, jfc));
    EXPECT_EQ(UBS_ERROR, context_->CreateJfs(jfsCfg, jfc, jfs));
    EXPECT_EQ(UBS_ERROR, context_->CreateJfr(jfrCfg, jfc, jfr));
    EXPECT_EQ(UBS_ERROR, context_->CreateJetty(jettyCfg, URMA_UTP, jfs, jfr, jetty));
    EXPECT_EQ(0U, context_->CreateJfsCfg(8, URMA_TM_RM).depth);
    EXPECT_EQ(0U, context_->CreateJfrCfg(8, URMA_TM_RM).depth);
    context_->raw_context_ = saved;

    auto nullJfce = MakeRef<UrmaJfce>(nullptr, context_);
    EXPECT_EQ(UBS_ERROR, nullJfce->SetNonBlocking());
}

TEST_F(UrmaWrapperTest, Context_CreateJfsJfrJettyRejectsNullJfc)
{
    UrmaJfsPtr jfs;
    UrmaJfrPtr jfr;
    UrmaJettyPtr jetty;
    MOCKER_CPP(&UrmaApi::urma_create_jfs).expects(never());
    EXPECT_EQ(UBS_INVALID_PARAM, context_->CreateJfsJfrJetty(nullptr, 16, 16, URMA_TM_RM, URMA_CTP, jfs, jfr, jetty));
}

TEST_F(UrmaWrapperTest, Context_CreateJfsJfrJettyStopsAtFailedStage)
{
    auto jfc = MakeJfc();
    for (int stage = 0; stage < 3; ++stage) {
        SCOPED_TRACE(stage);
        UrmaJfsPtr jfs;
        UrmaJfrPtr jfr;
        UrmaJettyPtr jetty;
        if (stage == 0) {
            MOCKER_CPP(&UrmaApi::urma_create_jfs).expects(once()).will(returnValue(static_cast<urma_jfs_t *>(nullptr)));
            MOCKER_CPP(&UrmaApi::urma_create_jfr).expects(never());
            MOCKER_CPP(&UrmaApi::urma_create_jetty).expects(never());
        } else if (stage == 1) {
            MOCKER_CPP(&UrmaApi::urma_create_jfr).expects(once()).will(returnValue(static_cast<urma_jfr_t *>(nullptr)));
            MOCKER_CPP(&UrmaApi::urma_create_jetty).expects(never());
        } else {
            MOCKER_CPP(&UrmaApi::urma_create_jetty)
                .expects(once())
                .will(returnValue(static_cast<urma_jetty_t *>(nullptr)));
        }
        EXPECT_EQ(UBS_ERROR, context_->CreateJfsJfrJetty(jfc, 16, 16, URMA_TM_RM, URMA_CTP, jfs, jfr, jetty));
        EXPECT_EQ(stage > 0, jfs != nullptr);
        EXPECT_EQ(stage > 1, jfr != nullptr);
        EXPECT_EQ(nullptr, jetty.Get());
        GlobalMockObject::verify();
    }
}

TEST_F(UrmaWrapperTest, Context_CreateJfsJfrJettyPopulatesResources)
{
    auto jfc = MakeJfc();
    UrmaJfsPtr jfs;
    UrmaJfrPtr jfr;
    UrmaJettyPtr jetty;
    ASSERT_EQ(UBS_OK, context_->CreateJfsJfrJetty(jfc, 16, 16, URMA_TM_RM, URMA_CTP, jfs, jfr, jetty));
    ASSERT_NE(nullptr, jfs.Get());
    ASSERT_NE(nullptr, jfr.Get());
    ASSERT_NE(nullptr, jetty.Get());
    EXPECT_EQ(&g_jfs, jfs->Handle());
    EXPECT_EQ(&g_jfr, jfr->Handle());
    EXPECT_EQ(&g_jetty, jetty->Handle());
    EXPECT_EQ(16U, g_jfs.jfs_cfg.depth);
    EXPECT_EQ(16U, g_jfr.jfr_cfg.depth);
    EXPECT_EQ(URMA_TM_RM, g_jetty.jetty_cfg.jfs_cfg.trans_mode);
}

TEST_F(UrmaWrapperTest, DestroyWrappersToleratesEveryDriverDeleteFailure)
{
    g_failDelete = true;
    g_jetty.jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RC;
    {
        auto jfce = MakeRef<UrmaJfce>(&g_jfce, context_);
        auto jfc = MakeRef<UrmaJfc>(EVENT_POLLING, &g_jfc, &g_jfce, context_);
        auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
        auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
        auto jetty = MakeRef<UrmaJetty>(URMA_UTP, 1, &g_jetty, context_, jfs, jfr);
        jetty->raw_target_jetty_ = &g_targetJetty;
    }
    EXPECT_GE(g_deleteCount, 7U);
}

TEST_F(UrmaWrapperTest, DestroyWrappersSkipDriverCallsWhenApiIsUnloaded)
{
    auto jfce = MakeJfce();
    auto jfc = MakeJfc();
    auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
    auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
    auto jetty = MakeRef<UrmaJetty>(URMA_UTP, 1, &g_jetty, context_, jfs, jfr);
    jetty->raw_target_jetty_ = &g_targetJetty;
    UrmaApi::LOADED = false;

    jetty->Destroy();
    jfr->Destroy();
    jfs->Destroy();
    jfc->Destroy();
    jfce->Destroy();
    EXPECT_EQ(0U, g_deleteCount);
    EXPECT_EQ(nullptr, jetty->Handle());
    EXPECT_EQ(nullptr, jfr->Handle());
    EXPECT_EQ(nullptr, jfs->Handle());
    EXPECT_EQ(nullptr, jfc->Handle());
    EXPECT_EQ(nullptr, jfce->GetRawJfce());
    UrmaApi::LOADED = true;
}

TEST_F(UrmaWrapperTest, Jetty_ImportsNonRcAndRcTargets)
{
    auto jfc = MakeJfc();
    auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
    auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
    UrmaJetty jetty(URMA_UTP, 1, &g_jetty, context_, jfs, jfr);
    urma_jetty_id_t id{};
    uint32_t token = 9;
    g_failImport = true;
    EXPECT_EQ(UBS_ERROR, jetty.ImportRemoteJetty(id, token));
    g_failImport = false;
    g_jetty.jetty_cfg.jfs_cfg.trans_mode = URMA_TM_UM;
    EXPECT_EQ(UBS_OK, jetty.ImportRemoteJetty(id, token));
    jetty.raw_target_jetty_ = nullptr;
    g_jetty.jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RC;
    g_failBind = true;
    EXPECT_EQ(UBS_ERROR, jetty.ImportRemoteJetty(id, token));
    g_failBind = false;
    EXPECT_EQ(UBS_OK, jetty.ImportRemoteJetty(id, token));
}

TEST_F(UrmaWrapperTest, Jetty_PostOperationsPopulateRequests)
{
    auto jfc = MakeJfc();
    auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
    auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
    UrmaJetty jetty(URMA_UTP, 1, &g_jetty, context_, jfs, jfr);
    jetty.raw_target_jetty_ = &g_targetJetty;
    urma_jfs_wr_t first{};
    urma_jfs_wr_t second{};
    first.next = &second;
    urma_jfs_wr_t *badSend = nullptr;
    EXPECT_EQ(URMA_SUCCESS, jetty.PostSendWr(&first, &badSend));
    EXPECT_EQ(&g_targetJetty, first.tjetty);
    EXPECT_EQ(&g_targetJetty, second.tjetty);
    urma_jfr_wr_t recv{};
    urma_jfr_wr_t *badRecv = nullptr;
    EXPECT_EQ(URMA_SUCCESS, jetty.PostRecvWr(&recv, &badRecv));
    urma_rw_wr_t rw{};
    EXPECT_EQ(URMA_SUCCESS, jetty.PostWriteImm(rw, 77));
    ASSERT_TRUE(g_hasPostedWr);
    EXPECT_EQ(URMA_OPC_WRITE_IMM, g_postedWr.opcode);
    EXPECT_EQ(3u, g_postCount);
}

TEST_F(UrmaWrapperTest, StreamOperatorsIncludeKeyFields)
{
    urma_eid_t eid{};
    urma_jetty_id_t id{};
    id.id = 12;
    urma_jfc_cfg_t jfc{};
    jfc.depth = 13;
    urma_jfr_cfg_t jfr{};
    jfr.depth = 14;
    urma_jfs_cfg_t jfs{};
    jfs.depth = 15;
    urma_jetty_cfg_t jetty{};
    jetty.jfs_cfg.depth = 16;
    std::ostringstream os;
    os << eid << id << jfc << jfr << jfs << jetty << g_jetty;
    EXPECT_NE(std::string::npos, os.str().find("jetty_id"));
    EXPECT_NE(std::string::npos, os.str().find("depth: 16"));
}

TEST_F(UrmaWrapperTest, RxResponseCtrlMessageBuildsWriteImmediateRequest)
{
    auto jfc = MakeJfc();
    auto jfs = MakeRef<UrmaJfs>(&g_jfs, context_, jfc);
    auto jfr = MakeRef<UrmaJfr>(&g_jfr, 11, context_, jfc);
    auto jetty = MakeRef<UrmaJetty>(URMA_UTP, 1, &g_jetty, context_, jfs, jfr);
    jetty->raw_target_jetty_ = &g_targetJetty;

    std::vector<uint8_t> recvBuf(2 * UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT);
    auto urmaSocket = MakeRef<UrmaSocket>(91);
    urmaSocket->recv_buf_addr_ = reinterpret_cast<uintptr_t>(recvBuf.data());
    urmaSocket->rmt_send_buf_addr_ = 0x8000;
    urmaSocket->local_ctrl_target_seg_ = &g_targetSeg;
    urmaSocket->remote_ctrl_target_seg_ = &g_targetSeg;
    urmaSocket->jetty_ = jetty;
    SocketPtr socket = RefConvert<UrmaSocket, Socket>(urmaSocket);
    auto *response = reinterpret_cast<UrmaCtrlMessage *>(recvBuf.data() + UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT);
    response->head.request_id = 7;
    response->head.sequence_no = 9;

    UrmaRxOps rx(91);
    EXPECT_EQ(UBS_OK, rx.ResponseCtrlMessage(socket, POST_WRITE, 1, 0));
    EXPECT_EQ(1u, g_postCount);
    g_failPost = true;
    EXPECT_EQ(UBS_ERROR, rx.ResponseCtrlMessage(socket, POST_WRITE, 1, 0));
    EXPECT_EQ(2u, g_postCount);

    urmaSocket->jetty_ = nullptr;
}
