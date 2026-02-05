#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# script for sync umq.
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly CURRENT_SCRIPT_DIR=$(realpath $(dirname ${0}))
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HCOM_UMQ_ROOT_DIR="${HCOM_ROOT_DIR}/src/hcom/umq"
readonly HCOM_UMQ_TEST_DIR="${HCOM_ROOT_DIR}/test/hcom/umq_test"
readonly TMP_UMDK_DIR="${HCOM_ROOT_DIR}/build/tmp_umdk_dir_for_update_umq"

# prepare tmp umdk dir
if [ -d "${TMP_UMDK_DIR}" ]; then
	rm -rf ${TMP_UMDK_DIR}
fi
mkdir -p ${TMP_UMDK_DIR}

# prepare umdk code
cd ${TMP_UMDK_DIR}
git clone https://atomgit.com/openeuler/umdk.git
cd umdk
git checkout br_openEuler_24.03_LTS_SP3
git checkout f89ad5d7e5406d558b3a516916e6823315be3d76

# update umq
cp ${TMP_UMDK_DIR}/umdk/src/urpc/config/umq* "${HCOM_UMQ_ROOT_DIR}/config"
echo "${HCOM_LOG_TAG} update umq config dir success."

cp ${TMP_UMDK_DIR}/umdk/src/urpc/examples/umq/umq* "${HCOM_UMQ_ROOT_DIR}/examples/umq"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/examples/umq/connection_setup_tool/*.c "${HCOM_UMQ_ROOT_DIR}/examples/umq/connection_setup_tool"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/examples/umq/connection_setup_tool/*.h "${HCOM_UMQ_ROOT_DIR}/examples/umq/connection_setup_tool"
echo "${HCOM_LOG_TAG} update umq examples dir success."

cp -r ${TMP_UMDK_DIR}/umdk/src/urpc/include/umq/* "${HCOM_UMQ_ROOT_DIR}/include/umq"
cp -r ${TMP_UMDK_DIR}/umdk/src/urpc/include/framework/* "${HCOM_UMQ_ROOT_DIR}/include/framework"
echo "${HCOM_LOG_TAG} update umq include dir success."

cp ${TMP_UMDK_DIR}/umdk/src/urpc/tools/perftest/perftest* "${HCOM_UMQ_ROOT_DIR}/tools/perftest"
cp ${TMP_UMDK_DIR}/umdk/src/urma/common/include/ub_get_clock.h "${HCOM_UMQ_ROOT_DIR}/tools/perftest"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/tools/perftest/umq/umq* "${HCOM_UMQ_ROOT_DIR}/tools/perftest/umq"
echo "${HCOM_LOG_TAG} update umq perftest dir success."

cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/*.c "${HCOM_UMQ_ROOT_DIR}/src"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/*.h "${HCOM_UMQ_ROOT_DIR}/src"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/dfx/*.c "${HCOM_UMQ_ROOT_DIR}/src/dfx"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/dfx/*.h "${HCOM_UMQ_ROOT_DIR}/src/dfx"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/qbuf/*.c "${HCOM_UMQ_ROOT_DIR}/src/qbuf"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/qbuf/*.h "${HCOM_UMQ_ROOT_DIR}/src/qbuf"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ipc/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ipc"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ipc/*.h "${HCOM_UMQ_ROOT_DIR}/src/umq_ipc"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ub"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/*.h "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/flow_control/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core/flow_control"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/flow_control/*.h "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core/flow_control"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/private/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core/private"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ub/core/private/*.h "${HCOM_UMQ_ROOT_DIR}/src/umq_ub/core/private"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ubmm/*.c "${HCOM_UMQ_ROOT_DIR}/src/umq_ubmm"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/umq/umq_ubmm/*.h "${HCOM_UMQ_ROOT_DIR}/src/umq_ubmm"
echo "${HCOM_LOG_TAG} update umq src dir success."

cp ${TMP_UMDK_DIR}/umdk/src/urpc/util/*.c "${HCOM_UMQ_ROOT_DIR}/util"
cp ${TMP_UMDK_DIR}/umdk/src/urpc/util/*.h "${HCOM_UMQ_ROOT_DIR}/util"
echo "${HCOM_LOG_TAG} update umq util dir success."

cp ${TMP_UMDK_DIR}/umdk/test/urpc/util/test* "${HCOM_UMQ_TEST_DIR}/util"
echo "${HCOM_LOG_TAG} [SUCCESS] update umq from umdk success."

# clean
rm -rf ${TMP_UMDK_DIR}
