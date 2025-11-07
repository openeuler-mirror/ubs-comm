#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for run gtest and generate gtest report
# version: 1.0.0
# change log:
# ***********************************************************************
set -e

readonly HCOM_LOG_TAG="[$(basename ${0})]"
readonly CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
readonly HCOM_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly HCOM_BUILD_DIR="${HCOM_ROOT_DIR}/tmp_build_dir"
readonly HCOM_GTEST_RESULT="${HCOM_ROOT_DIR}/tmp_build_dir/gtest_report.xml"
readonly HCOM_GTEST_TEMP_DIR="${HCOM_ROOT_DIR}/tmp_build_dir/res_xml"

cd ${HCOM_BUILD_DIR}

./hcom_ut --gtest_output=xml:./res_xml/ut_result.xml
./hcom_test --gtest_output=xml:./res_xml/llt_result.xml

# ****************************************
# combine gtest report
# ****************************************
echo '<?xml version="1.0" encoding="UTF-8"?>' > ${HCOM_GTEST_RESULT}
tests_val=$(cat res_xml/* |grep "<testsuites "|awk -F "tests=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
failures_val=$(cat res_xml/* |grep "<testsuites "|awk -F "failures=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
disabled_val=$(cat res_xml/* |grep "<testsuites "|awk -F "disabled=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
errors_val=$(cat res_xml/* |grep "<testsuites "|awk -F "errors=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
time_val=$(cat res_xml/* |grep "<testsuites "|awk -F "time=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}' | awk '{sum+=$1} END {print sum}')
timestamp_val=$(cat res_xml/* |grep "<testsuites "| head -n 1|awk -F "timestamp=" '{print $2}'|awk '{print $1}'|awk -F "\"" '{print $2}')
echo "<testsuites tests=\"${tests_val}\" failures=\"${failures_val}\" disabled=\"${disabled_val}\" errors=\"${errors_val}\" time=\"${time_val}\" timestamp=\"${timestamp_val}\" name=\"AllTests\">" >> ${HCOM_GTEST_RESULT}
cat res_xml/* | grep -v testsuites |grep -v "xml version" >> ${HCOM_GTEST_RESULT}
echo '</testsuites>' >> ${HCOM_GTEST_RESULT}
