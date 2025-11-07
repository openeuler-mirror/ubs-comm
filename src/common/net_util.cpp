// Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
// Author: bao

#include <fstream>
#include "net_common.h"
#include "net_util.h"

namespace ock {
namespace hcom {
bool BuffToHexString(void *buff, uint32_t buffSize, std::string &out)
{
    static const std::string hex = "0123456789ABCDEF";

    if (NN_UNLIKELY(buff == nullptr)) {
        NN_LOG_ERROR("Invalid buff ptr for serialize as buff is nullptr");
        return false;
    }

    if (NN_UNLIKELY(buffSize > UINT32_MAX / NN_NO2)) {
        NN_LOG_ERROR("Invalid buff size as is over half of UINT32_MAX");
        return false;
    }

    auto tmpBuff = reinterpret_cast<uint8_t *>(buff);
    out.clear();
    out.reserve(buffSize * NN_NO2);

    for (uint32_t i = 0; i < buffSize; i++) {
        // push back high 4 bit
        out.push_back(hex[static_cast<unsigned char>(tmpBuff[i]) >> NN_NO4]);
        // push back low 4 bit
        out.push_back(hex[static_cast<unsigned char>(tmpBuff[i]) & 0xF]);
    }

    return true;
}

bool HexStringToBuff(const std::string &str, uint32_t buffSize, void *buff)
{
    if (NN_UNLIKELY(buff == nullptr)) {
        NN_LOG_ERROR("Invalid buff ptr for serialize as buff is nullptr");
        return false;
    }

    if (NN_UNLIKELY(buffSize > UINT32_MAX / NN_NO2) || NN_UNLIKELY(str.size() < buffSize * NN_NO2)) {
        NN_LOG_ERROR("Invalid str or buff size is over half of UINT32_MAX");
        return false;
    }

    auto tmpBuff = reinterpret_cast<uint8_t *>(buff);
    for (uint32_t i = 0; i < buffSize * NN_NO2; i += NN_NO2) {
        std::string byte = str.substr(i, NN_NO2);
        char *remain = nullptr;
        long value = strtol(byte.c_str(), &remain, NN_NO16);
        if (NN_UNLIKELY(remain == nullptr || strlen(remain) > 0 || value > NN_NO255)) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to get value as " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return false;
        }
        tmpBuff[i / NN_NO2] = value;
    }

    return true;
}

uint32_t GenerateSecureRandomUint32()
{
    uint32_t rand = 0;
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if (!urandom.is_open()) {
        NN_LOG_ERROR("Failed to open urandom");
    }
    urandom.read(reinterpret_cast<char*>(&rand), sizeof(uint32_t));
    if (!urandom) {
        urandom.close();
        NN_LOG_ERROR("Failed to read from urandom");
    }
    urandom.close();
    return rand;
}
} // namespace hcom
} // namespace ock
