cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-lifecycle-manager)

idf_build_get_property(idf_version IDF_VERSION)
if(idf_version VERSION_LESS "5.4.0")
    message(FATAL_ERROR "ESP-IDF v5.4 or later is required.")
endif()
