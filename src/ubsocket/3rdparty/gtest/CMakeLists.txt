# GTest and GTest::gtest_main
# Supports offline build via FETCHCONTENT_SOURCE_DIR_GOOGLETEST cmake variable

include(FetchContent)
if(DEFINED FETCHCONTENT_SOURCE_DIR_GOOGLETEST)
  FetchContent_Declare(googletest SOURCE_DIR ${FETCHCONTENT_SOURCE_DIR_GOOGLETEST})
else()
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git
    GIT_TAG        release-1.12.1
    GIT_SHALLOW    true
    GIT_PROGRESS   true
  )
endif()
FetchContent_MakeAvailable(googletest)
