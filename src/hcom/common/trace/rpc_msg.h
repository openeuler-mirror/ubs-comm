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

#ifndef RPC_MSG_H
#define RPC_MSG_H
#include <stdint.h>

#define VERSION 1
#define MAGIC_CODE 0xABABABAB
#define INVALID_OPCODE 0xFFFFFFFF

namespace ock {
namespace hcom {

struct MessageHeader {
    uint32_t version = VERSION;
    uint32_t magicCode = MAGIC_CODE;
    uint32_t crc = 0;
    uint32_t opcode = INVALID_OPCODE;
    uint32_t bodySize = 0;
    uint32_t reserved = 0;
    explicit MessageHeader(uint32_t opcode) : opcode(opcode) {}
};

class Message {
public:
    Message(void *data, uint32_t dataSize) : mData(data), mSize(dataSize) {}
    Message() : Message(nullptr, 0) {}

    void *GetData() const
    {
        return mData;
    }

    void SetMsg(void *data, uint32_t size)
    {
        mData = data;
        mSize = size;
    }

    uint32_t GetSize() const
    {
        return mSize;
    }

    const MessageHeader *GetHeader() const
    {
        if (mData == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<MessageHeader *>(mData);
    }

private:
    void *mData = nullptr;
    uint32_t mSize = 0;
};

class MessageValidator {
public:
    static bool Validate(const Message &message)
    {
        void *messageData = message.GetData();
        uint32_t messageSize = message.GetSize();
        if (messageData == nullptr || messageSize == 0) {
            return false;
        }

        MessageHeader *header = reinterpret_cast<MessageHeader *>(messageData);
        if (header->version != VERSION ||
            header->magicCode != MAGIC_CODE ||
            header->bodySize + sizeof(MessageHeader) > messageSize) {
            return false;
        }
        return true;
    }
};

}
}

#endif // RPC_MSG_H