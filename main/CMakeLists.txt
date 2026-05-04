set(JSON_COMPONENT json)
set(DRIVER_COMPONENT driver)
if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(JSON_COMPONENT cjson)
    set(DRIVER_COMPONENT esp_driver_gpio)
endif()

idf_component_register(
    SRCS
        "main.c"
        "wifi_config.c"
        "wifi_config_util.c"
        "form_urlencoded.c"
        "github_update.c"
        "lifecycle_reset.c"
        "lifecycle_restart_counter.c"
        "nvs_store.c"
    INCLUDE_DIRS
        "."
        "include"
        "content"
    REQUIRES
        freertos
        esp_wifi
        nvs_flash
        ${DRIVER_COMPONENT}
        esp_event
        esp_netif
        esp_timer
        http_parser
        esp_http_client
        esp_https_ota
        app_update
        mbedtls
        ${JSON_COMPONENT}
    PRIV_REQUIRES
        spi_flash
    EMBED_TXTFILES
        "content/index.html"
)
