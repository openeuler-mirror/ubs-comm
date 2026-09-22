set(LIBBOUNDSCHECK_INSTALL_DIR "${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck")
message(STATUS "LIBBOUNDSCHECK_INSTALL_DIR: ${LIBBOUNDSCHECK_INSTALL_DIR}")
# -----------------------------
# 查找 libboundscheck 头文件和库
# -----------------------------
find_path(LIBBOUNDSCHECK_INCLUDE_DIR
        NAMES securec.h
        PATHS /usr/include
        NO_DEFAULT_PATH
)

find_library(LIBBOUNDSCHECK_SHARED_LIBRARY
        NAMES libboundscheck.so boundscheck
        PATHS /usr/lib64 /usr/lib
        NO_DEFAULT_PATH
)

# -----------------------------
# 验证是否找到
# -----------------------------
if(NOT LIBBOUNDSCHECK_INCLUDE_DIR OR NOT LIBBOUNDSCHECK_SHARED_LIBRARY)
        message(STATUS
                "libboundscheck not found! Suggest install the RPM package:\n"
                "  sudo rpm -ivh libboundscheck-v1.1.11-6.oe2403.aarch64.rpm\n"
                "Or via yum/dnf:\n"
                "  sudo yum install libboundscheck\n"
                "Expected:\n"
                "  Header: /usr/include/securec.h\n"
                "  Shared: /usr/lib64/libboundscheck.so"
        )
        find_path(LIBBOUNDSCHECK_INCLUDE_DIR
                NAMES securec.h
                PATHS ${LIBBOUNDSCHECK_INSTALL_DIR}/include/
                NO_DEFAULT_PATH
        )

        find_library(LIBBOUNDSCHECK_SHARED_LIBRARY
                NAMES libboundscheck.so boundscheck
                PATHS ${LIBBOUNDSCHECK_INSTALL_DIR}/lib/
                NO_DEFAULT_PATH
        )
        if(NOT LIBBOUNDSCHECK_INCLUDE_DIR OR NOT LIBBOUNDSCHECK_SHARED_LIBRARY)
                message(STATUS "=========install from soucecode=========")
                
                include(FetchContent)

                # 假设 umdk-urma 仓库包含 urma 的头文件
                FetchContent_Declare(
                        libboundscheck
                        GIT_REPOSITORY https://atomgit.com/openeuler/libboundscheck.git
                        GIT_TAG        v1.1.16
                )
                FetchContent_MakeAvailable(libboundscheck)
                # 获取 hcom 源码路径
                FetchContent_GetProperties(libboundscheck)
                set(SECUREC_SRC_DIR ${libboundscheck_SOURCE_DIR}/src/hcom)
                execute_process(
                        COMMAND make CC=${CMAKE_C_COMPILER}
                        WORKING_DIRECTORY ${libboundscheck_SOURCE_DIR}
                        RESULT_VARIABLE MAKE_RESULT
                )
                EXEC_PROGRAM(mkdir ${HCOM_BUILD_PATH} ARGS -p ${LIBBOUNDSCHECK_INSTALL_DIR})
                EXEC_PROGRAM(cp ${LIBBOUNDSCHECK_INSTALL_DIR} ARGS -rf ${libboundscheck_SOURCE_DIR}/lib/ ${LIBBOUNDSCHECK_INSTALL_DIR})
                EXEC_PROGRAM(cp ${LIBBOUNDSCHECK_INSTALL_DIR} ARGS -rf ${libboundscheck_SOURCE_DIR}/include/ ${LIBBOUNDSCHECK_INSTALL_DIR})

                set(LIBBOUNDSCHECK_INCLUDE_DIR ${LIBBOUNDSCHECK_INSTALL_DIR}/include)
                set(LIBBOUNDSCHECK_SHARED_LIBRARY ${LIBBOUNDSCHECK_INSTALL_DIR}/lib/libboundscheck.so)
        else()
                message("-- libboundscheck: ${LIBBOUNDSCHECK_SHARED_LIBRARY}")
                message("-- libboundscheck: has been built, ignored")
        endif()
endif()

message(STATUS "libboundscheck headers: ${LIBBOUNDSCHECK_INCLUDE_DIR}")
message(STATUS "libboundscheck shared: ${LIBBOUNDSCHECK_SHARED_LIBRARY}")

# -----------------------------
# 链接 INTERFACE 库
# -----------------------------
target_include_directories(hcom PRIVATE ${LIBBOUNDSCHECK_INCLUDE_DIR})
target_include_directories(hcom_static_obj PRIVATE ${LIBBOUNDSCHECK_INCLUDE_DIR})

# 链接共享库
target_link_libraries(hcom ${LIBBOUNDSCHECK_SHARED_LIBRARY})
target_link_libraries(hcom_static_obj ${LIBBOUNDSCHECK_SHARED_LIBRARY})