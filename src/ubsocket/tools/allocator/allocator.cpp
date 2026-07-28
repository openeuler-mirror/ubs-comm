/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <getopt.h>
#include <sys/mman.h>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/ubsocket_logger.h"
#include "iobuf/allocator/ubsocket_alloc_fixed_size.h"

using namespace ock::ubs;
using namespace ock::ubs::allocator;

namespace {
/* 模拟 NIC 内存注册回调：ADD 时 mlock 锁定物理页，REMOVE 时 munlock 解锁 */
Result MockExpandCallback(uintptr_t address, uint64_t size, uint32_t flags)
{
    if (flags == BlockExpandFlag::ADD) {
        if (mlock(reinterpret_cast<void *>(address), size) != 0) {
            UBS_VLOG_ERR("mlock failed, addr=0x%lx size=%lu", address, size);
            return UBS_ERROR;
        }
        UBS_VLOG_INFO("[expand] ADD addr=0x%lx size=%lu", address, size);
    } else if (flags == BlockExpandFlag::REMOVE) {
        munlock(reinterpret_cast<void *>(address), size);
        UBS_VLOG_INFO("[expand] REMOVE addr=0x%lx size=%lu", address, size);
    }
    return UBS_OK;
}

void PrintUsage(const char *prog)
{
    UBS_VLOG_INFO("Usage: %s [options]", prog);
    UBS_VLOG_INFO("Options:");
    UBS_VLOG_INFO("  --pool_space_start_tb <n>   default 2");
    UBS_VLOG_INFO("  --pool_space_size_gb <n>    default 32");
    UBS_VLOG_INFO("  --block_size_mb <n>        default 8");
    UBS_VLOG_INFO("  --init_block_count <n>     default 32");
    UBS_VLOG_INFO("  --mini_block_size_kb <n>   default 8");
    UBS_VLOG_INFO("  --alloc_count <n>          default 100, mini blocks to allocate");
    UBS_VLOG_INFO("  -h, --help                 show this help");
}

struct Args {
    uint32_t pool_space_start_tb = 2;
    uint32_t pool_space_size_gb = 32;
    uint16_t block_size_mb = 8;
    uint16_t init_block_count = 32;
    uint32_t mini_block_size_kb = 8;
    uint32_t alloc_count = 100;
};

bool ParseArgs(int argc, char *argv[], Args &args)
{
    static struct option long_opts[] = {{"pool_space_start_tb", required_argument, nullptr, 'a'},
                                        {"pool_space_size_gb", required_argument, nullptr, 'b'},
                                        {"block_size_mb", required_argument, nullptr, 'c'},
                                        {"init_block_count", required_argument, nullptr, 'd'},
                                        {"mini_block_size_kb", required_argument, nullptr, 'e'},
                                        {"alloc_count", required_argument, nullptr, 'f'},
                                        {"help", no_argument, nullptr, 'h'},
                                        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'a':
                args.pool_space_start_tb = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'b':
                args.pool_space_size_gb = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'c':
                args.block_size_mb = static_cast<uint16_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'd':
                args.init_block_count = static_cast<uint16_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'e':
                args.mini_block_size_kb = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'f':
                args.alloc_count = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'h':
            default:
                PrintUsage(argv[0]);
                return false;
        }
    }
    return true;
}

int RunAllocateFree(FixedAllocator &alloc, const Args &args)
{
    UBS_VLOG_INFO("=== Allocate/Free test ===");
    std::vector<uintptr_t> addrs;
    addrs.reserve(args.alloc_count);

    /* step1: 批量分配 */
    for (uint32_t i = 0; i < args.alloc_count; i++) {
        uintptr_t addr = 0;
        if (!alloc.Allocate(addr)) {
            UBS_VLOG_ERR("Allocate failed at index %u", i);
            return -1;
        }
        addrs.push_back(addr);
    }
    UBS_VLOG_INFO("Allocated %zu mini blocks", addrs.size());

    /* step2: 验证地址范围 */
    for (size_t i = 0; i < addrs.size(); i++) {
        if (!alloc.InRange(addrs[i], args.mini_block_size_kb * 1024ULL)) {
            UBS_VLOG_ERR("InRange check failed for addr 0x%lx", addrs[i]);
            return -1;
        }
    }
    UBS_VLOG_INFO("InRange check passed for all addresses");

    /* step3: 简单内存写入验证（确认物理页可用） */
    for (uint32_t i = 0; i < std::min<uint32_t>(args.alloc_count, 10U); i++) {
        auto p = reinterpret_cast<volatile uint8_t *>(addrs[i]);
        *p = static_cast<uint8_t>(i & 0xFF);
        if (*p != static_cast<uint8_t>(i & 0xFF)) {
            UBS_VLOG_ERR("Memory write/read check failed at addr 0x%lx", addrs[i]);
            return -1;
        }
    }
    UBS_VLOG_INFO("Memory write/read check passed");

    /* step4: 全部释放 */
    for (auto addr : addrs) {
        alloc.Free(addr);
    }
    UBS_VLOG_INFO("Freed %zu mini blocks", addrs.size());

    /* step5: 再次分配验证回收可用 */
    addrs.clear();
    for (uint32_t i = 0; i < args.alloc_count; i++) {
        uintptr_t addr = 0;
        if (!alloc.Allocate(addr)) {
            UBS_VLOG_ERR("Re-allocate failed at index %u", i);
            return -1;
        }
        addrs.push_back(addr);
    }
    UBS_VLOG_INFO("Re-allocated %zu mini blocks", addrs.size());

    for (auto addr : addrs) {
        alloc.Free(addr);
    }
    UBS_VLOG_INFO("Freed all again");

    return 0;
}
} /* namespace */

int main(int argc, char *argv[])
{
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        return 0;
    }

    UBS_VLOG_INFO("Allocator test with options:");
    UBS_VLOG_INFO("  pool_space_start_tb = %u", args.pool_space_start_tb);
    UBS_VLOG_INFO("  pool_space_size_gb  = %u", args.pool_space_size_gb);
    UBS_VLOG_INFO("  block_size_mb       = %u", args.block_size_mb);
    UBS_VLOG_INFO("  init_block_count    = %u", args.init_block_count);
    UBS_VLOG_INFO("  mini_block_size_kb  = %u", args.mini_block_size_kb);
    UBS_VLOG_INFO("  alloc_count         = %u", args.alloc_count);

    /* 初始化 */
    FixedAllocatorOptions opts;
    opts.pool_space_start_tb = args.pool_space_start_tb;
    opts.pool_space_size_gb = args.pool_space_size_gb;
    opts.block_size_mb = args.block_size_mb;
    opts.init_block_count = args.init_block_count;
    opts.mini_block_size_kb = args.mini_block_size_kb;
    opts.async_expand = false;
    opts.expanded_callback = MockExpandCallback;

    FixedAllocator alloc;
    auto ret = alloc.Init(opts);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("Init failed, ret=%d", static_cast<int>(ret));
        return -1;
    }
    UBS_VLOG_INFO("Init OK");

    int rc = RunAllocateFree(alloc, args);

    alloc.UnInit();
    UBS_VLOG_INFO("UnInit OK");

    if (rc == 0) {
        UBS_VLOG_INFO("=== ALL TESTS PASSED ===");
    } else {
        UBS_VLOG_ERR("=== TESTS FAILED ===");
    }
    return rc;
}
