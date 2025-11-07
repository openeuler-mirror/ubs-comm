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
#ifndef HCOM_UT_HELPER_H
#define HCOM_UT_HELPER_H

#ifdef RDMA_BUILD_ENABLED
#include "transport/rdma/rdma_common.h"
#endif
#include "common/net_util.h"
#include "hcom.h"
#include "hcom_def.h"

using namespace ock::hcom;

#ifdef MOCK_VERBS
#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"
#ifdef RDMA_BUILD_ENABLED
#define MOCK_VERSION MOCKER(ReadRoCEVersionFromFile).stubs().will(returnValue(0));
#else
#define MOCK_VERSION
#endif
#else
#define BASE_IP "192.168.100.204"
#define IP_SEG "192.168.100.0/24"
#define MOCK_VERSION
#endif

using Handlers = struct hdlrs {
    UBSHcomNetDriverNewEndPointHandler newEpHandler = [](const std::string &ipPort, const UBSHcomNetEndpointPtr &,
        const std::string &payload) { return 0; };
    UBSHcomNetDriverEndpointBrokenHandler epBrokenHandler = [](const UBSHcomNetEndpointPtr &) { return 0; };
    UBSHcomNetDriverSentHandler sentHandler = [](const UBSHcomNetRequestContext &) { return 0; };
    UBSHcomNetDriverOneSideDoneHandler oneSideDoneHandler = [](const UBSHcomNetRequestContext &) { return 0; };
    UBSHcomNetDriverReceivedHandler receivedHandler = [](const UBSHcomNetRequestContext &) { return 0; };
};

struct DummyObj {
    int tag = -1;
    explicit DummyObj(int _tag) : tag(_tag) {}
    DummyObj() = default;
    DEFINE_RDMA_REF_COUNT_VARIABLE;
    DEFINE_RDMA_REF_COUNT_FUNCTIONS
    ~DummyObj() {}
};

using OBJ_LIFE_CYCLE = enum _o_l_c_ {
    NONE = 0,
    INIT,
    DEINIT
};

using DRIVER_STATE = enum _d_s_ {
    DRIVER_STATE_NONE = 0,
    DRIVER_STATE_INIT = 1 << 0,
    DRIVER_STATE_START = 1 << 1,
    DRIVER_STATE_STOP = 1 << 2,
    DRIVER_STATE_UNINIT = 1 << 3
};

struct NoisyObj {
    OBJ_LIFE_CYCLE &state;
    explicit NoisyObj(OBJ_LIFE_CYCLE &_state) : state(_state)
    {
        state = INIT;
    }
    DEFINE_RDMA_REF_COUNT_VARIABLE;
    DEFINE_RDMA_REF_COUNT_FUNCTIONS
    ~NoisyObj()
    {
        state = DEINIT;
    }
};

#define UT_CHECK_RESULT_TRUE(result) ASSERT_EQ(true, (result));


#define UT_CHECK_RESULT_FALSE(result) ASSERT_EQ(false, (result));

#define UT_CHECK_RESULT_OK(result) ASSERT_EQ(NN_OK, (result));


#define UT_CHECK_RESULT_NOK(result) ASSERT_NE(NN_OK, result);


#define UT_CHECK_RESULT_NOT_NULL(result) ASSERT_NE(nullptr, result);


class UTHelper {
public:
    static bool ServerCreateDriver(UBSHcomNetDriver *&serverDriver, Handlers &handlers,
        UBSHcomNetDriverOptions &options, uint16_t port);
    static bool ClientCreateDriver(UBSHcomNetDriver *&clientDriver, Handlers &handlers,
        UBSHcomNetDriverOptions &options, uint16_t port);
    static bool ClientConnect(UBSHcomNetDriver *clientDriver, UBSHcomNetEndpointPtr &clientEp, uint16_t srvNo = 0,
        uint16_t clientNo = 0);
    static bool ClientSend(UBSHcomNetEndpointPtr &clientEp, sem_t *sem);
    static NResult GetDriver(UBSHcomNetDriver *&driver, DRIVER_STATE state, bool isServer,
        UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::RDMA);
    static NResult GetDriverStateMask(UBSHcomNetDriver *&driver, uint16_t stateMask, bool isServer,
        UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::RDMA);
    static NResult ForwardDriverState(UBSHcomNetDriver *&driver, DRIVER_STATE state);
    static NResult ForwardDriverStateMask(UBSHcomNetDriver *&driver, uint16_t state);
};

#endif // HCOM_UT_HELPER_H
