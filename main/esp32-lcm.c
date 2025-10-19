#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_timer.h>
#include <esp_log.h>

#define LCM_NVS_NS   "lcm"
#define LCM_KEY_CNT  "hw_rst_cnt"
#define LCM_KEY_T0   "hw_rst_t0"

#ifndef CONFIG_LCM_HW_RESET_WINDOW_MS
#define CONFIG_LCM_HW_RESET_WINDOW_MS 12000
#endif
#ifndef CONFIG_LCM_HW_RESET_TARGET
#define CONFIG_LCM_HW_RESET_TARGET 10
#endif

ESP_SYSTEM_INIT_FN(lcm_reset_sentry, SECONDARY, ESP_SYSTEM_INIT_ALL_CORES, 100)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nvs_handle_t nvh;
    if (nvs_open(LCM_NVS_NS, NVS_READWRITE, &nvh) != ESP_OK) {
        return ESP_OK;
    }

    uint32_t cnt = 0;
    (void)nvs_get_u32(nvh, LCM_KEY_CNT, &cnt);
    uint64_t t0 = 0;
    (void)nvs_get_u64(nvh, LCM_KEY_T0, &t0);

    const uint64_t now = esp_timer_get_time() / 1000ULL;
    const uint32_t window_ms = CONFIG_LCM_HW_RESET_WINDOW_MS;

    if (t0 == 0 || (now - t0) > window_ms) {
        cnt = 0;
        t0 = now;
    }

    cnt++;
    nvs_set_u32(nvh, LCM_KEY_CNT, cnt);
    nvs_set_u64(nvh, LCM_KEY_T0, t0);
    nvs_commit(nvh);
    nvs_close(nvh);

    if (cnt < CONFIG_LCM_HW_RESET_TARGET) {
        return ESP_OK;
    }

    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory) {
        esp_ota_set_boot_partition(factory);
    }

    const esp_partition_t *otadata =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

    if (nvs_open(LCM_NVS_NS, NVS_READWRITE, &nvh) == ESP_OK) {
        nvs_erase_key(nvh, LCM_KEY_CNT);
        nvs_erase_key(nvh, LCM_KEY_T0);
        nvs_commit(nvh);
        nvs_close(nvh);
    }

    esp_restart();
    return ESP_OK;
}
