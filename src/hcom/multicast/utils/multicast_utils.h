/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
*/
#ifndef HCOM_MULTICAST_UTILS_H
#define HCOM_MULTICAST_UTILS_H

#include <string>

namespace ock {
namespace hcom {

class MulticastUtils {
public:
    static std::string GetFilteredDeviceIP(const std::string& ipMask);
    static bool ParseUrl(const std::string &url, std::string &ip, uint16_t &port);
};

}
}
#endif