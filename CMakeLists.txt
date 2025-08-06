cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-lifecycle-manager)

# Probeer eerste detectie via CMake-afhankelijkheid
idf_build_get_property(idf_version IDF_VERSION)

# Fallback naar omgevingsvariabele als leeg
if(NOT idf_version)
    set(idf_version "$ENV{IDF_VERSION}")
endif()

# Verdere controle
if(NOT idf_version)
    message(WARNING "Kon ESP‑IDF‑versie niet automatisch detecteren. Ga door zonder versiecheck.")
else()
    if(idf_version VERSION_LESS "5.4.0")
        message(FATAL_ERROR
            "ESP‑IDF v5.4.0 of later is vereist. Gedetecteerd: '${idf_version}'"
        )
    endif()
endif()
