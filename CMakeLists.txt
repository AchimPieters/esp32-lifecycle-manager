cmake_minimum_required(VERSION 3.5)

set(DEFAULT_FW_VERSION "0.0.0")
set(PROJECT_BASE_VERSION "${DEFAULT_FW_VERSION}")

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    ERROR_QUIET
    RESULT_VARIABLE GIT_TAG_RESULT
)

if(GIT_TAG_RESULT EQUAL 0)
    string(STRIP "${GIT_TAG}" GIT_TAG_STRIPPED)
    if(GIT_TAG_STRIPPED)
        set(_semver_match "")
        string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+$" _semver_match "${GIT_TAG_STRIPPED}")
        if(_semver_match)
            set(PROJECT_BASE_VERSION "${GIT_TAG_STRIPPED}")
        else()
            message(WARNING "Ignoring non-semver git tag '${GIT_TAG_STRIPPED}' for PROJECT_VER")
        endif()
    endif()
endif()

set(PROJECT_VER "${PROJECT_BASE_VERSION}")

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
        set(PROJECT_VER "${PROJECT_BASE_VERSION}+${GIT_COMMIT_HASH_STRIPPED}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-lifecycle-manager)
