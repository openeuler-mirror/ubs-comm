#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for adapting gcc 4.8.5
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

echo "hcom adaptation gcc 4.8.5 ..."
GCC_VERSION=$(gcc --version | grep "4.8.5")
if [ -n "$GCC_VERSION" ]; then
    sed -i '/Wdate-time/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
    sed -i '/Wduplicated-branches/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
    sed -i '/Wduplicated-cond/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
    sed -i '/Wimplicit-fallthrough/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
    sed -i '/Wshift-negative-value/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
    sed -i '/Wshift-overflow/d' ../3rdparty/secure_c/huawei_secure_c/src/Makefile
fi
