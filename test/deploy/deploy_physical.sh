#!/bin/bash
CUR_PATH=$(cd $(dirname $0);pwd)
# 判断当前用户是否已经申请仿真环境，如果没有申请，执行部署脚本直接报错
if [ ! -e "$HOST_CFG" ]; then
    echo "Error：File $HOST_CFG is not exist, please apply for a test environment first."
    exit 1
fi

# 获取申请的环境节点的IP和端口，如果环境为多节点，业务需要自行适配
NODES=$(cat ${HOST_CFG} | awk '{print $2, $3}')

for NODE in ${NODES}; do
    NODE_IP=$(echo ${NODE} | awk '{print $1}')
    NODE_PORT=$(echo ${NODE} | awk '{print $2}')

    # 删除目标节点的旧安装包，并上传新安装包
    ssh root@${NODE_IP} -p ${NODE_PORT} rm -rf /opt/install/package/OCK-CommunicationSuite*
    scp -P ${NODE_PORT} ${WORKSPACE}/BeiMing_output/OCK-CommunicationSuite_2.0.0_aarch64.tar.gz root@${NODE_IP}:/opt/install/package/

    # 在目标节点上解压和安装
    ssh root@${NODE_IP} -p ${NODE_PORT} << EOF
    cd /opt/install/package/;
    tar -zxvf OCK-CommunicationSuite_2.0.0_aarch64.tar.gz;
    rpm -ivh --nodeps OCK-CommunicationSuite_HCOM*.rpm --force;
EOF
done