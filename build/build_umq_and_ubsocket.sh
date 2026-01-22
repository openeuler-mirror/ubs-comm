#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Script for building UMQ and UBSOCKET.
# Build options can be configured through environment variables.
# (1) UMQ_BUILD(optional, default is off) => build umq or not.(on/off)
# (2) UBSOCKET_BUILD(optional, default is off) => build ubsocket or not.(on/off)
# (3) UBSOCKET_UT(optional, default is off) => run ubsocket ut or not.(on/off)

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

    if ! cmake ..; then
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
    else
        echo "[Error]: Some ubsocket UT tests failed."
        cd "${ROOT_DIR}"
        exit 1
    fi
    
    cd build

    if make coverage; then
        echo "Make ubsocket coverage successfully."
        cd "${ROOT_DIR}"
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
