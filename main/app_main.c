#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_err.h>

#include "wifi_config.h"
#include "github_update.h"

static const char *TAG = "app";

static void wifi_ready(void) {
    ESP_LOGI(TAG, "WiFi connected, checking for updates");
    esp_err_t err = github_update_if_needed("AchimPieters/esp32-lifecycle-manager", false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing WiFi configuration");
    wifi_config_init("ESP32", NULL, wifi_ready);

    // Keep main task alive
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
