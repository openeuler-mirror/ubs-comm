/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_API_HELPER_H
#define UBS_COMM_UMQ_API_HELPER_H

#include <securec.h>
#include <cstdint>
#include <cstring>
#include <mockcpp/mockcpp.hpp>
#include <new>

#include "iobuf/ubsocket_iobuf.h"
#include "umq_api.h"
#include "umq_pro_types.h"

/**
 * umq_api_helper.h — UMQ API mock缓冲与结构体构造的快捷方式。
 *
 * 提供两类缓冲构造:
 * 1. AllocMockBufWithBlock() — 8K对齐Block+umq_buf_t，用于需要PtrFloorToBoundary回溯Block的场景。
 *    内部使用8槽静态池(MOCK_BUF_WITH_BLOCK_COUNT=8)，SetUp中调用ResetMockBufWithBlockIndex()重置。
 *    Block初始nshared=1，与生产代码一致；IncRef→2, DecRef→1(存活)；DecRef→0时blockmem_deallocate_zero_copy
 *    被调用但g_zcopy_allocator为nullptr所以安全。
 *
 * 2. AllocMockBuf() — 简单umq_buf_t(无Block)，用于不需要Block回溯的场景。
 *
 * 仍不够灵活时(自定义opcode/nshared/data_size/total_data_size等)，可在test case中直接构造:
 *   umq_buf_t buf = {};
 *   buf.buf_data = ...;
 *   buf.data_size = ...;
 *   buf.status = ...;
 *   memcpy(buf.qbuf_ext, &bufPro, sizeof(umq_buf_pro_t));
 *   Block *block = GetBlockFromMockBuf(&buf);  // 如需Block则手写8K对齐分配+placement new
 */

namespace ock {
namespace ubs {
namespace test {

constexpr uint64_t TEST_UMQ_HANDLE = 12345ULL;
constexpr uint32_t TEST_BUF_DATA_SIZE = 8192;
constexpr uint32_t TEST_BUF_SIZE = 64;
constexpr int TEST_INTERRUPT_FD = 42;
constexpr int MOCK_BUF_WITH_BLOCK_COUNT = 8;
constexpr uint32_t MOCK_BLOCK_MEM_SIZE = 16384;
constexpr uint32_t BLOCK_ALIGNMENT_SIZE = 8192;

struct MockBufWithBlock {
    alignas(BLOCK_ALIGNMENT_SIZE) uint8_t blockMem[MOCK_BLOCK_MEM_SIZE];
    umq_buf_pro_t bufPro;
    umq_buf_t buf;
};

inline MockBufWithBlock g_mockBufsWithBlock[MOCK_BUF_WITH_BLOCK_COUNT];
inline int g_mockBufWithBlockIndex = 0;

inline void ResetMockBufWithBlockIndex()
{
    g_mockBufWithBlockIndex = 0;
}

inline umq_buf_t *AllocMockBufWithBlock(uint32_t size, umq_buf_status_t status = UMQ_BUF_SUCCESS)
{
    int idx = g_mockBufWithBlockIndex++;
    if (idx >= MOCK_BUF_WITH_BLOCK_COUNT) {
        return nullptr;
    }

    MockBufWithBlock &b = g_mockBufsWithBlock[idx];
    char *dataPtr = reinterpret_cast<char *>(b.blockMem) + sizeof(Block);

    new (b.blockMem) Block(dataPtr, MOCK_BLOCK_MEM_SIZE - sizeof(Block));

    memset(&b.bufPro, 0, sizeof(umq_buf_pro_t));
    b.bufPro.opcode = UMQ_OPC_SEND;

    memset(&b.buf, 0, sizeof(umq_buf_t));
    b.buf.buf_data = dataPtr;
    b.buf.data_size = size;
    b.buf.total_data_size = size;
    b.buf.status = status;
    memcpy(b.buf.qbuf_ext, &b.bufPro, sizeof(umq_buf_pro_t));
    b.buf.qbuf_next = nullptr;
    b.buf.io_direction = UMQ_IO_RX;

    return &b.buf;
}

inline Block *GetBlockFromMockBuf(umq_buf_t *buf)
{
    if (buf == nullptr || buf->buf_data == nullptr) {
        return nullptr;
    }
    uint64_t addr = reinterpret_cast<uint64_t>(buf->buf_data);
    uint64_t floorAddr = addr & ~static_cast<uint64_t>(BLOCK_ALIGNMENT_SIZE - 1);
    return reinterpret_cast<Block *>(floorAddr);
}

inline umq_buf_t *AllocMockBuf(uint32_t size, umq_buf_status_t status = UMQ_BUF_SUCCESS)
{
    static uint8_t bufData[TEST_BUF_DATA_SIZE];
    static umq_buf_pro_t bufPro;
    static umq_buf_t mockBuf;

    memset(&bufPro, 0, sizeof(umq_buf_pro_t));
    bufPro.opcode = UMQ_OPC_SEND;

    memset(&mockBuf, 0, sizeof(umq_buf_t));
    mockBuf.buf_data = reinterpret_cast<char *>(bufData);
    mockBuf.data_size = size;
    mockBuf.total_data_size = size;
    mockBuf.status = status;
    memcpy(mockBuf.qbuf_ext, &bufPro, sizeof(umq_buf_pro_t));
    mockBuf.qbuf_next = nullptr;
    mockBuf.io_direction = UMQ_IO_RX;

    return &mockBuf;
}

inline umq_eid_t MakeTestEid(uint8_t val)
{
    umq_eid_t eid = {};
    memset(eid.raw, val, sizeof(eid.raw));
    return eid;
}

inline umq_init_cfg_t MakeTestInitCfg()
{
    umq_init_cfg_t cfg = {};
    return cfg;
}

inline umq_create_option_t MakeTestCreateOption()
{
    umq_create_option_t opt = {};
    return opt;
}

inline umq_interrupt_option_t MakeTestInterruptOption()
{
    umq_interrupt_option_t opt = {};
    return opt;
}

inline umq_trans_info_t MakeTestTransInfo()
{
    umq_trans_info_t info = {};
    return info;
}

inline umq_alloc_option_t MakeTestAllocOption()
{
    umq_alloc_option_t opt = {};
    return opt;
}

inline umq_dev_info_t MakeTestDevInfo()
{
    umq_dev_info_t info = {};
    return info;
}

inline umq_cfg_get_t MakeTestCfgGet()
{
    umq_cfg_get_t cfg = {};
    return cfg;
}

inline umq_route_key_t MakeTestRouteKey()
{
    umq_route_key_t key = {};
    return key;
}

inline umq_route_list_t MakeTestRouteList()
{
    umq_route_list_t list = {};
    return list;
}

} // namespace test
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_API_HELPER_H