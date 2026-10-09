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
#include "golden_cmd_show.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_wrapper.h"
#include "under_api/urma/dl_urma_api.h"

namespace golden {

void SubCommandShow::SetRules() noexcept
{
    param_rules_[PARAM_DEVICE_TYPE] = {PARAM_DEVICE_TYPE, PDT_STR_ENUM, true, "", "ub", ""};
    param_rules_[PARAM_DEVICE_NAME] = {PARAM_DEVICE_NAME, PDT_STR, false, "", "", ""};
    param_rules_[PARAM_DEVICE_DETAIL] = {PARAM_DEVICE_DETAIL, PDT_STR_ENUM, false, "short", "short|whole", ""};

    example_.push_back("UB device: " + program + " " + name_ + " --" + PARAM_DEVICE_TYPE + "=ub");
}

int SubCommandShow::DoInitialize() noexcept
{
    device_type_ = param_rules_[PARAM_DEVICE_TYPE].strRule.value;
    device_name_ = param_rules_[PARAM_DEVICE_NAME].strRule.value;
    device_details_ = param_rules_[PARAM_DEVICE_DETAIL].strRule.value;

    LOG_DEBUG(*this);

    return 0;
}

int SubCommandShow::DoExecute() noexcept
{
    LOG_DEBUG("enter");

    bool show_whole_info = (device_details_ == "whole");

    if (device_type_ == "ub") {
#ifdef URMA_DLOPEN_BACKEND_ENABLED
        using namespace ock::ubs;
        using namespace ock::ubs::urma;
        auto result = Urma::Init();
        if (result != 0) {
            std::cout << "Init urma backend failed" << std::endl;
            return -1;
        }

        UrmaDevice::Init();
        auto devices = UrmaDevice::AllDevices();
        if (!device_name_.empty()) {
            std::cout << "Devices whose name contains '" << device_name_ << "' (ordered by device name):" << std::endl;
        } else {
            std::cout << "All devices (ordered by device name):" << std::endl;
        }

        uint32_t filtered_dev_count = 0;
        for (auto &device : devices) {
            if (device.second.Get() == nullptr) {
                continue;
            }

            if (device.first.find(device_name_) == std::string::npos) {
                continue;
            }
            ++filtered_dev_count;
            std::cout << device.second->ToString(show_whole_info, "  ", "\n") << std::endl;
        }

        std::cout << std::endl << "Total device count: " << devices.size() << std::endl;
        if (!device_name_.empty()) {
            std::cout << "Device count whose name contains '" << device_name_ << "': " << filtered_dev_count
                      << std::endl;
        }
#else
        std::cout << "urma backend is not enabled" << std::endl;
#endif

        LOG_DEBUG("leave with ub type, show whole: " << show_whole_info);
        return 0;
    }

    std::cout << "Un-reachable path" << std::endl;
    return -1;
}

} // namespace golden