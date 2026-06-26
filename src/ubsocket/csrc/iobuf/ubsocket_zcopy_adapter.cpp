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

#include "ubsocket_zcopy_adapter.h"

namespace ock {
namespace ubs {
UbsZeroCopyAllocator *g_zcopy_allocator = nullptr;

void *blockmem_allocate_zero_copy(size_t size)
{
    if (g_zcopy_allocator) {
        return g_zcopy_allocator->allocate(size);
    }
    return nullptr;
}

void blockmem_deallocate_zero_copy(void *addr)
{
    if (addr == nullptr) {
        return;
    }
    if (g_zcopy_allocator) {
        return g_zcopy_allocator->deallocate(addr);
    }
}

bool DynSymScanner::ParseBrpcAllocator()
{
    // Try using symbols that are more likely to be correct
    RecordApi(RTLD_DEFAULT, BRPC_ALLOC_SYMBOL_DEFAULT, alloc_addr_);
    RecordApi(RTLD_DEFAULT, BRPC_DEALLOC_SYMBOL_DEFAULT, dealloc_addr_);
    if (alloc_addr_ != nullptr && dealloc_addr_ != nullptr) {
        UBS_VLOG_DEBUG("Dynamic Symbol Scanner Found: %s(default), "
                       "(iobuf::blockmem_allocate)\n",
                       BRPC_ALLOC_SYMBOL_DEFAULT);
        UBS_VLOG_DEBUG("Dynamic Symbol Scanner Found: %s(default), "
                       "(iobuf::blockmem_deallocate)\n",
                       BRPC_DEALLOC_SYMBOL_DEFAULT);
        return true;
    }

    if (!LoadProgram() || !ParseElfStruction()) {
        UnloadProgram();
        return false;
    }

    for (size_t i = 0; i < num_symbols_; i++) {
        uint8_t bind = ELF64_ST_BIND(symbols_[i].st_info);
        uint8_t type = ELF64_ST_TYPE(symbols_[i].st_info);
        if ((bind != STB_GLOBAL && bind != STB_WEAK) || type != STT_OBJECT) {
            continue;
        }

        const char *name = strtab_data_ + symbols_[i].st_name;
        if (ParseBrpcBlockMemAllocate(name)) {
            alloc_addr_ = (blockmem_allocate_t *)(ehdr_.e_type == ET_EXEC ? (char *)symbols_[i].st_value :
                                                                            (char *)base_addr_ + symbols_[i].st_value);
            UBS_VLOG_DEBUG("Dynamic Symbol Scanner Found: %s, (iobuf::blockmem_allocate)\n", name);
        } else if (ParseBrpcBlockMemDeallocate(name)) {
            dealloc_addr_ = (blockmem_deallocate_t *)(ehdr_.e_type == ET_EXEC ?
                                                          (char *)symbols_[i].st_value :
                                                          (char *)base_addr_ + symbols_[i].st_value);
            UBS_VLOG_DEBUG("Dynamic Symbol Scanner Found: %s, (iobuf::blockmem_deallocate)\n", name);
        }
    }

    if ((alloc_addr_ == nullptr) || (dealloc_addr_ == nullptr)) {
        UnloadProgram();
        return false;
    }

    return true;
}

blockmem_allocate_t *DynSymScanner::GetBrpcAllocSymAddr()
{
    return alloc_addr_;
}

blockmem_deallocate_t *DynSymScanner::GetBrpcDeallocSymAddr()
{
    return dealloc_addr_;
}

// Looking for the base address for current executable program
void *DynSymScanner::GetBaseAddress()
{
    FILE *fp = fopen(SELF_MAP_PATH, "r");
    if (fp == nullptr) {
        UBS_VLOG_WARN("Failed to open self map\n");
        return nullptr;
    }

    char line[LINK_STR_MAX];
    void *base = nullptr;

    // Looking for the path for current executable program
    ssize_t len = readlink(SELF_EXE_PATH, exe_path_, EXE_STR_MAX - 1);
    if (len < 0 || len > EXE_STR_MAX - 1) {
        fclose(fp);
        UBS_VLOG_WARN("Failed to readlink self exe path\n");
        return nullptr;
    } else {
        exe_path_[len] = '\0';
    }

    while (fgets(line, sizeof(line), fp) != nullptr) {
        // Find lines containing "r-xp" (executable code segments)
        if (strstr(line, "r-xp") && strstr(line, exe_path_)) {
            uint64_t start;
            uint64_t end;
            // Parse address range (format: "start-end")
            if (sscanf(line, "%lx-%lx", &start, &end) == EXPECTED_ADDR_RANGE_FIELDS) {
                base = (void *)(uintptr_t)start;
                break;
            }
        }
    }

    (void)fclose(fp);
    return base;
}

void DynSymScanner::UnloadProgram()
{
    free(shdrs_);
    shdrs_ = nullptr;

    free(shstr_);
    shstr_ = nullptr;

    free(strtab_data_);
    strtab_data_ = nullptr;

    free(symbols_);
    symbols_ = nullptr;

    if (fd_ != -1) {
        LibcApi::close(fd_);
    }
}

bool DynSymScanner::LoadProgram()
{
    base_addr_ = GetBaseAddress();
    if (base_addr_ == nullptr) {
        UBS_VLOG_WARN("Failed to get base address\n");
        return false;
    }

    fd_ = LibcApi::open(exe_path_, O_RDONLY);
    if (fd_ < 0) {
        UBS_VLOG_WARN("Failed to open executable\n");
        return false;
    }

    return true;
}

bool DynSymScanner::ParseBrpcBlockMemAllocate(const char *name)
{
    if (!name) {
        return false;
    }

    const char *keywords[] = {"butil", "iobuf", "blockmem_allocate"};
    constexpr int numKeywords = 3;

    for (int i = 0; i < numKeywords; ++i) {
        if (strstr(name, keywords[i]) == nullptr) {
            return false;
        }
    }

    if (strstr(name, "asan") != nullptr || strstr(name, "gcov") != nullptr ||
        strstr(name, "reset_blockmem_allocate_and_deallocate") != nullptr) {
        return false;
    }

    return true;
}

bool DynSymScanner::ParseBrpcBlockMemDeallocate(const char *name)
{
    if (!name) {
        return false;
    }

    const char *keywords[] = {"butil", "iobuf", "blockmem_deallocate"};
    constexpr int numKeywords = 3;

    for (int i = 0; i < numKeywords; ++i) {
        if (strstr(name, keywords[i]) == nullptr) {
            return false;
        }
    }

    if (strstr(name, "asan") != nullptr || strstr(name, "gcov") != nullptr) {
        return false;
    }

    return true;
}

bool DynSymScanner::ParseElfStruction()
{
    Elf64_Shdr *shstrtab = nullptr;
    Elf64_Shdr *symtab = nullptr;
    Elf64_Shdr *strtab = nullptr;
    // parse ELF header
    if (LibcApi::read(fd_, &ehdr_, sizeof(ehdr_)) != (size_t)sizeof(ehdr_)) {
        UBS_VLOG_WARN("Failed to read ELF header");
        return false;
    }

    // validate ELF header
    if (memcmp(ehdr_.e_ident, ELFMAG, SELFMAG) != 0) {
        UBS_VLOG_WARN("Not a valid ELF file\n");
        return false;
    }

    if (ehdr_.e_type == ET_EXEC) {
        UBS_VLOG_DEBUG("Parsing position-dependent executable file\n");
    } else if (ehdr_.e_type == ET_DYN) {
        UBS_VLOG_DEBUG("Parsing position-independent executable or shared object file\n");
    } else {
        UBS_VLOG_ERR("Invalid ELF file\n");
        return false;
    }

    // looking for section header table
    if (lseek(fd_, ehdr_.e_shoff, SEEK_SET) == -1) {
        UBS_VLOG_WARN("Failed to lseek for section header table");
        return false;
    }

    uint64_t total_size = (uint64_t)ehdr_.e_shentsize * (uint64_t)ehdr_.e_shnum;
    shdrs_ = (Elf64_Shdr *)malloc(total_size);
    if (shdrs_ == nullptr) {
        UBS_VLOG_WARN("malloc failed for section headers");
        return false;
    }

    ssize_t read_len = LibcApi::read(fd_, shdrs_, total_size);
    if (read_len < 0 || (uint64_t)read_len != total_size) {
        UBS_VLOG_WARN("Failed to read section headers");
        goto FREE_SHDRS;
    }

    // looking for section header string table section
    shstrtab = &shdrs_[ehdr_.e_shstrndx];
    shstr_ = (char *)malloc(shstrtab->sh_size);
    if (shstr_ == nullptr) {
        UBS_VLOG_WARN("Failed to malloc for section string table");
        goto FREE_SHDRS;
    }

    if (lseek(fd_, shstrtab->sh_offset, SEEK_SET) == -1) {
        UBS_VLOG_WARN("Failed to lseek for section string table");
        goto FREE_SHSTR;
    }

    if (LibcApi::read(fd_, shstr_, shstrtab->sh_size) != (ssize_t)shstrtab->sh_size) {
        UBS_VLOG_WARN("Failed to read section string table");
        goto FREE_SHSTR;
    }

    // looking for symbol table and string table
    for (int i = 0; i < ehdr_.e_shnum; i++) {
        const char *name = shstr_ + shdrs_[i].sh_name;
        if (shdrs_[i].sh_type == SHT_SYMTAB && strcmp(name, ".symtab") == 0) {
            symtab = &shdrs_[i];
        } else if (shdrs_[i].sh_type == SHT_STRTAB && strcmp(name, ".strtab") == 0) {
            strtab = &shdrs_[i];
        }
    }

    if (!symtab || !strtab) {
        UBS_VLOG_WARN("Symbol table or string table not found\n");
        goto FREE_SHSTR;
    }

    // load string table information
    strtab_data_ = (char *)malloc(strtab->sh_size);
    if (strtab_data_ == nullptr) {
        UBS_VLOG_WARN("Failed to malloc for string table");
        goto FREE_SHSTR;
    }

    if (lseek(fd_, strtab->sh_offset, SEEK_SET) == -1) {
        UBS_VLOG_WARN("Failed to lseek for string table");
        goto FREE_STRTAB_DATA;
    }

    if (LibcApi::read(fd_, strtab_data_, strtab->sh_size) != (ssize_t)strtab->sh_size) {
        UBS_VLOG_WARN("Failed to read string table");
        goto FREE_STRTAB_DATA;
    }

    // load symbol table information
    symbols_ = (Elf64_Sym *)malloc(symtab->sh_size);
    if (symbols_ == nullptr) {
        UBS_VLOG_WARN("malloc failed for symbols");
        goto FREE_STRTAB_DATA;
    }

    if (lseek(fd_, symtab->sh_offset, SEEK_SET) == -1) {
        UBS_VLOG_WARN("Failed to lseek for symbols");
        goto FREE_SYMBOLS;
    }

    if (LibcApi::read(fd_, symbols_, symtab->sh_size) != (ssize_t)symtab->sh_size) {
        UBS_VLOG_WARN("Failed to read symbols");
        goto FREE_SYMBOLS;
    }

    num_symbols_ = symtab->sh_size / symtab->sh_entsize;

    return true;

FREE_SYMBOLS:
    free(symbols_);
    symbols_ = nullptr;

FREE_STRTAB_DATA:
    free(strtab_data_);
    strtab_data_ = nullptr;

FREE_SHSTR:
    free(shstr_);
    shstr_ = nullptr;

FREE_SHDRS:
    free(shdrs_);
    shdrs_ = nullptr;

    return false;
}

UbsZcopyAdapter::UbsZcopyAdapter()
    : alloc_addr_(nullptr),
      dealloc_addr_(nullptr),
      alloc_addr_origin_(nullptr),
      dealloc_addr_origin_(nullptr),
      is_intercepted_(false)
{
}

UbsZcopyAdapter::~UbsZcopyAdapter()
{
    Restore();
}

bool UbsZcopyAdapter::Intercept(const char *alloc_sym_str, const char *dealloc_sym_str)
{
    if (is_intercepted_) {
        return true;
    }
    const char *alloc_sym = GetBrpcAllocSymStr(alloc_sym_str);
    const char *dealloc_sym = GetBrpcDeallocSymStr(dealloc_sym_str);

    // 1. 尝试通过 dlsym 直接获取符号地址
    if (alloc_sym && dealloc_sym) {
        RecordApi(RTLD_DEFAULT, alloc_sym, alloc_addr_);
        RecordApi(RTLD_DEFAULT, dealloc_sym, dealloc_addr_);
    }

    // 2. 如果直接获取失败，尝试通过 ELF 扫描器降级查找
    if (alloc_addr_ == nullptr || dealloc_addr_ == nullptr) {
        DynSymScanner scanner;
        if (!scanner.ParseBrpcAllocator()) {
            return false;
        }
        alloc_addr_ = scanner.GetBrpcAllocSymAddr();
        dealloc_addr_ = scanner.GetBrpcDeallocSymAddr();
    }
    // 3. 执行替换（Hook）并记录原始地址
    if (alloc_addr_ && dealloc_addr_) {
        RecordAndSetBrpcAllocator();
        is_intercepted_ = true;
        return true;
    }
    return false;
}

void UbsZcopyAdapter::Restore()
{
    if (!is_intercepted_) {
        return;
    }
    if (alloc_addr_ != nullptr) {
        *alloc_addr_ = alloc_addr_origin_;
    }
    if (dealloc_addr_ != nullptr) {
        *dealloc_addr_ = dealloc_addr_origin_;
    }
    is_intercepted_ = false;
}

const char *UbsZcopyAdapter::GetBrpcAllocSymStr(const char *str)
{
    return (str && strlen(str) > 0) ? str : nullptr;
}

const char *UbsZcopyAdapter::GetBrpcDeallocSymStr(const char *str)
{
    return (str && strlen(str) > 0) ? str : nullptr;
}

void UbsZcopyAdapter::RecordAndSetBrpcAllocator()
{
    alloc_addr_origin_ = *alloc_addr_;
    dealloc_addr_origin_ = *dealloc_addr_;
    *alloc_addr_ = blockmem_allocate_zero_copy;
    *dealloc_addr_ = blockmem_deallocate_zero_copy;
}

void UbsZcopyAdapter::ResetBrpcAllocator()
{
    if (alloc_addr_ != nullptr) {
        *alloc_addr_ = alloc_addr_origin_;
    }
    if (dealloc_addr_ != nullptr) {
        *dealloc_addr_ = dealloc_addr_origin_;
    }
}

} // namespace ubs
} // namespace ock