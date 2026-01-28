#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# Script for building HCOM.
# Build options can be configured through environment variables.
# (1) HCOM_BUILD_TYPE(optional, default is release) => set build type.(release/debug)
# (2) HCOM_BUILD_TESTS(optional, default is off) => enable build test or not.(on/off)
# (3) HCOM_BUILD_JAVA_SDK(optional, default is off) => build java sdk or not.(on/off)
# (4) HCOM_BUILD_SERVICE(optional, default is on) => build service level or not.(on/off)
# (5) HCOM_BUILD_RDMA(optional, default is on) => build rdma or not.(on/off)
# (6) HCOM_BUILD_SOCK(optional, default is on) => build sock (tcp/uds) or not.(on/off)
# (7) HCOM_BUILD_SHM(optional, default is on) => build shm or not.(on/off)
# (8) HCOM_BUILD_EXAMPLE(optional, default is off) => build example and perf.(on/off)
# (9) HCOM_ENABLE_ARM_KP(optional, default is on) => check kunpeng or not.(on/off)
# (10) HCOM_TEST_TOOL_PATH(optional) => test tool install path.(mockcpp/gtest/dtfuzz)
# (11) HCOM_CI_WORKSPACE(optional) => ci workspace, for buildInfo.properties file.
# (12) HCOM_BUILD_RPM(optional, default is on) => build rpm.(on/off)
# (13) HCOM_BUILD_TOOLS_PERF(optional, default is off) => build rpm.(on/off)
# (14) HCOM_BUILD_HW_CRC(optional, default is off) => build with hardware based crc.(on/off)
# (15) BUILD_HCOM(optional, default is ON) => build hcom.(ON/OFF)
# (16) HCOM_BUILD_MULTICAST(optional, default is off) => build multicast or not.(on/off)

# version: 1.0.0
# change log:
# ***********************************************************************
set -e
(set -o pipefail) 2>/dev/null && set -o pipefail

readonly HCOM_ROOT_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_BUILD_DIR="${HCOM_ROOT_DIR}/tmp_build_dir"
readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly HCOM_INSTALL_DIR="${HCOM_ROOT_DIR}/dist/hcom"

echo "HCOM ROOT: ${HCOM_ROOT_DIR}"
echo "HCOM BUILD DIR: ${HCOM_BUILD_DIR}"
echo "HCOM INSTALL DIR: ${HCOM_INSTALL_DIR}"

HCOM_COMPONENT_VERSION="1.0.0"

function show_help() {
    echo "Usage: $0 [COMMAND] [OPTION]"
    echo "Build the project with specified options."
    echo "Commands: clean"
    echo "Options:"
    echo "    -t, --type TYPE       Set build type. debug/release"
}

function clean_dir() {
    [[ -n "${HCOM_BUILD_DIR}" ]] && rm -rf "${HCOM_BUILD_DIR}"
    [[ -n "${HCOM_INSTALL_DIR}" ]] && rm -rf "${HCOM_INSTALL_DIR}"
    echo "Cleanup: ${HCOM_BUILD_DIR}, ${HCOM_INSTALL_DIR}"
}

# 编译类型通过环境变量 HCOM_BUILD_TYPE 和命令行参数 -t 二选一，如果两者都提供了，
# 则优先使用命令行参数。默认编译类型为 Release.
HCOM_BUILD_TYPE="${HCOM_BUILD_TYPE,,}"
HCOM_BUILD_TYPE="${HCOM_BUILD_TYPE:-release}"

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -t|--type) HCOM_BUILD_TYPE="${2,,}"; shift ;;
        clean) clean_dir; exit 0 ;;
        *) echo "Unknown parameter passed: $1"; show_help; exit 1 ;;
    esac
    shift
done

echo "HCOM BUILD TYPE: ${HCOM_BUILD_TYPE}"

# Hardware CRC is disabled by default
HCOM_BUILD_HW_CRC="${HCOM_BUILD_HW_CRC:-off}"
echo "${HCOM_LOG_TAG} hcom build hw crc: ${HCOM_BUILD_HW_CRC}"

# check whether build UB, default is off
HCOM_BUILD_UB="${HCOM_BUILD_UB:-off}"
echo "${HCOM_LOG_TAG} hcom build ub: ${HCOM_BUILD_UB}"

# check whether build service module, default is on
HCOM_BUILD_SERVICE="${HCOM_BUILD_SERVICE:-on}"
echo "${HCOM_LOG_TAG} hcom build service: ${HCOM_BUILD_SERVICE}"

# check whether build RDMA module, default is on
HCOM_BUILD_RDMA="${HCOM_BUILD_RDMA:-on}"
echo "${HCOM_LOG_TAG} hcom build rdma: ${HCOM_BUILD_RDMA}"

# check whether build sock(tcp and uds) module, default is on
HCOM_BUILD_SOCK="${HCOM_BUILD_SOCK:-on}"
echo "${HCOM_LOG_TAG} hcom build sock: ${HCOM_BUILD_SOCK}"

# check whether build shm module, default is on
HCOM_BUILD_SHM="${HCOM_BUILD_SHM:-on}"
echo "${HCOM_LOG_TAG} hcom build shm: ${HCOM_BUILD_SHM}"

# check whether build multcast module, default is off
HCOM_BUILD_MULTICAST="${HCOM_BUILD_MULTICAST:-off}"
echo "${HCOM_LOG_TAG} hcom build multicast: ${HCOM_BUILD_MULTICAST}"

# check whether check kunpeng, default is off
HCOM_ENABLE_ARM_KP="${HCOM_ENABLE_ARM_KP:-off}"
echo "${HCOM_LOG_TAG} hcom enable arm kunpeng check: ${HCOM_ENABLE_ARM_KP}"

# check whether build java sdk, default is off.
HCOM_BUILD_JAVA_SDK="${HCOM_BUILD_JAVA_SDK:-off}"
echo "${HCOM_LOG_TAG} hcom build java sdk: ${HCOM_BUILD_JAVA_SDK}"

# check whether enable unittest, default is off.
HCOM_BUILD_TESTS="${HCOM_BUILD_TESTS:-off}"
echo "${HCOM_LOG_TAG} hcom build tests: ${HCOM_BUILD_TESTS}"

# check whether enable unittest, default is off.
BUILD_HCOM="${BUILD_HCOM:-ON}"
echo "${HCOM_LOG_TAG} build hcom: ${BUILD_HCOM}"

# check whether test tools are installed
if [[ "${HCOM_BUILD_TESTS,,}" == "on" ]]; then
    [[ -z "${HCOM_TEST_TOOL_PATH}" ]] && HCOM_TEST_TOOL_PATH="${HCOM_ROOT_DIR}/dist/hcom_test_tools"
    echo "${HCOM_LOG_TAG} hcom test tools path: ${HCOM_TEST_TOOL_PATH}"
    if [[ ! -d "${HCOM_TEST_TOOL_PATH}" ]]; then
        echo "${HCOM_LOG_TAG} hcom test tools are not installed, installing..."
        bash "${HCOM_ROOT_DIR}/build/install_test_tools.sh"
    fi
fi

# Fresh build everytime
[[ -n "${HCOM_BUILD_DIR}" ]] && rm -rf "${HCOM_BUILD_DIR}"
[[ -n "${HCOM_INSTALL_DIR}" ]] && rm -rf "${HCOM_INSTALL_DIR}"

cmake -S"${HCOM_ROOT_DIR}" -B"${HCOM_BUILD_DIR}" \
    -DBUILD_HCOM=${BUILD_HCOM} \
    -DCMAKE_INSTALL_PREFIX="${HCOM_INSTALL_DIR}" \
    -DCMAKE_BUILD_TYPE=${HCOM_BUILD_TYPE} \
    -DBUILD_TESTS=${HCOM_BUILD_TESTS} \
    -DTEST_TOOL_INSTALL_PATH="${HCOM_TEST_TOOL_PATH}" \
    -DBUILD_JAVA_SDK=${HCOM_BUILD_JAVA_SDK} \
    -DBUILD_WITH_HW_CRC=${HCOM_BUILD_HW_CRC} \
    -DBUILD_WITH_UB=${HCOM_BUILD_UB} \
    -DBUILD_WITH_RDMA=${HCOM_BUILD_RDMA} \
    -DBUILD_WITH_SOCK=${HCOM_BUILD_SOCK} \
    -DBUILD_WITH_SHM=${HCOM_BUILD_SHM} \
    -DBUILD_WITH_MULTICAST=${HCOM_BUILD_MULTICAST} \
    -DENABLE_ARM_KP=${HCOM_ENABLE_ARM_KP} \
    -DHCOM_COMPONENT_VERSION="${HCOM_COMPONENT_VERSION}"

cmake --build "${HCOM_BUILD_DIR}" -j $(nproc)

# Install to the specified path
cmake --build "${HCOM_BUILD_DIR}" --target install

# collect objects and make software package
output=$(HCOM_COMPONENT_VERSION=${HCOM_COMPONENT_VERSION} bash "${HCOM_ROOT_DIR}/build/make_software_package.sh" -t "${HCOM_BUILD_TYPE}")

# build example and perf
[[ "${HCOM_BUILD_EXAMPLE,,}" == "on" ]] && bash "${HCOM_ROOT_DIR}/build/build_example_perf.sh"

bash "${HCOM_ROOT_DIR}/build/build_umq_and_ubsocket.sh"

# 不要删除本行。因 `[[ A ]] && B` 为表达式，其返回值会返回给 shell，一旦
# HCOM_BUILD_EXAMPLE 不为 on 就会返回 1 导致 CI 构建失败.
echo "${HCOM_LOG_TAG} $0 succeeds"
