#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for build hcom example and perf.
# Build options can be configured through environment variables.
# (1) HCOM_BUILD_TYPE(optional, default is release) => set build type.(release/debug)
# (2) HCOM_INSTALL_DIR(optional) => directory where hcom is installed.
# (3) HCOM_BUILD_DIR(optional) => directory for building and outputing example and perf.
# (4) HCOM_BUILD_JAVA_SDK(optional, default is off) => build java example or not.(on/off)
# version: 1.0.0
# change log:
# ***********************************************************************
set -eo pipefail

readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HTRACER_CLI_SRC_DIR="${HCOM_ROOT_DIR}/test/tools/hcom_tracer"
readonly HTRACER_CLI_BUILD_DIR="${HCOM_ROOT_DIR}/test/tools/hcom_tracer/build"

# ****************************************
# build htracer_cli
# ****************************************
cd ${HTRACER_CLI_SRC_DIR} || { echo "Error: hcom test/tools/hcom_tracer directory not found!"; exit 1; }

# 如果build目录存在，清理
if [ -d "build" ]; then
    rm -rf build/*
fi

mkdir -p build
cd build || exit 1

cmake ..
make -j8

if [ $? -eq 0 ]; then
    echo -e "\n\033[32mhtracer_cli compiled successfully!\033[0m"
else
    echo -e "\n\033[31mError: Failed to compile htracer_cli\033[0m"
    exit 1
fi