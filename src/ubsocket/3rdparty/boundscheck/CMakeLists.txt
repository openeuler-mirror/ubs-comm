# Use boundscheck or openeuler::boundscheck in target_link_libraries.

# Use system libboundscheck if available.
find_path(BOUNDSCHECK_INCLUDE_DIR
  NAMES securec.h
  HINTS /usr/include/
  NO_DEFAULT_PATH
)
find_library(BOUNDSCHECK_LIBRARY
  NAMES boundscheck
  HINTS /usr/lib/ /usr/lib64/
  NO_DEFAULT_PATH
)

if(BOUNDSCHECK_INCLUDE_DIR AND BOUNDSCHECK_LIBRARY)
  message(STATUS "libboundscheck is found!")

  add_library(boundscheck_if INTERFACE)
  target_include_directories(boundscheck_if INTERFACE ${BOUNDSCHECK_INCLUDE_DIR})
  target_link_libraries(boundscheck_if INTERFACE ${BOUNDSCHECK_LIBRARY})

  # Visible to users.
  add_library(boundscheck ALIAS boundscheck_if)
  add_library(openeuler::boundscheck ALIAS boundscheck_if)
  return()
endif()


message(STATUS "libboundscheck is not found on system, starting to build from source.")

# The boundscheck project lacks the necessary CMake target exports for FetchContent.
# Providing a local CMake wrapper.
add_library(boundscheck_impl STATIC "")

# Visible to users.
add_library(boundscheck ALIAS boundscheck_impl)
add_library(openeuler::boundscheck ALIAS boundscheck_impl)

include(FetchContent)

FetchContent_Declare(
  openeuler_libboundscheck
  GIT_REPOSITORY https://atomgit.com/openeuler/libboundscheck.git
  GIT_TAG        v1.1.16
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(openeuler_libboundscheck)

target_include_directories(boundscheck_impl
  PUBLIC
    ${openeuler_libboundscheck_SOURCE_DIR}/include/
)

target_sources(boundscheck_impl
  PRIVATE
    # secure input/output
    ${openeuler_libboundscheck_SOURCE_DIR}/src/securecutil.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/secureinput_a.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/secureinput_w.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/secureprintoutput_a.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/secureprintoutput_w.c

    # securec
    ${openeuler_libboundscheck_SOURCE_DIR}/src/memcpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wmemcpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/memmove_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wmemmove_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/memset_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/strcpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wcscpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/strncpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wcsncpy_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/strcat_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wcscat_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/strncat_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wcsncat_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/strtok_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wcstok_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/sprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/swprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vsprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vswprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/snprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vsnprintf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/scanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/wscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vwscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/fscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/fwscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vfscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vfwscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/sscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/swscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vsscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/vswscanf_s.c
    ${openeuler_libboundscheck_SOURCE_DIR}/src/gets_s.c
)
