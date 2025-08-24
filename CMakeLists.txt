cmake_minimum_required(VERSION 3.16)

# Include the component build system from ESP-IDF so that the
# idf_component_register macro is available when building this
# repository as a standalone component.
include($ENV{IDF_PATH}/tools/cmake/component.cmake)

idf_component_register(
    SRCS "src/wifi_config.c" "src/form_urlencoded.c" "src/wifi_config_util.c" "src/github_update.c"
    INCLUDE_DIRS "include" "content"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer http_parser esp_https_ota esp_http_client mbedtls json
)
