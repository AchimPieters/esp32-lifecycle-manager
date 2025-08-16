idf_component_register(
    SRCS
        
        "src/wifi_config.c"
        "src/safe_auto_connect.c"
        "src/ota_stub.c"
    INCLUDE_DIRS
        "include" "content" "src"
    PRIV_REQUIRES
        esp_http_server nvs_flash esp_event esp_wifi esp_netif esp_timer http_parser lcm32

    
    )
