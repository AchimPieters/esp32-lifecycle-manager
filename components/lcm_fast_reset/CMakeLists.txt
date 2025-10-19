idf_component_register(
    SRCS "lcm_fast_reset.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash app_update esp_timer esp_system
)
