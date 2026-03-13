/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-08-25
 *Note:
 *History: 2025-08-25
*/

#ifndef BRPC_DYNSYM_SCANNER_H
#define BRPC_DYNSYM_SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <stdint.h>
#include <regex>
#include "socket_adapter.h"
#include "brpc_iobuf_adapter.h"
#include "../util_vlog.h"

#define LINK_STR_MAX   (256)
#define EXE_STR_MAX    (1024)
#define EXPECTED_ADDR_RANGE_FIELDS    (2)
#define SELF_MAP_PATH  "/proc/self/maps"
#define SELF_EXE_PATH  "/proc/self/exe"
#define BRPC_ALLOC_SYMBOL_DEFAULT  "_ZN5butil5iobuf17blockmem_allocateE"
#define BRPC_DEALLOC_SYMBOL_DEFAULT  "_ZN5butil5iobuf19blockmem_deallocateE"

namespace Brpc {

class DynSymScanner {
public:
    ~DynSymScanner()
    {
        UnloadProgram();
    }
    
    bool ParseBrpcAllocator()
    {
        // Try using symbols that are more likely to be correct
        RecordApi(RTLD_DEFAULT, BRPC_ALLOC_SYMBOL_DEFAULT, m_alloc_addr, ubsocket::UTIL_VLOG_LEVEL_WARN);
        RecordApi(RTLD_DEFAULT, BRPC_DEALLOC_SYMBOL_DEFAULT, m_dealloc_addr, ubsocket::UTIL_VLOG_LEVEL_WARN);
        if (m_alloc_addr != nullptr && m_dealloc_addr != nullptr) {
            RPC_ADPT_VLOG_INFO("Dynamic Symbol Scanner Found: %s(default), "
                               "(butil::iobuf::blockmem_allocate)\n",
                               BRPC_ALLOC_SYMBOL_DEFAULT);
            RPC_ADPT_VLOG_INFO("Dynamic Symbol Scanner Found: %s(default), "
                               "(butil::iobuf::blockmem_deallocate)\n",
                               BRPC_DEALLOC_SYMBOL_DEFAULT);
            return true;    
        }

        if (!LoadProgram() || !ParseElfStruction()) {
            UnloadProgram();
            return false;
        }

        for (size_t i = 0; i< m_num_symbols; i++){
            uint8_t bind = ELF64_ST_BIND(m_symbols[i].st_info);
            uint8_t type = ELF64_ST_TYPE(m_symbols[i].st_info);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) || type != STT_OBJECT){
                continue;
            }

            const char *name = m_strtab_data + m_symbols[i].st_name;
            if (ParseBrpcBlockMemAllocate(name)) {
                m_alloc_addr = (IOBuf::blockmem_allocate_t *)(m_ehdr.e_type == ET_EXEC ?
                    (char *)m_symbols[i].st_value : (char *)m_base_addr + m_symbols[i].st_value);
                RPC_ADPT_VLOG_INFO("Dynamic Symbol Scanner Found: %s, (butil::iobuf::blockmem_allocate)\n", name);
            } else if (ParseBrpcBlockMemDeallocate(name)) {
                m_dealloc_addr = (IOBuf::blockmem_deallocate_t *)(m_ehdr.e_type == ET_EXEC ?
                    (char *)m_symbols[i].st_value : (char *)m_base_addr + m_symbols[i].st_value);
                RPC_ADPT_VLOG_INFO("Dynamic Symbol Scanner Found: %s, (butil::iobuf::blockmem_deallocate)\n", name);
            }
        }

        if ((m_alloc_addr == nullptr) || (m_dealloc_addr == nullptr)) {
            UnloadProgram();
            return false;
        }

        return true;
    }

    IOBuf::blockmem_allocate_t *GetBrpcAllocSymAddr()
    {
        return m_alloc_addr;
    }

    IOBuf::blockmem_deallocate_t *GetBrpcDeallocSymAddr()
    {
        return m_dealloc_addr;
    }

protected:
    // Looking for the base address for current executable program
    void *GetBaseAddress()
    {
        FILE *fp = fopen(SELF_MAP_PATH, "r");
        if (fp == nullptr) {
            RPC_ADPT_VLOG_WARN("Failed to open self map\n");
            return  NULL;
        }

        char line[LINK_STR_MAX];
        void* base = NULL;

        // Looking for the path for current executable program
        ssize_t len = readlink(SELF_EXE_PATH, m_exe_path, EXE_STR_MAX - 1);
        if (len < 0 || len > EXE_STR_MAX -1) {
            fclose(fp);
            RPC_ADPT_VLOG_WARN("Failed to readlink self exe path\n");
            return NULL;
        } else {
            m_exe_path[len] = '\0';
        }

        while (fgets(line, sizeof(line), fp) != NULL){
            // Find lines containing "r-xp" (executable code segments)
            if (strstr(line, "r-xp") && strstr(line, m_exe_path)) {
                uint64_t start;
                uint64_t end;
                // Parse address range (format: "start-end")
                if (sscanf_s(line, "%lx-%lx", &start, &end) == EXPECTED_ADDR_RANGE_FIELDS) {
                    base = (void *)(uintptr_t)start;
                    break;
                }
            }
        }

        (void)fclose(fp);
        return base;
    }
    
    void UnloadProgram()
    {
        free(m_shdrs);
        m_shdrs = nullptr;

        free(m_shstr);
        m_shstr = nullptr;

        free(m_strtab_data);
        m_strtab_data = nullptr;

        free(m_symbols);
        m_symbols = nullptr;

        if(m_fd != -1){
            OsAPiMgr::GetOriginApi()->close(m_fd);
        }
    }

    bool LoadProgram()
    {
        m_base_addr = GetBaseAddress();
        if (m_base_addr == nullptr) {
            RPC_ADPT_VLOG_WARN("Failed to get base address\n");
            return false;
        } 

        m_fd = OsAPiMgr::GetOriginApi()->open(m_exe_path, O_RDONLY);
        if (m_fd < 0) {
            RPC_ADPT_VLOG_WARN("Failed to open executable\n");
            return false;
        }

        return true;
    }

    bool ParseBrpcBlockMemAllocate(const char *name)
    {
        if (!name) {
            return false;
        }

        const char* keywords[] = {"butil", "iobuf", "blockmem_allocate"};
        constexpr int numKeywords = 3;

        for (int i = 0; i < numKeywords; ++i) {
            if (strstr(name, keywords[i]) == nullptr) {
                return false;
            }
        }
        
        if (strstr(name, "asan") != nullptr ||
            strstr(name, "gcov") != nullptr ||
            strstr(name, "reset_blockmem_allocate_and_deallocate") != nullptr) {
            return false;
        }

        return true;
    }

    bool ParseBrpcBlockMemDeallocate(const char *name)
    {
        if (!name) {
            return false;
        }

        const char* keywords[] = {"butil", "iobuf", "blockmem_deallocate"};
        constexpr int numKeywords = 3;

        for (int i = 0; i < numKeywords; ++i) {
            if (strstr(name, keywords[i]) == nullptr) {
                return false;
            }
        }
        
        if (strstr(name, "asan") != nullptr ||
            strstr(name, "gcov") != nullptr) {
            return false;
        }

        return true;
    }

    bool ParseElfStruction()
    {
        Elf64_Shdr *shstrtab = nullptr;
        Elf64_Shdr *symtab = nullptr;
        Elf64_Shdr *strtab = nullptr;
        // parse ELF header
        if (OsAPiMgr::GetOriginApi()->read(m_fd, &m_ehdr, sizeof(m_ehdr)) != (size_t)sizeof(m_ehdr)) {
            RPC_ADPT_VLOG_WARN("Failed to read ELF header");
            return false;
        }

        // validate ELF header
        if (memcmp(m_ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            RPC_ADPT_VLOG_WARN("Not a valid ELF file\n");
            return false;
        }

        if (m_ehdr.e_type == ET_EXEC) {
            RPC_ADPT_VLOG_INFO("Parsing position-dependent executable file\n");
        } else if (m_ehdr.e_type == ET_DYN) {
            RPC_ADPT_VLOG_INFO("Parsing position-independent executable or shared object file\n");
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,  "Invalid ELF file\n");
            return false;
        }

        // looking for section header table
        if (lseek(m_fd, m_ehdr.e_shoff, SEEK_SET) == -1) {
            RPC_ADPT_VLOG_WARN("Failed to lseek for section header table");
            return false;
        }

        uint64_t total_size = (uint64_t)m_ehdr.e_shentsize * (uint64_t)m_ehdr.e_shnum;
        m_shdrs = (Elf64_Shdr *)malloc(total_size);
        if (m_shdrs == nullptr) {
            RPC_ADPT_VLOG_WARN("malloc failed for section headers");
            return false;
        }

        ssize_t read_len = OsAPiMgr::GetOriginApi()->read(m_fd, m_shdrs, total_size);
        if (read_len < 0 || (uint64_t)read_len != total_size) {
            RPC_ADPT_VLOG_WARN("Failed to read section headers");
            goto FREE_SHDRS;
        }

        // looking for section header string table section
        shstrtab = &m_shdrs[m_ehdr.e_shstrndx];
        m_shstr = (char *)malloc(shstrtab->sh_size);
        if (m_shstr == nullptr) {
            RPC_ADPT_VLOG_WARN("Failed to malloc for section string table");
            goto FREE_SHDRS;
        }

        if (lseek(m_fd, shstrtab->sh_offset, SEEK_SET) == -1) {
            RPC_ADPT_VLOG_WARN("Failed to lseek for section string table");
            goto FREE_SHSTR;
        }

        if (OsAPiMgr::GetOriginApi()->read(m_fd, m_shstr, shstrtab->sh_size) != (ssize_t)shstrtab->sh_size) {
            RPC_ADPT_VLOG_WARN("Failed to read section string table");
            goto FREE_SHSTR;
        }

        // looking for symbol table and string table
        for (int i = 0; i < m_ehdr.e_shnum; i++) {
            const char *name = m_shstr + m_shdrs[i].sh_name;
            if (m_shdrs[i].sh_type == SHT_SYMTAB && strcmp(name, ".symtab") == 0) {
                symtab = &m_shdrs[i];
            } else if (m_shdrs[i].sh_type == SHT_STRTAB && strcmp(name, ".strtab") == 0) {
                strtab = &m_shdrs[i];
            }
        }

        if (!symtab || !strtab) {
            RPC_ADPT_VLOG_WARN("Symbol table or string table not found\n");
            goto FREE_SHSTR;
        }

        // load string table information
        m_strtab_data = (char *)malloc(strtab->sh_size);
        if (m_strtab_data == nullptr) {
            RPC_ADPT_VLOG_WARN("Failed to malloc for string table");
            goto FREE_SHSTR;
        }

        if (lseek(m_fd, strtab->sh_offset, SEEK_SET) == -1) {
            RPC_ADPT_VLOG_WARN("Failed to lseek for string table");
            goto FREE_STRTAB_DATA;
        }

        if (OsAPiMgr::GetOriginApi()->read(m_fd, m_strtab_data, strtab->sh_size) != (ssize_t)strtab->sh_size) {
            RPC_ADPT_VLOG_WARN("Failed to read string table");
            goto FREE_STRTAB_DATA;
        }

        // load symbol table information
        m_symbols = (Elf64_Sym *)malloc(symtab->sh_size);
        if (m_symbols == nullptr) {
            RPC_ADPT_VLOG_WARN("malloc failed for symbols");
            goto FREE_STRTAB_DATA;
        }

        if (lseek(m_fd, symtab->sh_offset, SEEK_SET) == -1) {
            RPC_ADPT_VLOG_WARN("Failed to lseek for symbols");
            goto FREE_SYMBOLS;
        }

        if (OsAPiMgr::GetOriginApi()->read(m_fd, m_symbols, symtab->sh_size) != (ssize_t)symtab->sh_size) {
            RPC_ADPT_VLOG_WARN("Failed to read symbols");
            goto FREE_SYMBOLS;
        }

        m_num_symbols = symtab->sh_size / symtab->sh_entsize;

        return true;

    FREE_SYMBOLS:
        free(m_symbols);
        m_symbols = nullptr;

    FREE_STRTAB_DATA:
        free(m_strtab_data);
        m_strtab_data = nullptr;

    FREE_SHSTR:
        free(m_shstr);
        m_shstr = nullptr;

    FREE_SHDRS:
        free(m_shdrs);
        m_shdrs = nullptr;

        return false;
    }

    char m_exe_path[EXE_STR_MAX] = {0};
    Elf64_Ehdr m_ehdr;
    Elf64_Shdr *m_shdrs = nullptr;
    char *m_shstr = nullptr;
    char *m_strtab_data = nullptr;
    Elf64_Sym *m_symbols = nullptr;
    size_t m_num_symbols = 0;
    void *m_base_addr = nullptr;
    int m_fd = -1;

    IOBuf::blockmem_allocate_t *m_alloc_addr = nullptr;
    IOBuf::blockmem_deallocate_t *m_dealloc_addr = nullptr;
};

}

#endif
