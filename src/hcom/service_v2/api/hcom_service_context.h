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
#ifndef HCOM_API_HCOM_CONTEXT_H_
#define HCOM_API_HCOM_CONTEXT_H_
#include <cstdint>
#include "hcom_service_def.h"
#include "hcom_service_channel.h"

namespace ock {
namespace hcom {

using UBSHcomChannelPtr = NetRef<UBSHcomChannel>;
using UBSHcomRequestContext = UBSHcomNetRequestContext;

constexpr uint16_t EPIDX_BIT_NUM = 32;

enum class Operation : uint8_t {
    SER_RECEIVED = 0,     /* support invoke all functions */
    SER_RECEIVED_RAW = 1, /* support invoke most functions except OpInfo() */
    SER_SENT = 2,         /* support invoke basic functions except
                                Message()、MessageData()、MessageDataLen()、RspCtx()、all ReplySend*() */
    SER_SENT_RAW = 3,     /* support invoke basic functions except
                                Message()、MessageData()、MessageDataLen()、RspCtx()、OpInfo()、all ReplySend*() */
    SER_ONE_SIDE = 4,     /* support invoke basic functions except
                                Message()、MessageData()、MessageDataLen()、RspCtx()、OpInfo()、all ReplySend*() */
    SER_RNDV = 5,         /* support invoke all functions */
    SER_RNDV_SGL = 6,     /* support invoke all functions */
    SER_MULTIRAIL_RNDV_RAW = 7,     /* support invoke all functions */
    SER_INVALID_OP_TYPE = 255,      /* support invoke all functions */
};

/* *
 * @brief Context of request received, operationInfo/message/channel can be got from it,
 * and reply message with it
 */
class UBSHcomServiceContext {
public:
    enum Operation : uint8_t {
        SER_RECEIVED = 0,     /* support invoke all functions */
        SER_RECEIVED_RAW = 1, /* support invoke most functions except OpInfo() */
        SER_SENT = 2,         /* support invoke basic functions except
                                 Message()、MessageData()、MessageDataLen()、RspCtx()、all ReplySend*() */
        SER_SENT_RAW = 3,     /* support invoke basic functions except
                                 Message()、MessageData()、MessageDataLen()、RspCtx()、OpInfo()、all ReplySend*() */
        SER_ONE_SIDE = 4,     /* support invoke basic functions except
                                 Message()、MessageData()、MessageDataLen()、RspCtx()、OpInfo()、all ReplySend*() */
        SER_RNDV = 5,
        SER_INVALID_OP_TYPE = 255,
    };

    /**
     * @brief Get result of the operation
     */
    SerResult Result() const;

    /**
     * @brief Get the channel ptr
     */
    const UBSHcomChannelPtr &Channel() const;

    /**
     * @brief Get the operation type
     * @return SER_INVALID_OP_TYPE if failed
     */
    Operation OpType() const;

    /**
     * @brief Get response context for send rsp message in other thread
     * note: only support SER_RECEIVED/SER_RECEIVED_RAW invoke
     * @return ture if success, false if failed
     */
    uintptr_t RspCtx() const;

    /**
     * @brief Get op code by user input
     * note: only support SER_SENT/SER_RECEIVED invoke
     * @return 0~999 if success, others if failed
     */
    uint16_t OpCode() const;

    int32_t ErrorCode() const;

    /**
     * @brief Get the message data received which valid in callback lifetime
     * note1: only support SER_RECEIVED/SER_RECEIVED_RAW invoke
     * note2: if user want to use message in other thread, need to copy message.data by self or invoke clone()
     * @return valid address if success, nullptr if failed
     */
    void *MessageData() const;

    /**
     * @brief Get the message data received which valid in callback lifetime
     * note1: only support SER_RECEIVED/SER_RECEIVED_RAW invoke
     * @return valid length if success, 0 if failed
     */
    uint32_t MessageDataLen() const;

    /**
     * @brief clone service context
     * @param copyData : true means malloc and copy receive data.
     * @return SER_OK if success, others if failed
     */
    static SerResult Clone(UBSHcomServiceContext &newOne,
                           const UBSHcomServiceContext &oldOne,
                           bool copyData = true);

    /**
     * @brief check current context timeout or not
     * @return true if timeout, false if not timeout
     */
    bool IsTimeout() const;

    void Invalidate();

    virtual ~UBSHcomServiceContext()
    {
        Invalidate();
    }

    UBSHcomServiceContext() = default;

protected:
    UBSHcomServiceContext(const UBSHcomRequestContext &ctx, UBSHcomChannel *ch);

    enum DataType : uint8_t {
        OUTER_DATA = 0,    /* assign by UBSHcomRequestContext.Message()->Data() */
        MEM_POOL_DATA = 1, /* alloc from channel mem pool */

        INVALID_DATA = 255,
    };

    SerResult CopyData(void *data, uint32_t dataLen);

    UBSHcomChannelPtr mCh;       /* channel ptr */
    uint64_t mTimeoutTraceMs = 0; /* record timeout time trace, 0 means never timeout */
    void *mData = nullptr; /* received/received raw message data address */
    uint32_t mDataLen = 0; /* received/received raw message data len */
    int32_t mErrorCode;
    int32_t mResult = 0;     /* context result */
    uint32_t mEpIdxInCh = 0; /* for response */
    uint32_t mSeqNo = 0;     /* for response */
    uint32_t mReadCount = 0;
    uint16_t mOpCode;
    UBSHcomRequestContext::NN_OpType mOpType =
            UBSHcomRequestContext::NN_INVALID_OP_TYPE; /* operate original type */
    DataType mDataType = DataType::INVALID_DATA; /* type of mData */
    // 64B cache rsv 12 Bytes

    uint8_t rsv[12];

    friend class HcomServiceImp;
    friend class UBSHcomChannel;
    friend class HcomServiceGlobalObject;
    friend class HcomPeriodicManager;
    friend class HcomChannelImp;
};

inline SerResult UBSHcomServiceContext::Result() const
{
    return mResult;
}

inline const UBSHcomChannelPtr &UBSHcomServiceContext::Channel() const
{
    return mCh;
}

inline UBSHcomServiceContext::Operation UBSHcomServiceContext::OpType() const
{
    switch (mOpType) {
        case UBSHcomRequestContext::NN_SENT:
            return Operation::SER_SENT;
        case UBSHcomRequestContext::NN_SENT_RAW:
        case UBSHcomRequestContext::NN_SENT_RAW_SGL:
            return Operation::SER_SENT_RAW;
        case UBSHcomRequestContext::NN_RECEIVED:
            return Operation::SER_RECEIVED;
        case UBSHcomRequestContext::NN_RECEIVED_RAW:
            return Operation::SER_RECEIVED_RAW;
        case UBSHcomRequestContext::NN_WRITTEN:
        case UBSHcomRequestContext::NN_SGL_WRITTEN:
        case UBSHcomRequestContext::NN_READ:
        case UBSHcomRequestContext::NN_SGL_READ:
            return Operation::SER_ONE_SIDE;
        case UBSHcomRequestContext::NN_RNDV:
            return Operation::SER_RNDV;
        default:
            return Operation::SER_INVALID_OP_TYPE;
    }
}

inline uintptr_t UBSHcomServiceContext::RspCtx() const
{
    /* 8byte: low 4 byte for seq no, high 4 byte for epIndex */
    uintptr_t rsp = mEpIdxInCh;
    rsp = (rsp << EPIDX_BIT_NUM) | mSeqNo;
    return rsp;
}

inline int32_t UBSHcomServiceContext::ErrorCode() const
{
    return mErrorCode;
}

inline uint16_t UBSHcomServiceContext::OpCode() const
{
    return mOpCode;
}

inline void *UBSHcomServiceContext::MessageData() const
{
    return mData;
}

inline uint32_t UBSHcomServiceContext::MessageDataLen() const
{
    return mDataLen;
}

inline void UBSHcomServiceContext::Invalidate()
{
    if (mDataType == MEM_POOL_DATA && mData != nullptr) {
        free(mData);
        mData = nullptr;
        mDataType = DataType::INVALID_DATA;
    }
    mCh.Set(nullptr);
}

}
}
#endif // HCOM_CONTEXT_H