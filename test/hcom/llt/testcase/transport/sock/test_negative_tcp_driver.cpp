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
#include "test_negative_tcp_driver.h"
#include "mockcpp/mockcpp.hpp"
#include "ut_helper.h"

using namespace ock::hcom;

TestNegativeTcpDriver::TestNegativeTcpDriver() {}

void TestNegativeTcpDriver::SetUp()
{
    MOCK_VERSION
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNegativeTcpDriver::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNegativeTcpDriver, Overload)
{
    NResult result;
    UBSHcomNetDriver *server;
    UBSHcomNetDriverDeviceInfo deviceInfo;
    bool ret = server->LocalSupport(ock::hcom::TCP, deviceInfo);
    ASSERT_EQ(true, ret);
    for (int i = 0; i < 10; ++i) {
        result = UTHelper::GetDriver(server, DRIVER_STATE_START, true, UBSHcomNetDriverProtocol::TCP);
        std::cout << "create driver " << i << std::endl;
        UT_CHECK_RESULT_OK(result)
        server->Stop();
        server->UnInitialize();
    }
}

TEST_F(TestNegativeTcpDriver, UseBeforeInit)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_START, true, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_NOT_NULL(server)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_NONE, false, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_NOT_NULL(driver)

    UBSHcomNetMemoryRegionPtr mr;
    result = driver->CreateMemoryRegion(NN_NO1024, mr);
    UT_CHECK_RESULT_NOK(result)
    result = driver->Start();
    UT_CHECK_RESULT_NOK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_NOK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_INIT);
    UT_CHECK_RESULT_OK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_NOK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_START);
    UT_CHECK_RESULT_OK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(server, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    std::string name1 = server->Name();
    std::string name2 = driver->Name();
    UBSHcomNetDriver::DestroyInstance(name1);
    UBSHcomNetDriver::DestroyInstance(name2);
}

TEST_F(TestNegativeTcpDriver, DestroyUnownedMr)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *driver;
    UBSHcomNetDriver *driver1;

    result = UTHelper::GetDriver(driver, DRIVER_STATE_INIT, false, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)

    result = UTHelper::GetDriver(driver1, DRIVER_STATE_INIT, false, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)

    UBSHcomNetMemoryRegionPtr mr1;
    result = driver1->CreateMemoryRegion(NN_NO1024, mr1);
    UT_CHECK_RESULT_OK(result)

    driver->DestroyMemoryRegion(mr1);
    ASSERT_NE(mr1.Get()->GetAddress(), 0);

    driver->UnInitialize();
    driver1->UnInitialize();
    std::string name1 = driver->Name();
    std::string name2 = driver1->Name();
    UBSHcomNetDriver::DestroyInstance(name1);
    UBSHcomNetDriver::DestroyInstance(name2);
}

TEST_F(TestNegativeTcpDriver, UseAfterStop)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_STOP, true, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_STOP, false, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_FALSE(driver->IsStarted())
    result = driver->Start();
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_TRUE(driver->IsStarted())
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(server, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    std::string name1 = server->Name();
    std::string name2 = driver->Name();
    UBSHcomNetDriver::DestroyInstance(name1);
    UBSHcomNetDriver::DestroyInstance(name2);
}

TEST_F(TestNegativeTcpDriver, DiscontinuousState)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *driver;
    result =
        UTHelper::GetDriverStateMask(driver, DRIVER_STATE_INIT | DRIVER_STATE_START, false,
        UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_TRUE(driver->IsStarted())
    UT_CHECK_RESULT_TRUE(driver->IsInited())
    driver->Stop();
    UT_CHECK_RESULT_FALSE(driver->IsStarted())
    UT_CHECK_RESULT_TRUE(driver->IsInited())
    std::string name1 = driver->Name();
    UBSHcomNetDriver::DestroyInstance(name1);
}

TEST_F(TestNegativeTcpDriver, UseAfterUninit)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_START, true, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_UNINIT, false, UBSHcomNetDriverProtocol::TCP);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_FALSE(driver->IsStarted())
    UT_CHECK_RESULT_FALSE(driver->IsInited())

    UBSHcomNetMemoryRegionPtr mr;
    result = driver->CreateMemoryRegion(NN_NO1024, mr);
    UT_CHECK_RESULT_NOK(result)
    result = driver->Start();
    UT_CHECK_RESULT_NOK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_NOK(result)

    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_INIT);
    UT_CHECK_RESULT_OK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_NOK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_START);
    UT_CHECK_RESULT_OK(result)
    result = driver->Connect("halo", ep);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(server, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(driver, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::ForwardDriverStateMask(server, DRIVER_STATE_STOP | DRIVER_STATE_UNINIT);
    UT_CHECK_RESULT_OK(result)
    std::string name1 = server->Name();
    std::string name2 = driver->Name();
    UBSHcomNetDriver::DestroyInstance(name1);
    UBSHcomNetDriver::DestroyInstance(name2);
}
