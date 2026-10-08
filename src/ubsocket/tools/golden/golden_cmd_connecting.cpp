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
#include "golden_cmd_connecting.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_wrapper.h"
#include "golden_cmd_profiling.h"

namespace golden {
using namespace ock::ubs;
using namespace ock::ubs::urma;

void SubCommandConnecting::SetRules() noexcept
{
    param_rules_[PARAM_CONN_DEV_NAME] = {PARAM_CONN_DEV_NAME, PDT_STR, true, "", "", ""};
    param_rules_[PARAM_CONN_DEV_EID] = {PARAM_CONN_DEV_EID, PDT_INT64, true, 0, 0, 100L, ""};
    param_rules_[PARAM_CONN_THREAD] = {PARAM_CONN_THREAD, PDT_INT64, false, 1, 1, 1024L, ""};
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, false, "urma_rtp", "urma_rtp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, true, "", "", ""};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001 --" + PARAM_CONN_DEV_NAME +
                       "= --" + PARAM_CONN_DEV_EID + "=0");
    example_.push_back("client: " + program + " " + name_ + " --" + PARAM_ROLE + "=client --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001 --" + PARAM_CONN_DEV_NAME +
                       "= --" + PARAM_CONN_DEV_EID + "=0");
}

int SubCommandConnecting::DoInitialize() noexcept
{
    device_name_ = param_rules_[PARAM_CONN_DEV_NAME].strRule.value;
    device_eid_index_ = param_rules_[PARAM_CONN_DEV_EID].int64Rule.value;
    thread_count_ = param_rules_[PARAM_CONN_THREAD].int64Rule.value;

    role_ = param_rules_[PARAM_ROLE].strRule.value;
    protocol_ = param_rules_[PARAM_PROTOCOL].strRule.value;
    ip_ = param_rules_[PARAM_IP].strRule.value;
    port_ = param_rules_[PARAM_PORT].int64Rule.value;

    LOG_DEBUG(*this);

    struct sockaddr_in addr;
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) {
        std::cout << "Invalid ip '" << ip_ << "'" << std::endl;
        return -1;
    }

    LOG_DEBUG(*this);

    LockRegistry::RegisterDefaultOps();
    GoldenProfiling::Init();

    return 0;
}

int SubCommandConnecting::DoExecute() noexcept
{
    LOG_DEBUG("enter");

    /* run client and server */
    if (protocol_ == "urma_rtp") {
        return DoExecuteUrma();
    }

    std::cout << "Un-reachable path" << std::endl;
    return -1;
}

int SubCommandConnecting::DoExecuteUrma() noexcept
{
#ifdef URMA_BACKEND_ENABLED
    using namespace ock::ubs;
    using namespace ock::ubs::urma;
    auto result = Urma::Init();
    if (result != 0) {
        std::cout << "Init urma backend failed" << std::endl;
        return -1;
    }

    UrmaDevice::Init();

    UrmaContextPtr context;
    if (UrmaContext::CreateContext(device_name_, device_eid_index_, context) != UBS_OK) {
        std::cout << "Create context failed" << std::endl;
        return -1;
    }

    if (role_ == "client") {
        UrmaClient client(*this, context);
        return client.Run();
    } else if (role_ == "server") {
        UrmaServer server(*this, context);
        return server.Run();
    }
#endif
    std::cout << "urma build not enabled, use -DBUILD_URMA_BACKEND=ON when compiling" << std::endl;
    return -1;
}

int UrmaClient::Run() noexcept
{
    LOG_DEBUG("start enter");

#ifdef URMA_BACKEND_ENABLED
    /* step0: create thread pool */
    GlobalSetting::UBS_THREAD_POOL_SIZE = cmd_.thread_count_;
    if (!thread_pool_.Start()) {
        std::cout << "Failed to start thread pool for server" << std::endl;
        return -1;
    }

    std::atomic<int32_t> finished_task = 0;
    for (int32_t i = 0; i < cmd_.thread_count_; i++) {
        thread_pool_.Execute([this, &finished_task]() {
            /* step1: create jfc, jfs, jfr, jetty */
            PROF_START(CONN_URMA_TOTAL);
            PROF_START(CONN_URMA_CREATE_JFC);
            urma_jfc_cfg_t jfc_cfg = context_->CreateJfcCfg(1024);
            UrmaJfcPtr jfc;
            if (this->context_->CreateJfc(jfc_cfg, EVENT_POLLING, jfc) != UBS_OK) {
                std::cout << "Create urma jfc failed, errno " << errno << std::endl;
                ++finished_task;
                PROF_END(CONN_URMA_CREATE_JFC, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_CREATE_JFC, true);

            PROF_START(CONN_URMA_CREATE_JFS);
            urma_jfs_cfg_t jfs_cfg = context_->CreateJfsCfg(256, URMA_TM_RM);
            UrmaJfsPtr jfs;
            if (context_->CreateJfs(jfs_cfg, jfc, jfs) != UBS_OK) {
                std::cout << "Create urma jfs failed, errno " << errno << std::endl;
                ++finished_task;
                PROF_END(CONN_URMA_CREATE_JFS, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_CREATE_JFS, true);

            PROF_START(CONN_URMA_CREATE_JFR);
            urma_jfr_cfg_t jfr_cfg = context_->CreateJfrCfg(256, URMA_TM_RM, 0);
            UrmaJfrPtr jfr;
            if (context_->CreateJfr(jfr_cfg, jfc, jfr) != UBS_OK) {
                std::cout << "Create urma jfr failed, errno " << errno << std::endl;
                ++finished_task;
                PROF_END(CONN_URMA_CREATE_JFR, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_CREATE_JFR, true);

            PROF_START(CONN_URMA_CREATE_JETTY);
            urma_jetty_cfg_t jetty_cfg = context_->CreateJettyCfg();
            UrmaJettyPtr jetty;
            if (context_->CreateJetty(jetty_cfg, URMA_RTP, jfs, jfr, jetty) != UBS_OK) {
                std::cout << "Create urma jetty failed, errno " << errno << std::endl;
                ++finished_task;
                PROF_END(CONN_URMA_CREATE_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_CREATE_JETTY, true);

            PROF_START(CONN_URMA_EXCHANGE_JETTY);
            /* step2: create socket */
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                ++finished_task;
                LOG_ERROR("Create socket failed");
                PROF_END(CONN_URMA_EXCHANGE_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            LOG_DEBUG("created a socket");

            /* step3: connect */
            struct sockaddr_in serv_addr;
            serv_addr.sin_port = htons(cmd_.port_);
            serv_addr.sin_family = AF_INET;
            inet_pton(AF_INET, cmd_.ip_.c_str(), &serv_addr.sin_addr);
            auto result = connect(fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr_in));
            if (result != 0) {
                std::cout << "Connect to '" << cmd_.ip_ << ":" << std::to_string(cmd_.port_) << "' failed\n";
                ++finished_task;
                close(fd);
                PROF_END(CONN_URMA_EXCHANGE_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }

            UrmaExchange my_exchange{};
            my_exchange.token = jfr_cfg.token_value.token;
            my_exchange.raw_jetty_id = jetty->RawJettyId();

            auto send = write(fd, &my_exchange, sizeof(UrmaExchange));
            if (send < 0) {
                std::cout << "Send exchange data to server failed, errno " << errno << std::endl;
                ++finished_task;
                close(fd);
                PROF_END(CONN_URMA_EXCHANGE_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            LOG_DEBUG("send exchange to server: " << my_exchange);

            UrmaExchange peer_exchange{};
            auto received = read(fd, &peer_exchange, sizeof(UrmaExchange));
            if (received < 0) {
                std::cout << "Read exchange data from server failed, errno " << errno << std::endl;
                ++finished_task;
                close(fd);
                PROF_END(CONN_URMA_EXCHANGE_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_EXCHANGE_JETTY, true);
            LOG_DEBUG("read exchange from server: " << peer_exchange);

            PROF_START(CONN_URMA_IMPORT_JETTY);
            if (jetty->ImportRemoteJetty(peer_exchange.raw_jetty_id, peer_exchange.token) != UBS_OK) {
                std::cout << "Import server jetty failed, errno " << errno << std::endl;
                ++finished_task;
                close(fd);
                PROF_END(CONN_URMA_IMPORT_JETTY, false);
                PROF_END(CONN_URMA_TOTAL, false);
                return;
            }
            PROF_END(CONN_URMA_IMPORT_JETTY, true);

            ++finished_task;
            close(fd);
            PROF_END(CONN_URMA_TOTAL, true);
        });
    }

    while (finished_task.load() < cmd_.thread_count_) {
        sleep(1);
        std::cout << finished_task.load() << " threads finished\n";
    }
    thread_pool_.Stop();

    /* dump performance */
    {
        using namespace ock::ubs::profiling;
        TraceGroupPtr group;
        if (Tracer::Instance().Combine(group) != 0) {
            std::cout << "Failed to get performance statistics" << std::endl;
            return 0;
        }

        std::cout.imbue(std::locale("en_US.UTF-8"));

        std::cout << "\nConnect performance summary:" << std::endl;
        auto total = group->Get(CONN_URMA_TOTAL).data;
        constexpr uint32_t K = 1000;
#define HEAD_COUT std::cout << "  " << std::left << std::setw(30)
        HEAD_COUT << "Concurrent threads: " << cmd_.thread_count_ << "\n";
        HEAD_COUT << "Finished threads: " << finished_task.load() << "\n";
        HEAD_COUT << "Connect times: " << total.success_count << "\n";
        HEAD_COUT << "Total time: " << total.total_time / K << " us\n";
        HEAD_COUT << "Avg time: " << total.total_time / total.success_count / K << " us\n";
        HEAD_COUT << "Min times: " << total.min_time / K << " us\n";
        HEAD_COUT << "Max times: " << total.max_time / K << " us\n";

        std::cout << "\nDetails:\n";
        auto jfc = group->Get(CONN_URMA_CREATE_JFC).data;
        auto jfr = group->Get(CONN_URMA_CREATE_JFR).data;
        auto jfs = group->Get(CONN_URMA_CREATE_JFS).data;
        auto jetty = group->Get(CONN_URMA_CREATE_JETTY).data;
        auto exchg = group->Get(CONN_URMA_EXCHANGE_JETTY).data;
        auto import = group->Get(CONN_URMA_IMPORT_JETTY).data;

        HEAD_COUT << "Create jfc avg time: " << jfc.total_time / jfc.success_count / K << " us\n";
        HEAD_COUT << "Create jfc min time: " << jfc.min_time / K << " us\n";
        HEAD_COUT << "Create jfc max time: " << jfc.max_time / K << " us\n\n";

        HEAD_COUT << "Create jfr avg time: " << jfr.total_time / jfr.success_count / K << " us\n";
        HEAD_COUT << "Create jfr min time: " << jfr.min_time / K << " us\n";
        HEAD_COUT << "Create jfr max time: " << jfr.max_time / K << " us\n\n";

        HEAD_COUT << "Create jfs avg time: " << jfs.total_time / jfs.success_count / K << " us\n";
        HEAD_COUT << "Create jfs min time: " << jfs.min_time / K << " us\n";
        HEAD_COUT << "Create jfs max time: " << jfs.max_time / K << " us\n\n";

        HEAD_COUT << "Create jetty avg time: " << jetty.total_time / jetty.success_count / K << " us\n";
        HEAD_COUT << "Create jetty min time: " << jetty.min_time / K << " us\n";
        HEAD_COUT << "Create jetty max time: " << jetty.max_time / K << " us\n\n";

        HEAD_COUT << "Exchange avg time: " << exchg.total_time / exchg.success_count / K << " us\n";
        HEAD_COUT << "Exchange min time: " << exchg.min_time / K << " us\n";
        HEAD_COUT << "Exchange max time: " << exchg.max_time / K << " us\n\n";

        HEAD_COUT << "Import jetty avg time: " << import.total_time / import.success_count / K << " us\n";
        HEAD_COUT << "Import jetty min time: " << import.min_time / K << " us\n";
        HEAD_COUT << "Import jetty max time: " << import.max_time / K << " us\n\n";
    }
#else
    std::cout << "URMA_BACKEND_ENABLED is not enabled" << std::endl;
#endif

    return 0;
}

int UrmaServer::Run() noexcept
{
    LOG_DEBUG("start enter");
#ifdef URMA_BACKEND_ENABLED
    /* step0: create thread pool */
    GlobalSetting::UBS_THREAD_POOL_SIZE = cmd_.thread_count_;
    if (!thread_pool_.Start()) {
        std::cout << "Failed to start thread pool for server" << std::endl;
        return -1;
    }

    /* step1: create socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Create socket failed");
        return -errno;
    }
    LOG_DEBUG("created a socket");

    socket_listen_fd_ = fd;

    int opt = 1;
    struct sockaddr_in address {
    };
    setsockopt(socket_listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(socket_listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &address.sin_addr);

    /* step2: bind and listen */
    bind(socket_listen_fd_, (struct sockaddr *)&address, sizeof(address));
    auto result = listen(socket_listen_fd_, 3);
    if (result < 0) {
        std::cout << "Listen at '" << cmd_.ip_ << ":" << cmd_.port_ << "' failed, errnor " << errno;
        return -errno;
    }

    LOG_DEBUG("listen at '" << cmd_.ip_ << ":" << cmd_.port_ << "'");

    /* step3: accept clients */
    std::cout << "Waiting for client to connect, please run following cmd for client:" << std::endl;
    std::cout << "  golden conn --role=client --protocol=" << cmd_.protocol_ << " --ip=" << cmd_.ip_
              << " --port=" << cmd_.port_ << " --thread-count=8 --device-eid-index=0"
              << " --device-name=..." << std::endl;

    uint32_t client_max = 8192;
    std::cout
        << "\n  '" << client_max
        << "' clients can be accepted, use 'ctrl+c' to abort this program if there is no enough client to connect\n";
    std::cout << "  Make sure open files is big enough, use 'ulimit -n' to check\n\n";

    uint32_t accepted_count = 0;
    while (client_max-- > 0) {
        struct sockaddr_in client_address {
        };
        socklen_t addr_len = sizeof(address);
        auto client_fd = accept(socket_listen_fd_, (struct sockaddr *)&client_address, &addr_len);

        std::cout << "\r  The " << ++accepted_count << " client is connecting with raw socket";
        if (accepted_count % 10 == 0) {
            std::cout << std::endl;
        }

        thread_pool_.Execute([this, client_fd]() {
            /* step1: create jfc, jfs, jfr, jetty */
            urma_jfc_cfg_t jfc_cfg = context_->CreateJfcCfg(1024);
            UrmaJfcPtr jfc;
            if (this->context_->CreateJfc(jfc_cfg, EVENT_POLLING, jfc) != UBS_OK) {
                std::cout << "Create urma jfc failed, errno " << errno << std::endl;
                return;
            }

            urma_jfs_cfg_t jfs_cfg = context_->CreateJfsCfg(256, URMA_TM_RM);
            UrmaJfsPtr jfs;
            if (context_->CreateJfs(jfs_cfg, jfc, jfs) != UBS_OK) {
                std::cout << "Create urma jfs failed, errno " << errno << std::endl;
                return;
            }

            urma_jfr_cfg_t jfr_cfg = context_->CreateJfrCfg(256, URMA_TM_RM, 0);
            UrmaJfrPtr jfr;
            if (context_->CreateJfr(jfr_cfg, jfc, jfr) != UBS_OK) {
                std::cout << "Create urma jfr failed, errno " << errno << std::endl;
                return;
            }

            urma_jetty_cfg_t jetty_cfg = context_->CreateJettyCfg();
            UrmaJettyPtr jetty;
            if (context_->CreateJetty(jetty_cfg, URMA_RTP, jfs, jfr, jetty) != UBS_OK) {
                std::cout << "Create urma jetty failed, errno " << errno << std::endl;
                return;
            }

            /* step2: exchange */
            UrmaExchange my_exchange{};
            my_exchange.token = jfr_cfg.token_value.token;
            my_exchange.raw_jetty_id = jetty->RawJettyId();

            auto send = write(client_fd, &my_exchange, sizeof(UrmaExchange));
            if (send < 0) {
                std::cout << "Send exchange data to client failed, errno " << errno << std::endl;
                close(client_fd);
                return;
            }
            LOG_DEBUG("send exchange to server: " << my_exchange);

            UrmaExchange peer_exchange{};
            auto received = read(client_fd, &peer_exchange, sizeof(UrmaExchange));
            LOG_DEBUG("read exchange from client: " << peer_exchange);
            if (received < 0) {
                std::cout << "Read exchange data from client failed, errno " << errno << std::endl;
                close(client_fd);
                return;
            }

            /* step3: import */
            auto result = jetty->ImportRemoteJetty(peer_exchange.raw_jetty_id, peer_exchange.token);
            if (result != UBS_OK) {
                std::cout << "Import remote jetty failed";
                close(client_fd);
                return;
            }

            /* close fd */
            close(client_fd);
        });
    }
#else
    std::cout << "URMA_BACKEND_ENABLED is not enabled" << std::endl;
#endif

    return 0;
}
} // namespace golden