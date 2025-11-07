/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_FACTORY_H
#define HCOM_PERF_TEST_FACTORY_H

#include <map>

#include "common/perf_test_logger.h"
#include "test_case/perf_test_base.h"

namespace hcom {
namespace perftest {

using CreateFunc = PerfTestBase* (*)(const PerfTestConfig& cfg);

class PerfTestFactory {
public:
    ~PerfTestFactory() = default;
    static PerfTestFactory &GetInstance()
    {
        static PerfTestFactory instance;
        return instance;
    }

    PerfTestBase* CreatePerfTest(PERF_TEST_TYPE type, const PerfTestConfig& cfg)
    {
        for (auto it : m_createFuncs) {
            if (it.first == static_cast<uint32_t>(type)) {
                return it.second(cfg);
            }
        }
        LOG_ERROR("Can't find create function for perf test(type=" << static_cast<uint32_t>(type) << ")!");
        return nullptr;
    }

    void RegistCreateFunc(PERF_TEST_TYPE type, CreateFunc func)
    {
        LOG_DEBUG("RegistCreateFunc for perf test(type=" << static_cast<uint32_t>(type) << ")");
        m_createFuncs.emplace(static_cast<uint32_t>(type), func);
    }

private:
    PerfTestFactory() = default;
    std::map<uint32_t, CreateFunc> m_createFuncs;
};


class PerfTestRegister {
public:
    PerfTestRegister(PERF_TEST_TYPE type, CreateFunc func)
    {
        PerfTestFactory::GetInstance().RegistCreateFunc(type, func);
    }
};

#define REGIST_PERF_TEST_CREATOR(TestType, TestClass)                             \
    static PerfTestBase *Create##TestClass(const PerfTestConfig& cfg)             \
    {                                                                             \
        return new (std::nothrow) TestClass(cfg);                                 \
    }                                                                             \
    static PerfTestRegister g_register_##TestClass(TestType, Create##TestClass)

}
}
#endif
