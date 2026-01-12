#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for generate line coverage report
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly HCOM_LOG_TAG="[$(basename ${0})]"
CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})

echo ${CURRENT_SCRIPT_DIR}
echo ${HCOM_ROOT_DIR}

cd ${HCOM_ROOT_DIR}/tmp_build_dir
# get the result of code coverage
lcov --rc lcov_branch_coverage=1 --rc lcov_excl_br_line="LCOV_EXCL_BR_LINE|NN_LOG*" \
    -b ../src/hcom/  -d ./test/hcom/llt/CMakeFiles/Hcomtest.dir/__/__/__/ -c -o lcov_report_llt.info
lcov --rc lcov_branch_coverage=1 --rc lcov_excl_br_line="LCOV_EXCL_BR_LINE|NN_LOG*" \
    -b ../src/hcom/  -d ./test/hcom/unit_test/CMakeFiles/hcom_ut.dir/__/__/__/ -c -o lcov_report_ut.info
lcov --rc lcov_branch_coverage=1 -a lcov_report_llt.info -a lcov_report_ut.info -o lcov_report_all.info

# filter the result, remove useless info
# hcom_c.cpp will significantly lower overall line coverage and code coverage, we'll deal with it later.
lcov --rc lcov_branch_coverage=1 --rc lcov_excl_br_line="LCOV_EXCL_BR_LINE|NN_LOG*" -r lcov_report_all.info \
    '*/googletest/*' '*/mockcpp/*' '/usr/include' '*/gcc/*' '*/c++/*' \
    '*/test/*' '*/rdma-core/*' '*/dist/*' \
    '*/src/api/capi/hcom_c.cpp' '*/src/api/capi/hcom_service_c.cpp' \
    '*/src/service/service_net_driver_manager.*' \
    '*/src/under_api/*' \
    -o lcov_report_filterd.info

# visualize the result
genhtml --branch-coverage -o gcover_report lcov_report_filterd.info
