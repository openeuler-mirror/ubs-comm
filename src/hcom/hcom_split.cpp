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

#include "hcom_split.h"
#include "securec.h"

namespace ock {
namespace hcom {

/// SyncCallWithSelfPoll 拼包专用。
/// - 在拼包时会尝试接收一次，如果发现首包为 RAW 包则直接返回并设置出参 data、
/// dataLen，此时出参 acc 无实际作用。
/// - 如果发现需要拼包，则会在出参 acc 中分配内存，当拼包完成时设置出参 data、
/// dataLen 表明实际收到的数据、数据长度。在拼包阶段发生任意错误都会提前返回。
/// \seealso SpliceMessage
SerResult SyncSpliceMessage(UBSHcomNetResponseContext &ctx, UBSHcomNetEndpoint *ep, int32_t timeout,
    std::string &acc, void *&data, uint32_t &dataLen)
{
    SerResult result = ep->Receive(timeout, ctx);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Channel sync call receive failed " << result << " ep id " << ep->Id());
        return result;
    }

    // 是否为拆包的一部分
    switch (ctx.Header().extHeaderType) {
        case UBSHcomExtHeaderType::RAW:
            data = ctx.Message()->Data();
            dataLen = ctx.Message()->DataLen();
            return result;

        case UBSHcomExtHeaderType::FRAGMENT:
            break;
    }

    while (true) {
        const uintptr_t msgAddr = reinterpret_cast<uintptr_t>(ctx.Message()->Data());
        const uint32_t msgSize = ctx.Message()->DataLen();
        if (msgSize < sizeof(UBSHcomFragmentHeader)) {
            NN_LOG_ERROR("SyncSpliceMessage: message size is invalid!");
            return SER_ERROR;
        }

        const UBSHcomFragmentHeader *serviceHeader = reinterpret_cast<UBSHcomFragmentHeader *>(msgAddr);
        const void *payload = reinterpret_cast<void *>(msgAddr + sizeof(UBSHcomFragmentHeader));
        const uint64_t payloadLen = msgSize - sizeof(UBSHcomFragmentHeader);
        const auto msgId = serviceHeader->msgId;
        const uint32_t totalLength = serviceHeader->totalLength;
        const uint32_t offset = serviceHeader->offset;

        NN_LOG_DEBUG("SyncSpliceMessage: id " << msgId << ", totalLength " << totalLength << ", offset " << offset
                                              << ", size " << payloadLen);

        // 避免因数据在网络中被篡改而造成高内存占用
        if (totalLength >= SERVICE_MAX_TOTAL_LENGTH) {
            NN_LOG_ERROR("SyncSpliceMessage: totalLength (" << totalLength << ") is larger than the maximum ("
                                                            << SERVICE_MAX_TOTAL_LENGTH << ")");
            return SER_SPLIT_INVALID_MSG;
        }

        // 首包分配足够大的内存
        if (offset == 0) {
            acc.resize(totalLength);
        }
        if (totalLength != static_cast<uint32_t>(acc.size())) {
            NN_LOG_ERROR("SyncSpliceMessage: the totalLength does not match with the first fragment. " <<
                totalLength << " != " << acc.size());
            return SER_SPLIT_INVALID_MSG;
        }

        // | msg1 | ... | last |
        // | msg2 | ... | ...  | last |
        //
        // 可能 msg1 的尾部消息丢失同时 msg2 的消息头部也丢失；或者是 msg1 的消
        // 息头部丢失导致 acc 未分配足够空间。
        if (NN_UNLIKELY(offset > acc.size())) {
            NN_LOG_ERROR("SyncSpliceMessage: the fragment is from another msg, or the first fragment is lost. offset = "
                         << offset << ", totalLength = " << acc.size());
            return SER_SPLIT_INVALID_MSG;
        }

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(acc.data()) + offset),
                                 acc.size() - offset, payload, payloadLen) != EOK)) {
            NN_LOG_ERROR("SyncSpliceMessage: the payload is too large.");
            return SER_SPLIT_INVALID_MSG;
        }

        // 拼包完成，如果数据在网络层面发现异常, SyncEp 可能会一直阻塞.
        if (offset + payloadLen == totalLength) {
            NN_LOG_DEBUG("SyncSpliceMessage: complete! id " << msgId);

            data = const_cast<char *>(acc.data());
            dataLen = acc.size();
            break;
        }

        result = ep->Receive(timeout, ctx);
        if (NN_UNLIKELY(result != SER_OK)) {
            NN_LOG_ERROR("Channel sync call receive failed " << result << " ep id " << ep->Id());
            return result;
        }

        // 在大包还未拼成、接收小包过程中出现 RAW 包，可能是尾部小包丢失。只能将
        // 此 RAW 包丢弃，用户状态机可能会发生错误。
        switch (ctx.Header().extHeaderType) {
            case UBSHcomExtHeaderType::RAW:
                NN_LOG_ERROR(
                    "SyncSpliceMessage: a RAW type msg is received during SpliceMessage, it will be discarded.");
                return SER_ERROR;

            case UBSHcomExtHeaderType::FRAGMENT:
                break;
        }
    }

    return SER_OK;
}

}  // namespace hcom
}  // namespace ock
