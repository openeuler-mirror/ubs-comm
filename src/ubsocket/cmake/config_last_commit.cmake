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

find_program(UBS_GIT_EXECUTABLE NAMES git)

if (EXISTS ${UBS_GIT_EXECUTABLE})
    execute_process(
            COMMAND ${UBS_GIT_EXECUTABLE} rev-parse HEAD
            RESULT_VARIABLE UBS_GIT_COMMIT_RESULT
            OUTPUT_VARIABLE UBS_GIT_COMMIT_ID
            OUTPUT_STRIP_TRAILING_WHITESPACE)

    if (UBS_GIT_COMMIT_RESULT EQUAL 0)
        add_compile_definitions(UBS_GIT_LAST_COMMIT=${UBS_GIT_COMMIT_ID})
        message(STATUS "ubsocket: set UBS_GIT_LAST_COMMIT to ${UBS_GIT_COMMIT_ID} as compile definition")
    else ()
        message(STATUS "ubsocket: failed to git last commit with git")
    endif ()

else ()
    message(STATUS "ubsocket: failed to find git command, not UBS_GIT_LAST_COMMIT will be set")
endif ()
