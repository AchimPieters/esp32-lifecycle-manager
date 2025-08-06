cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-lifecycle-manager)

idf_build_get_property(idf_version IDF_VERSION)

# Controleer of de versie-informatie aanwezig is
if(NOT idf_version OR idf_version VERSION_LESS "5.4.0")
    message(FATAL_ERROR
        "ESP-IDF v5.4.0 or later is required. Detected version: '${idf_version}'"
    )
endif()
