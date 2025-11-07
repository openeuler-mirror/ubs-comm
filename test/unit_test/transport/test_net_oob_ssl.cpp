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
#include <unistd.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "net_security_rand.h"
#include "transport/net_oob_ssl.h"

namespace ock {
namespace hcom {

class TestNetOobSsl : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestNetOobSsl::SetUp()
{
}

void TestNetOobSsl::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetOobSsl, RunInThread)
{
    NetDriverOobType type = NetDriverOobType::NET_OOB_TCP;
    std::string name = "test";
    UBSHcomTLSPrivateKeyCallback pkCallback = nullptr;
    UBSHcomTLSCertificationCallback ccCallback = nullptr;
    UBSHcomTLSCaCallback caCallback = nullptr;
    OOBSSLServer server {type, name, 0, pkCallback, ccCallback, caCallback};
    server.mOobType = NET_OOB_UDS;
    MOCKER_CPP(std::atomic<bool>::load).stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    server.mNeedStop = true;
    NetExecutorServicePtr es = new (std::nothrow) NetExecutorService(0, 0);
    server.mEs = es;
    EXPECT_NO_FATAL_FAILURE(server.RunInThread());
}

TEST_F(TestNetOobSsl, SendSecret)
{
    OOBSSLConnection conn {-1};
    MOCKER_CPP(NetSecrets::Init).stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    EXPECT_EQ(conn.SendSecret(), static_cast<int>(NN_ERROR));

    MOCKER_CPP(NetSecrets::Serialize).stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    EXPECT_EQ(conn.SendSecret(), static_cast<int>(NN_OOB_SSL_INIT_ERROR));

    EXPECT_EQ(conn.SendSecret(), static_cast<int>(NN_OOB_CONN_SEND_ERROR));
}

TEST_F(TestNetOobSsl, RecvSecret)
{
    OOBSSLConnection *conn = new (std::nothrow) OOBSSLConnection (-1);
    MOCKER_CPP(NetSecrets::Init).stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    EXPECT_EQ(conn->RecvSecret(), static_cast<int>(NN_ERROR));

    MOCKER_CPP(NetSecrets::Deserialize).stubs()
        .will(returnValue(false));

    OOBTCPConnection *tcpConn = static_cast<OOBTCPConnection *>(conn);
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Receive)
        .stubs()
        .will(returnValue(static_cast<int>(NN_OK)));

    EXPECT_EQ(conn->RecvSecret(), static_cast<int>(NN_OOB_SSL_INIT_ERROR));

    if (conn != nullptr) {
        delete conn;
        conn = nullptr;
    }
}

TEST_F(TestNetOobSsl, SSLClientRecvHandler)
{
    OOBSSLConnection conn {-1};
    EXPECT_EQ(conn.SSLClientRecvHandler(-1), static_cast<int>(NN_ERROR));
}

TEST_F(TestNetOobSsl, TlsConnectCbTaskRun)
{
    TlsConnectCbTask task {nullptr, -1, nullptr};
    MOCKER(::send).stubs().will(returnValue(0)).then(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(task.Run());
    EXPECT_NO_FATAL_FAILURE(task.Run());
}

TEST_F(TestNetOobSsl, TestSslRand)
{
    EXPECT_EQ(SecurityRandGenerator::SslRand(nullptr, 0), false);
    SSLAPI::randPrivBytes = [](unsigned char *buf, int num) { return 0; };
    SSLAPI::randStatus = []() { return 0; };
    SSLAPI::randPoll = []() { return 0; };
    void *out = malloc(1);
    EXPECT_EQ(SecurityRandGenerator::SslRand(out, 1), false);
    if (out != nullptr) {
        free(out);
        out = nullptr;
    }
}

TEST_F(TestNetOobSsl, NetSecretsInitSSLRandSecret)
{
    NetSecrets secret {};
    SSLAPI::randPrivBytes = [](unsigned char *buf, int num) { return 1; };
    SSLAPI::randStatus = []() { return 1; };
    SSLAPI::randPoll = []() { return 1; };
    EXPECT_EQ(secret.InitSSLRandSecret(), false);
    secret.mKeySecretLen = 1;
    EXPECT_EQ(secret.InitSSLRandSecret(), true);

    MOCKER_CPP(SecurityRandGenerator::SslRand).stubs()
        .will(returnValue(true))
        .then(returnValue(false))
        .then(returnValue(true))
        .then(returnValue(true))
        .then(returnValue(false))
        .then(returnValue(true));

    EXPECT_EQ(secret.InitSSLRandSecret(), false);
    EXPECT_EQ(secret.InitSSLRandSecret(), false);
    EXPECT_EQ(secret.InitSSLRandSecret(), true);
}

TEST_F(TestNetOobSsl, NetSecretsSerialize)
{
    NetSecrets secret {};

    EXPECT_EQ(secret.Serialize(nullptr, 0), false);

    size_t len = sizeof(uint8_t);
    char *dest = static_cast<char *>(malloc(len));
    EXPECT_EQ(secret.Serialize(dest, len), false);

    if (dest != nullptr) {
        free(dest);
        dest = nullptr;
    }
}

TEST_F(TestNetOobSsl, NetSecretsDeserialize)
{
    NetSecrets secret {};

    EXPECT_EQ(secret.Deserialize(nullptr, 0), false);

    size_t len = sizeof(uint8_t);
    char *dest = static_cast<char *>(malloc(len));
    EXPECT_EQ(secret.Deserialize(dest, len), false);
    if (dest != nullptr) {
        free(dest);
        dest = nullptr;
    }
}
}
}