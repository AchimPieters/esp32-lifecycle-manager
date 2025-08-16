idf_component_register(
  SRCS "lcm32.c" "ota_sig.c" "portal.c" "drd.c"
  INCLUDE_DIRS "include"
  REQUIRES app_update esp_http_client esp_timer nvs_flash esp_netif esp_wifi esp-tls mbedtls json esp_http_server
)
