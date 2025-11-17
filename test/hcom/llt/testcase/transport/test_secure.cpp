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
#include "test_secure.h"
#include "hcom.h"
#include "net_sock_common.h"

using namespace ock::hcom;

#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"
int g_ipPort = 6550;
static UBSHcomNetEndpointPtr serverEp;

static int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    serverEp = newEP;
    return 0;
}
static void EndPointBroken(const UBSHcomNetEndpointPtr &ep) {}
static int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}
static int RequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}
static int OneSideDone(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

// one way, provider registered, return valid
static int SecInfoProviderValidOne(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
    uint32_t &outLen, bool &needAutoFree)
{
    const char *kToken = "6G5NXCPJZB-"
        "eyJsaWNlbnNlSWQiOiI2RzVOWENQSlpCIiwibGljZW5zZWVOYW1lIjoic2lnbnVwIHNjb290ZXIiLCJhc3NpZ25lZU5hbWUiOiIiLCJhc3NpZ2"
        "5lZUVtYWlsIjoiIiwibGljZW5zZVJlc3RyaWN0aW9uIjoiIiwiY2hlY2tDb25jdXJyZW50VXNlIjpmYWxzZSwicHJvZHVjdHMiOlt7ImNvZGUi"
        "OiJQU0kiLCJmYWxsYmFja0RhdGUiOiIyMDI1LTA4LTAxIiwicGFpZFVwVG8iOiIyMDI1LTA4LTAxIiwiZXh0ZW5kZWQiOnRydWV9LHsiY29kZS"
        "I6IlBEQiIsImZhbGxiYWNrRGF0ZSI6IjIwMjUtMDgtMDEiLCJwYWlkVXBUbyI6IjIwMjUtMDgtMDEiLCJleHRlbmRlZCI6dHJ1ZX0seyJjb2Rl"
        "IjoiSUkiLCJmYWxsYmFja0RhdGUiOiIyMDI1LTA4LTAxIiwicGFpZFVwVG8iOiIyMDI1LTA4LTAxIiwiZXh0ZW5kZWQiOmZhbHNlfSx7ImNvZG"
        "UiOiJQUEMiLCJmYWxsYmFja0RhdGUiOiIyMDI1LTA4LTAxIiwicGFpZFVwVG8iOiIyMDI1LTA4LTAxIiwiZXh0ZW5kZWQiOnRydWV9LHsiY29k"
        "ZSI6IlBHTyIsImZhbGxiYWNrRGF0ZSI6IjIwMjUtMDgtMDEiLCJwYWlkVXBUbyI6IjIwMjUtMDgtMDEiLCJleHRlbmRlZCI6dHJ1ZX0seyJjb2"
        "RlIjoiUFNXIiwiZmFsbGJhY2tEYXRlIjoiMjAyNS0wOC0wMSIsInBhaWRVcFRvIjoiMjAyNS0wOC0wMSIsImV4dGVuZGVkIjp0cnVlfSx7ImNv"
        "ZGUiOiJQV1MiLCJmYWxsYmFja0RhdGUiOiIyMDI1LTA4LTAxIiwicGFpZFVwVG8iOiIyMDI1LTA4LTAxIiwiZXh0ZW5kZWQiOnRydWV9LHsiY2"
        "9kZSI6IlBQUyIsImZhbGxiYWNrRGF0ZSI6IjIwMjUtMDgtMDEiLCJwYWlkVXBUbyI6IjIwMjUtMDgtMDEiLCJleHRlbmRlZCI6dHJ1ZX0seyJj"
        "b2RlIjoiUFJCIiwiZmFsbGJhY2tEYXRlIjoiMjAyNS0wOC0wMSIsInBhaWRVcFRvIjoiMjAyNS0wOC0wMSIsImV4dGVuZGVkIjp0cnVlfSx7Im"
        "NvZGUiOiJQQ1dNUCIsImZhbGxiYWNrRGF0ZSI6IjIwMjUtMDgtMDEiLCJwYWlkVXBUbyI6IjIwMjUtMDgtMDEiLCJleHRlbmRlZCI6dHJ1ZX1d"
        "LCJtZXRhZGF0YSI6IjAxMjAyMjA5MDJQU0FOMDAwMDA1IiwiaGFzaCI6IlRSSUFMOi0xMDc4MzkwNTY4IiwiZ3JhY2VQZXJpb2REYXlzIjo3LC"
        "JhdXRvUHJvbG9uZ2F0ZWQiOmZhbHNlLCJpc0F1dG9Qcm9sb25nYXRlZCI6ZmFsc2V9-SnRVlQQR1/"
        "9nxZ2AXsQ0seYwU5OjaiUMXrnQIIdNRvykzqQ0Q+"
        "vjXlmO7iAUwhwlsyfoMrLuvmLYwoD7fV8Mpz9Gs2gsTR8DfSHuAdvZlFENlIuFoIqyO8BneM9paD0yLxiqxy/"
        "WWuOqW6c1v9ubbfdT6z9UnzSUjPKlsjXfq9J2gcDALrv9E0RPTOZqKfnsg7PF0wNQ0/d00dy1k3zI+zJyTRpDxkCaGgijlY/LZ/wqd/"
        "kRfcbQuRzdJ/JXa3nj26rACqykKXaBH5thuvkTyySOpZwZMJVJyW7B7ro/"
        "hkFCljZug3K+bTw5VwySzJtDcQ9tDYuu0zSAeXrcv2qrOg==-"
        "MIIETDCCAjSgAwIBAgIBDTANBgkqhkiG9w0BAQsFADAYMRYwFAYDVQQDDA1KZXRQcm9maWxlIENBMB4XDTIwMTAxOTA5MDU1M1oXDTIyMTAyMT"
        "A5MDU1M1owHzEdMBsGA1UEAwwUcHJvZDJ5LWZyb20tMjAyMDEwMTkwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCUlaUFc1wf+"
        "CfY9wzFWEL2euKQ5nswqb57V8QZG7d7RoR6rwYUIXseTOAFq210oMEe++LCjzKDuqwDfsyhgDNTgZBPAaC4vUU2oy+XR+"
        "Fq8nBixWIsH668HeOnRK6RRhsr0rJzRB95aZ3EAPzBuQ2qPaNGm17pAX0Rd6MPRgjp75IWwI9eA6aMEdPQEVN7uyOtM5zSsjoj79Lbu1fjShOn"
        "QZuJcsV8tqnayeFkNzv2LTOlofU/Tbx502Ro073gGjoeRzNvrynAP03pL486P3KCAyiNPhDs2z8/COMrxRlZW5mfzo0xsK0dQGNH3UoG/"
        "9RVwHG4eS8LFpMTR9oetHZBAgMBAAGjgZkwgZYwCQYDVR0TBAIwADAdBgNVHQ4EFgQUJNoRIpb1hUHAk0foMSNM9MCEAv8wSAYDVR0jBEEwP4A"
        "Uo562SGdCEjZBvW3gubSgUouX8bOhHKQaMBgxFjAUBgNVBAMMDUpldFByb2ZpbGUgQ0GCCQDSbLGDsoN54TATBgNVHSUEDDAKBggrBgEFBQcDA"
        "TALBgNVHQ8EBAMCBaAwDQYJKoZIhvcNAQELBQADggIBABqRoNGxAQct9dQUFK8xqhiZaYPd30TlmCmSAaGJ0eBpvkVeqA2jGYhAQRqFiAlFC63"
        "JKvWvRZO1iRuWCEfUMkdqQ9VQPXziE/"
        "BlsOIgrL6RlJfuFcEZ8TK3syIfIGQZNCxYhLLUuet2HE6LJYPQ5c0jH4kDooRpcVZ4rBxNwddpctUO2te9UU5/"
        "FjhioZQsPvd92qOTsV+8Cyl2fvNhNKD1Uu9ff5AkVIQn4JU23ozdB/R5oUlebwaTE6WZNBs+TA/qPj+5/"
        "we9NH71WRB0hqUoLI2AKKyiPw++FtN4Su1vsdDlrAzDj9ILjpjJKA1ImuVcG329/"
        "WTYIKysZ1CWK3zATg9BeCUPAV1pQy8ToXOq+RSYen6winZ2OO93eyHv2Iw5kbn1dqfBw1BuTE29V2FJKicJSu8iEOpfoafwJISXmz1wnnWL3V/"
        "0NxTulfWsXugOoLfv0ZIBP1xH9kmf22jjQ2JiHhQZP7ZDsreRrOeIQ/"
        "c4yR8IQvMLfC0WKQqrHu5ZzXTH4NO3CwGWSlTY74kE91zXB5mwWAx1jig+UXYc2w4RkVhy0//lOmVya/"
        "PEepuuTTI4+UJwC7qbVlh5zfhj8oTNUXgN0AOc+Q0/WFPl1aw5VV/VrO8FCoB15lFVlpKaQ1Yh+DVU8ke+rt9Th0BCHXe0uZOEmH0nOnH/"
        "0onD";
    flag = 1;
    output = const_cast<char *>(kToken);
    outLen = strlen(kToken);
    type = ock::hcom::NET_SEC_VALID_ONE_WAY;
    needAutoFree = false;
    NN_LOG_INFO("client auth info " << output << " len:" << outLen << " flag:" << flag << " sec type:" <<
        UBSHcomNetDriverSecTypeToString(type));
    return 0;
}

// two way, provider registered, return valid
static int SecInfoProviderValidTwo(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
    uint32_t &outLen, bool &needAutoFree)
{
    const char *kToken = "clientservertoken";
    flag = 1;
    output = const_cast<char *>(kToken);
    outLen = strlen(kToken);
    type = ock::hcom::NET_SEC_VALID_TWO_WAY;
    needAutoFree = false;
    NN_LOG_INFO("client auth info " << output << " len:" << outLen << " flag:" << flag << " sec type:" <<
        UBSHcomNetDriverSecTypeToString(type));
    return 0;
}

// token is empty string
static int SecInfoProviderValid(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
    uint32_t &outLen, bool &needAutoFree)
{
    const char *kToken = "";
    flag = 1;
    output = const_cast<char *>(kToken);
    outLen = strlen(kToken);
    type = ock::hcom::NET_SEC_VALID_ONE_WAY;
    needAutoFree = false;
    NN_LOG_INFO("client auth info " << output << " len:" << outLen << " flag:" << flag << " sec type:" <<
        UBSHcomNetDriverSecTypeToString(type));
    return 0;
}

// provider not registered, return valid
static int ProviderValid(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
    uint32_t &outLen, bool &needAutoFree)
{
    NN_LOG_WARN("client provider is not registered, but return valid");
    return 0;
}

// provider not registered, return invalid
static int SecInfoProviderInvalid(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
    uint32_t &outLen, bool &needAutoFree)
{
    NN_LOG_ERROR("invalid sec info");
    return -1;
}

// validator register, return valid
static int AuthValidatorValid(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    if (input != nullptr) {
        NN_LOG_INFO("client auth validate flag:" << flag);
    } else {
        NN_LOG_INFO("client auth validate flag:" << flag << " input:" << input << " input Len:" << inputLen);
    }
    return 0;
}

// validator not register, return valid
static int ValidatorValid(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    NN_LOG_WARN("server validator is not registered, but return valid");
    return 0;
}

// validator register, return invalid
static int AuthValidatorInvalid(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    NN_LOG_ERROR("Client authentication failed");
    return -1;
}

static NResult SendSingleRequest(UBSHcomNetEndpointPtr clientEp)
{
    std::string value = "hello world";
    NResult result = NN_OK;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = clientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return result;
    }
    return NN_OK;
}

static void SetCB(UBSHcomNetDriver *&driver, uint16_t port, bool isServer,
    const UBSHcomNetDriverEndpointSecInfoProvider &SecInfoProvider,
    const UBSHcomNetDriverEndpointSecInfoValidator &SecInfoValidator)
{
    if (isServer) {
        driver->RegisterNewEPHandler(
            std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    if (SecInfoProvider == nullptr) {
        driver->RegisterEndpointSecInfoProvider(nullptr);
    } else {
        driver->RegisterEndpointSecInfoProvider(std::bind(SecInfoProvider, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    }

    if (SecInfoValidator == nullptr) {
        driver->RegisterEndpointSecInfoValidator(nullptr);
    } else {
        driver->RegisterEndpointSecInfoValidator(std::bind(SecInfoValidator, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    driver->OobIpAndPort(BASE_IP, port);
}

static void SetDriverOptions(UBSHcomNetDriverOptions &sockOptions)
{
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    sockOptions.SetNetDeviceIpMask(IP_SEG);
    sockOptions.pollingBatchSize = 16;
    sockOptions.SetWorkerGroups("1");
    sockOptions.SetWorkerGroupsCpuSet("10-10");
    sockOptions.enableTls = false;
}

static void CloseDriver(UBSHcomNetDriver *&driver)
{
    if (driver->IsStarted()) {
        driver->Stop();
        driver->UnInitialize();
    }
}

// client : (provider, invalid) | server : (/, Y/N) | failed
TEST_F(TestSecure, OneWayCase1)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    /* client is registered, return invalid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_1", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderInvalid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();

    /* 1-1 server validator is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_1", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(sDriver);

    /* server validator is not registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_1", true);
    SetCB(sDriver, g_ipPort, true, nullptr, ValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(sDriver);

    /* 1-3 server validator is registered, return invalid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_1", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorInvalid);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(sDriver);

    /* 1-2 server validator is not registered, set nullptr */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_1", true);
    SetCB(sDriver, g_ipPort, true, nullptr, nullptr);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(sDriver);

    CloseDriver(cDriver);
}

// client : (provider, valid) | server : (/, Y/N) | pass
TEST_F(TestSecure, OneWayCase2)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    /* client provider is registered, return valid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_2", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidOne, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();

    /* 2-1 server validator is not registered, set nullptr */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_2", true);
    SetCB(sDriver, g_ipPort, true, nullptr, nullptr);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);
    CloseDriver(sDriver);

    /* 2-2 server validator is not registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_2", true);
    SetCB(sDriver, g_ipPort, true, nullptr, ValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);
    CloseDriver(sDriver);

    CloseDriver(cDriver);
}

// client : (provider, valid) | server : (validator, invalid) | failed
TEST_F(TestSecure, OneWayCase3)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    /* client provider is registered, return valid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_3", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidOne, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();

    /* 3-1 server validator is registered, return invalid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_3", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorInvalid);
    sDriver->Initialize(options);
    sDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);

    CloseDriver(sDriver);
    CloseDriver(cDriver);
}

// client : (provider, valid) | server: (validator, valid) | pass
TEST_F(TestSecure, OneWayCase4)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;

    /* client provider is registered, return valid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_4", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidOne, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();

    /* 4-1 server validator is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_4", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);
    result = SendSingleRequest(clientEp);
    EXPECT_EQ(result, NN_OK);

    CloseDriver(sDriver);
    CloseDriver(cDriver);
}

// client : (/, valid) | server : (validator, valid) | failed
TEST_F(TestSecure, OneWayCase5)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    /* server validator is registered, return valid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_5", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 5-1 client provider is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_5", false);
    SetCB(cDriver, g_ipPort, false, ProviderValid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 5-2 client provider is nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_5", false);
    SetCB(cDriver, g_ipPort, false, nullptr, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);

    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (/, valid) | server : (validator, invalid) | failed
TEST_F(TestSecure, OneWayCase6)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    /* server validator is registered, but return invalid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_6", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorInvalid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 6-1 client provider is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_6", false);
    SetCB(cDriver, g_ipPort, false, ProviderValid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 6-2 client provider is nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_6", false);
    SetCB(cDriver, g_ipPort, false, nullptr, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (/, valid/invalid) | server : (/, valid/invalid) | pass
TEST_F(TestSecure, OneWayCase7)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_ONE_WAY;
    /* server validator is not registered, but return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_7", true);
    SetCB(sDriver, g_ipPort, true, nullptr, ValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 7-1 client provider is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_7", false);
    SetCB(cDriver, g_ipPort, false, ProviderValid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 7-2 client provider is nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_7", false);
    SetCB(cDriver, g_ipPort, false, nullptr, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);

    /* server validator is nullptr */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_one_case_7", true);
    SetCB(sDriver, g_ipPort, true, nullptr, nullptr);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 7-3 client provider is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_7", false);
    SetCB(cDriver, g_ipPort, false, ProviderValid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 7-4 client provider is nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_one_case_7", false);
    SetCB(cDriver, g_ipPort, false, nullptr, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(/, Y/N) | server : (provider, N)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase8)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is registered, but return invalid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_8", true);
    SetCB(sDriver, g_ipPort, true, SecInfoProviderInvalid, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 8-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_8", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, ValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 8-2 client validator is not registered, set nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_8", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, Y/N) | server : (provider, N)(validator, Y) | failed,failed
TEST_F(TestSecure, TwoWayCase9)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is registered, but return invalid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_9", true);
    SetCB(sDriver, g_ipPort, true, SecInfoProviderInvalid, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 9-1 client validator is registered, return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_9", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 9-2 client validator is registered, return invalid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_9", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorInvalid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, Y/N) | server : (provider, Y)(validator, Y) | pass,failed
TEST_F(TestSecure, TwoWayCase10)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_10", true);
    SetCB(sDriver, g_ipPort, true, SecInfoProviderValidTwo, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 10-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_10", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, ValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);
    CloseDriver(cDriver);

    /* 10-2 client validator is not registered, set nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_10", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, N) | server : (provider, Y)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase11)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_11", true);
    SetCB(sDriver, g_ipPort, true, SecInfoProviderValidTwo, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 11-1 client validator is not registered, set nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_11", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorInvalid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, Y) | server : (provider, Y)(validator, Y) | pass
TEST_F(TestSecure, TwoWayCase12)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_12", true);
    SetCB(sDriver, g_ipPort, true, SecInfoProviderValidTwo, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 12-1 client validator is not registered, set nullptr */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_12", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);
    CloseDriver(cDriver);
    CloseDriver(sDriver);
}

// client : (provider, Y)(/, Y/N) | server : (/, Y)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase13)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is not registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_13", true);
    SetCB(sDriver, g_ipPort, true, ProviderValid, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 13-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_13", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, ValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 13-2 client validator is not registered, but return invalid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_13", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    CloseDriver(sDriver);
}

// client : (provider, Y)(/, Y/N) | server : (/, N)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase14)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is not registered, set nullptr */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_14", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 14-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_14", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, ValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 14-2 client validator is not registered, but return invalid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_14", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, Y/N) | server : (/, Y)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase15)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is not registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_15", true);
    SetCB(sDriver, g_ipPort, true, ProviderValid, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 15-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_15", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 15-2 client validator is not registered, but return invalid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_15", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorInvalid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    CloseDriver(sDriver);
}

// client : (provider, Y)(validator, Y/N) | server : (/, N)(validator, Y) | failed
TEST_F(TestSecure, TwoWayCase16)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    options.secType = ock::hcom::NET_SEC_VALID_TWO_WAY;
    /* server provider is not registered, set nullptr */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_two_case_16", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    /* 16-1 client validator is not registered, but return valid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_16", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorValid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    /* 16-2 client validator is not registered, but return invalid */
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_two_case_16", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValidTwo, AuthValidatorInvalid);
    cDriver->Initialize(options);
    cDriver->Start();
    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OOB_SEC_PROCESS_ERROR);
    CloseDriver(cDriver);

    CloseDriver(sDriver);
}

// client : (provider, valid) | server: (validator, valid) | pass
TEST_F(TestSecure, TokenEmptyString)
{
    UBSHcomNetDriver *sDriver = nullptr;
    UBSHcomNetDriver *cDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;
    NResult result = NN_OK;
    g_ipPort++;

    /* client provider is registered, return valid */
    UBSHcomNetDriverOptions options;
    SetDriverOptions(options);
    cDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "client_empty_string", false);
    SetCB(cDriver, g_ipPort, false, SecInfoProviderValid, nullptr);
    cDriver->Initialize(options);
    cDriver->Start();

    /* server validator is registered, return valid */
    sDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "server_empty_string", true);
    SetCB(sDriver, g_ipPort, true, nullptr, AuthValidatorValid);
    sDriver->Initialize(options);
    sDriver->Start();

    result = cDriver->Connect("hello world", clientEp, 0);
    EXPECT_EQ(result, NN_OK);

    CloseDriver(sDriver);
    CloseDriver(cDriver);
}