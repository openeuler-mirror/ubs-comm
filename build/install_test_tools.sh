#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# Script for preparing test environment.
# HCOM_TEST_TOOL_PATH is set to "${HCOM_ROOT_DIR}/dist/hcom_test_tools" by default.
# gtest will be installed to "${HCOM_TEST_TOOL_PATH}/googletest".
# mockcpp will be installed to "${HCOM_TEST_TOOL_PATH}/mockcpp".
# secodefuzz will be installed to "${HCOM_TEST_TOOL_PATH}/secodefuzz".
#
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly MOCKCPP_PATCH_FILENAME="0001-fix-page-size.patch"
readonly CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly TEST_TOOL_BUILD_DIR="${HCOM_ROOT_DIR}/build/tmp_dir_for_prepare_test"

if [ -z "${HCOM_TEST_TOOL_PATH}" ]; then
    echo "${HCOM_LOG_TAG} HCOM_TEST_TOOL_PATH is empty, set to default value."
    HCOM_TEST_TOOL_PATH="${HCOM_ROOT_DIR}/dist/hcom_test_tools"
fi
echo "${HCOM_LOG_TAG} HCOM_TEST_TOOL_PATH: ${HCOM_TEST_TOOL_PATH}"
echo "${HCOM_LOG_TAG} TEST_TOOL_BUILD_DIR: ${TEST_TOOL_BUILD_DIR}"

GTEST_INSTALL_PATH="${HCOM_TEST_TOOL_PATH}/googletest"
MOCKCPP_INSTALL_PATH="${HCOM_TEST_TOOL_PATH}/mockcpp"
SECODEFUZZ_INSTALL_PATH="${HCOM_TEST_TOOL_PATH}/secodefuzz"
echo "${HCOM_LOG_TAG} GTEST_INSTALL_PATH: ${GTEST_INSTALL_PATH}"
echo "${HCOM_LOG_TAG} MOCKCPP_INSTALL_PATH: ${MOCKCPP_INSTALL_PATH}"
echo "${HCOM_LOG_TAG} SECODEFUZZ_INSTALL_PATH: ${SECODEFUZZ_INSTALL_PATH}"

# prepare test tool build dir
if [ -d "${TEST_TOOL_BUILD_DIR}" ]; then
    rm -rf ${TEST_TOOL_BUILD_DIR}
fi
mkdir -p ${TEST_TOOL_BUILD_DIR}

# prepare googletest
cd ${TEST_TOOL_BUILD_DIR}
git clone https://github.com/google/googletest.git
cd googletest
git checkout -b release-1.12.1 release-1.12.1
mkdir build && cd build
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=${GTEST_INSTALL_PATH} -DINSTALL_GTEST=ON ..
make -j8
make install
echo "${HCOM_LOG_TAG} googletest install to ${GTEST_INSTALL_PATH} success."

# prepare mockcpp
cd ${TEST_TOOL_BUILD_DIR}
git clone https://github.com/sinojelly/mockcpp.git
cd mockcpp
git checkout -b mockcpp_arm v2.7
git apply ${HCOM_ROOT_DIR}/test/external_libs/mockcpp_support_arm64.patch
mkdir build && cd build
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${MOCKCPP_INSTALL_PATH} ..
make -j8
make install
echo "${HCOM_LOG_TAG} mockcpp install to ${MOCKCPP_INSTALL_PATH} success."

# prepare secodefuzz
cd ${TEST_TOOL_BUILD_DIR}
git clone https://codehub-dg-y.huawei.com/software-engineering-research-community/fuzz/secodefuzz.git
cd secodefuzz
git checkout -b v2.4.8 v2.4.8
bash build.sh
mkdir -p "${SECODEFUZZ_INSTALL_PATH}/lib"
cp ./examples/out-bin-x64/out/* "${SECODEFUZZ_INSTALL_PATH}/lib"
cp ./examples/out-bin-x64/libSecodefuzz.a "${SECODEFUZZ_INSTALL_PATH}/lib"
mkdir -p "${SECODEFUZZ_INSTALL_PATH}/include/secodefuzz"
cp ./Secodefuzz/secodeFuzz.h "${SECODEFUZZ_INSTALL_PATH}/include/secodefuzz"
echo "${HCOM_LOG_TAG} secodefuzz install to ${SECODEFUZZ_INSTALL_PATH} success."

# clean
rm -rf ${TEST_TOOL_BUILD_DIR}
