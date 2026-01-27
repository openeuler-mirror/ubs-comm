if(NOT DEFINED UMQ_LIB OR UMQ_LIB STREQUAL "")
  set(UMQ_LIB "/usr/lib64/libumq.so")
endif()

if(NOT DEFINED UMQ_INCLUDE OR UMQ_INCLUDE STREQUAL "")
  set(UMQ_INCLUDE "/home/zhangbohao/brmerge/devcomm/ubs-comm/src/hcom/umq/include/umq")
endif()

if(UBSOCKET_BUILD_TESTS)
  if(NOT DEFINED UMQ_BUF_LIB OR UMQ_BUF_LIB STREQUAL "")
    set(UMQ_BUF_LIB "/usr/lib64/libumq_buf.so")
    message(STATUS "UMQ_BUF_LIB is not set, Use the default one: ${UMQ_BUF_LIB}. Since we are building unit test, all symbols should be resolved after link phase")
  endif()
endif()

# Check
find_path(UMQ_PRO_API_H NAMES umq_pro_api.h HINTS ${UMQ_INCLUDE} REQUIRED)

message(STATUS "UMQ_INCLUDE: ${UMQ_INCLUDE}")
message(STATUS "UMQ_LIB: ${UMQ_LIB}")

find_package(OpenSSL REQUIRED)

add_library(umq INTERFACE)
target_include_directories(umq INTERFACE ${UMQ_INCLUDE})
target_link_libraries(umq
  INTERFACE
    ${UMQ_LIB}
    ${UMQ_BUF_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
)
