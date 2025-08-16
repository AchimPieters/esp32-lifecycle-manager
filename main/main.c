#include <inttypes.h>
#include "wifi_config.h"
#include "lcm32.h"
#include "drd.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "APP";

static void on_wifi_ready() {
    ESP_LOGI(TAG, "Wi-Fi ready, running OTA check at boot...");
    lcm32_check_and_update();
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    lcm32_init();

    uint32_t drd = lcm32_drd_get_count();
    if (drd >= 8) {
        ESP_LOGW(TAG, "Erasing Wi-Fi credentials (DRD count=%" PRIu32 ")", drd);
        wifi_config_reset();
        ESP_LOGW(TAG, "Wi-Fi creds erased; restarting in 4s to avoid DRD window");
        vTaskDelay(pdMS_TO_TICKS(4000));
        esp_restart();
    }

    // inject OTA fields into captive UI
    wifi_config_set_custom_html(
        "<h2>Over-the-Air Update (GitHub)</h2>"
        "<div class='field'>"
        "<label for='url'>GitHub OTA Repository (Owner/Repo or URL)</label>"
        "<input type='password' id='url' name='url' placeholder='Owner/Repo' />"
        "</div>"
        "<div class='field'>"
        "<label for='token'>GitHub Token (optional)</label>"
        "<input type='password' id='token' name='token' placeholder='ghp_xxx' />"
        "</div>"
        "<div class='field'>"
        "<label><input type='checkbox' id='pre' name='pre' value='1' /> Allow prereleases</label>"
        "</div>"
        "<div class='buttons'>"
        "<button type='submit' name='update' value='1'>Save & Check Update</button>"
        "</div>"
    );

    wifi_config_init("Homekit", NULL, on_wifi_ready);

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
