cmake_minimum_required(VERSION 3.5)

set(DEFAULT_FW_VERSION "0.0.2")
set(PROJECT_VER "${DEFAULT_FW_VERSION}")

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    ERROR_QUIET
    RESULT_VARIABLE GIT_HASH_RESULT
)

if(GIT_HASH_RESULT EQUAL 0)
    string(STRIP "${GIT_COMMIT_HASH}" GIT_COMMIT_HASH_STRIPPED)
    if(GIT_COMMIT_HASH_STRIPPED)
        set(PROJECT_VER "${DEFAULT_FW_VERSION}+${GIT_COMMIT_HASH_STRIPPED}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-lifecycle-manager)
