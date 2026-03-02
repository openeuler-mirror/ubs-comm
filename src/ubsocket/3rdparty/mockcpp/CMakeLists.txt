# Use mockcpp in target_link_libraries

include(FetchContent)

set(ARM_HUGEPAGE_SUPPORT_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/mockcpp_support_arm64.patch)
set(BUILD_AS_SUBPROJ_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/mockcpp_subproj_build.patch)

FetchContent_Declare(
  sj_mockcpp
  GIT_REPOSITORY https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/mockcpp.git
  GIT_TAG        v2.7
  GIT_SHALLOW    true
  GIT_PROGRESS   true
  PATCH_COMMAND  git -c user.name='Jenkins' -c user.email='jenkins@local' am --ignore-whitespace -3 ${ARM_HUGEPAGE_SUPPORT_PATCH} ${BUILD_AS_SUBPROJ_PATCH}
)
FetchContent_MakeAvailable(sj_mockcpp)
