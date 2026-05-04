cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main)

add_custom_command(TARGET app POST_BUILD
    COMMAND bash ${CMAKE_SOURCE_DIR}/generate_sig.sh
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating signature for ${PROJECT_NAME}.bin"
)
