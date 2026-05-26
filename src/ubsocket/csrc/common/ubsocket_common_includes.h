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
#ifndef UBS_COMM_UBSOCKET_INCLUDES_H
#define UBS_COMM_UBSOCKET_INCLUDES_H

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ubsocket_defines.h"
#include "ubsocket_errno.h"
#include "ubsocket_functions.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_lock.h"
#include "ubsocket_logger.h"
#include "ubsocket_obj_statistics.h"
#include "ubsocket_ref.h"
#include "ubsocket_set.h"
#include "ubsocket_profiling.h"

#endif // UBS_COMM_UBSOCKET_INCLUDES_H
