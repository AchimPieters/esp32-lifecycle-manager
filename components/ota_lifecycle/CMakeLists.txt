idf_component_register(
    SRCS "lifecycle_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        esp_system
        nvs_flash
        freertos
)
