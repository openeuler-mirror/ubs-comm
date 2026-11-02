/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "capi/hcom_multicast_c.h"

#define NO_SUBSCRIBER_EXIST 501

static const char *g_oobIp = "";
static uint16_t g_oobPort = 9981;
static uint16_t g_driverProtocol = 0;
static char g_ipSeg[128] = "192.168.100.0/24";
static uint32_t g_dataSize = 2048;
static int32_t g_asyncWorkerCpuId = -1;
static bool g_start = false;
static int g_threadNum = 1;
static int g_sendThreadCpuId = -1;
static int g_workerGroupNums = 1;
static int g_verbose = 0;
static int g_userChar = 0;
static uint64_t g_startTime = 0;
static uint64_t g_finishTime = 0;
static int g_pingCount = 500;
static bool g_isBroken = false;
static ubs_hcom_publisher_service g_publisherService = 0;
static ubs_hcom_publisher g_publisher = 0;
static bool g_isCbDone = false;
static bool g_enableTls = true;
static ubs_hcom_service_cipher_suite g_cipherSuite = C_SERVICE_AES_GCM_128;
static const char *g_envCertPath = "TLS_CERT_PATH";
static char g_certPath[512] = "";

static ubs_hcom_memory_region g_localMr = 0;
static uintptr_t g_localAddress = 0;
static uint64_t g_localKey = 0;

static uint64_t NowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int NewSubscriptionCallback(ubs_hcom_subscription_info info, void *usrArg)
{
    (void)usrArg;
    if (g_publisherService == 0 || g_publisher == 0) {
        printf("[ERROR] PublisherService or Publisher is nullptr.\n");
        return -1;
    }

    if (!ubs_hcom_publisher_add_subscription(g_publisher, info)) {
        printf("[ERROR] addSubscription failed.\n");
        return -1;
    }

    printf("[INFO] Publisher addSubscription success, now has %u subscriber.\n",
           ubs_hcom_publisher_get_subscriber_num(g_publisher));
    return 0;
}

static void PublisherSubscriberEpBroken(ubs_hcom_endpoint ep, void *usrArg)
{
    (void)ep;
    (void)usrArg;
    printf("[INFO] publisher ep broken.\n");
    g_isBroken = true;
}

static void EraseKeyPass(char *keyPass, int len)
{
    (void)keyPass;
    (void)len;
}

static int VerifyCert(void *x509, const char *crlPath)
{
    (void)x509;
    (void)crlPath;
    return 0;
}

static int CertCallback(const char *name, char **certPath)
{
    (void)name;
    static char path[600];
    if (snprintf(path, sizeof(path), "%s/server/cert.pem", g_certPath) < 0) {
        return -1;
    }
    *certPath = path;
    return 0;
}

static int PrivateKeyCallback(const char *name, char **priKeyPath, char **keyPass, int *keyPassLen,
                              ubs_hcom_tls_keypass_erase *erase)
{
    (void)name;
    static char path[600];
    static char content[] = "keypass";
    if (snprintf(path, sizeof(path), "%s/server/key.pem", g_certPath) < 0) {
        return -1;
    }
    *priKeyPath = path;
    *keyPass = content;
    *keyPassLen = (int)sizeof(content);
    *erase = EraseKeyPass;
    return 0;
}

static int CACallback(const char *name, char **caPath, char **crlPath, ubs_hcom_peer_cert_verify_type *verifyType,
                      ubs_hcom_tls_cert_verify *verify)
{
    (void)name;
    static char ca[600];
    static char crl[600];
    if (snprintf(ca, sizeof(ca), "%s/CA/cacert.pem", g_certPath) < 0 ||
        snprintf(crl, sizeof(crl), "%s/CA/ca.crl", g_certPath) < 0) {
        return -1;
    }
    *caPath = ca;
    if (access(crl, F_OK) == 0) {
        *crlPath = crl;
    }
    *verifyType = C_VERIFY_BY_DEFAULT;
    *verify = VerifyCert;
    return 0;
}

static bool CreatePublisherService(void)
{
    ubs_hcom_multicast_service_options options;
    memset(&options, 0, sizeof(options));
    options.maxSubscriberNum = 7;
    options.maxSendRecvDataSize = 4096;   /* 若开启TLS，需要设置比datasize略大一些 */
    options.maxSendRecvDataCount = 65535; /* 测试8节点8并发需要设置大一些 */
    options.workerGroupCpuIdsRangeFirst = (g_asyncWorkerCpuId >= 0) ? (uint32_t)g_asyncWorkerCpuId : 0;
    options.workerGroupCpuIdsRangeSecond = (g_asyncWorkerCpuId >= 0) ? (uint32_t)g_asyncWorkerCpuId : 0;
    options.workerGroupId = 0;
    options.workerGroupThreadCount = 1;
    options.workerGroupMode = C_SERVICE_BUSY_POLLING;
    options.qpRecvQueueSize = 4096;
    options.qpSendQueueSize = 4096;
    options.qpPrePostSize = 2048;
    options.qpBatchRePostSize = 20;
    options.completionQueueDepth = 16384; /* 测试8节点8并发需要设置大一些 */
    options.enableTls = g_enableTls;
    options.cipherSuite = g_cipherSuite;
    options.periodicCpuId = -1; /* 实际业务根据需要绑定超时定时器线程cpuId */
    options.protocol = (g_driverProtocol == 1) ? C_MULTICAST_TCP : C_MULTICAST_RDMA;

    if (ubs_hcom_publisher_service_create("Publisher", &options, &g_publisherService) != 0) {
        printf("[ERROR] Failed to create service.\n");
        return false;
    }

    for (int i = 1; i < g_workerGroupNums; i++) {
        ubs_hcom_publisher_service_add_workergroup(g_publisherService, (uint16_t)i, 1,
                                                   (uint32_t)(g_asyncWorkerCpuId + i),
                                                   (uint32_t)(g_asyncWorkerCpuId + i), 0);
    }

    char url[256];
    if (snprintf(url, sizeof(url), "tcp://%s:%u", g_oobIp, g_oobPort) < 0) {
        return false;
    }

    ubs_hcom_multicast_config config = ubs_hcom_publisher_service_get_config(g_publisherService);
    const char *ipMasks[1] = {g_ipSeg};
    ubs_hcom_multicast_config_set_device_ip_mask(config, ipMasks, 1);
    if (ubs_hcom_publisher_service_bind(g_publisherService, url, NewSubscriptionCallback, NULL, -1) != 0) {
        printf("[ERROR] Failed to bind service.\n");
        return false;
    }
    ubs_hcom_publisher_service_register_broken_handler(g_publisherService, PublisherSubscriberEpBroken, NULL);

    if (g_enableTls) {
        ubs_hcom_publisher_service_register_tls_callbacks(g_publisherService, CertCallback, PrivateKeyCallback,
                                                          CACallback);
    }

    printf("[INFO] PublisherService Created!\n");
    return true;
}

static bool StartPublisherService(void)
{
    if (g_publisherService == 0) {
        printf("[ERROR] PublisherService is nullptr.\n");
        return false;
    }
    if (ubs_hcom_publisher_service_start(g_publisherService) != 0) {
        printf("[ERROR] Failed to start PublisherService.\n");
        return false;
    }
    printf("[INFO] PublisherService Started!\n");
    return true;
}

static bool CreatePublisher(void)
{
    return ubs_hcom_publisher_service_create_publisher(g_publisherService, &g_publisher) == 0;
}

static void RegisterMem(void)
{
    if (ubs_hcom_publisher_service_register_memory_region(g_publisherService, g_dataSize, &g_localMr) != 0) {
        printf("[ERROR] Failed to register memory region.\n");
        return;
    }

    ubs_hcom_memory_region_info mrInfo;
    memset(&mrInfo, 0, sizeof(mrInfo));
    if (ubs_hcom_driver_get_memory_region_info(g_localMr, &mrInfo) != 0) {
        printf("[ERROR] Failed to get memory region info.\n");
        return;
    }
    g_localAddress = mrInfo.lAddress;
    g_localKey = mrInfo.lKey;
}

static void CallDoneCallback(ubs_hcom_publisher_context ctx, void *usrArg)
{
    (void)usrArg;
    if (g_verbose != 1) {
        __atomic_store_n(&g_isCbDone, true, __ATOMIC_RELEASE);
        return;
    }

    uint32_t count = ubs_hcom_publisher_context_get_subscriber_count(ctx);
    printf("[INFO] pubCtx subscriber size %u\n", count);
    for (uint32_t i = 0; i < count; i++) {
        ubs_hcom_multicast_subscriber_rsp_info info;
        memset(&info, 0, sizeof(info));
        if (ubs_hcom_publisher_context_get_subscriber_rsp_info(ctx, i, &info) != 0) {
            continue;
        }
        printf("[INFO] pubCtx subscribe status %d subInfo id %llu name %s subscriber data size %u data msg %s\n",
               (int)info.status, (unsigned long long)ubs_hcom_subscription_info_get_id(info.subInfo),
               ubs_hcom_subscription_info_get_name(info.subInfo), info.rspSize, (char *)info.rspData);
    }

    __atomic_store_n(&g_isCbDone, true, __ATOMIC_RELEASE);
}

static void MultiCast(void)
{
    ubs_hcom_multicast_op_info opInfo;
    memset(&opInfo, 0, sizeof(opInfo));
    opInfo.timeout = 10;

    ubs_hcom_multicast_request req;
    req.data = (void *)g_localAddress;
    req.size = (uint32_t)g_dataSize;
    req.lkey = g_localKey;

    int callRes = ubs_hcom_publisher_call(g_publisher, opInfo, req, CallDoneCallback, NULL);
    if (callRes == NO_SUBSCRIBER_EXIST) {
        __atomic_store_n(&g_isCbDone, true, __ATOMIC_RELEASE);
    }
}

static void *RunInThread(void *arg)
{
    int coreId = *(int *)arg;
    if (coreId >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreId, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    while (!g_start) {
        usleep(1);
    }
    switch (g_userChar) {
        case '0':
            for (int32_t i = 0; i < g_pingCount; i++) {
                MultiCast();
                while (!__atomic_load_n(&g_isCbDone, __ATOMIC_ACQUIRE) && !g_isBroken) {}
                __atomic_store_n(&g_isCbDone, false, __ATOMIC_RELEASE);
                if (g_isBroken) {
                    g_isBroken = false;
                    return NULL;
                }
            }
            break;
        default:
            break;
    }
    return NULL;
}

static void ListAllSubscribers(void)
{
    uint32_t count = 0;
    if (ubs_hcom_publisher_get_all_subscriber_info(g_publisher, NULL, &count) != 0 || count == 0) {
        printf("[INFO] no subscriber.\n");
        return;
    }

    ubs_hcom_subscription_info *infos =
        (ubs_hcom_subscription_info *)malloc(sizeof(ubs_hcom_subscription_info) * (size_t)count);
    if (infos == NULL) {
        return;
    }

    uint32_t capacity = count;
    if (ubs_hcom_publisher_get_all_subscriber_info(g_publisher, infos, &capacity) == 0) {
        for (uint32_t i = 0; i < count; i++) {
            printf("[INFO] subInfo id %llu name %s ip %s port %u\n",
                   (unsigned long long)ubs_hcom_subscription_info_get_id(infos[i]),
                   ubs_hcom_subscription_info_get_name(infos[i]), ubs_hcom_subscription_info_get_ip(infos[i]),
                   (unsigned)ubs_hcom_subscription_info_get_port(infos[i]));
        }
    }
    free(infos);
}

static void PrintSummary(void)
{
    printf("\tMultiCall Perf summary pingCount %d \n", g_pingCount);
    printf("\tMultiCall Thread count:\t\t%d\n", g_threadNum);
    printf("\tMultiCall postSend Total time(us):\t\t%f\n", (g_finishTime - g_startTime) / 1000.0);
    printf("\tMultiCall postSend Total time(ms):\t\t%f\n", (g_finishTime - g_startTime) / 1000000.0);
    printf("\tMultiCall postSend Total time(s):\t\t%f\n", (g_finishTime - g_startTime) / 1000000000.0);
    printf("\tMultiCall postSend Latency(us):\t\t%f\n", (g_finishTime - g_startTime) / g_pingCount / 1000.0);
    printf("\tMultiCall postSend Avg ops:\t\t%f pp/s\n",
           (g_pingCount * 1000000000.0) / (g_finishTime - g_startTime) * g_threadNum);
}

static void Test(void)
{
    printf("[INFO] input 0:multicast, l:list all subscriber, q mean quit!\n");
    while (1) {
        g_userChar = getchar();

        pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)g_threadNum);
        int *cpuIds = (int *)malloc(sizeof(int) * (size_t)g_threadNum);
        if (threads == NULL || cpuIds == NULL) {
            free(threads);
            free(cpuIds);
            return;
        }

        g_start = false;
        g_startTime = NowNs();
        for (int i = 0; i < g_threadNum; ++i) {
            int cpuId = -1;
            if (g_sendThreadCpuId > 0) {
                cpuId = g_sendThreadCpuId + i;
            }
            cpuIds[i] = cpuId;
            pthread_create(&threads[i], NULL, RunInThread, &cpuIds[i]);
        }
        printf("[INFO] Wait for finish\n");
        g_start = true;
        for (int i = 0; i < g_threadNum; ++i) {
            pthread_join(threads[i], NULL);
        }
        free(threads);
        free(cpuIds);

        switch (g_userChar) {
            case '0':
                break;
            case 'l':
                ListAllSubscribers();
                continue;
            case 'q':
                return;
            default:
                printf("[INFO] input 0:multicast, l:list all subscriber, q mean quit\n");
                continue;
        }

        g_finishTime = NowNs();
        PrintSummary();
    }
}

static int ValidateTlsCert(void)
{
    const char *envCertPath = getenv(g_envCertPath);
    if (envCertPath == NULL) {
        printf("[ERROR] env for TLS cert is not set, set %s\n", g_envCertPath);
        return -1;
    }

    if (snprintf(g_certPath, sizeof(g_certPath), "%s", envCertPath) < 0) {
        return -1;
    }

    char path[600];
    if (snprintf(path, sizeof(path), "%s/CA/cacert.pem", g_certPath) < 0 || access(path, F_OK) != 0) {
        printf("[ERROR] cacert.pem cannot be found under %s\n", g_certPath);
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/server/cert.pem", g_certPath) < 0 || access(path, F_OK) != 0) {
        printf("[ERROR] cert.pem cannot be found under %s\n", g_certPath);
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/server/key.pem", g_certPath) < 0 || access(path, F_OK) != 0) {
        printf("[ERROR] key.pem cannot be found under %s\n", g_certPath);
        return -1;
    }

    return 0;
}

static void MultiCastTest(void)
{
    if (g_enableTls) {
        if (ValidateTlsCert() != 0) {
            return;
        }
    }
    if (!CreatePublisherService()) {
        return;
    }
    StartPublisherService();
    CreatePublisher();

    RegisterMem();
    Test();

    ubs_hcom_publisher_service_destroy_publisher(g_publisherService, g_publisher);
    ubs_hcom_publisher_service_destroy(g_publisherService, "Publisher");
}

int main(int argc, char *argv[])
{
    struct option options[] = {
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"driver", required_argument, NULL, 'd'},
        {"pingpongtimes", required_argument, NULL, 't'},
        {"size", required_argument, NULL, 's'},
        {"workerCpuId", required_argument, NULL, 'c'},
        {"multiSendCpuId", required_argument, NULL, 'm'},
        {"threadnums", required_argument, NULL, 'n'},
        {"workernums", required_argument, NULL, 'w'},
        {"verbose", required_argument, NULL, 'v'},
        {"TLS enabled", required_argument, NULL, 'T'},
        {"cipherSuite", required_argument, NULL, 'C'},
        {NULL, 0, NULL, 0},
    };

    const char *usage =
        "usage\n"
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

    while ((ret = getopt_long(argc, argv, "i:p:d:t:s:c:m:n:w:v:T:C:", options, &index)) != -1) {
        switch (ret) {
            case 'i':
                g_oobIp = optarg;
                if (snprintf(g_ipSeg, sizeof(g_ipSeg), "%s/24", g_oobIp) < 0) {
                    return 0;
                }
                break;
            case 'p':
                g_oobPort = (uint16_t)strtoul(optarg, NULL, 0);
                break;
            case 'd':
                g_driverProtocol = (uint16_t)strtoul(optarg, NULL, 0);
                break;
            case 't':
                g_pingCount = (int)strtoul(optarg, NULL, 0);
                break;
            case 's':
                g_dataSize = (uint32_t)strtoul(optarg, NULL, 0);
                break;
            case 'c':
                g_asyncWorkerCpuId = (int32_t)(int16_t)strtoul(optarg, NULL, 0);
                break;
            case 'm':
                g_sendThreadCpuId = (int)(int16_t)strtoul(optarg, NULL, 0);
                break;
            case 'n':
                g_threadNum = (int)strtoul(optarg, NULL, 0);
                break;
            case 'w':
                g_workerGroupNums = (int)strtoul(optarg, NULL, 0);
                break;
            case 'v':
                g_verbose = (int)strtoul(optarg, NULL, 0);
                break;
            case 'T':
                g_enableTls = (strtoul(optarg, NULL, 0) != 0);
                break;
            case 'C':
                g_cipherSuite = (ubs_hcom_service_cipher_suite)strtoul(optarg, NULL, 0);
                break;
            default:
                printf("%s", usage);
                return 0;
        }
    }

    MultiCastTest();
    getchar();

    return 0;
}