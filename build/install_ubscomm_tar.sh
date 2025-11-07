#!/bin/bash
HCOM_PACKAGE_DIR=/opt/install/package/
HCOM_PACKAGE_NAME=ubs_comm-hcom-1.1*
HCOM_INCLUDE_DIR=/usr/include/hcom
HCOM_LIB_DIR=/usr/lib64/

# 获取匹配的文件列表
files=$(ls /opt/install/package/ubs_comm-hcom-1.1*.tar.gz 2>/dev/null)

if [ -z "$files" ]; then
    echo -e "\e[31m===  UbsComm安装失败, 没有找到对应的安装包  ===\e[0m"
    exit 1
else
    # 只取第一个匹配的文件
    first_file=$(echo "$files" | head -n 1)
fi

[[ -n "${HCOM_INCLUDE_DIR}" ]] && rm -rf "${HCOM_INCLUDE_DIR}"
[[ -n "${HCOM_PACKAGE_DIR}/${HCOM_PACKAGE_NAME}" ]] && rm -rf "${HCOM_PACKAGE_DIR}/${HCOM_PACKAGE_NAME}"

if [ -f "${HCOM_LIB_DIR}libhcom.so" ]; then
    rm -f "${HCOM_LIB_DIR}libhcom.so"
fi

if [ -f "${HCOM_LIB_DIR}libhcom_static.a" ]; then
    rm -f "${HCOM_LIB_DIR}libhcom_static.a"
fi

tar -zxvf "$first_file" -C ${HCOM_PACKAGE_DIR}|| {
    echo -e "\e[31m===  UbsComm安装失败, 解压失败  ===\e[0m"
    exit 1
}
cp ${HCOM_PACKAGE_DIR}${HCOM_PACKAGE_NAME}/hcom/lib/libhcom.so ${HCOM_LIB_DIR}|| {
    echo -e "\e[31m===  UbsComm安装失败, 没有对应的动态库文件  ===\e[0m"
    exit 1
}
cp ${HCOM_PACKAGE_DIR}${HCOM_PACKAGE_NAME}/hcom/lib/libhcom_static.a ${HCOM_LIB_DIR}|| {
    echo -e "\e[31m===  UbsComm安装失败, 没有对应的静态库文件  ===\e[0m"
    exit 1
}
cp -r ${HCOM_PACKAGE_DIR}${HCOM_PACKAGE_NAME}/hcom/include/hcom ${HCOM_INCLUDE_DIR}|| {
    echo -e "\e[31m===  UbsComm安装失败, 没有对应的头文件  ===\e[0m"
    exit 1
}
