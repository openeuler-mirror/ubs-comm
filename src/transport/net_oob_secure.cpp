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
#include "net_oob_secure.h"

namespace ock {
namespace hcom {
NResult OOBSecureProcess::SecProcessCompareEpNum(uint32_t localIpAddr, uint32_t listenPort,
    const std::string &mIpAndPort, const std::vector<NetOOBServer *> &oobServers)
{
    struct sockaddr_in addr {};
    bzero(&addr, sizeof(addr));
    addr.sin_addr.s_addr = localIpAddr;
    char ipStr[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(addr.sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr) {
        NN_LOG_ERROR("Failed to convert ip number to string");
        return NN_INVALID_IP;
    }
    std::string localIP(ipStr);
    std::string ip;
    uint32_t result;
    uint16_t port;

    for (auto &oobServer : oobServers) {
        result = static_cast<uint32_t>(oobServer->GetListenIp(ip));
        result |= static_cast<uint32_t>(oobServer->GetListenPort(port));
        if (result != NN_OK) {
            continue;
        }
        if (ip == localIP || port == listenPort) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            return oobServer->CompareEpNum(remoteIp);
        }
    }

    return NN_OK;
}

NResult OOBSecureProcess::SecProcessCompareEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
    const std::vector<NetOOBServer *> &oobServers)
{
    std::string udsName;
    int result;

    for (auto &oobServer : oobServers) {
        result = oobServer->GetUdsName(udsName);
        if (result != NN_OK) {
            continue;
        }
        if (udsName == localUdsName) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            return oobServer->CompareEpNum(remoteIp);
        }
    }

    return NN_OK;
}

void OOBSecureProcess::SecProcessAddEpNum(uint32_t localIpAddr, uint32_t listenPort, const std::string &mIpAndPort,
    const std::vector<NetOOBServer *> &oobServers)
{
    struct sockaddr_in addr {};
    bzero(&addr, sizeof(addr));
    addr.sin_addr.s_addr = localIpAddr;
    char ipStr[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(addr.sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr) {
        NN_LOG_ERROR("Failed to convert ip number to string");
        return;
    }
    std::string localIP(ipStr);
    std::string ip;
    uint32_t result;
    uint16_t port;

    for (auto &oobServer : oobServers) {
        result = static_cast<uint32_t>(oobServer->GetListenIp(ip));
        result |= static_cast<uint32_t>(oobServer->GetListenPort(port));
        if (result != NN_OK) {
            continue;
        }
        if (ip == localIP || port == listenPort) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            oobServer->AddEpNum(remoteIp);
            break;
        }
    }
}

void OOBSecureProcess::SecProcessAddEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
    const std::vector<NetOOBServer *> &oobServers)
{
    std::string udsName;
    int result;

    for (auto &oobServer : oobServers) {
        result = oobServer->GetUdsName(udsName);
        if (result != NN_OK) {
            continue;
        }
        if (udsName == localUdsName) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            oobServer->AddEpNum(remoteIp);
            break;
        }
    }
}

void OOBSecureProcess::SecProcessDelEpNum(uint32_t localIpAddr, uint32_t listenPort, const std::string &mIpAndPort,
    const std::vector<NetOOBServer *> &oobServers)
{
    struct sockaddr_in addr {};
    bzero(&addr, sizeof(addr));
    addr.sin_addr.s_addr = localIpAddr;
    char ipStr[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(addr.sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr) {
        NN_LOG_ERROR("Failed to convert ip number to string");
        return;
    }
    std::string localIP(ipStr);
    std::string ip;
    uint16_t port;
    uint32_t result;

    for (auto &oobServer : oobServers) {
        result = static_cast<uint32_t>(oobServer->GetListenIp(ip));
        result |= static_cast<uint32_t>(oobServer->GetListenPort(port));
        if (result != NN_OK) {
            continue;
        }
        if (ip == localIP || port == listenPort) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            oobServer->DelEpNum(remoteIp);
            break;
        }
    }
}

void OOBSecureProcess::SecProcessDelEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
    const std::vector<NetOOBServer *> &oobServers)
{
    std::string udsName;
    int result;

    for (auto &oobServer : oobServers) {
        result = oobServer->GetUdsName(udsName);
        if (result != NN_OK) {
            continue;
        }
        if (udsName == localUdsName) {
            size_t pos = mIpAndPort.find(':');
            std::string remoteIp = mIpAndPort.substr(0, pos);
            oobServer->DelEpNum(remoteIp);
            break;
        }
    }
}

NResult OOBSecureProcess::SecProcessInOOBServer(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
    const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection &conn,
    const std::string &driverName, UBSHcomNetDriverSecType sType)
{
    int result = 0;
    auto secType = static_cast<UBSHcomNetDriverSecType>(0);
    ConnectResp resp = ConnectResp::OK;

    uint64_t ctx = 0;
    // validate secure info in oob client
    if (NN_UNLIKELY(ValidateSecInfo(secInfoValidator, conn, driverName, secType, ctx, sType) != NN_OK)) {
        resp = ConnectResp::SEC_VALID_FAILED;
        if (NN_UNLIKELY((result = conn.Send(&resp, sizeof(ConnectResp))) != NN_OK)) {
            NN_LOG_ERROR("Failed to send secure validate result to " << conn.GetIpAndPort() << " in driver " <<
                driverName);
            return NN_OOB_SEC_PROCESS_ERROR;
        }
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    // send validate result to oob client
    if (NN_UNLIKELY((result = conn.Send(&resp, sizeof(ConnectResp))) != NN_OK)) {
        NN_LOG_ERROR("Failed to send secure validate result to " << conn.GetIpAndPort() << " in driver " << driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    // two-way case server need to send secure info to client
    // and no need to receive resp, client validate ok will return directly
    if (secType == NET_SEC_VALID_TWO_WAY) {
        if (NN_UNLIKELY(SendSecInfo(secInfoProvider, secInfoValidator, &conn, driverName, secType, ctx) != NN_OK)) {
            return NN_OOB_SEC_PROCESS_ERROR;
        }
    }

    return NN_OK;
}

NResult OOBSecureProcess::SecProcessInOOBClient(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
    const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection *conn,
    const std::string &driverName, uint64_t ctx, UBSHcomNetDriverSecType sType)
{
    // create and send secure info
    int result = 0;
    auto secType = static_cast<UBSHcomNetDriverSecType>(0);
    if (sType == NET_SEC_DISABLED) {
        // send header no valid to server (case 5)
        ConnSecHeader header(0, 0, 0, secType);
        if (NN_UNLIKELY((result = conn->Send(&header, sizeof(ConnSecHeader))) != NN_OK)) {
            NN_LOG_ERROR("Failed to send conn secure header to oob server " << conn->GetIpAndPort() << " in driver " <<
                driverName);
            return NN_OOB_SEC_PROCESS_ERROR;
        }
    }

    // create and send secure info to oob server
    if (sType != NET_SEC_DISABLED) {
        if (NN_UNLIKELY(SendSecInfo(secInfoProvider, secInfoValidator, conn, driverName, secType, ctx) != NN_OK)) {
            return NN_OOB_SEC_PROCESS_ERROR;
        }
        NN_LOG_TRACE_INFO("Secure info send to peer oob " << conn->GetIpAndPort() << " successfully, in driver " <<
            driverName);
    }

    // receive oob server validate result
    ConnectResp resp = {};
    void *tmpRsp = &resp;
    if (NN_UNLIKELY((result = conn->Receive(tmpRsp, sizeof(ConnectResp))) != NN_OK)) {
        return result;
    }
    if (resp != ConnectResp::OK) {
        NN_LOG_ERROR("Received failed response:" << resp << " for validate secure info from " << conn->GetIpAndPort() <<
            " in driver " << driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    // two-way case oob client should validate secure info from oob server
    // oob client validate result response no need to send to oob server, server will not receive
    if (secType == NET_SEC_VALID_TWO_WAY) {
        uint64_t ctxBack = 0;
        if (NN_UNLIKELY(ValidateSecInfo(secInfoValidator, *conn, driverName, secType, ctxBack, sType) != NN_OK)) {
            return NN_OOB_SEC_PROCESS_ERROR;
        }
    }

    NN_LOG_TRACE_INFO("The verification is successful");
    return NN_OK;
}

NResult OOBSecureProcess::SendSecInfo(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
    const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection *conn,
    const std::string &driverName, UBSHcomNetDriverSecType &secType, uint64_t ctx)
{
    int result = 0;
    // two-way case server provider not set return error (case 13)
    if (NN_UNLIKELY(secInfoProvider == nullptr)) {
        NN_LOG_ERROR("Failed to send secure info as secure info provider is null and secure type is " <<
            UBSHcomNetDriverSecTypeToString(secType) << " in driver " << driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    if (NN_UNLIKELY(conn == nullptr)) {
        NN_LOG_ERROR("Failed to send secure info as conn is null");
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    char *output = nullptr;
    uint32_t outLen = 0;
    int64_t flag = 0;
    secType = static_cast<UBSHcomNetDriverSecType>(0);
    bool needAutoFree = false;
    result = secInfoProvider(ctx, flag, secType, output, outLen, needAutoFree);
    if (NN_UNLIKELY(outLen > NN_NO2147483646)) {
        NN_LOG_ERROR("The outLen value cannot be greater than 2147483646 in driver " << driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    // client provider registered but call provider failed, return error (case 1)
    // or server provider registered but call provider failed, return error (case 9)
    if (NN_UNLIKELY(result != 0)) {
        NN_LOG_ERROR("Failed to create secure info in driver " << driverName << " as do provider callback result is:" <<
            result);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    NetLocalAutoFreePtr<char> secInfoAutoFree(output, true);
    if (!needAutoFree) {
        secInfoAutoFree.SetNull();
    }

    if (secType != NET_SEC_VALID_ONE_WAY && secType != NET_SEC_VALID_TWO_WAY) {
        NN_LOG_ERROR("Failed to create secure info in driver " << driverName << ", as secure type:" <<
            UBSHcomNetDriverSecTypeToString(secType) << " in provider is invalid");
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    // two-way case client should register validator (case 8/10)
    if (secType == NET_SEC_VALID_TWO_WAY && secInfoValidator == nullptr) {
        NN_LOG_ERROR("Failed to create secure info in driver " << driverName << ", as secure type is:" <<
            UBSHcomNetDriverSecTypeToString(secType) << " but validator callback not set");
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    NN_LOG_TRACE_INFO("Secure info should send to server:" << output << " len:" << outLen << " flag:" << flag <<
        " ctx:" << ctx << " sec type:" << UBSHcomNetDriverSecTypeToString(secType));

    ConnSecHeader header(flag, ctx, outLen, secType);
    if (NN_UNLIKELY((result = conn->Send(&header, sizeof(ConnSecHeader))) != NN_OK)) {
        NN_LOG_ERROR("Failed to send conn secure header to oob server " << conn->GetIpAndPort() << " in driver " <<
            driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    if (NN_UNLIKELY((result = conn->Send(output, outLen)) != NN_OK)) {
        NN_LOG_ERROR("Failed to send conn secure info to oob server " << conn->GetIpAndPort() << " in driver " <<
            driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    return NN_OK;
}

NResult OOBSecureProcess::ValidateSecInfo(const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator,
    OOBTCPConnection &conn, const std::string &driverName, UBSHcomNetDriverSecType &secType, uint64_t &ctx,
    UBSHcomNetDriverSecType sType)
{
    int result = 0;
    ConnSecHeader header {};
    void *headerBuf = &header;
    if (NN_UNLIKELY((result = conn.Receive(headerBuf, sizeof(ConnSecHeader))) != 0)) {
        NN_LOG_ERROR("Failed to read secure header from " << conn.GetIpAndPort() << " in driver " << driverName <<
            ", result " << result);
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    ctx = header.ctx;
    if (header.type > NET_SEC_VALID_TWO_WAY) {
        NN_LOG_ERROR("Failed to validate header as secure type is invalid");
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    // oob client not register provider will send type 0 (case 5/6/7)
    if (header.type == 0) {
        // oob server not register validator, validate success (case 7)
        if (sType == NET_SEC_DISABLED) {
            return NN_OK;
        }

        // oob server register validator but client not register provider (case 5/6)
        NN_LOG_ERROR("Failed to validate header as secure type is 0, oob " << conn.GetIpAndPort() <<
            " may not set provider");
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    secType = static_cast<UBSHcomNetDriverSecType>(header.type);

    NN_LOG_TRACE_INFO("Secure header flag:" << header.flag << " ctx:" << header.ctx << " len:" << header.secInfoLen <<
        " sec type:" << UBSHcomNetDriverSecTypeToString(secType));
    if (NN_UNLIKELY(header.secInfoLen > NN_NO2147483646)) {
        NN_LOG_ERROR("Receive secInfoLen greater than 2147483646 in " << driverName);
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    char *secInfo = new (std::nothrow) char[header.secInfoLen + NN_NO1];
    if (NN_UNLIKELY(secInfo == nullptr)) {
        NN_LOG_ERROR("Failed to new buffer for sec info from peer, probably out of memory");
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    NetLocalAutoFreePtr<char> secInfoAutoFree(secInfo, true);
    void *secBuf = static_cast<void *>(secInfo);
    if (NN_UNLIKELY((result = conn.Receive(secBuf, header.secInfoLen)) != 0)) {
        NN_LOG_ERROR("Failed to read secure info from " << conn.GetIpAndPort() << " in driver " << driverName <<
            ", result " << result);
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    secInfo[header.secInfoLen] = '\0';

    // client provider registered but server validator not registered, validate pass (case 2)
    int validateResult = 0;
    if (NN_UNLIKELY(secInfoValidator == nullptr)) {
        NN_LOG_WARN("Validator is null and secure type is:" << UBSHcomNetDriverSecTypeToString(secType) <<
            " in driver " << driverName << " , skip secure info validate");
        return NN_OK;
    }

    validateResult = secInfoValidator(header.ctx, header.flag, secInfo, header.secInfoLen);
    // client provider and server validator registered, but server validator validate failed (case 3)
    // or two-way case server provider and client validator registered, client validator but validate failed (case 11)
    if (validateResult != 0) {
        NN_LOG_ERROR("Failed to validate secure info received from " << conn.GetIpAndPort() << " in driver " <<
            driverName << ", validate result is:" << validateResult);
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    // client provider and server validator registered and validate success, pass (case 4)
    // or two-way case server provider and client validator registered and validate success, pass (case 12)
    NN_LOG_INFO("Validate secure info from peer oob " << conn.GetIpAndPort() << " successfully, in driver " <<
        driverName);

    return NN_OK;
}

NResult OOBSecureProcess::SecCheckConnectionHeader(const ConnectHeader &header, const UBSHcomNetDriverOptions &option,
    const bool &enableTls, const UBSHcomNetDriverProtocol &protocol, const uint32_t &majorVersion,
    const uint32_t &minorVersion, ConnRespWithUId &respWithUId)
{
    if (header.magic != option.magic) {
        NN_LOG_ERROR("Failed to match magic number from client, connection refused header.magic");
        respWithUId.connResp = MAGIC_MISMATCH;
        return NN_ERROR;
    }

    if (header.protocol != protocol) {
        NN_LOG_ERROR("Failed to match protocol " << protocol << " from client " << header.protocol <<
            ", connection refused");
        respWithUId.connResp = PROTOCOL_MISMATCH;
        return NN_ERROR;
    }

    if (header.majorVersion != majorVersion) {
        NN_LOG_ERROR("Failed to match majorVersion " << majorVersion << " from client " <<
            header.majorVersion << ", connection refused");
        respWithUId.connResp = VERSION_MISMATCH;
        return VERSION_MISMATCH;
    }

    if (header.minorVersion > minorVersion) {
        NN_LOG_ERROR("Failed to match minorVersion " << minorVersion << " from client " <<
            header.minorVersion << ", connection refused");
        respWithUId.connResp = VERSION_MISMATCH;
        return VERSION_MISMATCH;
    }

    if (enableTls) {
        if (header.tlsVersion < TLS_1_2 || header.tlsVersion > TLS_1_3) {
            NN_LOG_ERROR("Failed to match tls version from client " << header.tlsVersion <<
                ", connection refused");
            respWithUId.connResp = TLS_VERSION_MISMATCH;
            return NN_ERROR;
        }
    }

    return NN_OK;
}
}
}