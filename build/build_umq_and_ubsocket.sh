#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Script for building UMQ and UBSOCKET.
# Build options can be configured through environment variables.
# (1) UMQ_BUILD(optional, default is off) => build umq or not.(on/off)
# (2) UBSOCKET_BUILD(optional, default is off) => build ubsocket or not.(on/off)
# (3) UBSOCKET_UT(optional, default is off) => run ubsocket ut or not.(on/off)
# (4) USE_URMA_STUB(optional, default is OFF) => in CI environment, use urma stub or not.(ON/OFF)
# (5) UBSOCKET_PASS_RATE(optional, default is off) => run ubsocket ut pass rate or not.(on/off)
# (6) UBSOCKET_COVERAGE(optional, default is off) => run ubsocket ut coverage or not.(on/off)
# version: 1.0.0
# change log:
# ***********************************************************************
set -eo pipefail

readonly ROOT_DIR=$(dirname "$(realpath "$(dirname "${0}")")")

# check whether build umq, default is off
UMQ_BUILD="${UMQ_BUILD:-off}"
echo "build umq: ${UMQ_BUILD}"

# check whether build ubsocket, default is off
UBSOCKET_BUILD="${UBSOCKET_BUILD:-off}"
echo "build ubsocket: ${UBSOCKET_BUILD}"

# check whether build ubsocket, default is off
UBSOCKET_BUILD_SHM="${UBSOCKET_BUILD_SHM:-off}"
echo "build ubsocket shm: ${UBSOCKET_BUILD_SHM}"

# check whether run ubsocket ut, default is off
UBSOCKET_UT="${UBSOCKET_UT:-off}"
echo "run ubsocket ut: ${UBSOCKET_UT}"

# when in CI environment, open it
USE_URMA_STUB="${USE_URMA_STUB:-OFF}"
echo "use urma stub: ${USE_URMA_STUB}"

# check whether run ubsocket ut pass rate, default is off
UBSOCKET_PASS_RATE="${UBSOCKET_PASS_RATE:-off}"
echo "run ubsocket ut pass rate: ${UBSOCKET_PASS_RATE}"

# check whether run ubsocket ut coverage, default is off
UBSOCKET_COVERAGE="${UBSOCKET_COVERAGE:-off}"
echo "run ubsocket ut coverage: ${UBSOCKET_COVERAGE}"

# build umq, .so will store in "./src/hcom/umq/build/src"
function umq_build() {
    if [ "${UMQ_BUILD}" != "on" ]; then
        echo "UMQ_BUILD is off, skipping umq build."
        return 0
    fi

    local umq_dir="${ROOT_DIR}/src/hcom/umq"

    echo "Building UMQ ..."
    cd "${umq_dir}"
    mkdir -p build
    cd build

    if ! cmake -DUSE_URMA_STUB="${USE_URMA_STUB}" ..; then
        echo "[Error]: umq cmake failed."
        exit 1
    fi
    echo "umq cmake successfully."

    local num_cores=$(nproc 2>/dev/null || echo 4)

    if ! make -j"${num_cores}"; then
        echo "[Error]: umq make failed."
        exit 1
    fi
    echo "umq make successfully."
}
# build ubsocket, .so will store in "./src/ubsocket/build/brpc/librpc_adapter_brpc.so"
function ubsocket_build() {
    if [ "${UBSOCKET_BUILD}" != "on" ]; then
        echo "UBSOCKET_BUILD is off, skipping ubsocket build."
        return 0
    fi

    echo "Building ubsocket ..."

    if [ "${UMQ_BUILD}" != "on" ]; then
        echo "[Error]: ubsocket build needs UMQ_BUILD is on! You should build umq first."
        exit 1
    fi

    local ubsocket_dir="${ROOT_DIR}/src/ubsocket"

    cd "${ubsocket_dir}"

    if cmake -S. -Bbuild \
        -DBUILD_WITH_UBS_SHM=${UBSOCKET_BUILD_SHM} -DUMQ_INCLUDE="${ROOT_DIR}/src/hcom/umq/include/umq" \
        -DUMQ_LIB="${ROOT_DIR}/src/hcom/umq/build/src/libumq.so"; then
        echo "ubsocket cmake successfully"
    else
        echo "[Error]: ubsocket cmake failed."
        cd "${ROOT_DIR}"
        exit 1
    fi
    
    cd build

    local num_cores=$(nproc 2>/dev/null || echo 4)

    if ! make -j"${num_cores}"; then
        echo "[Error]: ubsocket make failed."
        exit 1
    fi
    echo "ubsocket make successfully."
}
# run ubsocket ut
function run_ubsocket_ut_tests() {
    if [ "${UBSOCKET_UT}" != "on" ]; then
        echo "UBSOCKET_UT is off, skipping ubsocket UT tests."
        return 0
    fi

    echo "Building ubsocket UT tests ..."

    if [ "${UMQ_BUILD}" != "on" ]; then
        echo "[Error]: ubsocket ut needs UMQ_BUILD is on! You should build umq first."
        exit 1
    fi

    local ubsocket_dir="${ROOT_DIR}/src/ubsocket"
    cd "${ubsocket_dir}"

    if cmake -S. -Bbuild \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUBSOCKET_BUILD_TESTS=ON \
        -DUBSOCKET_ENABLE_COVERAGE=ON \
        -DUMQ_INCLUDE="${ROOT_DIR}/src/hcom/umq/include/umq" \
        -DUMQ_LIB="${ROOT_DIR}/src/hcom/umq/build/src/libumq.so" \
        -DUMQ_BUF_LIB="${ROOT_DIR}/src/hcom/umq/build/src/qbuf/libumq_buf.so"; then
        echo "UT tests cmake successfully."
    else
        echo "[Error]: UT tests cmake failed."
        cd "${ROOT_DIR}"
        exit 1
    fi

    if cmake --build build -j32; then
        echo "UT tests build successfully."
    else
        echo "[Error]: UT tests build failed."
        cd "${ROOT_DIR}"
        exit 1
    fi

    if ctest --test-dir build --output-on-failure; then
        echo "All ubsocket UT tests successfully."
        return 0
    else
        echo "[Error]: Some ubsocket UT tests failed."
        cd "${ROOT_DIR}"
        exit 1
    fi
}
# run ubsocket ut pass rate
function run_ubsocket_pass_rate() {
    if [ "${UBSOCKET_PASS_RATE}" != "on" ]; then
        echo "UBSOCKET_PASS_RATE is off, skipping ubsocket ut pass rate."
        return 0
    fi

    echo "Generating ubsocket UT pass rate report ..."

    local ubsocket_build_dir="${ROOT_DIR}/src/ubsocket/build"
    cd "${ubsocket_build_dir}"

    ./ubsocket_test --gtest_output=xml:./ubsocket_ut.xml

    {
        tests_val=$(cat ubsocket_ut.xml |grep "<testsuites "|awk -F "tests=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
        failures_val=$(cat ubsocket_ut.xml |grep "<testsuites "|awk -F "failures=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
        disabled_val=$(cat ubsocket_ut.xml |grep "<testsuites "|awk -F "disabled=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
        errors_val=$(cat ubsocket_ut.xml |grep "<testsuites "|awk -F "errors=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
        time_val=$(cat ubsocket_ut.xml |grep "<testsuites "|awk -F "time=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
        timestamp_val=$(cat ubsocket_ut.xml |grep "<testsuites "| head -n 1|awk -F "timestamp=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}')
        pass_rate=$(echo "scale=2; ($tests_val - $failures_val - $errors_val) / $tests_val * 100" | bc)
        fail_rate=$(echo "scale=2; ($failures_val + $errors_val) / $tests_val * 100" | bc)

        echo "Tests Count: ${tests_val}"
        echo "Failure: ${failures_val}"
        echo "Disabled: ${disabled_val}"
        echo "Errors: ${errors_val}"
        echo "Use time: ${time_val}"
        echo "TimeStamp: ${timestamp_val}"
        echo "Pass Rate: ${pass_rate}%"
        echo "Fail Rate: ${fail_rate}%"
    } | tee pass_rate_summary.txt

    echo "Pass rate report generated in: ${ROOT_DIR}/src/ubsocket/build/pass_rate_summary.txt"
}
# run ubsocket ut coverage
function run_ubsocket_coverage() {
    if [ "${UBSOCKET_COVERAGE}" != "on" ]; then
        echo "UBSOCKET_COVERAGE is off, skipping ubsocket ut coverage."
        return 0
    fi

    echo "Generating ubsocket UT coverage report ..."

    local ubsocket_build_dir="${ROOT_DIR}/src/ubsocket/build"
    cd "${ubsocket_build_dir}"

    if make coverage; then
        echo "Make ubsocket coverage successfully."
        return 0
    else
        echo "[Error]: Make ubsocket coverage failed."
        cd "${ROOT_DIR}"
        exit 1
    fi
}

umq_build
ubsocket_build
run_ubsocket_ut_tests
run_ubsocket_pass_rate
run_ubsocket_coverage
