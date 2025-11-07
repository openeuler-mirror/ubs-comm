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
#include "ut_helper.h"

bool UTHelper::ServerCreateDriver(UBSHcomNetDriver *&serverDriver, Handlers &handlers, UBSHcomNetDriverOptions &options,
    uint16_t port)
{
    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA,
        "rdmaServer" + std::to_string(port), true);
    if (serverDriver == nullptr) {
        NN_LOG_ERROR("failed to create serverDriver already created");
        return false;
    }

    serverDriver->RegisterNewEPHandler(handlers.newEpHandler);
    serverDriver->RegisterEPBrokenHandler(handlers.epBrokenHandler);
    serverDriver->RegisterNewReqHandler(handlers.receivedHandler);
    serverDriver->RegisterReqPostedHandler(handlers.sentHandler);
    serverDriver->RegisterOneSideDoneHandler(handlers.oneSideDoneHandler);

    serverDriver->OobIpAndPort(BASE_IP, port);
    options.enableTls = false;
    int result = 0;
    if ((result = serverDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize serverDriver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver initialized");

    if ((result = serverDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start serverDriver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver started");
    UBSHcomNetMemoryRegionPtr mr;
    if (serverDriver->CreateMemoryRegion(NN_NO8192 * 16, mr) != 0) {
        NN_LOG_ERROR("failed to create server CreateMemoryRegion " << result);
        return false;
    }
    return true;
}

bool UTHelper::ClientCreateDriver(UBSHcomNetDriver *&clientDriver, Handlers &handlers, UBSHcomNetDriverOptions &options,
    uint16_t port)
{
    auto name = "rdmaClient" + std::to_string(port);
    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, name, false);
    if (clientDriver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }

    clientDriver->RegisterEPBrokenHandler(handlers.epBrokenHandler);
    clientDriver->RegisterNewReqHandler(handlers.receivedHandler);
    clientDriver->RegisterReqPostedHandler(handlers.sentHandler);
    clientDriver->RegisterOneSideDoneHandler(handlers.oneSideDoneHandler);

    clientDriver->OobIpAndPort(BASE_IP, port);
    options.enableTls = false;
    int result = 0;
    if ((result = clientDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver initialized");

    if ((result = clientDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver started");
    UBSHcomNetMemoryRegionPtr mr;
    if (clientDriver->CreateMemoryRegion(NN_NO8192 * 16, mr) != 0) {
        NN_LOG_ERROR("failed to create client CreateMemoryRegion " << result);
        return false;
    }
    return true;
}

bool UTHelper::ClientConnect(UBSHcomNetDriver *clientDriver, UBSHcomNetEndpointPtr &clientEp, uint16_t grpNo,
    uint16_t clientNo)
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
    if (clientDriver == nullptr) {
        NN_LOG_ERROR("clientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = clientDriver->Connect("hello world", clientEp, 0, grpNo, clientNo)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }
    return true;
}

bool UTHelper::ClientSend(UBSHcomNetEndpointPtr &clientEp, sem_t *sem)
{
    int result = 0;
    sem_init(sem, 0, 0);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    if ((result = clientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return false;
    }
    sem_wait(sem);
    return true;
}

static uint16_t basePort = 6900;
static int nameSeed = 0;

NResult UTHelper::GetDriverStateMask(UBSHcomNetDriver *&driver, uint16_t stateMask, bool isServer,
    UBSHcomNetDriverProtocol protocol)
{
    driver = UBSHcomNetDriver::Instance(protocol, std::to_string(nameSeed++), isServer);
    driver->OobIpAndPort(BASE_IP, isServer ? ++basePort : basePort);

    return ForwardDriverStateMask(driver, stateMask);
}

NResult UTHelper::GetDriver(UBSHcomNetDriver *&driver, DRIVER_STATE state, bool isServer,
    UBSHcomNetDriverProtocol protocol)
{
    driver = UBSHcomNetDriver::Instance(protocol, std::to_string(nameSeed++), isServer);
    driver->OobIpAndPort(BASE_IP, isServer ? ++basePort : basePort);

    return ForwardDriverState(driver, state);
}

NResult UTHelper::ForwardDriverState(UBSHcomNetDriver *&driver, DRIVER_STATE state)
{
    NResult result = NN_OK;
    if (state >= DRIVER_STATE_INIT) {
        UBSHcomNetDriverOptions options;
        options.mode = ock::hcom::NET_EVENT_POLLING;

        options.SetNetDeviceIpMask(IP_SEG);
        options.enableTls = false;
        result = driver->Initialize(options);
        if (result != NN_OK) {
            return result;
        }
    }
    if (state >= DRIVER_STATE_START) {
        Handlers handlers;
        driver->RegisterNewEPHandler(handlers.newEpHandler);
        driver->RegisterEPBrokenHandler(handlers.epBrokenHandler);
        driver->RegisterNewReqHandler(handlers.receivedHandler);
        driver->RegisterReqPostedHandler(handlers.sentHandler);
        driver->RegisterOneSideDoneHandler(handlers.oneSideDoneHandler);
        result = driver->Start();
        if (result != NN_OK) {
            return result;
        }
    }
    if (state >= DRIVER_STATE_STOP)
        driver->Stop();
    if (state >= DRIVER_STATE_UNINIT)
        driver->UnInitialize();
    return NN_OK;
}

NResult UTHelper::ForwardDriverStateMask(UBSHcomNetDriver *&driver, uint16_t state)
{
    NResult result = NN_OK;
    if (state & DRIVER_STATE_INIT) {
        UBSHcomNetDriverOptions options;
        options.mode = ock::hcom::NET_EVENT_POLLING;
        options.SetNetDeviceIpMask(IP_SEG);
        options.enableTls = false;
        result = driver->Initialize(options);
        if (result != NN_OK) {
            return result;
        }
    }
    if (state & DRIVER_STATE_START) {
        Handlers handlers;
        driver->RegisterNewEPHandler(handlers.newEpHandler);
        driver->RegisterEPBrokenHandler(handlers.epBrokenHandler);
        driver->RegisterNewReqHandler(handlers.receivedHandler);
        driver->RegisterReqPostedHandler(handlers.sentHandler);
        driver->RegisterOneSideDoneHandler(handlers.oneSideDoneHandler);
        result = driver->Start();
        if (result != NN_OK) {
            return result;
        }
    }
    if (state & DRIVER_STATE_STOP)
        driver->Stop();
    if (state & DRIVER_STATE_UNINIT)
        driver->UnInitialize();
    return NN_OK;
}