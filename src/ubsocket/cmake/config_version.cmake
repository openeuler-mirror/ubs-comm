#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# ubs-comm is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#

# read version content from file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/UBSOCKET_VERSION" UBS_VERSION_CONTENT)

# verify version format which should be x.x.x
# i.e. {major_version}.{minor_version}.{fix}
# all of them should be a digital
string(STRIP "${UBS_VERSION_CONTENT}" UBS_VERSION_RAW)
if (NOT UBS_VERSION_RAW MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "ubsocket: invalid version format in VERSION file: '${UBS_VERSION_RAW}'")
endif ()

# split it version string into single field
list(GET UBS_VERSION_RAW 0 DUMMY)
string(REPLACE "." ";" UBS_VERSION_LIST "${UBS_VERSION_RAW}")
list(LENGTH UBS_VERSION_LIST UBS_VERSION_LIST_LEN)
if (NOT UBS_VERSION_LIST_LEN EQUAL 3)
    message(FATAL_ERROR "ubsocket: expected exactly 3 version components, got: ${UBS_VERSION_LIST_LEN}")
endif ()
list(GET UBS_VERSION_LIST 0 UBS_VERSION_MAJOR)
list(GET UBS_VERSION_LIST 1 UBS_VERSION_MINOR)
list(GET UBS_VERSION_LIST 2 UBS_VERSION_FIX)

# add MACRO with single field
add_compile_definitions(UBS_VERSION_MAJOR=${UBS_VERSION_MAJOR}
        UBS_VERSION_MINOR=${UBS_VERSION_MINOR}
        UBS_VERSION_FIX=${UBS_VERSION_FIX})

# print
message(STATUS "ubsocket: UBS_VERSION_MAJOR=${UBS_VERSION_MAJOR} UBS_VERSION_MINOR=${UBS_VERSION_MINOR} UBS_VERSION_FIX=${UBS_VERSION_FIX}")