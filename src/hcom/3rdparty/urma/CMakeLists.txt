# -----------------------------
# 尝试查找系统已安装的 umdk-urma (RPM 模式)
# -----------------------------
find_path(URMA_INCLUDE_DIR
        NAMES urma_ubagg.h
        PATHS /usr/include/ub/umdk/urma/
        NO_DEFAULT_PATH
)

if(URMA_INCLUDE_DIR)
    message(STATUS "Found system-installed umdk-urma: headers=${URMA_INCLUDE_DIR}")
    set(USE_SYSTEM_URMA ON)
else()
    message(STATUS "umdk-urma not found in system. Fetching from source...")
    set(USE_SYSTEM_URMA OFF)
endif()

# -----------------------------
# 如果系统有，从系统路径拉取 umdk-urma 头文件; 如果系统没有，从源码拉取 umdk-urma 头文件
# -----------------------------
if(NOT USE_SYSTEM_URMA)
    include(FetchContent)

    # 假设 umdk-urma 仓库包含 urma 的头文件
    FetchContent_Declare(
            umdk
            GIT_REPOSITORY https://gitee.com/openeuler/umdk.git
            GIT_TAG        master
    )
    FetchContent_MakeAvailable(umdk)
    set(URMA_INCLUDE_DIR
        ${umdk_SOURCE_DIR}/src/urma/lib/urma/bond/include/ 
        ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/include/)
    execute_process(COMMAND ls -al ${umdk_SOURCE_DIR})
    foreach(dir ${URMA_INCLUDE_DIR})
        message(STATUS "URMA include directory: ${dir}")
    endforeach()
endif()
target_include_directories(hcom PRIVATE ${URMA_INCLUDE_DIR})
target_include_directories(hcom_static_obj PRIVATE ${URMA_INCLUDE_DIR})