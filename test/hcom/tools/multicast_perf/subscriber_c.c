/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "capi/hcom_multicast_c.h"

static int g_userChar = 0;
static const char *g_oobIp = "";
static uint16_t g_oobPort = 9981;
static uint16_t g_driverProtocol = 0;
static char g_ipSeg[128] = "192.168.100.0/24";
static uint32_t g_dataSize = 64;
static int32_t g_asyncWorkerCpuId = -1;
static uint8_t g_serverGroupNo = 0;
static bool g_enableTls = true;
static ubs_hcom_service_cipher_suite g_cipherSuite = C_SERVICE_AES_GCM_128;
static const char *g_envCertPath = "TLS_CERT_PATH";
static char g_certPath[512] = "";
static ubs_hcom_subscriber_service g_subscriberService = 0;
static ubs_hcom_subscriber g_subscriber = 0;

static void BrokenSubscriber(ubs_hcom_endpoint ep, void *usrArg)
{
    (void)ep;
    (void)usrArg;
    /* C API does not expose the peer ip/port of an endpoint, only a general log here */
    printf("[WARN] subscriber ep is broken.\n");
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
    if (snprintf(path, sizeof(path), "%s/client/cert.pem", g_certPath) < 0) {
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
    if (snprintf(path, sizeof(path), "%s/client/key.pem", g_certPath) < 0) {
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

static int ReceivedRequest(ubs_hcom_service_context context, void *usrArg)
{
    (void)usrArg;
    if (context == 0) {
        return -1;
    }

    char *data = (char *)malloc(g_dataSize);
    if (data == NULL) {
        return -1;
    }
    memset(data, 'A', g_dataSize);

    ubs_hcom_multicast_request req;
    req.data = data;
    req.size = g_dataSize;
    req.lkey = 0;

    int ret = ubs_hcom_multicast_context_reply(context, req);
    free(data);
    return ret;
}

static bool CreateSubscriberService(void)
{
    ubs_hcom_multicast_service_options options;
    memset(&options, 0, sizeof(options));
    options.maxSendRecvDataSize = 4096;
    options.workerGroupMode = C_SERVICE_BUSY_POLLING;
    options.workerGroupCpuIdsRangeFirst = (g_asyncWorkerCpuId >= 0) ? (uint32_t)g_asyncWorkerCpuId : 0;
    options.workerGroupCpuIdsRangeSecond = (g_asyncWorkerCpuId >= 0) ? (uint32_t)g_asyncWorkerCpuId : 0;
    options.workerGroupId = 1;
    options.qpRecvQueueSize = 4096;
    options.qpSendQueueSize = 4096;
    options.qpPrePostSize = 2048;
    options.qpBatchRePostSize = 10;
    options.publisherWrkGroupNo = g_serverGroupNo;
    options.enableTls = g_enableTls;
    options.cipherSuite = g_cipherSuite;
    options.protocol = (g_driverProtocol == 1) ? C_MULTICAST_TCP : C_MULTICAST_RDMA;

    if (ubs_hcom_subscriber_service_create("Subscriber", &options, &g_subscriberService) != 0) {
        printf("[ERROR] Failed to create service.\n");
        return false;
    }

    ubs_hcom_multicast_config config = ubs_hcom_subscriber_service_get_config(g_subscriberService);
    const char *ipMasks[1] = {g_ipSeg};
    ubs_hcom_multicast_config_set_device_ip_mask(config, ipMasks, 1);

    ubs_hcom_subscriber_service_register_recv_handler(g_subscriberService, ReceivedRequest, NULL);
    ubs_hcom_subscriber_service_register_broken_handler(g_subscriberService, BrokenSubscriber, NULL);

    if (g_enableTls) {
        ubs_hcom_subscriber_service_register_tls_callbacks(g_subscriberService, CertCallback, PrivateKeyCallback,
                                                           CACallback);
    }

    printf("[INFO] SubscriberService Created!\n");
    return true;
}

static bool StartSubscriberService(void)
{
    if (g_subscriberService == 0) {
        printf("[ERROR] SubscriberService is nullptr.\n");
        return false;
    }
    if (ubs_hcom_subscriber_service_start(g_subscriberService) != 0) {
        printf("[ERROR] Failed to start SubscriberService.\n");
        return false;
    }
    printf("[INFO] SubscriberService Started!\n");
    return true;
}

static bool CreateSubscriber(void)
{
    char url[256];
    if (snprintf(url, sizeof(url), "tcp://%s:%u", g_oobIp, g_oobPort) < 0) {
        return false;
    }
    if (ubs_hcom_subscriber_service_create_subscriber(g_subscriberService, url, &g_subscriber) == 0) {
        printf("[INFO] CreateSubscriber Success!\n");
        return true;
    }
    printf("[ERROR] CreateSubscriber Failed!\n");
    return false;
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

    if (snprintf(path, sizeof(path), "%s/client/cert.pem", g_certPath) < 0 || access(path, F_OK) != 0) {
        printf("[ERROR] cert.pem cannot be found under %s\n", g_certPath);
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/client/key.pem", g_certPath) < 0 || access(path, F_OK) != 0) {
        printf("[ERROR] key.pem cannot be found under %s\n", g_certPath);
        return -1;
    }

    return 0;
}

static void Test(void)
{
    printf("[INFO] input c:close subscriber, q mean quit\n");
    while (1) {
        g_userChar = getchar();

        switch (g_userChar) {
            case 'c':
                printf("[INFO] Begin to close Subscriber\n");
                ubs_hcom_subscriber_close(g_subscriber);
                continue;
            case 'q':
                ubs_hcom_subscriber_service_stop(g_subscriberService);
                return;
            default:
                printf("[INFO] input c:close subscriber, q mean quit\n");
                continue;
        }
    }
}

static void SubscriberTest(void)
{
    if (g_enableTls) {
        if (ValidateTlsCert() != 0) {
            return;
        }
    }

    if (!CreateSubscriberService()) {
        return;
    }
    StartSubscriberService();
    CreateSubscriber();

    Test();

    ubs_hcom_subscriber_service_destroy_subscriber(g_subscriberService, g_subscriber);
    ubs_hcom_subscriber_service_destroy(g_subscriberService, "Subscriber");
}

int main(int argc, char *argv[])
{
    struct option options[] = {
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"driver", required_argument, NULL, 'd'},
        {"size", required_argument, NULL, 's'},
        {"cpuId", required_argument, NULL, 'c'},
        {"serverWkrGroupNo", required_argument, NULL, 'g'},
        {"TLS enabled", required_argument, NULL, 'T'},
        {"cipherSuite", required_argument, NULL, 'C'},
        {NULL, 0, NULL, 0},
    };

    const char *usage =
        "usage\n"
        "        -i, --ip,                     coord server ip mask, e.g. 10.175.118.1;\n"
        "        -p, --port,                   coord server port, by default 9981; jetty id for UBC, e.g. 998\n"
        "        -d, --driver,                 multicast driver protocol, 0 means RDMA, 1 means TCP\n"
        "        -s, --size,                   max data size\n"
        "        -c, --cpuId,                  cpu to bind\n"
        "        -g, --serverWkrGroupNo,       server worker group no, default is 0\n"
        "        -T, --TLS enabled,            TLS enabled, default is false\n"
        "        -C, --cipherSuite             cipherSuite, 0 means AES_GCM_128, 1 means AES_GCM_256, "
        "2 means AES_CCM_128, 3 means CHACHA20_POLY1305 \n";

    int ret = 0;
    int index = 0;

    while ((ret = getopt_long(argc, argv, "i:p:d:s:c:g:T:C:", options, &index)) != -1) {
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
            case 's':
                g_dataSize = (uint32_t)strtoul(optarg, NULL, 0);
                break;
            case 'c':
                g_asyncWorkerCpuId = (int32_t)(int16_t)strtoul(optarg, NULL, 0);
                break;
            case 'g':
                g_serverGroupNo = (uint8_t)strtoul(optarg, NULL, 0);
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

    SubscriberTest();
    getchar();

    return 0;
}