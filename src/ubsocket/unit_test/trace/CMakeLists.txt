add_executable(utrace_test "")
add_test(NAME utrace_test COMMAND utrace_test)
set_target_properties(utrace_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# shutup, mockcpp.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

# gtest requires c++17
target_compile_features(utrace_test
  PRIVATE
    cxx_std_17
)

# Ignore access control in class(like private or protected)
add_compile_options(-fno-access-control)

target_link_libraries(utrace_test
  PRIVATE
    GTest::gtest_main
    mockcpp
    ubsocket
)

target_sources(utrace_test
  PRIVATE
    utracer_test.cpp
    utracer_tdigest_test.cpp
)