/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <thread>
#include <unistd.h>
#include <getopt.h>
#include <cstdio>
#include "securec.h"

#include "multicast/multicast_subscriber_service.h"
#include "multicast/multicast_subscriber.h"

using namespace ock::hcom;
int g_userChar = 0;
std::string g_oobIp = "";
uint16_t g_oobPort = 9981;

std::string g_ipSeg = "192.168.100.0/24";
int32_t g_dataSize = 64;
int16_t g_asyncWorkerCpuId = -1;
uint8_t g_serverGroupNo = 0;

bool g_enableTls = true;
CipherSuite g_cipherSuite = AES_GCM_128;
std::string g_envCertPath = "TLS_CERT_PATH";
std::string g_certPath = "";

ock::hcom::SubscriberService *g_subscriberService = nullptr;
ock::hcom::NetRef<ock::hcom::Subscriber> g_subScriber = nullptr;
void BrokenSubscriber(const UBSHcomNetEndpointPtr &ep)
{
    std::string ip;
    uint16_t port;
    ep->GetPeerIpPort(ip, port);
    NN_LOG_INFO("ep is broken remote ip " << ip << " remote port " << port);
}

static int DefaultNewEp(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    return 0;
}

int ReceivedRequest(UBSHcomServiceContext &context)
{
    auto subCtx = dynamic_cast<SubscriberContext *>(&context);
    if (subCtx == nullptr) {
        return -1;
    }

    char data[g_dataSize];
    if (memset_s(data, sizeof(data), 'A', g_dataSize) != 0) {
        return -1;
    }
    MultiRequest req(static_cast<void *>(data), g_dataSize);
    subCtx->Reply(req);

    return 0;
}

int PostSendRequest(UBSHcomServiceContext context)
{
    return 0;
}

void Erase(void *pass, int len) {}
int Verify(void *x509, const char *path)
{
    return 0;
}

bool CertCallback(const std::string &name, std::string &value)
{
    value = g_certPath + "/client/cert.pem";
    return true;
}

bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "keypass";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = g_certPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = g_certPath + "/CA/cacert.pem";
    std::string crlFile = g_certPath + "/CA/ca.crl";
    char buffer[PATH_MAX] = {0};
    if (realpath(crlFile.c_str(), buffer) != nullptr) {
        crlPath = crlFile;
    }
    peerCertVerifyType = VERIFY_BY_DEFAULT;
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}

bool CreateSubscriberService()
{
    ock::hcom::MulticastServiceOptions options;
    options.maxSendRecvDataSize = 4096;
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    options.workerGroupCpuIdsRange = std::make_pair(g_asyncWorkerCpuId, g_asyncWorkerCpuId);
    options.workerGroupId = 1;
    options.qpRecvQueueSize = 4096;
    options.qpSendQueueSize = 4096;
    options.qpPrePostSize = 2048;
    options.qpBatchRePostSize = 10;
    options.publisherWrkGroupNo = g_serverGroupNo;
    options.enableTls = g_enableTls;
    options.cipherSuite = g_cipherSuite;

    g_subscriberService = ock::hcom::SubscriberService::Create("Subscriber", options);
    if (g_subscriberService == nullptr) {
        NN_LOG_ERROR("Failed to create service.");
        return false;
    }

    g_subscriberService->GetConfig().SetDeviceIpMask({ g_ipSeg });
    g_subscriberService->RegisterRecvHandler(ReceivedRequest);
    g_subscriberService->RegisterBrokenHandler(BrokenSubscriber);

    if (g_enableTls) {
        g_subscriberService->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        g_subscriberService->RegisterTLSCertificationCallback(
            std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
        g_subscriberService->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    }
    NN_LOG_INFO("SubscriberService Created!");
    return true;
}

bool StartSubscriberService()
{
    if (g_subscriberService == nullptr) {
        NN_LOG_ERROR("SubscriberService is nullptr.");
        return false;
    }
    if (g_subscriberService->Start() != 0) {
        NN_LOG_ERROR("Failed to start NetService.");
        return false;
    }
    NN_LOG_INFO("SubscriberService Started!");
    return true;
}

bool CreateSubscriber()
{
    std::string url = "tcp://" + g_oobIp + ":" + std::to_string(g_oobPort);
    if (g_subscriberService->CreateSubscriber(url, g_subScriber) == ock::hcom::SER_OK) {
        NN_LOG_INFO("CreateSubscriber Success!");
        return true;
    }
    NN_LOG_ERROR("CreateSubscriber Failed!");
    return false;
}

int ValidateTlsCert()
{
    char *envCertPath = ::getenv(g_envCertPath.c_str());
    if (envCertPath == nullptr) {
        NN_LOG_ERROR("env for TLS cert is not set, set " << g_envCertPath);
        return -1;
    }

    g_certPath = envCertPath;
    if (::access((g_certPath + "/CA/cacert.pem").c_str(), F_OK) != 0) {
        NN_LOG_ERROR("cacert.pem cannot be found under " << g_certPath);
        return -1;
    }

    if (::access((g_certPath + "/client/cert.pem").c_str(), F_OK) != 0) {
        NN_LOG_ERROR("cert.pem cannot be found under " << g_certPath);
        return -1;
    }

    if (::access((g_certPath + "/client/key.pem").c_str(), F_OK) != 0) {
        NN_LOG_ERROR("key.pem cannot be found under " << g_certPath);
        return -1;
    }

    return 0;
}

void Test()
{
    int ret;
    while (true) {
        g_userChar = getchar();

        switch (g_userChar) {
            case 'c':
                NN_LOG_INFO("Begin to close Subscriber");
                g_subScriber->Close();
                continue;
            case 'q':
                g_subscriberService->Stop();
                return;
            default:
                NN_LOG_INFO("input c:close subscriber, q mean quit");
                continue;
        }
    }
}

int main(int argc, char *argv[])
{
    struct option options[] = {
        {"ip", required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"size", required_argument, nullptr, 's'},
        {"cpuId", required_argument, nullptr, 'c'},
        {"TLS enabled", required_argument, nullptr, 'T'},
        {"cipherSuite", required_argument, nullptr, 'C'},
        {nullptr, 0, nullptr, 0},
    };

    const char *usage = "usage\n"
        "        -i, --ip,                     coord server ip mask, e.g. 10.175.118.1;\n"
        "        -p, --port,                   coord server port, by default 9981; jetty id for UBC, e.g. 998\n"
        "        -s, --size,                   max data size\n"
        "        -c, --cpuId,                  cpu to bind\n"
        "        -g, --serverWkrGroupNo,       server worker group no, default is 0\n"
        "        -T, --TLS enabled,            TLS enabled, default is false\n"
        "        -C, --cipherSuite             cipherSuite, 0 means AES_GCM_128, 1 means AES_GCM_256, "
        "2 means AES_CCM_128, 3 means CHACHA20_POLY1305 \n";

    int ret = 0;
    int index = 0;

    std::string str = "i:p:s:c:g:T:C:";
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) {
        switch (ret) {
            case 'i':
                g_oobIp = optarg;
                g_ipSeg = g_oobIp + "/24";
                break;
            case 'p':
                g_oobPort = static_cast<uint16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 's':
                g_dataSize = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'c':
                g_asyncWorkerCpuId = static_cast<int16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'g':
                g_serverGroupNo = static_cast<uint8_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'T':
                g_enableTls = static_cast<bool>(strtoul(optarg, nullptr, 0));
                break;
            case 'C':
                g_cipherSuite = static_cast<CipherSuite>(strtoul(optarg, nullptr, 0));
                break;
            default:
                NN_LOG_ERROR("unexpected options");
                return 0;
        }
    }

    if (g_enableTls) {
        if (ValidateTlsCert() != 0) {
            return 0;
        }
    }

    CreateSubscriberService();
    StartSubscriberService();
    CreateSubscriber();

    Test();

    g_subscriberService->DestroySubscriber(g_subScriber);
    ock::hcom::SubscriberService::Destroy("Subscriber");

    return 0;
}