idf_component_register(
    SRCS "main.c" "esp32-wifi.c"
    REQUIRES freertos esp_wifi nvs_flash driver esp32-homekit esp_timer app_update spi_flash esp_system
)
