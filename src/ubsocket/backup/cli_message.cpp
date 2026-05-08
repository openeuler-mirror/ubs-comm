/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli message, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_message.h"

namespace Statistics {
    bool CLIMessage::AllocateIfNeed(uint32_t newSize)
    {
        if (newSize == 0) {
            printf("Invalid msg size %d alloc failed\n", newSize);
            return false;
        }
        if (newSize > mBufLen) {
            if (mBuf != nullptr) {
                free(mBuf);
            }

            if ((mBuf = malloc(newSize)) != nullptr) {
                mBufLen = newSize;
                return true;
            }
            mBuf = nullptr;
            mBufLen = 0;
            return false;
        }

        return true;
    }
}