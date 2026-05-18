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
#include "dl_umq_api.h"

namespace ock {
namespace ubs {
Result DlApi::Load(int libraries) noexcept
{
    Result result = UBS_OK;
    /* load libc api */
    if ((libraries & LOAD_LIBC) == LOAD_LIBC) {
        result = LibcApi::Load();
        UBS_VLOG_DEBUG("finished libc api loading, result: %d", result);
        if (result != UBS_OK) {
            goto ERROR;
        }
    }

    if ((libraries & LOAD_UMQ) == LOAD_UMQ) {
        result = UmqApi::Load();
        UBS_VLOG_DEBUG("finished umq api loading, result: %d", result);
        if (result != UBS_OK) {
            goto ERROR;
        }
    }

    goto OK;

ERROR:
    LibcApi::UnLoad();
    UmqApi::UnLoad();

OK:
    return result;
}

void DlApi::UnLoad(int libraries) noexcept
{
    LibcApi::UnLoad();
    UmqApi::UnLoad();
}
} // namespace ubs
} // namespace ock