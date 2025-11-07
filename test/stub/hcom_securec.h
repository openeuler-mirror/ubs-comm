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
#ifndef HCOM_HCOM_SECUREC_H
#define HCOM_HCOM_SECUREC_H

#include <cstdint>
#include <cstring>
#include <iostream>

namespace ock {
namespace hcom {

int memcpy_s(void *dest, size_t destMax, const void *src, size_t count);
int strcpy_error(char *strDest, size_t destMax, const char *strSrc);
int strcpy_s(char *strDest, size_t destMax, const char *strSrc);

}
}
#endif // HCOM_HCOM_SECUREC_H
