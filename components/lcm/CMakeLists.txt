idf_component_register(
    SRCS "src/esp32-lcm.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_timer app_update esp_partition
    PRIV_REQUIRES esp_system
)

# Forceer link van het anker-symbool zodat de init-hook altijd meekomt
target_link_options(${COMPONENT_LIB} PUBLIC -u lcm_reset_component_anchor_symbol)
