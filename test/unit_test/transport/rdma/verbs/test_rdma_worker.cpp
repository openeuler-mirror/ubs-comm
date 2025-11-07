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
//#ifdef RDMA_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <unistd.h>
#include <utility>
#include "hcom_utils.h"
#include "net_common.h"
#include "rdma_worker.h"

namespace ock {
namespace hcom {

class TestRdmaWorker : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    RDMAContext *ctx = nullptr;
    RDMAWorker *mWorker = nullptr;
};

void TestRdmaWorker::SetUp()
{
    RDMAGId gid = {};
    ctx = new (std::nothrow) RDMAContext(name, true, gid);
    ASSERT_NE(ctx, nullptr);

    RDMAWorkerOptions options{};
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    mWorker = new (std::nothrow) RDMAWorker(name, ctx, options, memPool, sglMemPool);
    ASSERT_NE(mWorker, nullptr);
}

void TestRdmaWorker::TearDown()
{
    if (mWorker != nullptr) {
        delete mWorker;
        mWorker = nullptr;
    }
    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestRdmaWorker, RdmaWorkerReInitializeCQ)
{
    mWorker->mInited = false;
    int ret = mWorker->ReInitializeCQ();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRdmaWorker, RdmaWorkerReInitializeCQTwo)
{
    RDMACq *tmpCQ = new (std::nothrow) RDMACq(name, ctx, false, 0);
    mWorker->mRDMACq = tmpCQ;
    MOCKER_CPP(&RDMACq::Initialize).stubs().will(returnValue(1)).then(returnValue(0));
    mWorker->mInited = true;
    int ret = mWorker->ReInitializeCQ();
    EXPECT_EQ(ret, 1);
    ret = mWorker->ReInitializeCQ();
    EXPECT_EQ(ret, 0);
    if (tmpCQ != nullptr) {
        delete tmpCQ;
        tmpCQ = nullptr;
    }
}

TEST_F(TestRdmaWorker, RdmaReadRoCEVersionFromFile)
{
    std::string version = "";
    std::string deviceName = "";
    EXPECT_EQ(ReadRoCEVersionFromFile(deviceName, 0, 0, version), static_cast<int>(RR_PARAM_INVALID));
}

}
}
//#endif