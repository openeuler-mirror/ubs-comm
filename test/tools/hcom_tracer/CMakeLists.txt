CMAKE_MINIMUM_REQUIRED(VERSION 3.12.1)
PROJECT(htracer)

SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_BUILD_TYPE "Release")
add_compile_options(
        -Wall
        -fstack-protector-strong
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_link_options(
        -pie
        -Wl,-z,relro,-z,now,-z,noexecstack
        -s
)

set(HCOM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../dist/hcom/include")
set(HCOM_LIB_DIR "${CMAKE_SOURCE_DIR}/../../../dist/hcom/lib")
set(SECUREC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../dist/hcom_3rdparty/huawei_secure_c/include")
set(SECUREC_LIB_DIR "${CMAKE_SOURCE_DIR}/../../../dist/hcom_3rdparty/huawei_secure_c/lib")

include_directories("${HCOM_INCLUDE_DIR}"
                    "${SECUREC_INCLUDE_DIR}")
link_directories("${HCOM_LIB_DIR}"
                 "${SECUREC_LIB_DIR}")

set(CMAKE_SKIP_BUILD_RPATH TRUE)

FILE(GLOB_RECURSE HTRACER_CLI_SRC
        "${CMAKE_SOURCE_DIR}/src/*")
ADD_EXECUTABLE(htracer_cli ${HTRACER_CLI_SRC})
TARGET_LINK_LIBRARIES(htracer_cli PUBLIC pthread boundscheck)
TARGET_INCLUDE_DIRECTORIES(htracer_cli PUBLIC . message common rpc)
install(TARGETS htracer_cli DESTINATION ${TARGET_INSTALL_BIN}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
