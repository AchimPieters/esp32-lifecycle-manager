#include <inttypes.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lifecycle_manager.h"

static const char *TAG = "lifecycle_reset";

static esp_err_t clear_nvs_storage(void) {
    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "nvs_flash_deinit failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init after erase failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "NVS flash erased and reinitialized");
    return ESP_OK;
}

static esp_err_t erase_otadata_partition(void) {
    const esp_partition_t *otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata == NULL) {
        ESP_LOGE(TAG, "OTA data partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Erasing OTA data partition '%s'", otadata->label);
    return esp_partition_erase_range(otadata, 0, otadata->size);
}

static esp_err_t erase_ota_app_partitions(void) {
    esp_partition_iterator_t it = esp_partition_find(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    esp_err_t overall = ESP_OK;

    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        esp_partition_iterator_t next = esp_partition_next(it);

        if (part->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                part->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            ESP_LOGI(TAG, "Erasing OTA app partition '%s'", part->label);
            esp_err_t err = esp_partition_erase_range(part, 0, part->size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to erase partition '%s': %s", part->label, esp_err_to_name(err));
                overall = err;
            }
        }

        esp_partition_iterator_release(it);
        it = next;
    }

    return overall;
}

esp_err_t lifecycle_factory_reset_execute(void) {
    ESP_LOGW(TAG, "Factory reset started");

    esp_err_t err = esp_wifi_restore();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_restore failed: %s", esp_err_to_name(err));
        return err;
    }

    err = clear_nvs_storage();
    if (err != ESP_OK) {
        return err;
    }

    err = erase_otadata_partition();
    if (err != ESP_OK) {
        return err;
    }

    err = erase_ota_app_partitions();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGW(TAG, "Factory reset completed; rebooting");
    esp_restart();
    return ESP_OK;
}
