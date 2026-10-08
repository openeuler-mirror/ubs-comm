/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_GOLDEN_CMD_DATA_H
#define UBS_COMM_GOLDEN_CMD_DATA_H

#include "golden_cmd.h"

namespace golden {

constexpr int64_t kWindowSize = 32;

SubCommand *CreateData(const ParamMap &params);

class SubCommandData : public SubCommand {
public:
    SubCommandData(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override;
    int DoInitialize() noexcept override;
    int DoExecute() noexcept override;

private:
    int ValidateCommonParams() noexcept;
    int ValidateClientParams() noexcept;
    int ValidateServerParams() noexcept;

    std::string role_;
    std::string protocol_;
    std::string ip_;
    int32_t port_;
    int64_t msgCount_;
    int64_t msgSize_;
    int64_t qps_;

    class DataClient;
    class DataServer;
};

} // namespace golden

#endif // UBS_COMM_GOLDEN_CMD_DATA_H
