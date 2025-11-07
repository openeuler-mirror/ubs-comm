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

#include "net_common.h"
#include "hcom.h"

namespace ock {
namespace hcom {

class TestNetCommon : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestNetCommon::SetUp()
{
}

void TestNetCommon::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetCommon, ParseWorkersGroupsErr)
{
    std::string workerStr;
    for (int i = 0; i < (NN_NO128 + 1); ++i) {
        if (i > 0) {
            workerStr += ",";
        }
        workerStr += "1";
    }
    std::vector<uint16_t> workerGroups;
    bool ret = NetFunc::NN_ParseWorkersGroups(workerStr, workerGroups);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetCommon, ParseWorkerGroupsCpusErr)
{
    std::string workerGroupCpusStr;
    for (int i = 0; i < (NN_NO128 + 1); ++i) {
        if (i > 0) {
            workerGroupCpusStr += ",";
        }
        workerGroupCpusStr += "1-1";
    }
    std::vector<std::pair<uint8_t, uint8_t>> workerGroupCpus;
    bool ret = NetFunc::NN_ParseWorkerGroupsCpus(workerGroupCpusStr, workerGroupCpus);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetCommon, FinalizeWorkerGroupCpusErr)
{
    std::vector<uint16_t> workerGroups = {2, 3};
    std::vector<std::pair<uint8_t, uint8_t>> workerGroupCpus = {{1, 3}, {2, 4}};
    std::vector<int16_t> flatWorkersCpus;
    bool ret = NetFunc::NN_FinalizeWorkerGroupCpus(workerGroups, workerGroupCpus, true, flatWorkersCpus);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetCommon, ParseWorkersGroupsThreadPriorityErr)
{
    std::string threadPriorityStr = "1,2,3";
    int groupNum = NN_NO4;
    std::vector<int16_t> threadPriority;
    bool ret = NetFunc::NN_ParseWorkersGroupsThreadPriority(threadPriorityStr, threadPriority, groupNum);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetCommon, VecstrToStr)
{
    std::vector<std::string> vec{};
    std::string linkStr{};
    std::string result{};
    std::string expect = "testtest2";
    vec.emplace_back("test");
    vec.emplace_back("test2");
    NetFunc::NN_VecStrToStr(vec, linkStr, result);
    EXPECT_EQ(expect, result);
}

TEST_F(TestNetCommon, ConvertIpAndPort)
{
    std::string badUrl = "1.2.3.4";
    std::string badUrl2 = "1.2.3.4:0";
    std::string goodUrl = "1.2.3.4:9981";
    std::string ip{};
    uint16_t port = 0;
    EXPECT_EQ(NetFunc::NN_ConvertIpAndPort(badUrl, ip, port), false);
    EXPECT_EQ(NetFunc::NN_ConvertIpAndPort(badUrl2, ip, port), false);
    EXPECT_EQ(NetFunc::NN_ConvertIpAndPort(goodUrl, ip, port), true);
}

TEST_F(TestNetCommon, SplitProtoUrl)
{
    std::string badUrl = "127.0.0.1:9981";
    std::string testUrl = "tcp://127.0.0.1:9981";
    std::string testUrl2 = "uds://name";
    std::string testUrl3 = "unknown://name";
    std::string testUrl4 = "ubc://1111:2222:0000:0000:0000:0000:0100:0000:888";

    NetProtocol protocol;
    std::string url{};
    EXPECT_EQ(NetFunc::NN_SplitProtoUrl(badUrl, protocol, url), false);
    EXPECT_EQ(NetFunc::NN_SplitProtoUrl(testUrl, protocol, url), true);
    EXPECT_EQ(NetFunc::NN_SplitProtoUrl(testUrl2, protocol, url), true);
    EXPECT_EQ(NetFunc::NN_SplitProtoUrl(testUrl3, protocol, url), false);
    EXPECT_EQ(NetFunc::NN_SplitProtoUrl(testUrl4, protocol, url), true);
    EXPECT_EQ(protocol, NetProtocol::NET_UBC);
    EXPECT_EQ(url, "1111:2222:0000:0000:0000:0000:0100:0000:888");
}

TEST_F(TestNetCommon, ConvertEidAndJettyId)
{
    std::string badUrl1 = "127.0.0.1:8888";
    std::string badUrl2 = "1111:2222:0000:0000:0000:0000:0100:0000";
    std::string badUrl3 = "1111:2222:0000:0000:0000:0000:0100:0000:";
    std::string badUrl4 = "1111:2222:0000:0000:0000:0000:0100:888";
    std::string badUrl5 = "1111:2222:0000:0000:0000:0000:0100:0000:2";
    std::string badUrl6 = "1111:2222:0000:0000:0000:0000:0100:0000:1024";
    std::string badUrl7 = "1111:2222:0000:0000:0000:0000:0100:0000::22";

    std::string testUrl1 = "1111:2222:3333:0000:0000:0000:0000:0000:4";
    std::string testUrl2 = "1111:2222:3333:0000:0000:0000:0000:0000:1023";
    std::string testUrl3 = "1111:2222:0000:0000:0000:0000:0100:0000:888";

    NetProtocol protocol;
    std::string eid;
    uint16_t jettyId = 0;
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl1, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl2, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl3, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl4, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl5, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl6, eid, jettyId), false);
    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(badUrl7, eid, jettyId), false);

    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(testUrl1, eid, jettyId), true);
    EXPECT_EQ(eid, "1111:2222:3333:0000:0000:0000:0000:0000");
    EXPECT_EQ(jettyId, 4);

    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(testUrl2, eid, jettyId), true);
    EXPECT_EQ(eid, "1111:2222:3333:0000:0000:0000:0000:0000");
    EXPECT_EQ(jettyId, 1023);

    EXPECT_EQ(NetFunc::NN_ConvertEidAndJettyId(testUrl3, eid, jettyId), true);
    EXPECT_EQ(eid, "1111:2222:0000:0000:0000:0000:0100:0000");
    EXPECT_EQ(jettyId, 888);
}

TEST_F(TestNetCommon, TestValidateUrl)
{
    EXPECT_EQ(NetFunc::NN_ValidateUrl("!@#$^"), static_cast<uint32_t>(NN_INVALID_PARAM));
    EXPECT_EQ(NetFunc::NN_ValidateUrl(""), static_cast<uint32_t>(NN_INVALID_PARAM));
    EXPECT_EQ(NetFunc::NN_ValidateUrl("tcp://127.0.0.1:8888"), static_cast<uint32_t>(NN_OK));
}
}
}