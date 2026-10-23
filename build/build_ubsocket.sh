#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Script for building UBSOCKET.
# Build options can be configured through environment variables.
# (1) UBSOCKET_BUILD(optional, default is off) => build ubsocket or not.(on/off)
# (2) UBSOCKET_BUILD_SHM(optional, default is off) => build ubsocket shm or not.(on/off)
# (3) UBSOCKET_UT(optional, default is off) => run ubsocket ut or not.(on/off)
# (4) UBSOCKET_PASS_RATE(optional, default is off) => run ubsocket ut pass rate or not.(on/off)
# (5) UBSOCKET_COVERAGE(optional, default is off) => run ubsocket ut coverage or not.(on/off)
# version: 1.0.0
# change log:
# ***********************************************************************
set -e
(set -o pipefail) 2>/dev/null && set -o pipefail

readonly ROOT_DIR=$(dirname "$(realpath "$(dirname "${0}")")")

# check whether build ubsocket, default is off
UBSOCKET_BUILD="${UBSOCKET_BUILD:-off}"
echo "build ubsocket: ${UBSOCKET_BUILD}"

# check whether build ubsocket, default is off
UBSOCKET_BUILD_SHM="${UBSOCKET_BUILD_SHM:-off}"
echo "build ubsocket shm: ${UBSOCKET_BUILD_SHM}"

# check whether run ubsocket ut, default is off
UBSOCKET_UT="${UBSOCKET_UT:-off}"
echo "run ubsocket ut: ${UBSOCKET_UT}"

# check whether run ubsocket ut pass rate, default is off
UBSOCKET_PASS_RATE="${UBSOCKET_PASS_RATE:-off}"
echo "run ubsocket ut pass rate: ${UBSOCKET_PASS_RATE}"

# check whether run ubsocket ut coverage, default is off
UBSOCKET_COVERAGE="${UBSOCKET_COVERAGE:-off}"
echo "run ubsocket ut coverage: ${UBSOCKET_COVERAGE}"

# build ubsocket, .so will store in "./src/ubsocket/build/brpc/librpc_adapter_brpc.so"
function ubsocket_build() {
    if [ "${UBSOCKET_BUILD}" != "on" ]; then
        echo "UBSOCKET_BUILD is off, skipping ubsocket build."
        return 0
    fi

    echo "Building ubsocket ..."

    local ubsocket_dir="${ROOT_DIR}/src/ubsocket"

    cd "${ubsocket_dir}"

    if cmake -S. -Bbuild \
        -DBUILD_WITH_UBS_SHM=${UBSOCKET_BUILD_SHM} \
        -DBUILD_URMA_DLOPEN_BACKEND=ON; then
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

    local ubsocket_dir="${ROOT_DIR}/src/ubsocket"
    cd "${ubsocket_dir}"

    local coverage_flag="OFF"
    if [ "${UBSOCKET_COVERAGE}" = "on" ]; then
        coverage_flag="ON"
    fi

    if cmake -S. -Bbuild \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUBSOCKET_BUILD_TESTS=ON \
        -DUBSOCKET_ENABLE_COVERAGE=${coverage_flag}; then
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

    # Discover all registered ctest tests dynamically
    local test_names=$(ctest -N 2>/dev/null | grep 'Test #' | awk '{print $NF}')
    local xml_files=()

    for test_name in ${test_names}; do
        echo "Running ${test_name} for pass rate report ..."
        ./${test_name} --gtest_output=xml:${test_name}.xml || true
        xml_files+=("${test_name}.xml")
    done

    if [ ${#xml_files[@]} -eq 0 ]; then
        echo "[Warning]: No test XML files found, skipping pass rate report."
        cd "${ROOT_DIR}"
        return 0
    fi

    tests_val=0
    failures_val=0
    disabled_val=0
    errors_val=0
    time_val=0

    for file in "${xml_files[@]}"; do
        if [ ! -f "$file" ]; then
            echo "[Warning]: ${file} not found, skipping."
            continue
        fi
        tests_val=$((tests_val + $(grep "<testsuites " "$file" | awk -F "tests=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')))
        failures_val=$((failures_val + $(grep "<testsuites " "$file" | awk -F "failures=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')))
        disabled_val=$((disabled_val + $(grep "<testsuites " "$file" | awk -F "disabled=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')))
        errors_val=$((errors_val + $(grep "<testsuites " "$file" | awk -F "errors=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')))
        time_val=$(echo "$time_val + $(grep "<testsuites " "$file" | awk -F "time=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')" | bc)
    done

    timestamp_val=$(cat "${xml_files[0]}" | grep "<testsuites " | head -n 1 | awk -F "timestamp=" '{print $2}' | awk '{print $1}' | awk -F "\"" '{print $2}')
    pass_rate=$(echo "scale=2; ($tests_val - $failures_val - $errors_val) / $tests_val * 100" | bc)
    fail_rate=$(echo "scale=2; ($failures_val + $errors_val) / $tests_val * 100" | bc)

    {
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

ubsocket_build
run_ubsocket_ut_tests
run_ubsocket_pass_rate
run_ubsocket_coverage