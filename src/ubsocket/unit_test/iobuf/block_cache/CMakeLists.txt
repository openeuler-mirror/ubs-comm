set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_compile_options(-fno-access-control)

set(BLOCK_CACHE_INCLUDE_DIRS
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/include
)

add_executable(block_cache_test "")
add_test(NAME block_cache_test COMMAND block_cache_test)
set_target_properties(block_cache_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(block_cache_test PRIVATE cxx_std_17)

target_compile_definitions(block_cache_test PRIVATE UBSOCKET_UNIT_TEST)

target_include_directories(block_cache_test PRIVATE ${BLOCK_CACHE_INCLUDE_DIRS})

target_link_libraries(block_cache_test PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(block_cache_test PRIVATE
    block_cache_test.cpp
)
