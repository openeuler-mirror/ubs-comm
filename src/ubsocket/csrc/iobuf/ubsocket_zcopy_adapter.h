/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_ZCOPY_ADAPTER_H
#define UBS_COMM_ZCOPY_ADAPTER_H

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <regex>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_libc_api.h"

constexpr uint32_t LINK_STR_MAX = 256;
constexpr uint32_t EXE_STR_MAX = 1024;
constexpr uint32_t EXPECTED_ADDR_RANGE_FIELDS = 2;
constexpr const char *SELF_MAP_PATH = "/proc/self/maps";
constexpr const char *SELF_EXE_PATH = "/proc/self/exe";
constexpr const char *BRPC_ALLOC_SYMBOL_DEFAULT = "_ZN5butil5iobuf17blockmem_allocateE";
constexpr const char *BRPC_DEALLOC_SYMBOL_DEFAULT = "_ZN5butil5iobuf19blockmem_deallocateE";
namespace ock {
namespace ubs {
class UbsZeroCopyAllocator {
public:
    virtual ~UbsZeroCopyAllocator() = default;
    virtual void *allocate(size_t size) = 0;
    virtual void deallocate(void *ptr) = 0;
};

extern UbsZeroCopyAllocator *g_zcopy_allocator;

typedef void *(*blockmem_allocate_t)(size_t);
typedef void (*blockmem_deallocate_t)(void *);

void *blockmem_allocate_zero_copy(size_t size);
void blockmem_deallocate_zero_copy(void *addr);

template <typename ApiType>
void RecordApi(void *handle, const char *symbol_name, ApiType &symbol)
{
    (void)dlerror();
    symbol = reinterpret_cast<ApiType>(dlsym(handle, symbol_name));
    char *dlerror_str = dlerror();
    if (!symbol || dlerror_str) {
        UBS_VLOG_WARN("Symbol not found: %s\n", symbol_name);
    }
}

class DynSymScanner {
public:
    ~DynSymScanner() = default;
    bool ParseBrpcAllocator();
    blockmem_allocate_t *GetBrpcAllocSymAddr();
    blockmem_deallocate_t *GetBrpcDeallocSymAddr();

protected:
    // Looking for the base address for current executable program
    void *GetBaseAddress();

    void UnloadProgram();

    bool LoadProgram();

    bool ParseBrpcBlockMemAllocate(const char *name);

    bool ParseBrpcBlockMemDeallocate(const char *name);

    bool ParseElfStruction();

    char exe_path_[EXE_STR_MAX] = {0};
    Elf64_Ehdr ehdr_;
    Elf64_Shdr *shdrs_ = nullptr;
    char *shstr_ = nullptr;
    char *strtab_data_ = nullptr;
    Elf64_Sym *symbols_ = nullptr;
    size_t num_symbols_ = 0;
    void *base_addr_ = nullptr;
    int fd_ = -1;

    blockmem_allocate_t *alloc_addr_ = nullptr;
    blockmem_deallocate_t *dealloc_addr_ = nullptr;
};

class UbsZcopyAdapter {
public:
    UbsZcopyAdapter();

    ~UbsZcopyAdapter();

    bool Intercept(const char *alloc_sym_str, const char *dealloc_sym_str);

    void Restore();

    void RecordAndSetBrpcAllocator();
    void ResetBrpcAllocator();

private:
    const char *GetBrpcAllocSymStr(const char *str);
    const char *GetBrpcDeallocSymStr(const char *str);

    blockmem_allocate_t *alloc_addr_ = nullptr;
    blockmem_deallocate_t *dealloc_addr_ = nullptr;
    blockmem_allocate_t alloc_addr_origin_ = nullptr;
    blockmem_deallocate_t dealloc_addr_origin_ = nullptr;
    bool is_intercepted_ = false;
};
} // namespace ubs
} // namespace ock
#endif