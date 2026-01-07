# GTest and GTest::gtest_main

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git
  GIT_TAG        release-1.12.1
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(googletest)
