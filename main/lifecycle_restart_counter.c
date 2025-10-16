#include "lifecycle_restart_counter.h"

#include <inttypes.h>
#include <string.h>

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_private/startup_internal.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "lifecycle_restart";
static const char *k_restart_counter_namespace = "lcm";
static const char *k_restart_counter_key = "restart_count";

static esp_timer_handle_t s_restart_counter_timer = NULL;
static uint32_t s_restart_counter_value = 0;
static bool s_bootstrap_invoked = false;
static esp_err_t s_bootstrap_status = ESP_FAIL;

static esp_err_t load_restart_counter_from_nvs(uint32_t *out_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_restart_counter_namespace, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_value = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_EARLY_LOGW(TAG, "Failed to open restart counter namespace: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t value = 0;
    err = nvs_get_u32(handle, k_restart_counter_key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_EARLY_LOGW(TAG, "Failed to read restart counter: %s", esp_err_to_name(err));
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        *out_value = value;
    }
    return err;
}

static esp_err_t save_restart_counter_to_nvs(uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_restart_counter_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open restart counter namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, k_restart_counter_key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist restart counter: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

static void restart_counter_timeout(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Restart counter timeout expired; clearing counter");
    lifecycle_restart_counter_reset();
}

static esp_err_t ensure_restart_counter_timer(void)
{
    if (s_restart_counter_timer != NULL) {
        return ESP_OK;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = restart_counter_timeout,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "rst_cnt",
    };

    return esp_timer_create(&timer_args, &s_restart_counter_timer);
}

static esp_err_t lifecycle_restart_counter_bootstrap(void)
{
    s_bootstrap_invoked = true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_EARLY_LOGW(TAG, "NVS init requires erase (err=%s)", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK) {
        ESP_EARLY_LOGE(TAG, "Failed to initialize NVS for restart counter: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t stored_count = 0;
    esp_err_t load_err = load_restart_counter_from_nvs(&stored_count);
    if (load_err != ESP_OK) {
        stored_count = 0;
    }

    esp_reset_reason_t reason = esp_reset_reason();
    bool is_power_cycle = (reason == ESP_RST_POWERON) || (reason == ESP_RST_EXT);

    const esp_partition_t *running = esp_ota_get_running_partition();
    bool running_is_factory = (running != NULL) &&
            (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);

    if (!is_power_cycle) {
        if (stored_count != 0) {
            save_restart_counter_to_nvs(0);
        }
        s_restart_counter_value = 0;
        return ESP_OK;
    }

    if (stored_count == UINT32_MAX) {
        stored_count = 0;
    }

    uint32_t new_count = stored_count + 1U;
    if (new_count > LIFECYCLE_RESTART_COUNTER_THRESHOLD_MAX) {
        new_count = LIFECYCLE_RESTART_COUNTER_THRESHOLD_MAX;
    }

    s_restart_counter_value = new_count;

    esp_err_t save_err = save_restart_counter_to_nvs(new_count);
    if (save_err != ESP_OK) {
        ESP_EARLY_LOGW(TAG, "Failed to persist restart counter during bootstrap: %s", esp_err_to_name(save_err));
    }

    if (new_count >= LIFECYCLE_RESTART_COUNTER_THRESHOLD_MIN && !running_is_factory) {
        const esp_partition_t *factory = esp_partition_find_first(
                ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory == NULL) {
            ESP_EARLY_LOGE(TAG, "Factory partition not found; cannot trigger factory reset");
            return ESP_OK;
        }

        esp_err_t set_err = esp_ota_set_boot_partition(factory);
        if (set_err != ESP_OK) {
            ESP_EARLY_LOGE(TAG, "Failed to select factory partition for reset: %s", esp_err_to_name(set_err));
            return ESP_OK;
        }

        ESP_EARLY_LOGW(TAG,
                        "Detected %" PRIu32 " power cycles while OTA app running; rebooting into factory",
                        new_count);
        esp_restart();
    }

    return ESP_OK;
}

ESP_SYSTEM_INIT_FN(lifecycle_restart_counter_bootstrap_fn, CORE,
                   ESP_SYSTEM_INIT_ALL_CORES, 200)
{
    s_bootstrap_status = lifecycle_restart_counter_bootstrap();
    return s_bootstrap_status;
}

bool lifecycle_restart_counter_is_available(void)
{
    return s_bootstrap_invoked && s_bootstrap_status == ESP_OK;
}

uint32_t lifecycle_restart_counter_get(void)
{
    if (!lifecycle_restart_counter_is_available()) {
        return 0;
    }
    return s_restart_counter_value;
}

void lifecycle_restart_counter_reset(void)
{
    if (!lifecycle_restart_counter_is_available()) {
        return;
    }

    if (s_restart_counter_timer != NULL) {
        esp_timer_stop(s_restart_counter_timer);
    }

    s_restart_counter_value = 0;
    if (save_restart_counter_to_nvs(0) == ESP_OK) {
        ESP_LOGI(TAG, "Restart counter reset");
    }
}

void lifecycle_restart_counter_schedule_reset(void)
{
    if (!lifecycle_restart_counter_is_available()) {
        return;
    }

    if (ensure_restart_counter_timer() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create restart counter timer");
        return;
    }

    esp_err_t stop_err = esp_timer_stop(s_restart_counter_timer);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop restart counter timer: %s", esp_err_to_name(stop_err));
    }

    esp_err_t start_err = esp_timer_start_once(
            s_restart_counter_timer,
            (uint64_t)LIFECYCLE_RESTART_COUNTER_RESET_TIMEOUT_MS * 1000ULL);
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start restart counter timer: %s", esp_err_to_name(start_err));
    } else {
        ESP_LOGD(TAG, "Restart counter timeout armed for %" PRIu32 " ms",
                 (uint32_t)LIFECYCLE_RESTART_COUNTER_RESET_TIMEOUT_MS);
    }
}
