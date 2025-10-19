idf_component_register(
    SRCS "src/esp32-lcm.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp32-homekit
        esp_timer
        app_update
        esp_partition
        esp_app_format
        esp_hw_support
        mdns
    PRIV_REQUIRES esp_system
)

# Forceer link van het anker-symbool zodat de init-hook altijd meekomt
target_link_options(${COMPONENT_LIB} PUBLIC -u lcm_reset_component_anchor_symbol)
