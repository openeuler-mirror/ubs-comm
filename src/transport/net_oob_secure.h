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
#ifndef OCK_HCOM_OOB_SECURE_PROCESS_H
#define OCK_HCOM_OOB_SECURE_PROCESS_H

#include "net_oob.h"

namespace ock {
namespace hcom {

class OOBSecureProcess {
public:
    /*
     * There are 32 cases for sec info process, please refer readme of this project
     */
    static NResult SecProcessInOOBClient(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
        const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection *conn,
        const std::string &driverName, uint64_t ctx, UBSHcomNetDriverSecType secType);

    static NResult SecProcessInOOBServer(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
        const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection &conn,
        const std::string &driverName, UBSHcomNetDriverSecType sType);

    static NResult SecProcessCompareEpNum(uint32_t localIpAddr, uint32_t listenPort, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static void SecProcessAddEpNum(uint32_t localIpAddr, uint32_t listenPort, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static void SecProcessDelEpNum(uint32_t localIpAddr, uint32_t listenPort, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static NResult SecProcessCompareEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static void SecProcessAddEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static void SecProcessDelEpNum(const std::string &localUdsName, const std::string &mIpAndPort,
        const std::vector<NetOOBServer *> &oobServers);

    static NResult SecCheckConnectionHeader(const ConnectHeader &header, const UBSHcomNetDriverOptions &option,
        const bool &enableTls, const UBSHcomNetDriverProtocol &protocol, const uint32_t &majorVersion,
        const uint32_t &minorVersion, ConnRespWithUId &respWithUId);

private:
    /*
     * Send sec info to peer via oob connection
     * step1: call sec info provider to create sec info
     * step2: send header to peer, always send, no matter sec info validate is enabled or not
     * step3: send sec info to peer
     *
     * In 1 way authentication case: only oob client calls this
     * In 2 ways authentications case: both oob client and oob sever calls this
     */
    static NResult SendSecInfo(const UBSHcomNetDriverEndpointSecInfoProvider &secInfoProvider,
        const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator, OOBTCPConnection *conn,
        const std::string &driverName, UBSHcomNetDriverSecType &secType, uint64_t ctx);

    /*
     * Validate sec info from peer via oob connection
     * step1: receive head from peer, always receive, no matter sec info validate is enabled or not
     * step2: receive sec info
     * step3: call sec info validator to validate sec info
     *
     * In 1 way authentication case: only oob server calls this
     * In 2 ways authentications case: both oob server and oob client calls this
     */
    static NResult ValidateSecInfo(const UBSHcomNetDriverEndpointSecInfoValidator &secInfoValidator,
        OOBTCPConnection &conn, const std::string &driverName, UBSHcomNetDriverSecType &secType,
        uint64_t &ctx, UBSHcomNetDriverSecType sType);
};

}
}

#endif