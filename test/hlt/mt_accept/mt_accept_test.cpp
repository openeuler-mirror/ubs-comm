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

#include <cassert>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <mpi.h>
#include <cstring>
#include <cstdint>
#include <getopt.h>
#include <pthread.h>
#include <cinttypes>
#include <sys/time.h>
#include <climits>
#include <sys/types.h>
#include "hcom_service.h"
namespace ock {
namespace hcom {

constexpr uint32_t NN_NO13 = 13;

NetService *service = nullptr;
NetService *client = nullptr;
NetChannelPtr channel = nullptr;

UBSHcomNetDriverProtocol driverType = TCP;
std::string oobIp = "";
uint16_t g_oobPort = 9981;
std::string ipSeg = "192.168.100.0/24";
std::string udsName = "SHM_UDS";
int32_t g_dataSize = 1024;
int32_t g_pingCount = 1000000;
int16_t g_asyncWorkerCpuId = -1;
uint32_t g_workerNum = 1;

uint64_t g_startTimeServer = 0;
uint64_t g_endTimeServer = 0;
int g_numprocs = 0;
int g_rank;
#define MPI_CHECK(stmt)                                                                           \
    do {                                                                                          \
        int mpiErrno = (stmt);                                                                    \
        if (MPI_SUCCESS != mpiErrno) {                                                            \
            fprintf(stderr, "[%s:%d] MPI call failed with %d \n", __FILE__, __LINE__, mpiErrno);  \
            exit(EXIT_FAILURE);                                                                   \
        }                                                                                         \
        assert(MPI_SUCCESS == mpiErrno);                                                          \
    } while (0)

int ValidateArguments(int argc, char *argv[])
{
    const char *usage = "usage\n"
        "        -d, --driver,                 driver type, 0 means rdma, 1 means tcp\n"
        "        -i, --ip,                     server ip mask, e.g. 10.175.118.1\n"
        "        -p, --port,                   server port, by default 9981\n"
        "        -s, --io size ,               max data size\n"
        "        -w, --worker num ,            worker num\n"
        "        -c, --cpuId,                  async worker\n";

    if (argc != NN_NO13) {
        printf("invalid param, %s, for example %s -d 0 -i rdma_nic_ip -p 9981 -s 1024 -w 1 -c 5\n", usage, argv[0]);
        return -1;
    }

    return 0;
}

int ProcessOptions(int argc, char *argv[])
{
    struct option options[] = {
        {"driver", required_argument, nullptr, 'd'},
        {"ip", required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"size", required_argument, nullptr, 's'},
        {"worker num", required_argument, nullptr, 'w'},
        {"cpuId", required_argument, nullptr, 'c'},
        {nullptr, 0, nullptr, 0},
    };

    if (ValidateArguments(argc, argv) != 0) {
        return -1;
    }

    int ret = 0;
    int index = 0;
    std::string str = "d:i:p:s:w:c:";
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) {
        switch (ret) {
            case 'd':
                driverType = static_cast<UBSHcomNetDriverProtocol>((uint16_t)strtoul(optarg, NULL, 0));
                if (driverType > SHM) {
                    printf("invalid driver type %d", driverType);
                    return -1;
                }
                break;
            case 'i':
                oobIp = optarg;
                ipSeg = oobIp + "/24";
                break;
            case 'p':
                g_oobPort = static_cast<uint16_t>(strtoul(optarg, nullptr, 0));
                break;
            case 's':
                g_dataSize = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'w':
                g_workerNum = static_cast<int32_t>(strtoul(optarg, nullptr, 0));
                break;
            case 'c':
                g_asyncWorkerCpuId = strtoul(optarg, nullptr, 0);
                break;
            default:
                printf("invalid param, for example -d 0 -i rdma_nic_ip -p 9981 -s 1024 -w 1 -c 5");
                return -1;
        }
    }
    return 0;
}
int g_count = 0;
int NewChannel(const std::string &ipPort, const NetChannelPtr &ch, const std::string &payload)
{
    g_count++;
    if (g_count == 1) {
        g_startTimeServer = MONOTONIC_TIME_NS();
        std::cout << "all connect startTimeServer: " << g_startTimeServer << " ns, count " << g_count << std::endl;
    }

    if (g_count == g_numprocs - 1) {
        g_endTimeServer = MONOTONIC_TIME_NS();
        double s = static_cast<double>(g_endTimeServer - g_startTimeServer) / 1000000000;
        std::cout << "all connect success: " << s << " s, count " << g_count <<
        ", numprocs " << g_numprocs << std::endl;
    }
    NN_LOG_INFO("new channel " << ch->Id() << " call from " << ipPort << " payload: " << payload);
    return 0;
}

void BrokenChannel(const NetChannelPtr &ch)
{
    NN_LOG_INFO("ep broken");
}

int ReceivedRequest(NetServiceContext &context)
{
    return 0;
}

int PostSendRequest(NetServiceContext context)
{
    return 0;
}
int OneSideDownRequest(NetServiceContext context)
{
    return 0;
}

bool HcomServerInitStart()
{
    if (service != nullptr) {
        NN_LOG_ERROR("service already created");
        return false;
    }

    service = NetService::Instance(driverType, "server1", true);
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create service already created");
        return false;
    }
    NetServiceOptions options{};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = NN_NO1024 + g_dataSize;
    if (driverType == SHM) {
        options.oobType = NET_OOB_UDS;
        UBSHcomNetOobUDSListenerOptions listenOpt;
        listenOpt.Name(udsName);
        listenOpt.perm = 0;
        service->AddOobUdsOptions(listenOpt);
    }
    if (g_asyncWorkerCpuId != -1) {
        std::string str = std::to_string(g_asyncWorkerCpuId) + "-" + std::to_string(g_asyncWorkerCpuId);
        options.SetWorkerGroupsCpuSet(str);
        NN_LOG_INFO(" set cpuId " << str);
    }
    options.SetNetDeviceIpMask(ipSeg);
    options.SetWorkerGroups(std::to_string(g_workerNum));
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);
    service->SetOobIpAndPort(oobIp, g_oobPort);
    service->RegisterNewChannelHandler(NewChannel);
    service->RegisterChannelBrokenHandler(BrokenChannel, ock::hcom::BROKEN_ALL);
    service->RegisterOpReceiveHandler(0, ReceivedRequest);
    service->RegisterOpSentHandler(0, PostSendRequest);
    service->RegisterOpOneSideHandler(0, OneSideDownRequest);

    int result = 0;
    if ((result = service->Start(options)) != 0) {
        NN_LOG_ERROR("failed to initialize service " << result);
        return false;
    }
    NN_LOG_INFO("service initialized and start");

    return true;
}

bool HcomClientInitStart(int rank)
{
    if (client != nullptr) {
        NN_LOG_ERROR("client already created");
        return false;
    }

    client = NetService::Instance(driverType, "client" + std::to_string(rank), false);
    if (client == nullptr) {
        NN_LOG_ERROR("failed to create client already created");
        return false;
    }
    NetServiceOptions options{};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = NN_NO1024 + g_dataSize;
    if (driverType == SHM) {
        options.oobType = NET_OOB_UDS;
    }
    if (g_asyncWorkerCpuId != -1) {
        std::string str = std::to_string(g_asyncWorkerCpuId) + "-" + std::to_string(g_asyncWorkerCpuId);
        options.SetWorkerGroupsCpuSet(str);
        NN_LOG_INFO("client set cpuId " << str);
    }
    options.SetNetDeviceIpMask(ipSeg);
    options.SetWorkerGroups(std::to_string(g_workerNum));
    NN_LOG_INFO("client set ip mask " << options.netDeviceIpMask);

    client->SetOobIpAndPort(oobIp, g_oobPort);

    client->RegisterChannelBrokenHandler(BrokenChannel, ock::hcom::BROKEN_ALL);
    client->RegisterOpReceiveHandler(0, ReceivedRequest);
    client->RegisterOpSentHandler(0, PostSendRequest);
    client->RegisterOpOneSideHandler(0, OneSideDownRequest);

    int result = 0;
    if ((result = client->Start(options)) != 0) {
        NN_LOG_ERROR("failed to start client " << result);
        return false;
    }
    NN_LOG_INFO("client" << rank << " initialized and start");

    return true;
}

bool HcomInit(int rank)
{
    bool ret;
    if (rank == 0) {
        ret = HcomServerInitStart();
    } else {
        ret = HcomClientInitStart(rank);
    }
    return ret;
}

void HcomClientUninit(int rank)
{
    client->Stop();
    NetService::DestroyInstance("client" + std::to_string(rank));
}

void HcomServerUninit()
{
    service->Stop();

    NetService::DestroyInstance("server1");
}

bool HcomClientConnect(int rank)
{
    if (client == nullptr) {
        NN_LOG_ERROR("client is null" << rank);
        return false;
    }
    int result = 0;
    NetServiceConnectOptions options{};

    if (driverType == SHM) {
        result = client->Connect(udsName, 0, "hello service", channel, options);
    } else {
        result = client->Connect(oobIp, g_oobPort, "hello service", channel, options);
    }

    if (result != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    NN_LOG_INFO("client" << rank << " success to connect to server, channel id " << channel->Id());

    return true;
}

int main(int argc, char *argv[])
{
    int ret1;
    bool ret;

    ret1 = ProcessOptions(argc, argv);
    if (ret1 != 0) {
        MPI_CHECK(MPI_Finalize());
        exit(EXIT_FAILURE);
    }

    MPI_CHECK(MPI_Init(&argc, &argv));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &g_rank));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &g_numprocs));

    if (g_numprocs < NN_NO2) {
        if (g_rank == 0) {
            fprintf(stderr, "This test requires at least two processes\n");
        }

        MPI_CHECK(MPI_Finalize());
        exit(EXIT_FAILURE);
    }

    ret = HcomInit(g_rank);
    if (!ret) {
        MPI_CHECK(MPI_Finalize());
        exit(EXIT_FAILURE);
    }

    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

    if (g_rank != 0) {
        // client process [do connect]
        ret = HcomClientConnect(g_rank);
        if (!ret) {
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_FAILURE);
        }
    }
    if (g_rank != 0) {
        HcomClientUninit(g_rank);
    }

    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

    if (g_rank == 0) {
        HcomServerUninit();
    }

    MPI_CHECK(MPI_Finalize());

    return EXIT_SUCCESS;
}
}
}