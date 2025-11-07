#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for upload line coverage report
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly HCOM_LOG_TAG="[$(basename ${0})]"
CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})

echo ${CURRENT_SCRIPT_DIR}
echo ${HCOM_ROOT_DIR}

cd ${HCOM_ROOT_DIR}/build/gcover_report
cp ../gtest_report.xml ./test_detail.xml
cp ../lcov_report_filterd.info ./coverage.info

zip -r lcov.zip *
artget pull "ock_3rdparty ock3rdparty1.0" -ru software -user p_OckCI \
    -pwd encryption:ETMsDgAAAYgIefwyABFBRVMvR0NNL05vUGFkZGluZwCAABAAEBKGslaG2E1RnzCAiRGoekcAAAAqIwJz1WwrhJUvE4ohzMKYYtHPTBeTa7LlILcfVZJoOuQOYEmRgSMNt85UABQBhk4+/kX90aleLjjXzrA/G5tcGw== \
    -rp "hdfsutil.jar" -ap "./"
java -jar hdfsutil.jar -prod -upload lcov.zip ${upload_path}/lcov.zip
