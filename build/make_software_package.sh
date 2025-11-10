#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for packaging hcom.
# Build options can be configured through environment variables.
# (1) HCOM_PRODUCT_NAME(optional) => product name(default BeiMing)
# (2) HCOM_PRODUCT_VERSION(optional) => product version(default 24.4)
# (3) HCOM_COMPONENT_VERSION(optional) => hcom version(default 1.0.0)
# (3) HCOM_PACKAGE_PATH(optional) => software package path(default ${HCOM_ROOT_DIR}/dist)
# version: 1.0.0
# change log:
# ***********************************************************************
set -eo pipefail

readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HCOM_INSTALL_DIR="${HCOM_ROOT_DIR}/dist/hcom"
readonly HCOM_INSTALL_TRACER_DIR="${HCOM_ROOT_DIR}/dist/hcom_3rdparty/hcom_tracer"
readonly HCOM_TRACER_TOOL="${HCOM_ROOT_DIR}/test/tools/hcom_tracer/build/htracer_cli"
readonly HCOM_COMPONENT_NAME="hcom"
readonly HCOM_BUILD_TIME=$(date "+%Y-%m-%d %Z")
readonly HCOM_BUILD_OS_TYPE=$(uname -s)
readonly HCOM_BUILD_OS_ARCH=$(uname -m)

function show_help() {
    echo "Usage: $0 [OPTION]"
    echo "Build the project with specified options."
    echo "Options:"
    echo "    -t, --type TYPE       Set build type. debug/release"
}

# 编译类型通过环境变量 HCOM_BUILD_TYPE 和命令行参数 -t 二选一，如果两者都提供了，
# 则优先使用命令行参数。
HCOM_BUILD_TYPE="release"

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -t|--type) HCOM_BUILD_TYPE="${2,,}"; shift ;;
        *) echo "Unknown parameter passed: $1"; show_help; exit 1 ;;
    esac
    shift
done

[[ ! -d "${HCOM_INSTALL_DIR}" ]] && echo "${HCOM_LOG_TAG} HCOM install directory(${HCOM_INSTALL_DIR}) does not exist." && exit 1
echo "${HCOM_LOG_TAG} HCOM install directory: ${HCOM_INSTALL_DIR}"

# ****************************************
# make HCOM software package
# ****************************************
[[ -z "${HCOM_PRODUCT_NAME}" ]] && HCOM_PRODUCT_NAME="BeiMing"
[[ -z "${HCOM_PRODUCT_VERSION}" ]] && HCOM_PRODUCT_VERSION="24.4"
[[ -z "${HCOM_COMPONENT_VERSION}" ]] && HCOM_COMPONENT_VERSION="1.0.0"
[[ -z "${HCOM_PACKAGE_PATH}" ]] && HCOM_PACKAGE_PATH="${HCOM_ROOT_DIR}/dist"
[[ -z "${HCOM_BUILD_OS_ARCH}" ]] && HCOM_BUILD_OS_ARCH="aarch64"
HCOM_COMPONENT_COMMIT_ID=""
if [ -d "${HCOM_ROOT_DIR}/.git" ] || (cd "${HCOM_ROOT_DIR}" && git rev-parse --is-inside-work-tree >/dev/null 2>&1); then
    HCOM_COMPONENT_COMMIT_ID=$(cd "${HCOM_ROOT_DIR}" && git rev-parse HEAD 2>/dev/null)
fi
# prepare HCOM software package directory
# hcom is published by BoostKit
cd "${HCOM_PACKAGE_PATH}"

if [[ -z "${OS}" || -z "${HCOM_BUILD_OS_ARCH}" ]]; then
    echo "${HCOM_LOG_TAG} env OS or env ARCH is empty!"
    HCOM_PACKAGE_NAME="BoostKit-${HCOM_COMPONENT_NAME}_${HCOM_COMPONENT_VERSION}_${HCOM_BUILD_OS_ARCH}"
else
    HCOM_PACKAGE_NAME="BoostKit-${HCOM_COMPONENT_NAME}_${HCOM_COMPONENT_VERSION}_${OS}_${HCOM_BUILD_OS_ARCH}"
fi

[[ -n "${HCOM_PACKAGE_NAME}" ]] && rm -rf "${HCOM_PACKAGE_NAME}"
mkdir -p "${HCOM_PACKAGE_NAME}"

# drop securec
rm -rf "${HCOM_INSTALL_DIR}/lib/securec"

# copy HCOM build dist
cp -r "${HCOM_INSTALL_DIR}" "${HCOM_PACKAGE_NAME}"

# check whether build tools perf only release type, default is OFF
HCOM_BUILD_TOOLS_PERF=${HCOM_BUILD_TOOLS_PERF:-off}
if [[ "${HCOM_BUILD_TOOLS_PERF,,}" == "on" && "${HCOM_BUILD_TYPE,,}" == "release" ]]; then
    bash "${HCOM_ROOT_DIR}/build/build_tools_perf.sh"
    # copy hcom_perf
    cp "${HCOM_ROOT_DIR}/test/tools/perf_test/build/hcom_perf" "${HCOM_PACKAGE_NAME}"/hcom/
    echo "${HCOM_LOG_TAG} hcom build tools perf success: ${HCOM_BUILD_TOOLS_PERF}"
fi

# check whether enable htracer_cli, default is off.
#HCOM_BUILD_HTRACER_CLI="${HCOM_BUILD_HTRACER_CLI:-off}"
echo "${HCOM_LOG_TAG} hcom build htracer_cli: ${HCOM_BUILD_HTRACER}"
if [[ "${HCOM_BUILD_HTRACER,,}" == "on" ]]; then
    bash "${HCOM_ROOT_DIR}/build/build_htracer_cli.sh"

    if [ ! -d "$HCOM_INSTALL_TRACER_DIR" ]; then
         mkdir -p "$HCOM_INSTALL_TRACER_DIR"
    fi
    # copy htracer_cli to dist
    cp "${HCOM_TRACER_TOOL}" "${HCOM_INSTALL_TRACER_DIR}"/

    if [ ! -d "${HCOM_PACKAGE_NAME}/hcom/bin" ]; then
         mkdir -p "${HCOM_PACKAGE_NAME}/hcom/bin"
    fi
    # copy htracer_cli software package
    cp -r "${HCOM_TRACER_TOOL}" "${HCOM_PACKAGE_NAME}/hcom/bin"
fi


# generate version info
VERSION_FILE="${HCOM_PACKAGE_PATH}/${HCOM_PACKAGE_NAME}/version.property"
echo "# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

# product info
product_name=@HCOM_PRODUCT_NAME@
product_version=@HCOM_PRODUCT_VERSION@

# component info
component_name=@HCOM_COMPONENT_NAME@
component_version=@HCOM_COMPONENT_VERSION@
component_commit_id=@HCOM_COMPONENT_COMMIT_ID@

# build info
build_time=@HCOM_BUILD_TIME@
build_ostype=@HCOM_BUILD_OS_TYPE@
build_osarch=@HCOM_BUILD_OS_ARCH@

" > "${VERSION_FILE}"

REQUIRED_VARS=("HCOM_PRODUCT_NAME" "HCOM_PRODUCT_VERSION" "HCOM_COMPONENT_NAME" "HCOM_COMPONENT_VERSION"
    "HCOM_BUILD_TIME" "HCOM_BUILD_OS_TYPE" "HCOM_BUILD_OS_ARCH")
for var in "${REQUIRED_VARS[@]}"; do
    [[ -z "${!var}" ]] && echo "${HCOM_LOG_TAG} missing environment: $var" && exit 1
    sed -i "s/@$var@/${!var}/g" "${VERSION_FILE}"
done
chmod 600 "${VERSION_FILE}"
echo "${HCOM_LOG_TAG} generate HCOM version info done"

# make HCOM software package
tar -czf "${HCOM_PACKAGE_NAME}.tar.gz" --exclude *.debug* "${HCOM_PACKAGE_NAME}" 
echo "${HCOM_LOG_TAG} make HCOM software package done.(${HCOM_PACKAGE_PATH}/${HCOM_PACKAGE_NAME}.tar.gz)"

# check whether enable build rpm, default is ON.
if [[ "${HCOM_BUILD_RPM,,}" == "off" ]]; then
    exit 0
fi

mkdir -p ~/rpmbuild/SOURCES/
cp "${HCOM_PACKAGE_PATH}/${HCOM_PACKAGE_NAME}.tar.gz" ~/rpmbuild/SOURCES/

cd "${HCOM_ROOT_DIR}"

# 定义基础的 rpmbuild 命令和公共参数
base_rpmbuild_cmd="rpmbuild --define \"package_name ${HCOM_PACKAGE_NAME}\" -bb hcom.spec"

# 添加特定于 Java SDK 的选项
[[ "${HCOM_BUILD_JAVA_SDK}" == "ON" ]] && base_rpmbuild_cmd="${base_rpmbuild_cmd} --with java_compile"

# 添加特定于 htracer_cli 的选项
[[ "${HCOM_BUILD_HTRACER,,}" == "on" ]] && base_rpmbuild_cmd="${base_rpmbuild_cmd} --define \"_with_htracer_cli 1\""

# 根据构建类型添加调试信息选项
[[ "${HCOM_BUILD_TYPE}" == "debug" ]] && base_rpmbuild_cmd="${base_rpmbuild_cmd} --define \"_build_type debug\""

# 根据是否需要性能工具包决定是否包含 hcom_perf
[[ "${HCOM_BUILD_TYPE}" == "release" && "${HCOM_BUILD_TOOLS_PERF}" == "ON" ]] && base_rpmbuild_cmd="${base_rpmbuild_cmd} --define \"_with_hcom_perf 1\""

# 执行最终的 rpmbuild 命令
eval "$base_rpmbuild_cmd"

if [[ "${HCOM_BUILD_TYPE,,}" == "debug" ]]; then
    cp ~/rpmbuild/RPMS/${HCOM_BUILD_OS_ARCH}/OCK-CommunicationSuite_HCOM_Debug-2.0.0-B099*.rpm "${HCOM_ROOT_DIR}/dist/OCK-CommunicationSuite_HCOM_Debug_2.0.0_${OS}-${HCOM_BUILD_OS_ARCH}.rpm"
else
    cp ~/rpmbuild/RPMS/${HCOM_BUILD_OS_ARCH}/OCK-CommunicationSuite_HCOM-2.0.0-B099*.rpm "${HCOM_ROOT_DIR}/dist/OCK-CommunicationSuite_HCOM_2.0.0_${OS}-${HCOM_BUILD_OS_ARCH}.rpm"
fi
