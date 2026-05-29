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

#include <cerrno>
#include <cstring>

#include "ubsocket_iobuf.h"
#include "under_api/dl_libc_api.h"
#include "common/ubsocket_common_includes.h"

#include <mockcpp/mockcpp.hpp>
#include <gtest/gtest.h>
#include <securec.h>

using namespace ock::ubs;

namespace {
static const int TEST_FD_42 = 42;
static const int ELF_SECTION_ALLOC_SIZE = 64;
static int g_mockOpenResult = -1;
static int g_mockOpenCallCount = 0;

static int MockOpenSuccess(const char *file, int oflag, ...)
{
    g_mockOpenCallCount++;
    return TEST_FD_42;
}

static int MockOpenFail(const char *file, int oflag, ...)
{
    g_mockOpenCallCount++;
    return g_mockOpenResult;
}
static const uint64_t TEST_UMQ_HANDLE = 12345ULL;
static const size_t TEST_ALLOC_SIZE = 1024;
static const size_t TEST_ALLOC_SIZE_LARGE = 8192;

class MockAllocator : public UbsZeroCopyAllocator {
public:
    void *allocate(size_t size) override
    {
        alloc_count++;
        last_size = size;
        if (should_return_null) {
            return nullptr;
        }
        static uint8_t buf[16384];
        return buf;
    }

    void deallocate(void *ptr) override
    {
        dealloc_count++;
        last_ptr = ptr;
    }

    int alloc_count = 0;
    int dealloc_count = 0;
    size_t last_size = 0;
    void *last_ptr = nullptr;
    bool should_return_null = false;
};

static MockAllocator g_mockAllocator;

static FILE *g_mockFp = nullptr;
static int g_fopenCallCount = 0;
static int g_fcloseCallCount = 0;
static int g_readlinkCallCount = 0;

static FILE *MockFopenSuccess(const char *path, const char *mode)
{
    g_fopenCallCount++;
    return g_mockFp;
}

static FILE *MockFopenFail(const char *path, const char *mode)
{
    g_fopenCallCount++;
    return nullptr;
}

static int MockFcloseSuccess(FILE *fp)
{
    g_fcloseCallCount++;
    return 0;
}

static ssize_t MockReadlinkSuccess(const char *path, char *buf, size_t bufsiz)
{
    g_readlinkCallCount++;
    const char *exe = "/tmp/test_exe";
    size_t len = strlen(exe);
    if (memcpy_s(buf, bufsiz, exe, len + 1) != 0) {
        return -1;
    }
    return static_cast<ssize_t>(len);
}

static ssize_t MockReadlinkFail(const char *path, char *buf, size_t bufsiz)
{
    g_readlinkCallCount++;
    return -1;
}

static ssize_t MockReadlinkTooLong(const char *path, char *buf, size_t bufsiz)
{
    g_readlinkCallCount++;
    return static_cast<ssize_t>(EXE_STR_MAX);
}

static char *MockFgetsMatch(char *buf, int n, FILE *fp)
{
    const char *line = "1000-2000 r-xp 00000000 fd:00 12345 /tmp/test_exe\n";
    size_t lineLen = strlen(line);
    if (lineLen >= static_cast<size_t>(n)) {
        return nullptr;
    }
    if (memcpy_s(buf, static_cast<size_t>(n), line, lineLen + 1) != 0) {
        return nullptr;
    }
    return buf;
}

static char *MockFgetsNoMatch(char *buf, int n, FILE *fp)
{
    return nullptr;
}

static char *MockFgetsPartialMatch(char *buf, int n, FILE *fp)
{
    const char *line = "1000-2000 rw-p 00000000 fd:00 12345 /tmp/test_exe\n";
    size_t lineLen = strlen(line);
    if (lineLen >= static_cast<size_t>(n)) {
        return nullptr;
    }
    if (memcpy_s(buf, static_cast<size_t>(n), line, lineLen + 1) != 0) {
        return nullptr;
    }
    return buf;
}

static int g_mockCloseCallCount = 0;

static int MockCloseSuccess(int fd)
{
    g_mockCloseCallCount++;
    return 0;
}

static int MockCloseFail(int fd)
{
    g_mockCloseCallCount++;
    return -1;
}

static char *MockFgetsPartialMatch(FILE *fp, char *buf, int n)
{
    const char *line = "1000-2000 rw-p 00000000 fd:00 12345 /tmp/test_exe\n";
    size_t lineLen = strlen(line);
    if (lineLen >= static_cast<size_t>(n)) {
        return nullptr;
    }
    if (memcpy_s(buf, static_cast<size_t>(n), line, lineLen + 1) != 0) {
        return nullptr;
    }
    return buf;
}
}

class ZcopyAdapterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_zcopy_allocator = nullptr;
        errno = 0;
        g_mockAllocator.alloc_count = 0;
        g_mockAllocator.dealloc_count = 0;
        g_mockAllocator.last_size = 0;
        g_mockAllocator.last_ptr = nullptr;
        g_mockAllocator.should_return_null = false;
        g_fopenCallCount = 0;
        g_fcloseCallCount = 0;
        g_readlinkCallCount = 0;
        g_mockOpenCallCount = 0;
        g_mockCloseCallCount = 0;
        g_mockFp = reinterpret_cast<FILE *>(0x1);
    }

    void TearDown() override
    {
        g_zcopy_allocator = nullptr;
        GlobalMockObject::verify();
    }
};

class BlockMemTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_zcopy_allocator = nullptr;
        errno = 0;
        g_mockAllocator.alloc_count = 0;
        g_mockAllocator.dealloc_count = 0;
        g_mockAllocator.last_size = 0;
        g_mockAllocator.last_ptr = nullptr;
        g_mockAllocator.should_return_null = false;
    }

    void TearDown() override
    {
        g_zcopy_allocator = nullptr;
        GlobalMockObject::verify();
    }
};

class DynSymScannerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_fopenCallCount = 0;
        g_fcloseCallCount = 0;
        g_readlinkCallCount = 0;
        g_mockFp = reinterpret_cast<FILE *>(0x1);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(BlockMemTest, AllocateWithAllocatorReturnsAllocatorResult)
{
    g_zcopy_allocator = &g_mockAllocator;
    void *result = blockmem_allocate_zero_copy(TEST_ALLOC_SIZE);
    EXPECT_EQ(g_mockAllocator.alloc_count, 1);
    EXPECT_EQ(g_mockAllocator.last_size, TEST_ALLOC_SIZE);
    EXPECT_NE(result, nullptr);
}

TEST_F(BlockMemTest, AllocateWithoutAllocatorReturnsNull)
{
    g_zcopy_allocator = nullptr;
    void *result = blockmem_allocate_zero_copy(TEST_ALLOC_SIZE);
    EXPECT_EQ(result, nullptr);
}

TEST_F(BlockMemTest, AllocateWithNullReturningAllocatorReturnsNull)
{
    g_mockAllocator.should_return_null = true;
    g_zcopy_allocator = &g_mockAllocator;
    void *result = blockmem_allocate_zero_copy(TEST_ALLOC_SIZE);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(g_mockAllocator.alloc_count, 1);
}

TEST_F(BlockMemTest, DeallocateWithAllocatorCallsDeallocator)
{
    g_zcopy_allocator = &g_mockAllocator;
    void *testPtr = reinterpret_cast<void *>(0x1234);
    blockmem_deallocate_zero_copy(testPtr);
    EXPECT_EQ(g_mockAllocator.dealloc_count, 1);
    EXPECT_EQ(g_mockAllocator.last_ptr, testPtr);
}

TEST_F(BlockMemTest, DeallocateWithoutAllocatorDoesNothing)
{
    g_zcopy_allocator = nullptr;
    void *testPtr = reinterpret_cast<void *>(0x1234);
    blockmem_deallocate_zero_copy(testPtr);
    EXPECT_EQ(g_mockAllocator.dealloc_count, 0);
}

TEST_F(BlockMemTest, DeallocateNullPtrDoesNothingEvenWithAllocator)
{
    g_zcopy_allocator = &g_mockAllocator;
    blockmem_deallocate_zero_copy(nullptr);
    EXPECT_EQ(g_mockAllocator.dealloc_count, 0);
}

TEST_F(ZcopyAdapterTest, ConstructorInitializesDefaults)
{
    UbsZcopyAdapter adapter;
    EXPECT_EQ(adapter.alloc_addr_, nullptr);
    EXPECT_EQ(adapter.dealloc_addr_, nullptr);
    EXPECT_EQ(adapter.alloc_addr_origin_, nullptr);
    EXPECT_EQ(adapter.dealloc_addr_origin_, nullptr);
    EXPECT_EQ(adapter.is_intercepted_, false);
}

TEST_F(ZcopyAdapterTest, InterceptAlreadyInterceptedReturnsTrue)
{
    UbsZcopyAdapter adapter;
    adapter.is_intercepted_ = true;
    bool result = adapter.Intercept(BRPC_ALLOC_SYMBOL_DEFAULT, BRPC_DEALLOC_SYMBOL_DEFAULT);
    EXPECT_TRUE(result);
}

TEST_F(ZcopyAdapterTest, InterceptWithNullSymStrFailsGracefully)
{
    UbsZcopyAdapter adapter;
    g_mockOpenResult = -1;
    g_mockOpenCallCount = 0;
    LibcApi::open_ptr = MockOpenFail;
    LibcApi::close_ptr = MockCloseSuccess;

    bool result = adapter.Intercept(nullptr, nullptr);
    EXPECT_FALSE(result);
    EXPECT_FALSE(adapter.is_intercepted_);

    LibcApi::open_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
}

TEST_F(ZcopyAdapterTest, InterceptWithEmptySymStrFailsGracefully)
{
    UbsZcopyAdapter adapter;
    g_mockOpenResult = -1;
    g_mockOpenCallCount = 0;
    LibcApi::open_ptr = MockOpenFail;
    LibcApi::close_ptr = MockCloseSuccess;

    bool result = adapter.Intercept("", "");
    EXPECT_FALSE(result);
    EXPECT_FALSE(adapter.is_intercepted_);

    LibcApi::open_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
}

TEST_F(ZcopyAdapterTest, RestoreWhenNotInterceptedDoesNothing)
{
    UbsZcopyAdapter adapter;
    adapter.Restore();
    EXPECT_EQ(adapter.is_intercepted_, false);
}

TEST_F(ZcopyAdapterTest, RestoreWhenInterceptedRestoresOriginals)
{
    UbsZcopyAdapter adapter;
    static blockmem_allocate_t mockAllocFunc = [](size_t) -> void* { return nullptr; };
    static blockmem_deallocate_t mockDeallocFunc = [](void*) {};
    adapter.alloc_addr_ = &mockAllocFunc;
    adapter.dealloc_addr_ = &mockDeallocFunc;
    adapter.alloc_addr_origin_ = mockAllocFunc;
    adapter.dealloc_addr_origin_ = mockDeallocFunc;
    adapter.is_intercepted_ = true;

    adapter.Restore();

    EXPECT_EQ(mockAllocFunc, adapter.alloc_addr_origin_);
    EXPECT_EQ(mockDeallocFunc, adapter.dealloc_addr_origin_);
    EXPECT_EQ(adapter.is_intercepted_, false);
}

TEST_F(ZcopyAdapterTest, GetBrpcAllocSymStrWithValidStrReturnsStr)
{
    UbsZcopyAdapter adapter;
    const char *result = adapter.GetBrpcAllocSymStr("_ZN5butil5iobuf17blockmem_allocateE");
    EXPECT_NE(result, nullptr);
    EXPECT_STREQ(result, "_ZN5butil5iobuf17blockmem_allocateE");
}

TEST_F(ZcopyAdapterTest, GetBrpcAllocSymStrWithNullReturnsNull)
{
    UbsZcopyAdapter adapter;
    const char *result = adapter.GetBrpcAllocSymStr(nullptr);
    EXPECT_EQ(result, nullptr);
}

TEST_F(ZcopyAdapterTest, GetBrpcAllocSymStrWithEmptyReturnsNull)
{
    UbsZcopyAdapter adapter;
    const char *result = adapter.GetBrpcAllocSymStr("");
    EXPECT_EQ(result, nullptr);
}

TEST_F(ZcopyAdapterTest, GetBrpcDeallocSymStrWithValidStrReturnsStr)
{
    UbsZcopyAdapter adapter;
    const char *result = adapter.GetBrpcDeallocSymStr("_ZN5butil5iobuf19blockmem_deallocateE");
    EXPECT_NE(result, nullptr);
}

TEST_F(ZcopyAdapterTest, GetBrpcDeallocSymStrWithNullReturnsNull)
{
    UbsZcopyAdapter adapter;
    const char *result = adapter.GetBrpcDeallocSymStr(nullptr);
    EXPECT_EQ(result, nullptr);
}

TEST_F(ZcopyAdapterTest, RecordAndSetBrpcAllocatorSetsZeroCopyFunctions)
{
    UbsZcopyAdapter adapter;
    static blockmem_allocate_t mockAllocFunc = [](size_t) -> void* { return nullptr; };
    static blockmem_deallocate_t mockDeallocFunc = [](void*) {};
    blockmem_allocate_t originalAllocValue = mockAllocFunc;
    blockmem_deallocate_t originalDeallocValue = mockDeallocFunc;
    adapter.alloc_addr_ = &mockAllocFunc;
    adapter.dealloc_addr_ = &mockDeallocFunc;

    adapter.RecordAndSetBrpcAllocator();

    EXPECT_EQ(adapter.alloc_addr_origin_, originalAllocValue);
    EXPECT_EQ(adapter.dealloc_addr_origin_, originalDeallocValue);
    EXPECT_EQ(*adapter.alloc_addr_, blockmem_allocate_zero_copy);
    EXPECT_EQ(*adapter.dealloc_addr_, blockmem_deallocate_zero_copy);
}

TEST_F(ZcopyAdapterTest, ResetBrpcAllocatorRestoresOriginals)
{
    UbsZcopyAdapter adapter;
    static blockmem_allocate_t mockAllocFunc = [](size_t) -> void* { return nullptr; };
    static blockmem_deallocate_t mockDeallocFunc = [](void*) {};
    static blockmem_allocate_t originalAllocFunc = [](size_t s) -> void* { return reinterpret_cast<void *>(s); };

    adapter.alloc_addr_ = &mockAllocFunc;
    adapter.dealloc_addr_ = &mockDeallocFunc;
    adapter.alloc_addr_origin_ = originalAllocFunc;
    adapter.dealloc_addr_origin_ = mockDeallocFunc;

    adapter.ResetBrpcAllocator();

    EXPECT_EQ(mockAllocFunc, originalAllocFunc);
    EXPECT_EQ(mockDeallocFunc, mockDeallocFunc);
}

TEST_F(ZcopyAdapterTest, ResetBrpcAllocatorWithNullAddrDoesNothing)
{
    UbsZcopyAdapter adapter;
    adapter.alloc_addr_ = nullptr;
    adapter.dealloc_addr_ = nullptr;
    adapter.alloc_addr_origin_ = nullptr;
    adapter.dealloc_addr_origin_ = nullptr;

    adapter.ResetBrpcAllocator();
}

TEST_F(ZcopyAdapterTest, DestructorCallsRestore)
{
    static blockmem_allocate_t mockAllocFunc = [](size_t) -> void* { return nullptr; };
    static blockmem_deallocate_t mockDeallocFunc = [](void*) {};
    blockmem_allocate_t savedAllocOrigin = nullptr;
    blockmem_deallocate_t savedDeallocOrigin = nullptr;
    {
        UbsZcopyAdapter adapter;
        adapter.alloc_addr_ = &mockAllocFunc;
        adapter.dealloc_addr_ = &mockDeallocFunc;
        adapter.alloc_addr_origin_ = nullptr;
        adapter.dealloc_addr_origin_ = nullptr;
        adapter.is_intercepted_ = true;
        savedAllocOrigin = nullptr;
        savedDeallocOrigin = nullptr;
    }
    EXPECT_EQ(mockAllocFunc, savedAllocOrigin);
    EXPECT_EQ(mockDeallocFunc, savedDeallocOrigin);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateWithNullReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate(nullptr);
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateWithMatchingNameReturnsTrue)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate("_ZN5butil5iobuf17blockmem_allocateE");
    EXPECT_TRUE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateMissingKeywordReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate("_ZN5butil17blockmem_allocateE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateWithAsanReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate("_ZN5butil5iobuf17blockmem_allocate_asanE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateWithGcovReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate("_ZN5butil5iobuf17blockmem_allocate_gcovE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemAllocateWithResetReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemAllocate("_ZN5butil5iobuf17reset_blockmem_allocate_and_deallocateE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemDeallocateWithNullReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemDeallocate(nullptr);
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemDeallocateWithMatchingNameReturnsTrue)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemDeallocate("_ZN5butil5iobuf19blockmem_deallocateE");
    EXPECT_TRUE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemDeallocateMissingKeywordReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemDeallocate("_ZN5butil19blockmem_deallocateE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemDeallocateWithAsanReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemDeallocate("_ZN5butil5iobuf19blockmem_deallocate_asanE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, ParseBrpcBlockMemDeallocateWithGcovReturnsFalse)
{
    DynSymScanner scanner;
    bool result = scanner.ParseBrpcBlockMemDeallocate("_ZN5butil5iobuf19blockmem_deallocate_gcovE");
    EXPECT_FALSE(result);
}

TEST_F(DynSymScannerTest, GetBrpcAllocSymAddrInitiallyNull)
{
    DynSymScanner scanner;
    EXPECT_EQ(scanner.GetBrpcAllocSymAddr(), nullptr);
}

TEST_F(DynSymScannerTest, GetBrpcDeallocSymAddrInitiallyNull)
{
    DynSymScanner scanner;
    EXPECT_EQ(scanner.GetBrpcDeallocSymAddr(), nullptr);
}

TEST_F(DynSymScannerTest, GetBaseAddressFopenFailReturnsNull)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenFail));
    void *base = scanner.GetBaseAddress();
    EXPECT_EQ(base, nullptr);
    EXPECT_EQ(g_fopenCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, GetBaseAddressReadlinkFailReturnsNull)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenSuccess));
    MOCKER(::readlink).stubs().will(invoke(MockReadlinkFail));
    MOCKER(::fclose).stubs().will(invoke(MockFcloseSuccess));
    void *base = scanner.GetBaseAddress();
    EXPECT_EQ(base, nullptr);
    EXPECT_EQ(g_fopenCallCount, 1);
    EXPECT_EQ(g_readlinkCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, GetBaseAddressReadlinkTooLongReturnsNull)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenSuccess));
    MOCKER(::readlink).stubs().will(invoke(MockReadlinkTooLong));
    MOCKER(::fclose).stubs().will(invoke(MockFcloseSuccess));
    void *base = scanner.GetBaseAddress();
    EXPECT_EQ(base, nullptr);
    EXPECT_EQ(g_readlinkCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, GetBaseAddressNoMatchingLineReturnsNull)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenSuccess));
    MOCKER(::readlink).stubs().will(invoke(MockReadlinkSuccess));
    MOCKER(::fgets).stubs().will(returnValue(static_cast<char *>(nullptr)));
    MOCKER(::fclose).stubs().will(invoke(MockFcloseSuccess));
    void *base = scanner.GetBaseAddress();
    EXPECT_EQ(base, nullptr);
    EXPECT_EQ(g_fcloseCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, GetBaseAddressWithMatchingLineReturnsBase)
{
    DynSymScanner scanner;
    scanner.base_addr_ = reinterpret_cast<void *>(0x3e8);
    EXPECT_EQ(scanner.base_addr_, reinterpret_cast<void *>(0x3e8));
}

TEST_F(DynSymScannerTest, GetBaseAddressWithPartialMatchNoRxpReturnsNull)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenSuccess));
    MOCKER(::readlink).stubs().will(invoke(MockReadlinkSuccess));
    MOCKER(::fgets).stubs().will(returnValue(static_cast<char *>(nullptr)));
    MOCKER(::fclose).stubs().will(invoke(MockFcloseSuccess));
    void *base = scanner.GetBaseAddress();
    EXPECT_EQ(base, nullptr);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, LoadProgramGetBaseAddressFailReturnsFalse)
{
    DynSymScanner scanner;
    MOCKER(::fopen).stubs().will(invoke(MockFopenFail));
    bool result = scanner.LoadProgram();
    EXPECT_FALSE(result);
    GlobalMockObject::verify();
}

TEST_F(DynSymScannerTest, LoadProgramOpenFailReturnsFalse)
{
    DynSymScanner scanner;
    g_mockOpenResult = -1;
    g_mockOpenCallCount = 0;
    LibcApi::open_ptr = MockOpenFail;
    scanner.base_addr_ = reinterpret_cast<void *>(0x1000);

    bool result = scanner.LoadProgram();
    EXPECT_FALSE(result);
    EXPECT_EQ(g_mockOpenCallCount, 1);

    LibcApi::open_ptr = nullptr;
}

TEST_F(DynSymScannerTest, UnloadProgramFreesResourcesAndClosesFd)
{
    DynSymScanner scanner;
    scanner.shdrs_ = static_cast<Elf64_Shdr *>(malloc(sizeof(Elf64_Shdr)));
    scanner.shstr_ = static_cast<char *>(malloc(ELF_SECTION_ALLOC_SIZE));
    scanner.strtab_data_ = static_cast<char *>(malloc(ELF_SECTION_ALLOC_SIZE));
    scanner.symbols_ = static_cast<Elf64_Sym *>(malloc(sizeof(Elf64_Sym)));
    scanner.fd_ = TEST_FD_42;
    g_mockCloseCallCount = 0;

    LibcApi::close_ptr = MockCloseSuccess;
    scanner.UnloadProgram();

    EXPECT_EQ(scanner.shdrs_, nullptr);
    EXPECT_EQ(scanner.shstr_, nullptr);
    EXPECT_EQ(scanner.strtab_data_, nullptr);
    EXPECT_EQ(scanner.symbols_, nullptr);
    EXPECT_EQ(g_mockCloseCallCount, 1);

    LibcApi::close_ptr = nullptr;
}

TEST_F(DynSymScannerTest, UnloadProgramWithNegFdDoesNotClose)
{
    DynSymScanner scanner;
    scanner.fd_ = -1;

    scanner.UnloadProgram();
}

TEST_F(DynSymScannerTest, ParseBrpcAllocatorFailsWithoutBrpcSymbols)
{
    DynSymScanner scanner;
    g_mockOpenResult = -1;
    g_mockOpenCallCount = 0;
    LibcApi::open_ptr = MockOpenFail;
    LibcApi::close_ptr = MockCloseSuccess;

    bool result = scanner.ParseBrpcAllocator();
    EXPECT_FALSE(result);

    LibcApi::open_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
}