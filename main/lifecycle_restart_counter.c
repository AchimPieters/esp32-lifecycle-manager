#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lifecycle_restart_counter.h"
#include "nvs_keys.h"
#include "nvs_store.h"

static const char *TAG = "restart_counter";

static esp_timer_handle_t restart_counter_timer = NULL;
static uint32_t restart_counter_value = 0U;
static uint32_t counter_timeout_ms = 0U;

static esp_err_t restart_counter_store(uint32_t value) {
    restart_counter_value = value;
    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_rw(NVS_NS_LCM, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_store_set_u32(handle, NVS_KEY_RESTART_COUNT, value);
    if (err == ESP_OK) {
        err = nvs_store_commit_and_close(handle);
    } else {
        nvs_store_close(handle);
    }
    return err;
}

static uint32_t restart_counter_load(void) {
    nvs_handle_t handle;
    uint32_t value = 0;

    esp_err_t err = nvs_store_open_rw(NVS_NS_LCM, &handle);
    if (err != ESP_OK) {
        restart_counter_value = 0;
        return 0;
    }

    err = nvs_store_get_u32(handle, NVS_KEY_RESTART_COUNT, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        err = ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read restart counter: %s", esp_err_to_name(err));
        value = 0;
    }

    nvs_store_close(handle);
    restart_counter_value = value;
    return value;
}

static void restart_counter_reset(void) {
    if (restart_counter_timer != NULL) {
        esp_err_t stop_err = esp_timer_stop(restart_counter_timer);
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop restart counter timer: %s", esp_err_to_name(stop_err));
        }
    }

    restart_counter_value = 0;
    if (restart_counter_store(0) == ESP_OK) {
        ESP_LOGI(TAG, "Restart counter reset");
    }
}

static void restart_counter_timeout(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Restart counter timeout expired; clearing counter");
    restart_counter_reset();
}

static void restart_counter_schedule_reset(void) {
    if (restart_counter_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = restart_counter_timeout,
            .arg = NULL,
            .name = "rst_cnt",
        };

        esp_err_t create_err = esp_timer_create(&args, &restart_counter_timer);
        if (create_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create restart counter timer: %s", esp_err_to_name(create_err));
            return;
        }
    }

    esp_err_t stop_err = esp_timer_stop(restart_counter_timer);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop restart counter timer: %s", esp_err_to_name(stop_err));
    }

    esp_err_t start_err = esp_timer_start_once(restart_counter_timer, (uint64_t)counter_timeout_ms * 1000ULL);
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start restart counter timer: %s", esp_err_to_name(start_err));
    } else {
        ESP_LOGD(TAG, "Restart counter timeout armed for %" PRIu32 " ms", counter_timeout_ms);
    }
}

bool lifecycle_restart_counter_process(uint32_t threshold_min,
                                       uint32_t threshold_max,
                                       uint32_t reset_timeout_ms,
                                       void (*on_factory_reset)(void)) {
    counter_timeout_ms = reset_timeout_ms;
    restart_counter_load();

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d, current powercycle counter: %" PRIu32, reason, restart_counter_value);
    if (reason != ESP_RST_POWERON && reason != ESP_RST_EXT && reason != ESP_RST_SW) {
        if (restart_counter_value != 0) {
            ESP_LOGI(TAG, "Reset reason %d detected; clearing restart counter", reason);
            restart_counter_reset();
        }
        return false;
    }

    uint32_t count = restart_counter_value;
    if (count >= UINT32_MAX) {
        count = 0;
    }
    count++;

    ESP_LOGI(TAG, "Consecutive power cycles: %" PRIu32, count);
    restart_counter_store(count);

    if (count > threshold_max) {
        ESP_LOGW(TAG,
                 "Detected %" PRIu32 " consecutive power cycles; exceeding maximum window %" PRIu32 ", resetting counter",
                 count, threshold_max);
        restart_counter_reset();
        restart_counter_schedule_reset();
        return false;
    }

    if (count >= threshold_min) {
        ESP_LOGW(TAG,
                 "Detected %" PRIu32 " consecutive power cycles within factory reset window (%" PRIu32 "-%" PRIu32 "); starting countdown",
                 count, threshold_min, threshold_max);

        for (int i = (int)threshold_min; i >= 0; --i) {
            ESP_LOGW(TAG, "Factory reset in %d", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        restart_counter_reset();
        ESP_LOGW(TAG, "Factory reset threshold reached=%" PRIu32, count);
        if (on_factory_reset) {
            on_factory_reset();
        }
        return true;
    }

    restart_counter_schedule_reset();
    return false;
}
