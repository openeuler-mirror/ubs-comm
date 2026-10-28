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
#include "dl_api.h"
#include "dl_libc_api.h"
#include "urma/dl_urma_api.h"

namespace ock {
namespace ubs {
Result DlApi::Load(int libraries) noexcept
{
    UBS_VLOG_DEBUG("enter, libraries: %d", libraries);
    Result result = UBS_OK;
    /* load libc api */
    if ((libraries & LOAD_LIBC) == LOAD_LIBC) {
        UBS_VLOG_DEBUG("load libc api");
        result = LibcApi::Load();
        UBS_VLOG_DEBUG("finished libc api loading, result: %d", result);
        if (result != UBS_OK) {
            goto ERROR;
        }
    }

    /* load urma api */
    if ((libraries & LOAD_URMA) == LOAD_URMA) {
        UBS_VLOG_DEBUG("load urma api");
        result = UrmaApi::Load();
        UBS_VLOG_DEBUG("finished urma api loading, result: %d", result);
        if (result != UBS_OK) {
            goto ERROR;
        }
    }

    goto OK;

ERROR:
    LibcApi::UnLoad();
    UrmaApi::UnLoad();

OK:
    return result;
}

void DlApi::UnLoad(int libraries) noexcept
{
    (void)libraries;
    LibcApi::UnLoad();
    UrmaApi::UnLoad();
}
} // namespace ubs
} // namespace ock