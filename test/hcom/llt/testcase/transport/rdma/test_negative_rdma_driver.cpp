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

#ifdef RDMA_BUILD_ENABLED
#include "test_negative_rdma_driver.h"
#include "mockcpp/mockcpp.hpp"
#include "ut_helper.h"

using namespace ock::hcom;

TestNegativeRdmaDriver::TestNegativeRdmaDriver() {}

void TestNegativeRdmaDriver::SetUp()
{
    MOCK_VERSION
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNegativeRdmaDriver::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNegativeRdmaDriver, FakeBusyPolling)
{
    NResult result;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_START, true);
    UT_CHECK_RESULT_OK(result)
}

TEST_F(TestNegativeRdmaDriver, UseBeforeInit)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_START, true);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_NOT_NULL(server)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_NONE, false);
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
    UBSHcomNetDriver::DestroyInstance(server->Name());
    UBSHcomNetDriver::DestroyInstance(driver->Name());
}

TEST_F(TestNegativeRdmaDriver, DestroyUnownedMr)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *driver;
    UBSHcomNetDriver *driver1;

    result = UTHelper::GetDriver(driver, DRIVER_STATE_INIT, false);
    UT_CHECK_RESULT_OK(result)

    result = UTHelper::GetDriver(driver1, DRIVER_STATE_INIT, false);
    UT_CHECK_RESULT_OK(result)

    UBSHcomNetMemoryRegionPtr mr1;
    result = driver1->CreateMemoryRegion(NN_NO1024, mr1);
    EXPECT_EQ(result, NN_OK);

    driver->DestroyMemoryRegion(mr1);
    EXPECT_NE(mr1.Get()->GetAddress(), 0);

    driver->UnInitialize();
    driver1->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(driver->Name());
    UBSHcomNetDriver::DestroyInstance(driver1->Name());
}

TEST_F(TestNegativeRdmaDriver, UseAfterStop)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_STOP, true);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_STOP, false);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_FALSE(driver->IsStarted())
    result = driver->Start();
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_TRUE(driver->IsStarted())
    UBSHcomNetDriver::DestroyInstance(server->Name());
    UBSHcomNetDriver::DestroyInstance(driver->Name());
}

TEST_F(TestNegativeRdmaDriver, DiscontinuousState)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *driver;
    result = UTHelper::GetDriverStateMask(driver, DRIVER_STATE_INIT | DRIVER_STATE_START | DRIVER_STATE_UNINIT, false);
    UT_CHECK_RESULT_OK(result)
    UT_CHECK_RESULT_TRUE(driver->IsStarted())
    UT_CHECK_RESULT_TRUE(driver->IsInited())
    driver->Stop();
    UT_CHECK_RESULT_FALSE(driver->IsStarted())
    UT_CHECK_RESULT_TRUE(driver->IsInited())
    UBSHcomNetDriver::DestroyInstance(driver->Name());
}

TEST_F(TestNegativeRdmaDriver, UseAfterUninit)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr;
    UBSHcomNetDriver *driver = nullptr;
    result = UTHelper::GetDriver(server, DRIVER_STATE_START, true);
    UT_CHECK_RESULT_OK(result)
    result = UTHelper::GetDriver(driver, DRIVER_STATE_UNINIT, false);
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
    UBSHcomNetDriver::DestroyInstance(server->Name());
    UBSHcomNetDriver::DestroyInstance(driver->Name());
}
#endif