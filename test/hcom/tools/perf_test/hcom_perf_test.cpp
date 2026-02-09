/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <unistd.h>
#include <getopt.h>

#include "hcom/hcom.h"

#include "common/perf_test_config.h"
#include "common/perf_test_common.h"
#include "common/perf_test_logger.h"
#include "test_case/perf_test_base.h"
#include "test_case/perf_test_factory.h"
#include "report/perf_test_report_base.h"
#include "report/perf_test_report_factory.h"

namespace hcom {
namespace perftest {

static bool IsUbcProtocol(ock::hcom::UBSHcomNetDriverProtocol &protocol)
{
    if (protocol == ock::hcom::UBSHcomNetDriverProtocol::UBC) {
        return true;
    }
    return false;
}

static bool IsSendType(PERF_TEST_TYPE &type)
{
    if ((type == PERF_TEST_TYPE::TRANSPORT_SEND_BW || type == PERF_TEST_TYPE::SERVICE_SEND_BW ||
        type == PERF_TEST_TYPE::TRANSPORT_SEND_LAT || type == PERF_TEST_TYPE::SERVICE_SEND_LAT)) {
        return true;
    }
    return false;
}

static bool RunAllSizeTest(PerfTestBase *pTest, const PerfTestConfig &cfg, PerfTestReportBase *report)
{
    uint64_t size = MESSAGE_SIZE_BASE;
    ock::hcom::UBSHcomNetDriverProtocol protocol = cfg.GetProtocol();
    PERF_TEST_TYPE type = cfg.GetType();
    while (size <= cfg.GetSize()) {
        PerfTestContext ctx;
        ctx.mIterations = cfg.GetIterations();
        ctx.mSize = size;
        if (!pTest->RunTest(&ctx)) {
            LOG_ERROR("run test failed");
            return false;
        }
        report->PrintReportElement(&ctx);
        sleep(1);
        size *= 2;
        if ((size >= UB_MAX_SIZE) && IsUbcProtocol(protocol) && IsSendType(type)) {
            return true;
        }
    }
    return true;
}

static void ServerRunAllSizeTest(PerfTestBase *pTest, const PerfTestConfig &cfg, PerfTestReportBase *report)
{
    uint64_t size = MESSAGE_SIZE_BASE;
    ock::hcom::UBSHcomNetDriverProtocol protocol = cfg.GetProtocol();
    PERF_TEST_TYPE type = cfg.GetType();
    while (size <= cfg.GetSize()) {
        PerfTestContext ctx;
        ctx.mIterations = cfg.GetIterations();
        ctx.mSize = size;
        if (!pTest->RunTest(&ctx)) {
            LOG_ERROR("run test failed");
            break;
        }
        size *= 2;
        if ((size >= UB_MAX_SIZE) && IsUbcProtocol(protocol) && IsSendType(type)) {
            break;
        }
    }
    pTest->UnInitialize();
    return;
}

void RunTest(const PerfTestConfig &cfg, PerfTestReportBase *report)
{
    PerfTestBase *pTest = PerfTestFactory::GetInstance().CreatePerfTest(cfg.GetType(), cfg);
    if (pTest == nullptr) {
        LOG_ERROR("create perf test failed!");
        return;
    }

    if (!pTest->Initialize()) {
        LOG_ERROR("instance create and start failed!");
        return;
    }
    PERF_TEST_TYPE type = cfg.GetType();
    // server死循环等待，input 'q'停止server进程
    // 如期望server也输出结果，则每个iteration开始和结束，需增加交互（client通知server）
    if (cfg.GetIsServer()) {
        if (type == PERF_TEST_TYPE::TRANSPORT_WRITE_LAT || type == PERF_TEST_TYPE::SERVICE_WRITE_LAT) {
            if (cfg.GetIsTestAllSize()) {
                ServerRunAllSizeTest(pTest, cfg, report);
            } else {
                PerfTestContext ctx;
                ctx.mIterations = cfg.GetIterations();
                ctx.mSize = cfg.GetSize();
                if (!pTest->RunTest(&ctx)) {
                    LOG_ERROR("run test failed");
                }
            }
            pTest->UnInitialize();
            return;
        }
        while (true) {
            auto tmpChar = getchar();
            switch (tmpChar) {
                case 'q':
                    pTest->UnInitialize();
                    return;
                default:
                    continue;
            }
        }
    }

    if (cfg.GetIsTestAllSize()) {
        report->PrintReportHead();
        RunAllSizeTest(pTest, cfg, report);
    } else {
        PerfTestContext ctx;
        ctx.mIterations = cfg.GetIterations();
        ctx.mSize = cfg.GetSize();
        if (!pTest->RunTest(&ctx)) {
            LOG_ERROR("run test failed");
            return;
        }
        report->PrintReportHead();
        report->PrintReportElement(&ctx);
    }

    report->PrintReportTail();
    pTest->UnInitialize();
}
}
}

using namespace hcom::perftest;
int main(int argc, char *argv[])
{
    // Parse parameters and check for conflicts
    PerfTestConfig cfg;
    if (!cfg.ParseArgs(argc, argv)) {
        LOG_ERROR("parse cfg failed");
        return -1;
    }

    cfg.Print();

    PerfTestReportBase *report = PerfTestReportFactory::GetInstance().CreatePerfTestReport(cfg);
    if (report == nullptr) {
        LOG_ERROR("create perf test report failed!");
        return -1;
    }

    RunTest(cfg, report);

    return 0;
}
