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
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdlib>
#include <vector>

#include "securec.h"
#include "net_pgtable.h"

constexpr size_t AlignDown(size_t n, size_t alignment)
{
    return n - (n % alignment);
}

namespace ock {
namespace hcom {
class TestPgTable : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }

protected:
    using SearchResult = std::vector<PgtRegion *>;
    PgTable mPgTable { pgdAlloc, pgdFree };

    SearchResult Search(PgtAddress from, PgtAddress to)
    {
        NN_LOG_INFO("Begin to search from "
            << "[0x" << std::hex << from << ".. 0x" << std::hex << to << "]");
        SearchResult result;
        mPgTable.SearchRange(from, to, pgdSearchCb, reinterpret_cast<void *>(&result));
        return result;
    }

    static PgtRegion *MakeRegion(PgtAddress start, PgtAddress end)
    {
        PgtRegion r = { start, end };
        return new PgtRegion(r);
    }

    static bool IsOverlap(const PgtRegion *region, PgtAddress from, PgtAddress to)
    {
        NN_LOG_DEBUG("regions" << region << " in the range 0x" << std::hex << region->start << "..0x" << region->end <<
            " from 0x" << from << " to: 0x" << to);
        return std::max(from, region->start) <= std::min(to, region->end);
    }

    static uint32_t CountOverlap(const std::vector<PgtRegion *> &regions, PgtAddress from, PgtAddress to)
    {
        uint32_t count = 0;
        for (const auto &item : regions) {
            if (IsOverlap(item, from, to)) {
                ++count;
            }
        }
        return count;
    }

    void TestSearchRegion(const PgtRegion &region)
    {
        SearchResult result;

        result = Search(region.start, region.end - 1);
        ASSERT_EQ(NN_NO1, result.size());
        EXPECT_EQ(&region, result.front());

        result = Search(region.start, region.end);
        ASSERT_EQ(NN_NO1, result.size());
        EXPECT_EQ(&region, result.front());

        result = Search(region.start, region.end + 1);
        ASSERT_EQ(NN_NO1, result.size());
        EXPECT_EQ(&region, result.front());
    }

private:
    static PgtDir *pgdAlloc(const PgTable &pgtable)
    {
        return new (std::nothrow) PgtDir;
    }

    static PgtDir *pgdAllocFailed(const PgTable &pgtable)
    {
        return nullptr;
    }

    static void pgdFree(const PgTable &pgtable, PgtDir *pgdir)
    {
        delete pgdir;
    }

    static void pgdSearchCb(const PgTable &pgtable, PgtRegion &region, void *arg)
    {
        NN_LOG_INFO("find the region push to result " << &region << "[0x" << std::hex << region.start << ".. 0x" <<
            std::hex << region.end << "]");
        SearchResult *result = reinterpret_cast<SearchResult *>(arg);
        result->push_back(&region);
    }
};

TEST_F(TestPgTable, BasicSuccess)
{
    PgtRegion region;

    region.start = 0x600800;
    region.end = 0x603400;

    NResult status = mPgTable.Insert(region);
    EXPECT_EQ(status, NN_OK);

    mPgTable.Dump();

    EXPECT_EQ(&region, mPgTable.Lookup(0x600800));
    EXPECT_EQ(&region, mPgTable.Lookup(0x602020));
    EXPECT_EQ(&region, mPgTable.Lookup(0x6033ff));
    EXPECT_EQ(nullptr, mPgTable.Lookup(0x603400));
    EXPECT_EQ(nullptr, mPgTable.Lookup(0x0));
    EXPECT_EQ(nullptr, mPgTable.Lookup(std::numeric_limits<PgtAddress>::max()));
    EXPECT_EQ(NN_NO1, mPgTable.mRegionCount);

    status = mPgTable.Remove(region);
    EXPECT_EQ(status, NN_OK);
    EXPECT_EQ(NN_NO0, mPgTable.mRegionCount);

    status = mPgTable.Insert(region);
    EXPECT_EQ(status, NN_OK);

    mPgTable.Dump();
}

TEST_F(TestPgTable, InsertPgdAllocFailed)
{
    PgtRegion region;
    region.start = 0x600800;
    region.end = 0x603400;

    PgTable pgTable { pgdAllocFailed, pgdFree };
    NResult status = pgTable.Insert(region);
    EXPECT_EQ(status, NN_ERROR);
}

TEST_F(TestPgTable, PgtExpandFailed)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    pgTable.mIndexShift = NN_NO64;
    bool ret = pgTable.PgtExpand();
    EXPECT_EQ(ret, false);
}

TEST_F(TestPgTable, InsertAndLookupAdjSuccess)
{
    // [0xc600000, 0xc600400) [0xc600400, 0xc600800)
    PgtRegion region1 = { 0xc600000, 0xc600400 };
    PgtRegion region2 = { 0xc600400, 0xc600800 };
    NResult status = mPgTable.Insert(region1);
    EXPECT_EQ(status, NN_OK);

    status = mPgTable.Insert(region2);
    EXPECT_EQ(status, NN_OK);

    mPgTable.Dump();
    EXPECT_EQ(&region2, mPgTable.Lookup(0xc600400));
    EXPECT_EQ(&region1, mPgTable.Lookup(0xc600000));

    status = mPgTable.Remove(region1);
    EXPECT_EQ(status, NN_OK);

    status = mPgTable.Remove(region2);
    EXPECT_EQ(status, NN_OK);
}

TEST_F(TestPgTable, InsertAlreadyExistFailed)
{
    PgtRegion region1 = { 0x4000, 0x6000 };
    NResult ret = mPgTable.Insert(region1);
    EXPECT_EQ(ret, NN_OK);

    PgtRegion region2 = { 0x5000, 0x7000 };
    ret = mPgTable.Insert(region2);
    EXPECT_EQ(ret, NN_ERROR);

    PgtRegion region3 = { 0x3000, 0x5000 };
    ret = mPgTable.Insert(region3);
    EXPECT_EQ(ret, NN_ERROR);

    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestPgTable, RemoveNonExistFailed)
{
    PgtRegion region1 = { 0x5000, 0x7000 };
    auto ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_ERROR);

    PgtRegion region2 = { 0x6000, 0x8000 };
    ret = mPgTable.Insert(region2);
    EXPECT_EQ(ret, NN_OK);

    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_ERROR);

    region1.start = 0x6000;
    region1.end = 0x6000;
    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_ERROR);

    region1 = region2;
    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_ERROR); /* should be pointer-equal */

    ret = mPgTable.Remove(region2);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestPgTable, SearchLargeRegionSuccess)
{
    PgtRegion region = { 0x3c03cb00, 0x3c03f600 };
    NResult ret = mPgTable.Insert(region);
    EXPECT_EQ(ret, NN_OK);

    SearchResult result;

    result = Search(0x36990000, 0x3c810000);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region, result.front());

    result = Search(region.start - 1, region.start);
    EXPECT_EQ(NN_NO1, result.size());

    result = Search(region.start, region.start + 1);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region, result.front());

    result = Search(region.end - 1, region.end);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region, result.front());

    result = Search(region.end, region.end + 1);
    EXPECT_EQ(0u, result.size());

    ret = mPgTable.Remove(region);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestPgTable, SearchNonContigRegionsSuccess)
{
    const size_t regionSize = (1UL << NN_NO28);

    // insert [0x7f6ef0000000 .. 0x7f6f00000000]
    auto start = 0x7f6ef0000000;
    auto end = start + regionSize;
    PgtRegion region1 = { start, end };
    NResult ret = mPgTable.Insert(region1);
    EXPECT_EQ(ret, NN_OK);

    // insert [0x7f6f2c021000 .. 0x7f6f3c021000]
    start = 0x7f6f2c021000;
    end = start + regionSize;
    PgtRegion region2 = { start, end };
    ret = mPgTable.Insert(region2);
    EXPECT_EQ(ret, NN_OK);

    // insert [0x7f6f42000000 .. 0x7f6f52000000]
    start = 0x7f6f42000000;
    end = start + regionSize;
    PgtRegion region3 = { start, end };
    ret = mPgTable.Insert(region3);
    EXPECT_EQ(ret, NN_OK);

    SearchResult result;

    // search the 1st region
    TestSearchRegion(region1);

    // search the 2nd region
    TestSearchRegion(region2);

    // search the 3rd region
    TestSearchRegion(region3);

    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_OK);

    ret = mPgTable.Remove(region2);
    EXPECT_EQ(ret, NN_OK);

    ret = mPgTable.Remove(region3);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestPgTable, SearchAdjRegionsSuccess)
{
    const size_t regionSize = (1UL << NN_NO28);
    // insert [0x7f6ef0000000 .. 0x7f6f00000000]
    auto start = 0x7f6ef0000000;
    auto end = start + regionSize;
    PgtRegion region1 = { start, end };
    NResult ret = mPgTable.Insert(region1);
    EXPECT_EQ(ret, NN_OK);

    // insert [0x7f6f00000000 .. 0x7f6f10000000]
    start = end;
    end = start + regionSize;
    PgtRegion region2 = { region1.end, 0x7f6f40000000 };
    ret = mPgTable.Insert(region2);
    EXPECT_EQ(ret, NN_OK);

    // insert [0x7f6f10000000 .. 0x7f6f20000000]
    start = end;
    end = start + regionSize;
    PgtRegion region3 = { region2.end, 0x7f6f48000000 };
    ret = mPgTable.Insert(region3);
    EXPECT_EQ(ret, NN_OK);

    SearchResult result;

    // search the 1st region
    result = Search(region1.start, region1.end - 1);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region1, result.front());

    result = Search(region1.start, region1.end);
    EXPECT_EQ(NN_NO2, result.size());
    EXPECT_EQ(&region1, result.front());

    result = Search(region1.start, region1.end + 1);
    EXPECT_EQ(NN_NO2, result.size());
    EXPECT_EQ(&region1, result.front());

    // search the 2nd region
    result = Search(region2.start, region2.end - 1);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region2, result.front());

    result = Search(region2.start, region2.end);
    EXPECT_EQ(NN_NO2, result.size());
    EXPECT_EQ(&region2, result.front());

    result = Search(region2.start, region2.end + 1);
    EXPECT_EQ(NN_NO2, result.size());
    EXPECT_EQ(&region2, result.front());

    // search the 3rd region
    result = Search(region3.start, region3.end - 1);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region3, result.front());

    result = Search(region3.start, region3.end);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region3, result.front());

    result = Search(region3.start, region3.end + 1);
    EXPECT_EQ(NN_NO1, result.size());
    EXPECT_EQ(&region3, result.front());

    ret = mPgTable.Remove(region1);
    EXPECT_EQ(ret, NN_OK);

    ret = mPgTable.Remove(region2);
    EXPECT_EQ(ret, NN_OK);

    ret = mPgTable.Remove(region3);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestPgTable, MultiSearchSuccess)
{
    uint32_t ucsRandSeed = ock::hcom::NN_NO1073741824;
    std::vector<PgtRegion *> regions;
    // Repeat execution 5 times, using different random data each time，to verify the stability of the page table
    for (int count = 0; count < NN_NO10; ++count) {
        PgtAddress min = std::numeric_limits<PgtAddress>::max();
        PgtAddress max = 0;

        /* generate random regions */
        uint32_t regionCount = 0;
        for (int i = 0; i < NN_NO10; ++i) {
            PgtAddress start = (rand_r(&ucsRandSeed) & 0x7fffffff) << NN_NO24;
            size_t randomSize = static_cast<size_t>(rand_r(&ucsRandSeed));
            size_t size = std::min(randomSize, std::numeric_limits<PgtAddress>::max() - start);
            PgtAddress end = start + AlignDown(size, PAGE_ADDR_ALIGN_MIN);

            min = std::min(start, min);
            max = std::max(start, max);
            auto region = MakeRegion(start, end);

            NN_LOG_INFO("begin to check insert count:" << count << " region index:" << i <<
                " regions in the range 0x" << std::hex << region->start << "..0x" << region->end << std::dec <<
                " total num:" << regionCount);

            if (CountOverlap(regions, region->start, region->end) != 0) {
                /* Make sure regions do not overlap */
                continue;
            }

            regions.push_back(region);
            ++regionCount;
        }

        /* Insert regions */
        for (const auto &item : regions) {
            mPgTable.Insert(*item);
        }

        /* Count how many fall in the [1/4, 3/4] range */
        PgtAddress from = ((min * NN_NO90) + (max * NN_NO10)) / NN_NO100;
        PgtAddress to = ((min * NN_NO10) + (max * NN_NO90)) / NN_NO100;
        uint32_t numInRange = CountOverlap(regions, from, to);

        SearchResult result = Search(from, to);
        NN_LOG_INFO("total region num " << regionCount << " found " << result.size() << "/" << numInRange <<
            " regions in the range 0x" << std::hex << from << "..0x" << to << std::dec);
        EXPECT_EQ(numInRange, result.size());
    }
}

TEST_F(TestPgTable, CleanUpSuccess)
{
    PgtRegion region1 = { 0xc600000, 0xc600400 };
    PgtRegion region2 = { 0xc600400, 0xc600800 };
    PgtRegion region3 = { 0xc600800, 0xc600b00 };
    EXPECT_EQ(mPgTable.Insert(region1), NN_OK);
    EXPECT_EQ(mPgTable.Insert(region2), NN_OK);
    EXPECT_EQ(mPgTable.Insert(region3), NN_OK);
    mPgTable.Dump();
    mPgTable.Cleanup();
    EXPECT_EQ(mPgTable.mRootEntry.IsPresent(), false);
}

TEST_F(TestPgTable, TestPgtEntry)
{
    PgtEntry entry;
    EXPECT_EQ(entry.GetRegion(), nullptr);
    EXPECT_EQ(entry.GetDir(), nullptr);
    entry.SetFlag(EntryFlags::REGION);
    entry.ClearFlag(EntryFlags::REGION);
    EXPECT_EQ(entry.GetRegion(), nullptr);

    PgtRegion region;
    region.start = 0x600800;
    region.end = 0x603400;
    PgtDir dir;
    dir.count = 0;
    MOCKER_CPP(&PgtEntry::CheckPtrValueAlign).stubs().will(returnValue(false));
    EXPECT_EQ(entry.SetRegion(region), false);
    EXPECT_EQ(entry.SetDir(dir), false);
}

PgtDir *pgdAllocFailed2(const PgTable &pgtable)
{
    return (PgtDir *)0x000001;
}

void pgdFreeFailed2(const PgTable &pgtable, PgtDir *pgdir)
{
    return;
}

TEST_F(TestPgTable, TestPgtDirAllocFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    const uint32_t order = 5;
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(true));
    MOCKER_CPP(&PgtEntry::SetDir).stubs().will(returnValue(false));
    pgTable.PgtEnsureCapacity(order, NN_NO0);

    MOCKER_CPP(&memset_s).stubs().will(returnValue(-1));
    EXPECT_EQ(pgTable.PgtDirAlloc(), nullptr);

    PgTable pgTable2 { pgdAllocFailed2, pgdFreeFailed2 };
    EXPECT_EQ(pgTable2.PgtDirAlloc(), nullptr);

    MOCKER_CPP(&NetPgTable::PgtExpand).stubs().will(returnValue(false));
    pgTable2.PgtEnsureCapacity(order, NN_NO0);

    pgTable2.PgtDirRelease(nullptr);
}

PgtDir *GetDirStub()
{
    PgtDir *dir = (PgtDir *)malloc(sizeof(PgtDir));
    if (dir == nullptr) {
        return nullptr;
    }
    dir->count = 1;
    return dir;
}

TEST_F(TestPgTable, TestPgtShrinkFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(true));
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(returnValue(true));
    MOCKER_CPP(&PgtEntry::GetDir).stubs().will(invoke(GetDirStub));
    EXPECT_EQ(pgTable.PgtShrink(), true);
    EXPECT_EQ(pgTable.PgtShrink(), false);
}

TEST_F(TestPgTable, TestInsertPageFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    PgtAddress address = 0x000001;
    PgtRegion region{};
    NResult ret = pgTable.InsertPage(address, 1, region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    address = 0x000010;
    ret = pgTable.InsertPage(address, 1, region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP(&PgTable::PgtShrink).stubs().will(returnValue(false));
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(returnValue(true));
    ret = pgTable.InsertPage(address, 0, region);
    EXPECT_EQ(ret, NN_ERROR);

    MOCKER_CPP(&PgTable::PgtCheckEntryDir).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(false));
    MOCKER_CPP(&PgtEntry::SetDir).stubs().will(returnValue(false));
    ret = pgTable.InsertPage(address, 0, region);
    EXPECT_EQ(ret, NN_ERROR);

    PgtDir *dir = nullptr;
    MOCKER_CPP(&PgTable::PgtDirAlloc).stubs().will(returnValue(dir));
    ret = pgTable.InsertPage(address, 0, region);
    EXPECT_EQ(ret, NN_ERROR);
}

bool HasFlagStub(EntryFlags flag)
{
    if (flag == EntryFlags::DIR) {
        return true;
    }
    return false;
}

TEST_F(TestPgTable, TestRemovePageFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    PgtAddress address = 0x000001;
    PgtRegion region{};
    NResult ret = pgTable.RemovePage(address, 1, region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(returnValue(true));
    ret = pgTable.RemovePage(address, 0, region);
    EXPECT_EQ(ret, NN_ERROR);

    GlobalMockObject::verify();
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(invoke(HasFlagStub));
    ret = pgTable.RemovePage(address, 0, region);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestPgTable, TestInsertFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    PgtRegion region{};
    region.start = 0x000002;
    region.end = 0x000001;
    NResult ret = pgTable.Insert(region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    region.start = 0x000000;
    region.end = 0x000002;
    ret = pgTable.Insert(region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    region.start = 0x000001;
    region.end = 0x000002;
    ret = pgTable.Insert(region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    region.start = 0x000001;
    region.end = 0x000010;
    ret = pgTable.Insert(region);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

PgtRegion *GetRegionStub()
{
    PgtRegion *reg = (PgtRegion *)malloc(sizeof(PgtRegion));
    if (reg == nullptr) {
        return nullptr;
    }
    reg->start = 0x000010;
    return reg;
}

TEST_F(TestPgTable, TestLookupFail)
{
    PgTable pgTable { pgdAlloc, pgdFree };
    PgtAddress address = 0x000001;
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(true));
    PgtRegion *reg = nullptr;
    MOCKER_CPP(&PgtEntry::GetRegion).stubs().will(returnValue(reg));
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(returnValue(true));
    EXPECT_EQ(pgTable.Lookup(address), nullptr);

    GlobalMockObject::verify();
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(invoke(HasFlagStub));
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(true));
    MOCKER_CPP(&PgtEntry::GetRegion).stubs().will(invoke(GetRegionStub));
    EXPECT_EQ(pgTable.Lookup(address), nullptr);

    GlobalMockObject::verify();
    MOCKER_CPP(&PgtEntry::HasFlag).stubs().will(invoke(HasFlagStub));
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(true));
    PgtDir *dir = nullptr;
    MOCKER_CPP(&PgtEntry::GetDir).stubs().will(returnValue(dir));
    EXPECT_EQ(pgTable.Lookup(address), nullptr);

    GlobalMockObject::verify();
    MOCKER_CPP(&PgtEntry::IsPresent).stubs().will(returnValue(false));
    EXPECT_EQ(pgTable.Lookup(address), nullptr);
}

} // namespace hcom
} // namespace ock
