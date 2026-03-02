#!/bin/bash
readonly MULTICAST_PERF_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_ROOT_DIR="${MULTICAST_PERF_DIR}/../../../.."
readonly HCOM_INSTALL_DIR="${HCOM_ROOT_DIR}/dist/hcom"

cd ${MULTICAST_PERF_DIR}
rm -rf build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=release \
      -DHCOM_INSTALL_DIR=${HCOM_INSTALL_DIR} ..
make -j8
