#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for build hcom example and perf.
# Build options can be configured through environment variables.
# (1) HCOM_TOOLS_BUILD_TYPE(optional, default is release) => set build type.(release/debug)
# (2) HCOM_TOOLS_INCLUDE_DIR(optional) => default dir {HCOM_ROOT_DIR}/dist/hcom/include.
# (3) HCOM_TOOLS_LIB_DIR(optional) => default dir {HCOM_ROOT_DIR}/dist/hcom/lib.
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly CURRENT_SCRIPT_DIR=$(realpath $(dirname ${0}))
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HCOM_TOOLS_PERF_DIR="${HCOM_ROOT_DIR}/test/hcom/tools/perf_test/build"

# default tools build type is release
if [ "${HCOM_TOOLS_BUILD_TYPE,,}" == "debug" ]; then
    HCOM_TOOLS_BUILD_TYPE="debug"
else
    HCOM_TOOLS_BUILD_TYPE="release"
fi
echo "${HCOM_LOG_TAG} hcom build type: ${HCOM_TOOLS_BUILD_TYPE}"

# 设置环境变量
HCOM_TOOLS_INCLUDE_DIR="${HCOM_TOOLS_INCLUDE_DIR:-${HCOM_ROOT_DIR}/dist/hcom/include}"
HCOM_TOOLS_LIB_DIR="${HCOM_TOOLS_LIB_DIR:-${HCOM_ROOT_DIR}/dist/hcom/lib}"

if [ ! -d "${HCOM_TOOLS_INCLUDE_DIR}" ]; then
    echo "Error: HCOM_TOOLS_INCLUDE_DIR does not exist."
    exit 1
fi

if [ ! -d "${HCOM_TOOLS_LIB_DIR}" ]; then
    echo "Error: HCOM_TOOLS_LIB_DIR does not exist."
    exit 1
fi

# check cpu num for parallel build
CPU_PROCESSOR_NUM=$(grep processor /proc/cpuinfo | wc -l)
echo "${HCOM_LOG_TAG} parallel build job num is ${CPU_PROCESSOR_NUM}"

# build tools perf
if [ -e "${HCOM_TOOLS_PERF_DIR}" ]; then
    # 如果存在，删除该路径（无论是文件还是目录）
    rm -rf "${HCOM_TOOLS_PERF_DIR}"
fi

mkdir -p "${HCOM_TOOLS_PERF_DIR}"

cd ${HCOM_TOOLS_PERF_DIR}

cmake -DCMAKE_BUILD_TYPE="${HCOM_TOOLS_BUILD_TYPE}"\
      -DHCOM_INCLUDE_DIR="${HCOM_TOOLS_INCLUDE_DIR}"\
      -DHCOM_LIB_DIR="${HCOM_TOOLS_LIB_DIR}" ..

if [ "$?" != 0 ]; then
    echo "${HCOM_LOG_TAG} hcom tools cmake failed"
    exit 1
fi

make clean
if [ "$?" != 0 ]; then
    echo "${HCOM_LOG_TAG} hcom tools make clean failed"
	exit 1
fi

make -j"${CPU_PROCESSOR_NUM}"
if [ "$?" != 0 ]; then
    echo "${HCOM_LOG_TAG} hcom tools make failed"
  	exit 1
fi

echo "${HCOM_LOG_TAG} hcom tools perf build success"
