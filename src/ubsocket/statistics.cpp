/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
*/

#include "statistics.h"

uint32_t Statistics::Recorder::m_title_len = 0;
volatile bool Statistics::GlobalStatsMgr::m_running = true;