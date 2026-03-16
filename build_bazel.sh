#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Script for building HCOM by Bazel.
# Build options are aligned with build.sh as much as possible.
# version: 1.1.0
# ***********************************************************************
set -eo pipefail

readonly HCOM_ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
readonly HCOM_LOG_TAG="[$(basename "$0")]"
readonly HCOM_INSTALL_DIR="${HCOM_ROOT_DIR}/dist/hcom"
readonly HCOM_UT_BUILD_DIR="${HCOM_ROOT_DIR}/tmp_build_dir_bazel_ut"
HCOM_SOVERSION="0"
HCOM_LIB_VERSION="0.0.1"

HCOM_COMPONENT_VERSION="${HCOM_COMPONENT_VERSION:-1.0.0}"
HCOM_BUILD_TYPE="${HCOM_BUILD_TYPE,,}"
HCOM_BUILD_TYPE="${HCOM_BUILD_TYPE:-release}"

# Same options as build.sh
HCOM_BUILD_HW_CRC="${HCOM_BUILD_HW_CRC:-off}"
HCOM_BUILD_UB="${HCOM_BUILD_UB:-off}"
HCOM_BUILD_SERVICE="${HCOM_BUILD_SERVICE:-on}"
HCOM_BUILD_RDMA="${HCOM_BUILD_RDMA:-on}"
HCOM_BUILD_SOCK="${HCOM_BUILD_SOCK:-on}"
HCOM_BUILD_SHM="${HCOM_BUILD_SHM:-on}"
HCOM_BUILD_MULTICAST="${HCOM_BUILD_MULTICAST:-off}"
HCOM_ENABLE_ARM_KP="${HCOM_ENABLE_ARM_KP:-off}"
HCOM_BUILD_JAVA_SDK="${HCOM_BUILD_JAVA_SDK:-off}"
HCOM_BUILD_TESTS="${HCOM_BUILD_TESTS:-off}"
HCOM_BUILD_EXAMPLE="${HCOM_BUILD_EXAMPLE:-off}"
HCOM_BUILD_RPM="${HCOM_BUILD_RPM:-off}"
HCOM_BUILD_TOOLS_PERF="${HCOM_BUILD_TOOLS_PERF:-off}"
BUILD_HCOM="${BUILD_HCOM:-ON}"
HCOM_BAZEL_CPU="${HCOM_BAZEL_CPU:-}"
HCOM_BUILD_ALLOCATOR_PROTECTION="${HCOM_BUILD_ALLOCATOR_PROTECTION:-off}"
HCOM_TEST_TOOL_PATH="${HCOM_TEST_TOOL_PATH:-${HCOM_ROOT_DIR}/dist/hcom_test_tools}"

function show_help() {
    echo "Usage: $0 [COMMAND] [OPTION]"
    echo "Build HCOM with Bazel."
    echo "Commands: clean"
    echo "Options:"
    echo "    -t, --type TYPE       Set build type. debug/release"
}

function to_cmake_onoff() {
    if [[ "${1,,}" == "on" ]]; then
        echo "ON"
    else
        echo "OFF"
    fi
}

function append_bazel_define_if() {
    local option_value="${1,,}"
    local expect_value="${2,,}"
    local define_kv="$3"
    if [[ "${option_value}" == "${expect_value}" ]]; then
        BAZEL_FLAGS+=("--define=${define_kv}")
    fi
}


function load_hcom_lib_version_from_cmake() {
    local cmake_file="${HCOM_ROOT_DIR}/src/hcom/CMakeLists.txt"
    local major=""
    local minor=""
    local patch=""

    if [[ -f "${cmake_file}" ]]; then
        local parsed=""
        parsed=$(awk '
            /set\(LIB_VERSION_MAJOR[[:space:]]+[0-9]+/ { major=$2; gsub(/\)/, "", major) }
            /set\(LIB_VERSION_MINOR[[:space:]]+[0-9]+/ { minor=$2; gsub(/\)/, "", minor) }
            /set\(LIB_VERSION_PATCH[[:space:]]+[0-9]+/ { patch=$2; gsub(/\)/, "", patch) }
            END { print major " " minor " " patch }
        ' "${cmake_file}")
        read -r major minor patch <<< "${parsed}"
    fi

    if [[ -z "${major}" || -z "${minor}" || -z "${patch}" ]]; then
        major="0"
        minor="0"
        patch="1"
        echo "${HCOM_LOG_TAG} warn: failed to parse lib version from CMakeLists, fallback to ${major}.${minor}.${patch}"
    fi

    HCOM_SOVERSION="${major}"
    HCOM_LIB_VERSION="${major}.${minor}.${patch}"
}

function verify_hcom_install_layout() {
    local expected=(
        "${HCOM_INSTALL_DIR}/lib/libhcom.so.${HCOM_LIB_VERSION}"
        "${HCOM_INSTALL_DIR}/lib/libhcom.so.${HCOM_SOVERSION}"
        "${HCOM_INSTALL_DIR}/lib/libhcom.so"
        "${HCOM_INSTALL_DIR}/lib/libhcom_static.a"
        "${HCOM_INSTALL_DIR}/lib/libboundscheck.so"
        "${HCOM_INSTALL_DIR}/include/hcom/hcom.h"
        "${HCOM_INSTALL_DIR}/include/hcom/capi/hcom_c.h"
    )

    local miss=0
    for item in "${expected[@]}"; do
        if [[ ! -e "${item}" ]]; then
            echo "${HCOM_LOG_TAG} error: missing install artifact: ${item}"
            miss=1
        fi
    done

    if [[ "${HCOM_BUILD_MULTICAST,,}" == "on" && ! -d "${HCOM_INSTALL_DIR}/include/hcom/multicast" ]]; then
        echo "${HCOM_LOG_TAG} error: missing multicast include directory: ${HCOM_INSTALL_DIR}/include/hcom/multicast"
        miss=1
    fi

    if [[ ${miss} -ne 0 ]]; then
        exit 1
    fi
}
function clean_dir() {
    [[ -n "${HCOM_INSTALL_DIR}" ]] && rm -rf "${HCOM_INSTALL_DIR}"
    [[ -n "${HCOM_UT_BUILD_DIR}" ]] && rm -rf "${HCOM_UT_BUILD_DIR}"
    if command -v bazel >/dev/null 2>&1; then
        bazel clean >/dev/null 2>&1 || true
    elif command -v bazelisk >/dev/null 2>&1; then
        bazelisk clean >/dev/null 2>&1 || true
    fi
    echo "Cleanup: ${HCOM_INSTALL_DIR}, ${HCOM_UT_BUILD_DIR} and bazel outputs"
}

function copy_if_exists() {
    local src="$1"
    local dst="$2"
    local hint="$3"
    if [[ -f "${src}" ]]; then
        cp -f "${src}" "${dst}"
    else
        echo "${HCOM_LOG_TAG} info: ${hint} not found, skip (${src})"
    fi
}

function cquery_first_file() {
    local target="$1"
    local regex="$2"
    "${BAZEL_BIN}" cquery "${BAZEL_FLAGS[@]}" --output=files "${target}" | grep -E "${regex}" | head -n 1 || true
}

function first_existing_file() {
    for candidate in "$@"; do
        if [[ -n "${candidate}" && -f "${candidate}" ]]; then
            echo "${candidate}"
            return 0
        fi
    done
    return 1
}


function copy_if_exists_silent() {
    local src="$1"
    local dst="$2"
    if [[ -f "${src}" ]]; then
        cp -f "${src}" "${dst}"
    fi
}

function run_hcom_test_build_fallback() {
    echo "${HCOM_LOG_TAG} HCOM_BUILD_TESTS=on: Bazel test targets are not available; using CMake fallback to build UT binaries."

    if [[ ! -d "${HCOM_TEST_TOOL_PATH}" ]]; then
        echo "${HCOM_LOG_TAG} hcom test tools are not installed, installing..."
        HCOM_TEST_TOOL_PATH="${HCOM_TEST_TOOL_PATH}" bash "${HCOM_ROOT_DIR}/build/install_test_tools.sh"
    fi

    rm -rf "${HCOM_UT_BUILD_DIR}"

    cmake -S"${HCOM_ROOT_DIR}" -B"${HCOM_UT_BUILD_DIR}" \
        -DBUILD_HCOM=ON \
        -DCMAKE_BUILD_TYPE=debug \
        -DBUILD_TESTS=ON \
        -DTEST_TOOL_INSTALL_PATH="${HCOM_TEST_TOOL_PATH}" \
        -DBUILD_JAVA_SDK=OFF \
        -DBUILD_WITH_HW_CRC=$(to_cmake_onoff "${HCOM_BUILD_HW_CRC}") \
        -DBUILD_WITH_UB=$(to_cmake_onoff "${HCOM_BUILD_UB}") \
        -DBUILD_WITH_RDMA=$(to_cmake_onoff "${HCOM_BUILD_RDMA}") \
        -DBUILD_WITH_SOCK=$(to_cmake_onoff "${HCOM_BUILD_SOCK}") \
        -DBUILD_WITH_SHM=$(to_cmake_onoff "${HCOM_BUILD_SHM}") \
        -DBUILD_WITH_MULTICAST=$(to_cmake_onoff "${HCOM_BUILD_MULTICAST}") \
        -DENABLE_ARM_KP=$(to_cmake_onoff "${HCOM_ENABLE_ARM_KP}") \
        -DHCOM_COMPONENT_VERSION="${HCOM_COMPONENT_VERSION}"

    cmake --build "${HCOM_UT_BUILD_DIR}" -j "$(nproc)"

    echo "${HCOM_LOG_TAG} UT binaries are ready: ${HCOM_UT_BUILD_DIR}/hcom_ut, ${HCOM_UT_BUILD_DIR}/hcom_test"
    echo "${HCOM_LOG_TAG} To run UT and generate xml manually:"
    echo "  ${HCOM_UT_BUILD_DIR}/hcom_ut --gtest_output=xml:${HCOM_UT_BUILD_DIR}/res_xml/ut_result.xml"
    echo "  ${HCOM_UT_BUILD_DIR}/hcom_test --gtest_output=xml:${HCOM_UT_BUILD_DIR}/res_xml/llt_result.xml"
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -t|--type) HCOM_BUILD_TYPE="${2,,}"; shift ;;
        clean) clean_dir; exit 0 ;;
        *) echo "Unknown parameter passed: $1"; show_help; exit 1 ;;
    esac
    shift
done

if [[ "${HCOM_BUILD_TYPE}" != "release" && "${HCOM_BUILD_TYPE}" != "debug" ]]; then
    echo "${HCOM_LOG_TAG} invalid HCOM_BUILD_TYPE: ${HCOM_BUILD_TYPE}, expected debug/release"
    exit 1
fi

BAZEL_BIN=""
if command -v bazel >/dev/null 2>&1; then
    BAZEL_BIN="bazel"
elif command -v bazelisk >/dev/null 2>&1; then
    BAZEL_BIN="bazelisk"
else
    echo "${HCOM_LOG_TAG} bazel/bazelisk not found in PATH"
    exit 1
fi

load_hcom_lib_version_from_cmake


echo "HCOM ROOT: ${HCOM_ROOT_DIR}"
echo "HCOM INSTALL DIR: ${HCOM_INSTALL_DIR}"
echo "${HCOM_LOG_TAG} using bazel command: ${BAZEL_BIN}"
echo "${HCOM_LOG_TAG} hcom build type: ${HCOM_BUILD_TYPE}"
echo "${HCOM_LOG_TAG} hcom build hw crc: ${HCOM_BUILD_HW_CRC}"
echo "${HCOM_LOG_TAG} hcom build ub: ${HCOM_BUILD_UB}"
echo "${HCOM_LOG_TAG} hcom build service: ${HCOM_BUILD_SERVICE}"
echo "${HCOM_LOG_TAG} hcom build rdma: ${HCOM_BUILD_RDMA}"
echo "${HCOM_LOG_TAG} hcom build sock: ${HCOM_BUILD_SOCK}"
echo "${HCOM_LOG_TAG} hcom build shm: ${HCOM_BUILD_SHM}"
echo "${HCOM_LOG_TAG} hcom build multicast: ${HCOM_BUILD_MULTICAST}"
echo "${HCOM_LOG_TAG} hcom enable arm kunpeng check: ${HCOM_ENABLE_ARM_KP}"
echo "${HCOM_LOG_TAG} hcom build java sdk: ${HCOM_BUILD_JAVA_SDK}"
echo "${HCOM_LOG_TAG} hcom build tests: ${HCOM_BUILD_TESTS}"
echo "${HCOM_LOG_TAG} hcom test tools path: ${HCOM_TEST_TOOL_PATH}"
echo "${HCOM_LOG_TAG} build hcom: ${BUILD_HCOM}"
echo "${HCOM_LOG_TAG} build rpm: ${HCOM_BUILD_RPM}"
echo "${HCOM_LOG_TAG} lib version (from CMake): ${HCOM_LIB_VERSION} (soversion=${HCOM_SOVERSION})"

# BUILD_WITH_SERVICE is currently a no-op in existing CMake logic too.
if [[ "${HCOM_BUILD_SERVICE,,}" != "on" ]]; then
    echo "${HCOM_LOG_TAG} info: BUILD_WITH_SERVICE=off currently has no source-level effect in CMake and Bazel, treated as no-op."
fi

# Java SDK is impossible in current repository state.
if [[ "${HCOM_BUILD_JAVA_SDK,,}" == "on" ]]; then
    echo "${HCOM_LOG_TAG} error: HCOM_BUILD_JAVA_SDK=on cannot be implemented now."
    echo "${HCOM_LOG_TAG} reason: current repo has no java_sdk sources/rules (src/hcom/api/java_sdk is absent)."
    echo "${HCOM_LOG_TAG} action: set HCOM_BUILD_JAVA_SDK=off; add java_sdk sources and Bazel/CMake rules first if needed."
    exit 1
fi

if [[ "${BUILD_HCOM^^}" != "ON" ]]; then
    echo "${HCOM_LOG_TAG} BUILD_HCOM=${BUILD_HCOM}, skip hcom bazel build."
    echo "${HCOM_LOG_TAG} $0 succeeds"
    exit 0
fi

# Fresh install directory every run (same as build.sh semantics)
[[ -n "${HCOM_INSTALL_DIR}" ]] && rm -rf "${HCOM_INSTALL_DIR}"
mkdir -p "${HCOM_INSTALL_DIR}/lib" "${HCOM_INSTALL_DIR}/include/hcom/capi"

declare -a BAZEL_FLAGS
BAZEL_FLAGS+=("--noenable_bzlmod")

if [[ "${HCOM_BUILD_TYPE}" == "release" ]]; then
    BAZEL_FLAGS+=("-c" "opt")
else
    BAZEL_FLAGS+=("-c" "dbg")
fi

if [[ -n "${HCOM_BAZEL_CPU}" ]]; then
    BAZEL_FLAGS+=("--cpu=${HCOM_BAZEL_CPU}")
fi

# metadata macros are provided by @hcom_build_metadata repository rule.

# Map build options to Bazel defines.
append_bazel_define_if "${HCOM_BUILD_RDMA}" "off" "hcom_enable_rdma=0"
append_bazel_define_if "${HCOM_BUILD_SHM}" "off" "hcom_enable_shm=0"
append_bazel_define_if "${HCOM_BUILD_SOCK}" "off" "hcom_enable_sock=0"
append_bazel_define_if "${HCOM_BUILD_UB}" "on" "hcom_enable_ub=1"
append_bazel_define_if "${HCOM_BUILD_MULTICAST}" "on" "hcom_enable_multicast=1"
append_bazel_define_if "${HCOM_BUILD_HW_CRC}" "on" "hcom_enable_hw_crc=1"
append_bazel_define_if "${HCOM_ENABLE_ARM_KP}" "on" "hcom_enable_arm_kp=1"
append_bazel_define_if "${HCOM_BUILD_ALLOCATOR_PROTECTION}" "on" "hcom_enable_allocator_protection=1"

cd "${HCOM_ROOT_DIR}"
"${BAZEL_BIN}" build "${BAZEL_FLAGS[@]}" //src/hcom:hcom_static //src/hcom:libhcom.so //src:libboundscheck.so

# Collect artifacts from bazel outputs (fallback to cquery/find when needed)
SHARED_LIB_PATH=$(first_existing_file \
    "${HCOM_ROOT_DIR}/bazel-bin/src/hcom/libhcom.so" \
    "$(cquery_first_file //src/hcom:libhcom.so '/libhcom\.so$')" \
) || true

STATIC_LIB_PATH=$(first_existing_file \
    "${HCOM_ROOT_DIR}/bazel-bin/src/hcom/libhcom_static.a" \
    "${HCOM_ROOT_DIR}/bazel-bin/src/hcom/libhcom_static.pic.a" \
    "$(cquery_first_file //src/hcom:hcom_static '/libhcom_static(\\.pic)?\\.(a|lo)$')" \
    "$(find "${HCOM_ROOT_DIR}/bazel-bin/src/hcom" -maxdepth 2 -type f \( -name 'libhcom_static.a' -o -name 'libhcom_static.pic.a' -o -name 'libhcom_static.lo' -o -name 'libhcom_static.pic.lo' \) | head -n 1 || true)" \
) || true

BOUNDSCHECK_SO_PATH=$(first_existing_file \
    "${HCOM_ROOT_DIR}/bazel-bin/src/libboundscheck.so" \
    "$(cquery_first_file //src:libboundscheck.so '/libboundscheck\.so$')" \
    "$(find "${HCOM_ROOT_DIR}/bazel-bin/src" -maxdepth 2 -type f -name 'libboundscheck.so' | head -n 1 || true)" \
) || true

if [[ -z "${SHARED_LIB_PATH}" || ! -f "${SHARED_LIB_PATH}" ]]; then
    echo "${HCOM_LOG_TAG} failed to locate libhcom.so in bazel outputs"
    exit 1
fi
if [[ -z "${STATIC_LIB_PATH}" || ! -f "${STATIC_LIB_PATH}" ]]; then
    echo "${HCOM_LOG_TAG} failed to locate libhcom static archive (a/pic.a/lo/pic.lo) in bazel outputs"
    "${BAZEL_BIN}" cquery "${BAZEL_FLAGS[@]}" --output=files //src/hcom:hcom_static || true
    exit 1
fi
if [[ -z "${BOUNDSCHECK_SO_PATH}" || ! -f "${BOUNDSCHECK_SO_PATH}" ]]; then
    echo "${HCOM_LOG_TAG} failed to locate libboundscheck.so in bazel outputs"
    "${BAZEL_BIN}" cquery "${BAZEL_FLAGS[@]}" --output=files //src:libboundscheck.so || true
    exit 1
fi

cp -f "${SHARED_LIB_PATH}" "${HCOM_INSTALL_DIR}/lib/libhcom.so.${HCOM_LIB_VERSION}"
ln -sfn "libhcom.so.${HCOM_LIB_VERSION}" "${HCOM_INSTALL_DIR}/lib/libhcom.so.${HCOM_SOVERSION}"
ln -sfn "libhcom.so.${HCOM_SOVERSION}" "${HCOM_INSTALL_DIR}/lib/libhcom.so"
cp -f "${STATIC_LIB_PATH}" "${HCOM_INSTALL_DIR}/lib/libhcom_static.a"
cp -f "${BOUNDSCHECK_SO_PATH}" "${HCOM_INSTALL_DIR}/lib/libboundscheck.so"

# Install headers with the same layout as CMake install.
for header in "${HCOM_ROOT_DIR}"/src/hcom/hcom*.h; do
    [[ -f "${header}" ]] && cp -f "${header}" "${HCOM_INSTALL_DIR}/include/hcom/"
done

declare -a HCOM_SERVICE_HEADERS=(
    "${HCOM_ROOT_DIR}/src/hcom/service_v2/api/hcom_service_channel.h"
    "${HCOM_ROOT_DIR}/src/hcom/service_v2/api/hcom_service_def.h"
    "${HCOM_ROOT_DIR}/src/hcom/service_v2/api/hcom_service_context.h"
    "${HCOM_ROOT_DIR}/src/hcom/service_v2/api/hcom_service.h"
)
declare -a HCOM_CAPI_HEADERS=(
    "${HCOM_ROOT_DIR}/src/hcom/api/capi_v2/hcom_c.h"
    "${HCOM_ROOT_DIR}/src/hcom/api/capi_v2/hcom_service_c.h"
)
local_header=""
for local_header in "${HCOM_SERVICE_HEADERS[@]}"; do
    copy_if_exists "${local_header}" "${HCOM_INSTALL_DIR}/include/hcom/" "service header"
done
for local_header in "${HCOM_CAPI_HEADERS[@]}"; do
    copy_if_exists "${local_header}" "${HCOM_INSTALL_DIR}/include/hcom/capi/" "capi header"
done
# legacy capi header is optional; keep silent when absent to match CMake install behavior.
copy_if_exists_silent "${HCOM_ROOT_DIR}/src/hcom/api/capi/hcom_cgo_c.h" "${HCOM_INSTALL_DIR}/include/hcom/capi/"

if [[ "${HCOM_BUILD_MULTICAST,,}" == "on" ]]; then
    mkdir -p "${HCOM_INSTALL_DIR}/include/hcom/multicast"
    for header in "${HCOM_ROOT_DIR}"/src/hcom/multicast/include/multicast*.h; do
        [[ -f "${header}" ]] && cp -f "${header}" "${HCOM_INSTALL_DIR}/include/hcom/multicast/"
    done
fi

verify_hcom_install_layout

# Adaptation for temporary Bazel gap: build UT targets through CMake if requested.
if [[ "${HCOM_BUILD_TESTS,,}" == "on" ]]; then
    run_hcom_test_build_fallback
fi

# Collect objects and package software (aligned with build.sh behavior).
HCOM_COMPONENT_VERSION="${HCOM_COMPONENT_VERSION}" \
    HCOM_BUILD_RPM="${HCOM_BUILD_RPM}" \
    HCOM_BUILD_TOOLS_PERF="${HCOM_BUILD_TOOLS_PERF}" \
    HCOM_BUILD_MULTICAST="${HCOM_BUILD_MULTICAST}" \
    HCOM_BUILD_JAVA_SDK="${HCOM_BUILD_JAVA_SDK}" \
    HCOM_CI_WORKSPACE="${HCOM_CI_WORKSPACE}" \
    bash "${HCOM_ROOT_DIR}/build/make_software_package.sh" -t "${HCOM_BUILD_TYPE}"

# Build example and perf when requested.
[[ "${HCOM_BUILD_EXAMPLE,,}" == "on" ]] && bash "${HCOM_ROOT_DIR}/build/build_example_perf.sh"


echo "${HCOM_LOG_TAG} $0 succeeds"

