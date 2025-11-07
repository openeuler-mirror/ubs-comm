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
#ifdef UB_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/poll.h>
#include <fcntl.h>

#include "hcom.h"
#include "ub_common.h"
#include "net_ub_driver_oob.h"
#include "net_ub_endpoint.h"

#include "net_monotonic.h"
#include "net_security_alg.h"
#include "hcom_utils.h"
#include "ub_urma_wrapper_jetty.h"

namespace ock {
namespace hcom {
class TestNetUBOobDriver : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    UBEId ubEid{};
    NetDriverUBWithOob *mDriver = nullptr;
    UBContext *ctx = nullptr;
    UBWorker *mWorker = nullptr;
    UBJfc *cq = nullptr;
    UBJetty *qp = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
    NetUBAsyncEndpoint *NEP = nullptr;
    UBMemoryRegionFixedBuffer *Mr = nullptr;
};

void TestNetUBOobDriver::SetUp()
{
    bool startOobSvr = false;
    UBSHcomNetDriverProtocol protocol = UBC;
    mDriver = new (std::nothrow) NetDriverUBWithOob(name, startOobSvr, protocol);
    ASSERT_NE(mDriver, nullptr);
}

void TestNetUBOobDriver::TearDown()
{
    GlobalMockObject::verify();
    if (mDriver != nullptr) {
        delete mDriver;
        mDriver = nullptr;
    }
}

TEST_F(TestNetUBOobDriver, NetUBDriverRunInUbEventThread)
{
    mDriver->mNeedStopEvent = true;

    UBContext *ubCtx = nullptr;
    int result = UBContext::Create(name, ubEid, ubCtx);
    ASSERT_EQ(result, 0);

    ubCtx->mUrmaContext = new (std::nothrow) urma_context_t();
    ASSERT_NE(ubCtx->mUrmaContext, nullptr);

    mDriver->mContext = ubCtx;
    EXPECT_NO_FATAL_FAILURE(mDriver->RunInUbEventThread());

    urma_context_t *urmaContext = nullptr;
    MOCKER_CPP(&UBContext::GetContext).stubs().will(returnValue(urmaContext));
    EXPECT_NO_FATAL_FAILURE(mDriver->RunInUbEventThread());

    delete ubCtx->mUrmaContext;
    ubCtx->mUrmaContext = nullptr;
}
}
}
#endif