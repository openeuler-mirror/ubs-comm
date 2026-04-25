/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <unistd.h>
#include <thread>
#include <getopt.h>
#include <cstdio>
#include <sched.h>
#include <pthread.h>
#include <atomic>

#include "multicast/multicast_publisher_service.h"
#include "multicast/multicast_publisher.h"

using namespace ock::hcom;

constexpr uint16_t NO_SUBSCRIBER_EXIST = 501;

std::string g_oobIp = "";
uint16_t g_oobPort = 9981;
uint16_t g_driverProtocol = 0;
std::string g_ipSeg = "192.168.100.0/24";
int32_t g_dataSize = 2048;
int16_t g_asyncWorkerCpuId = -1;
bool g_start = false;
int g_threadNum = 1;
int g_sendThreadCpuId = -1;
int g_workerGroupNums = 1;
int g_verbose = 0;
int g_userChar = 0;
uint64_t g_startTime = 0;
uint64_t g_finishTime = 0;
int g_pingCount = 500;
bool g_isBroken = false;
PublisherService *g_publisherService = nullptr;
NetRef<ock::hcom::Publisher> g_publisher = nullptr;
std::atomic<bool> g_isCbDone { false };
bool g_enableTls = true;
CipherSuite g_cipherSuite = AES_GCM_128;
std::string g_envCertPath = "TLS_CERT_PATH";
std::string g_certPath = "";

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint64_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));
TestRegMrInfo g_localMrInfo;

bool NewSubscriptionCallBack(ock::hcom::SubscriptionInfoPtr &info)
{
    if (g_publisherService == nullptr || g_publisher == nullptr) {
        NN_LOG_ERROR("PublisherService or Publisher is nullptr.");
        return -1;
    }

    if (!g_publisher->AddSubscription(info)) {
        NN_LOG_ERROR("addSubscription failed.");
        return -1;
    }

    std::vector<SubscriptionInfoPtr> subInfo = g_publisher->GetAllSubscriberInfo();
    NN_LOG_INFO("Publisher addSubscription success, now has " << subInfo.size() << " subscriber.");
    return 0;
}

void PublisherSubscriberEpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("publisher ep broken id " << ep->Id());
    auto info = g_publisher->GetSubscribeByEpId(ep->Id());
    g_publisher->DelSubscription(info);
    g_isBroken = true;
}

static int DefaultNewEp(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
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
    value = g_certPath + "/server/cert.pem";
    return true;
}

bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "keypass";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = g_certPath + "/server/key.pem";
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

bool CreatePublisherService()
{
    ock::hcom::MulticastServiceOptions options;
    options.maxSubscriberNum = 7;
    options.maxSendRecvDataSize = 4096;   // 若开启TLS，需要设置比datasize略大一些
    options.maxSendRecvDataCount = 65535; // 测试8节点8并发需要设置大一些
    options.workerGroupCpuIdsRange = std::make_pair(g_asyncWorkerCpuId, g_asyncWorkerCpuId);
    options.workerGroupId = 0;
    options.workerGroupThreadCount = 1;
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    options.qpRecvQueueSize = 4096;
    options.qpSendQueueSize = 4096;
    options.qpPrePostSize = 2048;
    options.qpBatchRePostSize = 20;
    options.completionQueueDepth = 16384; // 测试8节点8并发需要设置大一些
    options.enableTls = g_enableTls;
    options.cipherSuite = g_cipherSuite;
    options.periodicCpuId = -1;  // 实际业务根据需要绑定超时定时器线程cpuId
    if (g_driverProtocol == 1) {
        options.protocol = UBSHcomNetDriverProtocol::TCP;
    }
    g_publisherService = ock::hcom::PublisherService::Create("Publisher", options);
    if (g_publisherService == nullptr) {
        NN_LOG_ERROR("Failed to create service.");
        return false;
    }

    for (int i = 1; i < g_workerGroupNums; i++) {
        g_publisherService->AddWorkerGroup(i, 1, std::make_pair(g_asyncWorkerCpuId + i, g_asyncWorkerCpuId + i), 0);
    }

    std::string url = "tcp://" + g_oobIp + ":" + std::to_string(g_oobPort);

    g_publisherService->GetConfig().SetDeviceIpMask({ g_ipSeg });
    g_publisherService->Bind(url, NewSubscriptionCallBack, -1);
    g_publisherService->RegisterBrokenHandler(PublisherSubscriberEpBroken);

    if (g_enableTls) {
        g_publisherService->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        g_publisherService->RegisterTLSCertificationCallback(
            std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
        g_publisherService->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    }

    NN_LOG_INFO("PublisherService Created!");
    return true;
}

bool StartPublisherService()
{
    if (g_publisherService == nullptr) {
        NN_LOG_ERROR("PublisherService is nullptr.");
        return false;
    }
    if (g_publisherService->Start() != 0) {
        NN_LOG_ERROR("Failed to start NetService.");
        return false;
    }
    NN_LOG_INFO("PublisherService Started!");
    return true;
}

bool CreatePublisher()
{
    if (g_publisherService->CreatePublisher(g_publisher) == ock::hcom::SER_OK) {
        return true;
    }
    return false;
}

int PostSendRequest(UBSHcomServiceContext context)
{
    return 0;
}

void ListAllSubscribers()
{
    std::vector<SubscriptionInfoPtr> subInfos = g_publisher->GetAllSubscriberInfo();
    for (const auto &subInfo : subInfos) {
        NN_LOG_INFO("subInfo id " << subInfo->GetId() << " name " << subInfo->GetName() << " ip " <<
            subInfo->GetIp() << " port " << subInfo->GetPort());
    }
}

void MultiCast()
{
    UBSHcomNetTransOpInfo opInfo;
    opInfo.timeout = 10;
    uint16_t callRes = 0;

    MultiCastCallback *newCallback = NewMultiCastCallback(
        [](PublisherContext &context) {
            if (g_verbose != 1) {
                g_isCbDone.store(true);
                return;
            }
            const auto& infos = context.GetSubscriberRspInfo();
            NN_LOG_DEBUG("pubCtx subscriber size " << infos.size());
            for (const auto& info : infos) {
                auto status = info.GetStatus();
                auto subInfo = info.GetSubInfos();
                auto msgInfo = info.GetMultiResponse();
                NN_LOG_INFO("pubCtx subscribe status " << static_cast<int>(status) << " subInfo id " <<
                    (subInfo.Get() != nullptr ? subInfo->GetId() : 0) << " name " <<
                    (subInfo.Get() != nullptr ? subInfo->GetName() : "") << " subscriber data size " <<
                    msgInfo.size << " data msg " << reinterpret_cast<char *>(msgInfo.data));
            }

            g_isCbDone.store(true);
        },
        std::placeholders::_1);
    if (newCallback == nullptr) {
        NN_LOG_ERROR("Async call malloc callback failed");
        return;
    }

    MultiRequest req(reinterpret_cast<void*>(g_localMrInfo.lAddress), g_localMrInfo.size, g_localMrInfo.lKey);
    callRes = g_publisher->Call(opInfo, req, newCallback);
    if (callRes == NO_SUBSCRIBER_EXIST) {
        g_isCbDone.store(true);
    }
}

void RunInThread(int coreId)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    while (!g_start) {
        usleep(1);
    }
    switch (g_userChar) {
        case '0':
            for (int32_t i = 0; i < g_pingCount; i++) {
                MultiCast();
                while (!g_isCbDone.load() && !g_isBroken) {
                }
                g_isCbDone.store(false);
                if (g_isBroken) {
                    g_isBroken = false;
                    return;
                }
            }
            break;
        default:
            return;
    }
}

void Test()
{
    NN_LOG_INFO("input 0:mullticast, q mean quit!");
    while (true) {
        g_userChar = getchar();

        std::vector<std::thread> threads(g_threadNum);
        g_start = false;
        g_startTime = MONOTONIC_TIME_NS();
        for (int i = 0; i < g_threadNum; ++i) {
            int cpuId = -1;
            if (g_sendThreadCpuId > 0) {
                cpuId = g_sendThreadCpuId + i;
            }
            threads[i] = std::thread(RunInThread, cpuId);
        }
        NN_LOG_INFO("Wait for finish");
        g_start = true;
        for (auto &t : threads) {
            t.join();
        }

        switch (g_userChar) {
            case '0':
                break;
            case 'l':
                ListAllSubscribers();
                continue;
            case 'q':
                return;
            default:
                NN_LOG_INFO("input 0:multicast, l:list all subscriber, q mean quit");
                continue;
        }

        if (g_userChar == 'd' || g_userChar == 'c' || g_userChar == 'r') {
            continue;
        }

        g_finishTime = MONOTONIC_TIME_NS();

        printf("\tMultiCall Perf summary pingCount %d \n", g_pingCount);
        printf("\tMultiCall Thread count:\t\t%d\n", g_threadNum);
        printf("\tMultiCall postSend Total time(us):\t\t%f\n", (g_finishTime - g_startTime) / 1000.0);
        printf("\tMultiCall postSend Total time(ms):\t\t%f\n", (g_finishTime - g_startTime) / 1000000.0);
        printf("\tMultiCall postSend Total time(s):\t\t%f\n", (g_finishTime - g_startTime) / 1000000000.0);
        printf("\tMultiCall postSend Latency(us):\t\t%f\n", (g_finishTime - g_startTime) / g_pingCount / 1000.0);
        printf("\tMultiCall postSend Avg ops:\t\t%f pp/s\n",
            (g_pingCount * 1000000000.0) / (g_finishTime - g_startTime) * g_threadNum);
    }
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

    if (::access((g_certPath + "/server/cert.pem").c_str(), F_OK) != 0) {
        NN_LOG_ERROR("cert.pem cannot be found under " << g_certPath);
        return -1;
    }

    if (::access((g_certPath + "/server/key.pem").c_str(), F_OK) != 0) {
        NN_LOG_ERROR("key.pem cannot be found under " << g_certPath);
        return -1;
    }

    return 0;
}

void RegisterMem()
{
    UBSHcomNetMemoryRegionPtr mr;
    g_publisherService->RegisterMemoryRegion(g_dataSize, mr);
    g_localMrInfo.lAddress = mr->GetAddress();
    g_localMrInfo.lKey = mr->GetLKey();
    g_localMrInfo.size = g_dataSize;
}

void MultiCastTest()
{
    if (g_enableTls) {
        if (ValidateTlsCert() != 0) {
            return;
        }
    }
    CreatePublisherService();
    StartPublisherService();
    CreatePublisher();

    RegisterMem();
    Test();

    g_publisherService->DestroyPublisher(g_publisher);
    ock::hcom::PublisherService::Destroy("Publisher");
}


int main(int argc, char *argv[])
{
    struct option options[] = {
        {"ip", required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"driver", required_argument, nullptr, 'd'},
        {"pingpongtimes", required_argument, nullptr, 't'},
        {"size", required_argument, nullptr, 's'},
        {"workerCpuId", required_argument, nullptr, 'c'},
        {"multiSendCpuId", required_argument, nullptr, 'm'},
        {"threadnums", required_argument, nullptr, 'n'},
        {"workernums", required_argument, nullptr, 'w'},
        {"verbose", required_argument, nullptr, 'v'},
        {"TLS enabled", required_argument, nullptr, 'T'},
        {"cipherSuite", required_argument, nullptr, 'C'},
        {nullptr, 0, nullptr, 0},
    };

    const char *usage = "usage\n"
        "        -i, --ip,                     coord server ip mask, e.g. 10.175.118.1;\n"
        "        -p, --port,                   coord server port, by default 9981; jetty id for UBC, e.g. 998\n"
        "        -d, --driver,                 multicast driver protocol, 0 means RDMA, 1 means TCP\n"
        "        -t, --pingpongtimes,          ping pong times\n"
        "        -s, --size,                   max data size\n"
        "        -c, --workerCpuId,            worker cpu to bind\n"
        "        -m, --multiSendCpuId,         multicast send thread cpu to bind\n"
        "        -n, --threadnums,             multicast send thread nums\n"
        "        -w, --workerGroupNums         publisher worker group nums\n"
        "        -v, --verbose                 verbose for detail\n"
        "        -T, --TLS enabled             TLS enabled\n"
        "        -C, --cipherSuite             cipherSuite, 0 means AES_GCM_128, 1 means AES_GCM_256, "
        "2 means AES_CCM_128, 3 means CHACHA20_POLY1305 \n";

    int ret = 0;
    int index = 0;

    std::string str = "i:p:d:t:s:c:m:n:w:v:T:C:";
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) {
        switch (ret) {
            case 'i':
                g_oobIp = optarg;
                g_ipSeg = g_oobIp + "/24";
                break;
            case 'p':
                g_oobPort = static_cast<uint16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'd':
                g_driverProtocol = static_cast<uint16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 't':
                g_pingCount = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 's':
                g_dataSize = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'c':
                g_asyncWorkerCpuId = static_cast<int16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'm':
                g_sendThreadCpuId = static_cast<int16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'n':
                g_threadNum = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'w':
                g_workerGroupNums = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'v':
                g_verbose = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
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

    MultiCastTest();
    getchar();

    return 0;
}