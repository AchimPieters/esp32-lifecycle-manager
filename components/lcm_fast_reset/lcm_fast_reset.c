#include "lcm_fast_reset.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"

#define TAG "lcm_fast_reset"

#define LCM_RST_NS_DEFAULT   "lcm_rst"
#define LCM_RST_KEY_COUNT    "count"
#define LCM_RST_KEY_ARMED    "armed" // 0/1: tijdens stabilisatieperiode niet wissen bij crash

typedef struct {
    const char* ns;
    int threshold;
    int64_t stable_ms;
} lcm_rst_params_t;

static lcm_rst_params_t P = {
    .ns        = LCM_RST_NS_DEFAULT,
    .threshold = 10,
    .stable_ms = 8000
};

static void factory_reset_now(void)
{
    ESP_EARLY_LOGW(TAG, "=== HARDWARE FACTORY RESET TRIGGERED ===");

    // 1) wis NVS (en phy_init indien gewenst via bootloader-config)
    //    We wissen hier iig NVS direct; overige partities laat je door bootloader-config
    //    doen na reboot (CONFIG_BOOTLOADER_DATA_FACTORY_RESET).
    nvs_flash_erase(); // volledige NVS partitie wissen

    // 2) Forceer boot naar factory: door OTA-data te resetten
    const esp_partition_t* factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                              ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                              NULL);
    if (factory) {
        esp_ota_set_boot_partition(factory);
    }

    // 3) (Optioneel) rollback annuleren indien in progress
    esp_ota_mark_app_valid_cancel_rollback();

    // 4) Reboot
    esp_restart();
}

static void clear_counter_timer_cb(void* arg)
{
    // Als we deze timer halen, is het systeem "stabiel lang genoeg" aan gebleven.
    nvs_handle_t h;
    if (nvs_open(P.ns, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, LCM_RST_KEY_COUNT, 0);
        nvs_set_i32(h, LCM_RST_KEY_ARMED, 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Stable window passed (%lld ms). Counter cleared.", (long long)P.stable_ms);
}

static void lcm_fast_reset_internal_init(void)
{
    // Zorg dat NVS init is
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Reparatie indien NVS layout gewijzigd is
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    // Teller ophalen en ophogen
    nvs_handle_t h;
    int32_t count = 0;
    int32_t armed = 0;

    ESP_ERROR_CHECK(nvs_open(P.ns, NVS_READWRITE, &h));
    if (nvs_get_i32(h, LCM_RST_KEY_COUNT, &count) != ESP_OK) {
        count = 0;
    }
    if (nvs_get_i32(h, LCM_RST_KEY_ARMED, &armed) != ESP_OK) {
        armed = 0;
    }

    // Als vorige boot "stabiel" was, zou armed = 0 moeten zijn (counter mag gereset zijn).
    // We verhogen altijd aan het begin van de boot.
    count += 1;
    ESP_ERROR_CHECK(nvs_set_i32(h, LCM_RST_KEY_COUNT, count));

    // We "armen" de cyclus: als we de stabilisatie-timer niet halen (crash/power-off),
    // blijft armed==1 en de counter niet gewist.
    ESP_ERROR_CHECK(nvs_set_i32(h, LCM_RST_KEY_ARMED, 1));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);

    ESP_LOGI(TAG, "Fast-reset counter incremented: %d", (int)count);

    if (count >= P.threshold) {
        factory_reset_now();
        return; // komt niet terug
    }

    // Start stabilisatie-timer: bij halen ⇒ teller wissen
    const esp_timer_create_args_t targs = {
        .callback = &clear_counter_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lcm_rst_stable"
    };
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &t));
    ESP_ERROR_CHECK(esp_timer_start_once(t, P.stable_ms * 1000)); // microseconds

    // NB: Als het device < stable_ms draait en reset/power-off krijgt, wordt clear-counter
    //     niet uitgevoerd. Bij volgende boot blijft counter staan en wordt weer +1 gedaan.
}

void lcm_fast_reset_init(void)
{
    lcm_fast_reset_internal_init();
}

void lcm_fast_reset_init_with_cfg(const lcm_fast_reset_cfg_t* cfg)
{
    if (cfg) {
        if (cfg->nvs_namespace) {
            P.ns = cfg->nvs_namespace;
        }
        if (cfg->threshold > 0) {
            P.threshold = cfg->threshold;
        }
        if (cfg->stable_ms > 0) {
            P.stable_ms = cfg->stable_ms;
        }
    }
    lcm_fast_reset_internal_init();
}
