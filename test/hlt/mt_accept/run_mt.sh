#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for run mt
# version: 1.0.0
# change log:
# ***********************************************************************
#设置client和server总数量
num=$1

# 获取当前目录
CURRENT_PATH=$(cd $(dirname ${0}) && pwd)

# 获取当前目录的父目录
parent_dir=$(dirname "$(pwd)")

# 获取父目录的父目录
grandparent_dir=$(dirname "$parent_dir")

# 获取父目录的父目录的父目录
greatgrandparent_dir=$(dirname "$grandparent_dir")

echo "mpi building ... "
export LD_LIBRARY_PATH=${greatgrandparent_dir}/build/src:$LD_LIBRARY_PATH
mpicc -o3 -Wall -I${greatgrandparent_dir}/src -L${greatgrandparent_dir}/build/src -lhcom -lstdc++ -o test ./mt_accept_test.cpp

#mpirun -n 1001 -x LD_LIBRARY_PATH -x HCOM_SET_LOG_LEVEL=3 -x HCOM_CONNECTION_RETRY_TIME=1 -host 96.10.130.125:128,96.10.130.126:128,96.10.130.127:128,96.10.130.128:128,96.10.130.129:128,96.10.130.130:128,96.10.130.131:128,96.10.130.132:105 ./test -d 1 -i 10.10.3.126 -p 9982 -s 1024 -w 1 -c -1
echo "test running ... "
mpirun -n ${num} -x LD_LIBRARY_PATH -x HCOM_SET_LOG_LEVEL=3 -x HCOM_CONNECTION_RETRY_TIME=1 -hostfile ${CURRENT_PATH}/crossfile ./test -d 1 -i 10.10.3.126 -p 9982 -s 1024 -w 1 -c -1

