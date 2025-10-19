idf_component_register(
    SRCS "main.c" "esp32-lcm.c"
    REQUIRES freertos esp_wifi esp_event esp_netif nvs_flash driver esp32-homekit esp_timer app_update spi_flash esp_system espressif__mdns
)
