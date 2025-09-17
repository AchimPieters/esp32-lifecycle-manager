#include "nvs_utils.h"

#include <esp_log.h>
#include <nvs_flash.h>

static const char *TAG = "nvs_utils";

esp_err_t nvs_init_with_recovery(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed (%s); erasing and retrying", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed after recovery attempt: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "NVS initialized successfully");
    }

    return err;
}
