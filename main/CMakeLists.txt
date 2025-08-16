idf_component_register(SRCS "main.c"
                       REQUIRES esp32-wifi-bootstrap lcm32 nvs_flash esp_netif esp_wifi esp_event)
