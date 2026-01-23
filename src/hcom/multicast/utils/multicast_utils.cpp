/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
*/
#include "multicast_utils.h"
#include "net_common.h"

namespace ock {
namespace hcom {

std::string MulticastUtils::GetFilteredDeviceIP(const std::string& ipMask)
{
    std::string res;
    std::vector<std::string> filterVec;
    NetFunc::NN_SplitStr(ipMask, ",", filterVec);
    if (filterVec.empty()) {
        NN_LOG_WARN("Invalid ip mask " << ipMask);
        return res;
    }

    std::vector<std::string> filteredIp;
    for (auto &mask : filterVec) {
        FilterIp(mask, filteredIp);
    }

    if (filteredIp.empty()) {
        NN_LOG_WARN("No matched ip found with " << ipMask);
        return res;
    }

    res = filteredIp[0];
    return res;
}

bool MulticastUtils::ParseUrl(const std::string &url, std::string &ip, uint16_t &port)
{
    NetProtocol protocal;
    std::string urlSuffix;

    std::string separator("://");
    std::string::size_type pos = url.find(separator);
    if (NN_UNLIKELY(pos == std::string::npos)) {
        NN_LOG_ERROR("Invalid url, must be like tcp://127.0.0.1:9981");
        return false;
    }

    std::string protoStr = url.substr(0, pos);
    if (protoStr != "tcp") {
        return false;
    }

    std::string tmpUrl = url.substr(pos + separator.size());
    if (NN_UNLIKELY(!NetFunc::NN_ConvertIpAndPort(tmpUrl, ip, port))) {
        NN_LOG_ERROR("Invalid url: " << url <<" should be like 127.0.0.1:9981");
        return false;
    }

    return true;
}

}
}