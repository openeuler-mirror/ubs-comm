ARG BASE_IMAGE=hub.oepkgs.net/openeuler/openeuler:24.03-lts-sp3
FROM ${BASE_IMAGE}

ARG REPO_DIR=/workspace

# 安装构建依赖（openEuler 工具链 + ubs-comm 构建/UT 依赖）
# 镜像仅提供预装依赖的环境，不打包源码；源码通过挂载 /workspace 提供
# 注意：gtest/gmock 不在此处安装（RPM 版路径与 HCOM CMake 不匹配）。
# HCOM UT 由 build.sh 自动执行 build/install_test_tools.sh 从源码编译安装；
# UBSocket UT 通过 FetchContent 在 cmake 配置时自动拉取。
RUN dnf install -y \
        cmake gcc gcc-c++ make git \
        rdma-core-devel openssl-devel libboundscheck time \
        python3 python3-pip \
    && dnf clean all

WORKDIR ${REPO_DIR}
CMD ["/bin/bash"]
