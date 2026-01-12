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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_c.h"
#include "hcom_service_c.h"
#include "service_channel_imp.h"
#include "net_rdma_async_endpoint.h"
#include "net_param_validator.h"
#include "net_mem_allocator.h"
#include "hcom.h"

namespace ock {
namespace hcom {
class TestHcomCapi : public testing::Test {
public:
    TestHcomCapi();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestHcomCapi::TestHcomCapi() {}

void TestHcomCapi::SetUp()
{
}

void TestHcomCapi::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestHcomCapi, TestCopySglInfo)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, src, 1), static_cast<uint32_t>(NN_INVALID_PARAM));
    free(src);
}

TEST_F(TestHcomCapi, TestCopySglInfoFail)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    src->iov = static_cast<ubs_hcom_readwrite_sge *>(malloc(sizeof(ubs_hcom_readwrite_sge)));
    src->iovCount = C_NET_SGE_MAX_IOV + 1;
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, src, 1), static_cast<uint32_t>(NN_INVALID_PARAM));
    free(src->iov);
    free(src);
}

TEST_F(TestHcomCapi, TestCopySglInfoNormal)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    src->iov = static_cast<ubs_hcom_readwrite_sge *>(malloc(sizeof(ubs_hcom_readwrite_sge)));
    src->iovCount = 1;
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, src, 1), static_cast<uint32_t>(NN_INVALID_PARAM));
    free(src->iov);
    free(src);
}

TEST_F(TestHcomCapi, TestPostSendRawSgl)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    src->iov = static_cast<ubs_hcom_readwrite_sge *>(malloc(sizeof(ubs_hcom_readwrite_sge)));
    src->iovCount = 1;
    src->upCtxData[0] = 1;
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpoint *endpoint = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, workerIndex);
    EXPECT_NE(ubs_hcom_ep_post_send_raw_sgl(reinterpret_cast<ubs_hcom_endpoint>(endpoint), src, 1),
        static_cast<uint32_t>(NN_OK));
    delete endpoint;
    free(src->iov);
    free(src);
}

TEST_F(TestHcomCapi, TestPostSendRawSglFail)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, src, 0), static_cast<uint32_t>(NN_INVALID_PARAM));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, nullptr, 0), static_cast<uint32_t>(NN_INVALID_PARAM));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(0, nullptr, 0), static_cast<uint32_t>(NN_INVALID_PARAM));
    free(src);
}

TEST_F(TestHcomCapi, TestSendRecvFds)
{
    EXPECT_EQ(ubs_hcom_channel_send_fds(0, nullptr, 0), static_cast<uint32_t>(SER_INVALID_PARAM));
    EXPECT_EQ(ubs_hcom_channel_recv_fds(0, nullptr, 0, 0), static_cast<uint32_t>(SER_INVALID_PARAM));
    InnerConnectOptions opt {};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    EXPECT_EQ(ubs_hcom_channel_send_fds(channel, nullptr, 0), static_cast<uint32_t>(SER_ERROR));
    EXPECT_EQ(ubs_hcom_channel_recv_fds(channel, nullptr, 0, 0), static_cast<uint32_t>(SER_ERROR));
    channel = 0;
    delete ch;
}

TEST_F(TestHcomCapi, TestConvertServiceConnectOptionsToInnerOptions)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    ubs_hcom_service_connect_options connectOpt {};
    connectOpt.mode = C_CLIENT_SELF_POLL_BUSY;
    ubs_hcom_channel channel = 0;
    EXPECT_NE(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
    connectOpt.mode = C_CLIENT_SELF_POLL_EVENT;
    EXPECT_NE(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
    ubs_hcom_service_destroy(service, "service0");
}

TEST_F(TestHcomCapi, TestConvertServiceConnectOptionsToInnerOptionsFail)
{
    ubs_hcom_service service = 0;
    ubs_hcom_service_connect_options connectOpt {};
    EXPECT_NE(ubs_hcom_service_connect(service, "url", nullptr, connectOpt), 0);

    ubs_hcom_service_options opt {};
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    EXPECT_NE(ubs_hcom_service_connect(service, nullptr, nullptr, connectOpt), 0);
    EXPECT_NE(ubs_hcom_service_connect(service, "url", nullptr, connectOpt), 0);
}

TEST_F(TestHcomCapi, TestServiceDoConnectFail)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);

    MOCKER_CPP(&HcomServiceImp::ValidateServiceOption).stubs().will(returnValue(static_cast<SerResult>(SER_OK)));
    MOCKER_CPP(&HcomServiceImp::InitDriver).stubs().will(returnValue(static_cast<SerResult>(SER_OK)));
    MOCKER_CPP(&ConnectOptionsCheck).stubs().will(returnValue(true));
    ret = ubs_hcom_service_start(service);
    ASSERT_EQ(ret, 0);
    ubs_hcom_service_connect_options connectOpt {};
    connectOpt.mode = C_CLIENT_SELF_POLL_BUSY;
    ubs_hcom_channel channel = 0;
    EXPECT_NE(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
}

int32_t ConnectStub(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt)
{
    InnerConnectOptions opt2{};
    UBSHcomChannelPtr fake_channel = new (std::nothrow) HcomChannelImp(0, false, opt2);
    ch = fake_channel;
    return SER_OK;
}

TEST_F(TestHcomCapi, TestServiceConnectNormal)
{
    UBSHcomServiceOptions opt{};
    UBSHcomService *servicet = new (std::nothrow) HcomServiceImp(UBSHcomServiceProtocol::RDMA, "service0", opt);
    EXPECT_NE(servicet, nullptr);
    ubs_hcom_service service = reinterpret_cast<ubs_hcom_service>(servicet);
    MOCKER_CPP_VIRTUAL(*servicet, &UBSHcomService::Connect).stubs().will(invoke(ConnectStub));
    ubs_hcom_service_connect_options connectOpt {};
    connectOpt.mode = C_CLIENT_SELF_POLL_BUSY;
    ubs_hcom_channel channel = 0;
    EXPECT_EQ(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
    delete servicet;
}

int CertCb(const char *name, char **certPath)
{
    return 0;
}

int PriKeyCb(const char *name, char **priKeyPath, char **keyPass, ubs_hcom_tls_keypass_erase *erase)
{
    return 0;
}

int CaCb(const char *name, char **caPath, char **crlPath, ubs_hcom_peer_cert_verify_type *verifyType,
         ubs_hcom_tls_cert_verify *verify)
{
    return 0;
}

TEST_F(TestHcomCapi, TestSetTlsOptions)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, false, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_128,
        nullptr, nullptr, nullptr));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(0, false, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_128,
        nullptr, nullptr, nullptr));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_128,
        nullptr, nullptr, nullptr));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_128,
        CertCb, PriKeyCb, CaCb));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_3, C_SERVICE_AES_GCM_128,
        CertCb, PriKeyCb, CaCb));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_256,
        CertCb, PriKeyCb, CaCb));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_2, C_SERVICE_AES_CCM_128,
        CertCb, PriKeyCb, CaCb));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, true, C_SERVICE_TLS_1_2, C_SERVICE_CHACHA20_POLY1305,
        CertCb, PriKeyCb, CaCb));
    ubs_hcom_service_destroy(service, "service0");
}

TEST_F(TestHcomCapi, TestSetUbcMode)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_UBC, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    ubs_hcom_service_ubc_mode ubcMode = C_SERVICE_HIGHBANDWIDTH;
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_ubcmode(service, ubcMode));
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_ubcmode(0, ubcMode));
    ubcMode = C_SERVICE_LOWLATENCY;
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_ubcmode(service, ubcMode));
    ubs_hcom_service_destroy(service, "service0");
}

void CommonCb(void *arg, ubs_hcom_service_context context)
{
    return;
}

TEST_F(TestHcomCapi, TestChannelRecv)
{
    EXPECT_EQ(ubs_hcom_channel_recv(0, 0, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    InnerConnectOptions opt{};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    EXPECT_EQ(ubs_hcom_channel_recv(channel, 0, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    UBSHcomServiceContext ctx{};
    ubs_hcom_service_context serviceContext = reinterpret_cast<ubs_hcom_service_context>(&ctx);
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    uintptr_t address = NN_NO100;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, 0, nullptr),
        static_cast<uint32_t>(SER_INVALID_PARAM));

    MOCKER_CPP_VIRTUAL(*ch, &UBSHcomChannel::Recv).stubs().will(returnValue(static_cast<int>(SER_OK)));
    uint32_t size = NN_NO1024;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, size, nullptr), static_cast<uint32_t>(SER_OK));

    ubs_hcom_channel_callback cb;
    cb.cb = CommonCb;
    cb.arg = NULL;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, size, &cb), static_cast<uint32_t>(SER_OK));
    channel = 0;
    delete ch;
}

TEST_F(TestHcomCapi, SetTwoSideThreshold)
{
    ubs_hcom_twoside_threshold twoSideThreshold;
    twoSideThreshold.splitThreshold = NN_NO8192;
    twoSideThreshold.rndvThreshold = NN_NO8192;
    EXPECT_EQ(ubs_hcom_channel_set_twoside_threshold(0, twoSideThreshold), static_cast<uint32_t>(SER_INVALID_PARAM));

    InnerConnectOptions opt{};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    MOCKER_CPP_VIRTUAL(*ch, &UBSHcomChannel::SetTwoSideThreshold).stubs().will(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(ubs_hcom_channel_set_twoside_threshold(channel, twoSideThreshold), static_cast<uint32_t>(SER_OK));

    channel = 0;
    delete ch;
}

TEST_F(TestHcomCapi, TestCreateAllocator)
{
    ubs_hcom_memory_allocator_type type = C_DYNAMIC_SIZE;
    int ret = ubs_hcom_mem_allocator_create(type, nullptr, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ubs_hcom_memory_allocator_options options{};
    options.address = NN_NO0;
    ret = ubs_hcom_mem_allocator_create(type, &options, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ubs_hcom_memory_allocator allocator = NN_NO100;
    ret = ubs_hcom_mem_allocator_create(type, &options, &allocator);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    options.address = NN_NO100;
    options.size = NN_NO0;
    ret = ubs_hcom_mem_allocator_create(type, &options, &allocator);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    options.size = NN_NO4096;
    options.cacheTierPolicy = C_TIER_TIMES;
    MOCKER_CPP(&NetMemAllocator::MemoryRegionInit).stubs().will(returnValue(0));
    ret = ubs_hcom_mem_allocator_create(type, &options, &allocator);
    EXPECT_EQ(ret, SER_OK);

    ret = ubs_hcom_mem_allocator_set_mr_key(allocator, 0);
    EXPECT_EQ(ret, SER_OK);
    uintptr_t *offset = reinterpret_cast<uintptr_t *>(malloc(sizeof(uint64_t)));
    ASSERT_NE(offset, nullptr);
    ret = ubs_hcom_mem_allocator_get_offset(allocator, NN_NO200, offset);
    EXPECT_EQ(ret, SER_OK);
    uintptr_t *freesize = reinterpret_cast<uintptr_t *>(malloc(sizeof(uint64_t)));
    ASSERT_NE(freesize, nullptr);
    ret = ubs_hcom_mem_allocator_get_free_size(allocator, freesize);
    EXPECT_EQ(ret, SER_OK);

    uint64_t *key = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t)));
    ASSERT_NE(key, nullptr);
    MOCKER_CPP(&NetMemAllocator::RegionMalloc).stubs().will(returnValue(0));
    ret = ubs_hcom_mem_allocator_allocate(allocator, 1, offset, key);
    EXPECT_EQ(ret, SER_OK);

    MOCKER_CPP(&NetMemAllocator::RegionFree).stubs().will(returnValue(0));
    *offset = NN_NO100;
    ret = ubs_hcom_mem_allocator_free(allocator, *offset);
    EXPECT_EQ(ret, SER_OK);
    ret = ubs_hcom_mem_allocator_destroy(allocator);
    EXPECT_EQ(ret, SER_OK);
    free(offset);
    free(freesize);
    free(key);
}

TEST_F(TestHcomCapi, TestCheckSupport)
{
    ubs_hcom_driver_type type = static_cast<ubs_hcom_driver_type>(-1);
    ubs_hcom_device_info info{};
    const int maxSge = 2;
    info.maxSge = maxSge;
    int ret = ubs_hcom_check_local_support(type, nullptr);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_check_local_support(type, &info);
    EXPECT_EQ(ret, 0);

    type = C_DRIVER_RDMA;
    ret = ubs_hcom_check_local_support(type, &info);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestHcomCapi, TestCreateDriver)
{
    const uint8_t startOobSvr = 2;
    ubs_hcom_driver_type type = static_cast<ubs_hcom_driver_type>(-1);
    ubs_hcom_driver *driver = reinterpret_cast<ubs_hcom_driver *>(malloc(sizeof(ubs_hcom_driver)));
    ASSERT_NE(driver, nullptr);
    int ret = ubs_hcom_driver_create(type, nullptr, 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create(type, nullptr, 0, driver);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create(type, "driver0", 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create(type, "driver0", startOobSvr, driver);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create(type, "driver0", 1, driver);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    type = C_DRIVER_RDMA;
    ret = ubs_hcom_driver_create(type, "driver0", 1, driver);
    EXPECT_EQ(ret, NN_OK);

    ret = ubs_hcom_driver_create(type, "driver0", 0, driver);
    EXPECT_EQ(ret, NN_OK);

    UBSHcomNetDriver *temp = nullptr;
    MOCKER(&UBSHcomNetDriver::Instance).stubs().will(returnValue(temp));
    ret = ubs_hcom_driver_create(type, "driver0", 1, driver);
    EXPECT_EQ(ret, NN_NEW_OBJECT_FAILED);

    ret = ubs_hcom_driver_destroy(*driver);
    EXPECT_EQ(ret, NN_OK);

    free(driver);
}

bool GetOobIpAndPortStub(std::vector<std::pair<std::string, uint16_t>> &result)
{
    const uint16_t port = 8080;
    const char *ip = "1.2.3.4";
    std::pair<std::string, uint16_t> p1{};
    p1.first = ip;
    p1.second = port;
    result.push_back(p1);
    return true;
}

void AddOobOptionsStub(const UBSHcomNetOobListenerOptions &option)
{
    return;
}

void AddOobUdsOptionsStub(const UBSHcomNetOobUDSListenerOptions &option)
{
    return;
}

TEST_F(TestHcomCapi, TestDriverGetIpport)
{
    UBSHcomNetDriver *driver = new (std::nothrow) NetDriverRDMAWithOob("device0", true, UBSHcomNetDriverProtocol::RDMA);
    ubs_hcom_driver_type type = C_DRIVER_RDMA;
    ubs_hcom_driver drivert = reinterpret_cast<ubs_hcom_driver>(driver);
    const uint16_t port = 8080;
    const char *ip = "1.2.3.4";
    MOCKER_CPP(&UBSHcomNetDriver::AddOobOptions).stubs().will(invoke(AddOobOptionsStub));
    MOCKER_CPP(&UBSHcomNetDriver::AddOobUdsOptions).stubs().will(invoke(AddOobUdsOptionsStub));
    ubs_hcom_driver_set_ipport(drivert, ip, port);
    ubs_hcom_driver_set_udsname(drivert, "driver0");

    ubs_hcom_driver_uds_listen_opts option{};
    option.name[0] = 'd';
    option.perm = 1;
    option.targetWorkerCount = 1;
    ubs_hcom_driver_add_uds_opt(drivert, option);

    ubs_hcom_driver_listen_opts options{};
    std::string ipv4 = "121.0.1.3";
    ipv4.copy(options.ip, ipv4.length());
    options.port = port;
    options.targetWorkerCount = 1;
    ubs_hcom_driver_add_oob_opt(drivert, options);

    char **ipArray = nullptr;
    uint16_t *portArray = nullptr;
    int length{};
    MOCKER_CPP(&UBSHcomNetDriver::GetOobIpAndPort).stubs().will(invoke(GetOobIpAndPortStub));
    EXPECT_EQ(ubs_hcom_driver_get_ipport(drivert, &ipArray, &portArray, &length), true);

    void *devAttr = nullptr;
    MOCKER(malloc).stubs().will(returnValue(devAttr));
    EXPECT_EQ(ubs_hcom_driver_get_ipport(drivert, &ipArray, &portArray, &length), false);

    delete driver;
}

TEST_F(TestHcomCapi, TestDriverInitialize)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);
    ubs_hcom_driver_opts options{};
    options.workerGroups[0] = '1';
    options.workerGroupsCpuSet[0] = '1';
    options.oobPortRange[0] = '1';
    const char *ip = "1.2.3.4";
    const uint16_t port = 8080;

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::Initialize).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    int ret = ubs_hcom_driver_initialize(driver, options);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::Start).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_driver_start(driver);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::Connect,
        NResult(UBSHcomNetDriver::*)(const std::string &, UBSHcomNetEndpointPtr &, uint32_t, uint8_t, uint8_t))
        .stubs().will(returnValue(static_cast<NResult>(NN_ERROR)));
    ubs_hcom_endpoint ep = NN_NO100;
    ret = ubs_hcom_driver_connect(driver, nullptr, &ep, 0);
    EXPECT_EQ(ret, NN_ERROR);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::Connect,
        NResult(UBSHcomNetDriver::*)(const std::string &, uint16_t, const std::string &, UBSHcomNetEndpointPtr &,
        uint32_t, uint8_t, uint8_t, uint64_t)).stubs().will(returnValue(static_cast<NResult>(NN_ERROR)));
    ret = ubs_hcom_driver_connect_to_ipport(driver, nullptr, 0, nullptr, &ep, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = ubs_hcom_driver_connect_to_ipport(driver, ip, 0, nullptr, &ep, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = ubs_hcom_driver_connect_to_ipport(driver, nullptr, port, nullptr, &ep, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = ubs_hcom_driver_connect_to_ipport(driver, ip, port, nullptr, &ep, 0);
    EXPECT_EQ(ret, NN_ERROR);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::Stop).stubs().will(ignoreReturnValue());
    ubs_hcom_driver_stop(driver);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::UnInitialize).stubs().will(ignoreReturnValue());
    ubs_hcom_driver_uninitialize(driver);

    delete drivert;
}

int HandleEp(ubs_hcom_endpoint ep, uint64_t usrCtx, const char *payLoad)
{
    return NN_OK;
}

TEST_F(TestHcomCapi, TestDriverEpHandle)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);
    uintptr_t ret = ubs_hcom_driver_register_ep_handler(0, C_EP_NEW, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_ep_handler(driver, C_EP_NEW, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_ep_handler(driver, static_cast<ubs_hcom_ep_handler_type>(-1), HandleEp, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_ep_handler(driver, C_EP_NEW, HandleEp, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_ep_handler(C_EP_NEW, ret);

    ret = ubs_hcom_driver_register_ep_handler(driver, C_EP_BROKEN, HandleEp, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_ep_handler(C_EP_BROKEN, ret);

    delete drivert;
}

int HandleRequest(ubs_hcom_request_context *ctx, uint64_t usrCtx)
{
    return NN_OK;
}

TEST_F(TestHcomCapi, TestDriverOpHandle)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);
    uintptr_t ret = ubs_hcom_driver_register_op_handler(0, C_OP_REQUEST_RECEIVED, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_op_handler(driver, C_OP_REQUEST_RECEIVED, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_op_handler(driver, static_cast<ubs_hcom_op_handler_type>(-1), HandleRequest, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_op_handler(driver, C_OP_REQUEST_RECEIVED, HandleRequest, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_op_handler(C_OP_REQUEST_RECEIVED, ret);

    ret = ubs_hcom_driver_register_op_handler(driver, C_OP_REQUEST_POSTED, HandleRequest, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_op_handler(C_OP_REQUEST_POSTED, ret);

    ret = ubs_hcom_driver_register_op_handler(driver, C_OP_READWRITE_DONE, HandleRequest, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_op_handler(C_OP_READWRITE_DONE, ret);

    delete drivert;
}

void HandleIdle(uint8_t wkrGrpIdx, uint16_t idxInGrp, uint64_t usrCtx)
{
    return;
}

TEST_F(TestHcomCapi, TestDriverIdleHandle)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);
    uintptr_t ret = ubs_hcom_driver_register_idle_handler(0, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_idle_handler(driver, nullptr, 0);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_idle_handler(driver, HandleIdle, 0);
    EXPECT_NE(ret, 0);
    ubs_hcom_driver_unregister_idle_handler(ret);

    delete drivert;
}

int HandleProvider(uint64_t ctx, int64_t *flag, ubs_hcom_driver_sec_type *type, char **output,
    uint32_t *outLen, int *needAutoFree)
{
    return NN_OK;
}

int HandleValidator(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    return NN_OK;
}

TEST_F(TestHcomCapi, TestDriverRegisterProviderValidator)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);

    uintptr_t ret = ubs_hcom_driver_register_secinfo_provider(0, nullptr);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_secinfo_validator(0, nullptr);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_secinfo_provider(driver, nullptr);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_secinfo_validator(driver, nullptr);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_secinfo_provider(driver, HandleProvider);
    EXPECT_NE(ret, 0);
    ret = ubs_hcom_driver_register_secinfo_validator(driver, HandleValidator);
    EXPECT_NE(ret, 0);

    delete drivert;
}

int certCbStub(const char *name, char **certPath)
{
    return NN_OK;
}

int PriKeyCbStub(const char *name, char **priKeyPath, char **keyPass, ubs_hcom_tls_keypass_erase *erase)
{
    return NN_OK;
}

int CaCbStub(const char *name, char **caPath, char **crlPath,
    ubs_hcom_peer_cert_verify_type *verifyType, ubs_hcom_tls_cert_verify *verify)
{
    return NN_OK;
}

TEST_F(TestHcomCapi, TestDriverRegisterTls)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);

    uintptr_t ret = ubs_hcom_driver_register_tls_cb(0, nullptr, nullptr, nullptr);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_tls_cb(driver, certCbStub, nullptr, nullptr);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_tls_cb(driver, certCbStub, PriKeyCbStub, nullptr);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_tls_cb(driver, certCbStub, nullptr, CaCbStub);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_tls_cb(driver, nullptr, PriKeyCbStub, nullptr);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_tls_cb(driver, nullptr, nullptr, CaCbStub);
    EXPECT_EQ(ret, 0);
    ret = ubs_hcom_driver_register_tls_cb(driver, nullptr, PriKeyCbStub, CaCbStub);
    EXPECT_EQ(ret, 0);

    ret = ubs_hcom_driver_register_tls_cb(driver, certCbStub, PriKeyCbStub, CaCbStub);
    EXPECT_NE(ret, 0);

    delete drivert;
}

TEST_F(TestHcomCapi, TestDriverCreateMemoryRegion)
{
    UBSHcomNetDriver *drivert = new (std::nothrow) NetDriverRDMAWithOob("device0",
        true, UBSHcomNetDriverProtocol::RDMA);
    EXPECT_NE(drivert, nullptr);
    ubs_hcom_driver driver = reinterpret_cast<ubs_hcom_driver>(drivert);
    ubs_hcom_memory_region mr = NN_NO100;

    int ret = ubs_hcom_driver_create_memory_region(0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create_memory_region(driver, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::CreateMemoryRegion,
        NResult(UBSHcomNetDriver::*)(uint64_t size, UBSHcomNetMemoryRegionPtr &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_driver_create_memory_region(driver, 1, &mr);
    EXPECT_EQ(ret, NN_OK);

    ubs_hcom_memory_region_info info{};
    ret = ubs_hcom_driver_get_memory_region_info(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_get_memory_region_info(mr, nullptr);
    EXPECT_EQ(ret, NN_PARAM_INVALID);
    ret = ubs_hcom_driver_get_memory_region_info(mr, &info);
    EXPECT_EQ(ret, NN_ERROR);

    ret = ubs_hcom_driver_create_assign_memory_region(0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_driver_create_assign_memory_region(driver, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::CreateMemoryRegion,
        NResult(UBSHcomNetDriver::*)(uintptr_t, uint64_t, UBSHcomNetMemoryRegionPtr &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_driver_create_assign_memory_region(driver, NN_NO100, 1, &mr);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP_VIRTUAL(*drivert, &UBSHcomNetDriver::DestroyMemoryRegion).stubs().will(ignoreReturnValue());
    ubs_hcom_driver_destroy_memory_region(driver, mr);
    delete drivert;
}

TEST_F(TestHcomCapi, TestEndpointSetGetOperations)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    uint64_t ctx = 1;
    ubs_hcom_ep_set_context(ep, ctx);
    EXPECT_EQ(ubs_hcom_ep_get_context(ep), ctx);

    EXPECT_EQ(ubs_hcom_ep_get_worker_idx(0), NET_INVALID_WORKER_INDEX);
    EXPECT_EQ(ubs_hcom_ep_get_worker_idx(ep), workerWholeIndex.idxInGrp);
    EXPECT_EQ(ubs_hcom_ep_get_workergroup_idx(0), NET_INVALID_WORKER_GROUP_INDEX);
    EXPECT_EQ(ubs_hcom_ep_get_workergroup_idx(ep), workerWholeIndex.grpIdx);

    EXPECT_EQ(ubs_hcom_ep_get_listen_port(0), 0);
    EXPECT_EQ(ubs_hcom_ep_get_listen_port(ep), 0);

    EXPECT_EQ(ubs_hcom_ep_version(0), 0);
    EXPECT_EQ(ubs_hcom_ep_version(ep), 0);

    const int32_t timeout = 10;
    ubs_hcom_ep_set_timeout(0, 0);
    ubs_hcom_ep_set_timeout(ep, timeout);

    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostSend)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_send_request req{};
    req.data = NN_NO100;
    req.size = NN_NO10;
    req.upCtxSize = NN_NO1024;
    std::string s = "request";
    s.copy(req.upCtxData, s.length());

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostSend,
        NResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, uint32_t))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    int ret = ubs_hcom_ep_post_send(ep, 0, &req);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_send(ep, 0, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostSendWithOpinfo)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_send_request req{};
    req.data = NN_NO100;
    req.size = NN_NO10;
    req.upCtxSize = NN_NO1024;
    std::string s = "request";
    s.copy(req.upCtxData, s.length());
    ubs_hcom_opinfo opInfo = {0, 1, -1, 0};

    int ret = ubs_hcom_ep_post_send_with_opinfo(ep, 0, &req, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostSend,
        NResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, const UBSHcomNetTransOpInfo &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_send_with_opinfo(ep, 0, &req, &opInfo);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_send_with_opinfo(ep, 0, &req, &opInfo);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostSendRaw)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_send_request req{};
    req.data = NN_NO100;
    req.size = NN_NO10;
    req.upCtxSize = NN_NO1024;
    std::string s = "request";
    s.copy(req.upCtxData, s.length());
    int ret = ubs_hcom_ep_post_send_raw(ep, nullptr, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_send_raw(ep, &req, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostSendRaw,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &, uint32_t))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_send_raw(ep, &req, 1);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_send_raw(ep, &req, 1);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostSendRawSgl)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_readwrite_request_sgl req{nullptr, 1, 1};
    std::string s = "request";
    s.copy(req.upCtxData, s.length());

    int ret = ubs_hcom_ep_post_send_raw_sgl(ep, nullptr, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_send_raw_sgl(ep, &req, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_send_raw_sgl(ep, &req, 1);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ubs_hcom_readwrite_sge iov = {NN_NO100, NN_NO200, 0, 1, NN_NO100};
    req.iov = &iov;
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostSendRawSgl,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &, uint32_t))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_send_raw_sgl(ep, &req, 1);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_send_raw_sgl(ep, &req, 1);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostSendWithSeqno)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_send_request req{};
    req.data = NN_NO100;
    req.size = NN_NO10;
    req.upCtxSize = NN_NO1024;
    std::string s = "request";
    s.copy(req.upCtxData, s.length());

    int ret = ubs_hcom_ep_post_send_with_seqno(ep, 0, nullptr, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostSend,
        NResult(UBSHcomNetEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, uint32_t))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_send_with_seqno(ep, 0, &req, 0);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_send_with_seqno(ep, 0, &req, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostReadAndWrite)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    const uint16_t upCtxSize = 10;
    ubs_hcom_readwrite_request req;
    req.lMRA = NN_NO100;
    req.rMRA = NN_NO200;
    req.rKey = 1;
    req.lKey = 0;
    req.size = 1;
    req.upCtxSize = upCtxSize;
    std::string s = "request";
    s.copy(req.upCtxData, s.length());

    int ret = ubs_hcom_ep_post_read(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_read(ep, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write(ep, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostRead,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostWrite,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransRequest &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_read(ep, &req);
    EXPECT_EQ(ret, NN_OK);
    ret = ubs_hcom_ep_post_write(ep, &req);
    EXPECT_EQ(ret, NN_OK);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_read(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpPostReadAndWriteSgl)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);
    ubs_hcom_readwrite_request_sgl req{nullptr, 1, 1};
    std::string s = "request";
    s.copy(req.upCtxData, s.length());

    int ret = ubs_hcom_ep_post_read_sgl(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_read_sgl(ep, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_read_sgl(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write_sgl(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write_sgl(ep, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write_sgl(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ubs_hcom_readwrite_sge iov = {NN_NO100, NN_NO200, 0, 1, NN_NO100};
    req.iov = &iov;
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostRead,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::PostWrite,
        NResult(UBSHcomNetEndpoint::*)(const UBSHcomNetTransSglRequest &))
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_ep_post_read_sgl(ep, &req);
    EXPECT_EQ(ret, NN_OK);
    ret = ubs_hcom_ep_post_write_sgl(ep, &req);
    EXPECT_EQ(ret, NN_OK);
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(-1));
    ret = ubs_hcom_ep_post_read_sgl(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_post_write_sgl(ep, &req);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpReceive)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);

    int ret = ubs_hcom_ep_wait_completion(0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_wait_completion(ep, 0);
    EXPECT_EQ(ret, NN_INVALID_OPERATION);

    ubs_hcom_response_context *ctx =
        reinterpret_cast<ubs_hcom_response_context *>(malloc(sizeof(ubs_hcom_response_context)));
    ASSERT_NE(ctx, nullptr);
    ctx->opCode = 0;
    ctx->seqNo = 0;
    ctx->msgSize = 1;
    ctx->msgData = const_cast<char *>("00");
    ret = ubs_hcom_ep_receive(0, 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive(0, 0, &ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive(ep, 0, &ctx);
    EXPECT_EQ(ret, NN_INVALID_OPERATION);

    ret = ubs_hcom_ep_receive_raw(0, 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive_raw(0, 0, &ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive_raw(ep, 0, &ctx);
    EXPECT_EQ(ret, NN_INVALID_OPERATION);

    ret = ubs_hcom_ep_receive_raw_sgl(0, 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive_raw_sgl(ep, 0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_ep_receive_raw_sgl(ep, 0, &ctx);
    EXPECT_EQ(ret, NN_INVALID_OPERATION);
    delete ept;
}

TEST_F(TestHcomCapi, TestEpReferAndErrStr)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::Close).stubs().will(ignoreReturnValue());
    ubs_hcom_ep_refer(0);
    ubs_hcom_ep_close(0);
    ubs_hcom_ep_destroy(0);
    ubs_hcom_ep_refer(ep);
    ubs_hcom_ep_close(ep);
    ubs_hcom_ep_destroy(ep);

    const int16_t errCode = 101;
    EXPECT_STREQ(ubs_hcom_err_str(0), "OK");
    EXPECT_STREQ(ubs_hcom_err_str(errCode), "net invalid ip");
}

TEST_F(TestHcomCapi, TestEstimateLen)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);

    uint64_t ret = ubs_hcom_estimate_encrypt_len(0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_estimate_decrypt_len(0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_estimate_encrypt_len(ep, 0);
    EXPECT_EQ(ret, NN_OK);
    ret = ubs_hcom_estimate_decrypt_len(ep, 0);
    EXPECT_EQ(ret, NN_OK);
    delete ept;
}

TEST_F(TestHcomCapi, TestEncryptAndDecrypt)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);

    uint64_t cipherLen = 1;
    uint64_t rawLen = 1;
    int data[1];
    int ret = ubs_hcom_encrypt(0, nullptr, 0, nullptr, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_decrypt(0, nullptr, 0, nullptr, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_encrypt(ep, nullptr, 0, nullptr, nullptr);
    EXPECT_EQ(ret, SER_INVALID_PARAM);
    ret = ubs_hcom_decrypt(ep, nullptr, 0, nullptr, nullptr);
    EXPECT_EQ(ret, SER_INVALID_PARAM);
    ret = ubs_hcom_decrypt(ep, nullptr, 0, nullptr, &rawLen);
    EXPECT_EQ(ret, SER_INVALID_PARAM);

    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::Encrypt).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::Decrypt).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_encrypt(ep, nullptr, 0, nullptr, &cipherLen);
    EXPECT_EQ(ret, NN_OK);
    ret = ubs_hcom_decrypt(ep, nullptr, 0, data, &rawLen);
    EXPECT_EQ(ret, NN_OK);
    delete ept;
}

TEST_F(TestHcomCapi, TestFDS)
{
    UBSHcomNetWorkerIndex workerWholeIndex{};
    workerWholeIndex.Set(0, 0, 0);
    UBSHcomNetEndpoint *ept = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerWholeIndex);
    ubs_hcom_endpoint ep = reinterpret_cast<ubs_hcom_endpoint>(ept);

    ubs_hcom_uds_id_info idInfo = {0, 1, 2};
    int ret = ubs_hcom_send_fds(0, nullptr, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_receive_fds(0, nullptr, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_get_remote_uds_info(0, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = ubs_hcom_get_remote_uds_info(ep, nullptr);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = ubs_hcom_send_fds(ep, nullptr, 0);
    EXPECT_EQ(ret, NN_EXCHANGE_FD_NOT_SUPPORT);
    ret = ubs_hcom_receive_fds(ep, nullptr, 0, 0);
    EXPECT_EQ(ret, NN_EXCHANGE_FD_NOT_SUPPORT);
    ret = ubs_hcom_get_remote_uds_info(ep, &idInfo);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    MOCKER_CPP_VIRTUAL(*ept, &UBSHcomNetEndpoint::GetRemoteUdsIdInfo)
        .stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    ret = ubs_hcom_get_remote_uds_info(ep, &idInfo);
    EXPECT_EQ(ret, NN_OK);
    delete ept;
}

}
}