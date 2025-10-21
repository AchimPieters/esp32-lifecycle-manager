#include "esp32_lifecycle_manager.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_reset_reason.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/soc_caps.h>

#ifndef CONFIG_LCM_LOG_TAG
#define CONFIG_LCM_LOG_TAG "LCM"
#endif

#ifndef CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT
#define CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT 10
#endif

#ifndef CONFIG_LCM_FACTORY_RESET_COUNTDOWN_SECONDS
#define CONFIG_LCM_FACTORY_RESET_COUNTDOWN_SECONDS 11
#endif

#ifndef CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS
#define CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_LCM_RESTART_COUNTER_NAMESPACE
#define CONFIG_LCM_RESTART_COUNTER_NAMESPACE "lcm"
#endif

#ifndef CONFIG_LCM_RESTART_COUNTER_KEY
#define CONFIG_LCM_RESTART_COUNTER_KEY "restart_count"
#endif

_Static_assert(CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT > 0,
               "CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT must be > 0");
_Static_assert(CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS > 0,
               "CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS must be > 0");

static const char *TAG = CONFIG_LCM_LOG_TAG;

static bool s_nvs_initialised = false;
static bool s_restart_counter_loaded = false;
static uint32_t s_restart_counter_value = 0;
static esp_timer_handle_t s_restart_counter_timer = NULL;
static lifecycle_factory_reset_callback_t s_factory_reset_cb = NULL;
static void *s_factory_reset_ctx = NULL;

static const char *reset_reason_to_string(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXTERNAL";
        case ESP_RST_SW:
            return "SOFTWARE";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
#ifdef ESP_RST_PWOFF
        case ESP_RST_PWOFF:
            return "PWOFF";
#endif
#if SOC_PMU_SUPPORTED
        case ESP_RST_PMU:
            return "PMU";
#endif
        default:
            return "UNKNOWN";
    }
}

static esp_err_t ensure_nvs_initialised(void) {
    if (s_nvs_initialised) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_nvs_initialised = true;
    } else {
        ESP_LOGE(TAG, "Failed to initialise NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t lifecycle_nvs_init(void) {
    return ensure_nvs_initialised();
}

static esp_err_t open_restart_counter_handle(nvs_handle_t *handle) {
    esp_err_t err = ensure_nvs_initialised();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_open(CONFIG_LCM_RESTART_COUNTER_NAMESPACE, NVS_READWRITE, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to open NVS namespace '%s': %s",
                 CONFIG_LCM_RESTART_COUNTER_NAMESPACE,
                 esp_err_to_name(err));
    }
    return err;
}

static esp_err_t load_restart_counter(uint32_t *out_value) {
    if (s_restart_counter_loaded) {
        *out_value = s_restart_counter_value;
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = open_restart_counter_handle(&handle);
    if (err != ESP_OK) {
        *out_value = 0;
        return err;
    }

    uint32_t value = 0;
    err = nvs_get_u32(handle, CONFIG_LCM_RESTART_COUNTER_KEY, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        err = ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to read restart counter: %s",
                 esp_err_to_name(err));
        value = 0;
    }

    nvs_close(handle);

    s_restart_counter_loaded = true;
    s_restart_counter_value = value;
    *out_value = value;
    return err;
}

static esp_err_t store_restart_counter(uint32_t value) {
    nvs_handle_t handle;
    esp_err_t err = open_restart_counter_handle(&handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, CONFIG_LCM_RESTART_COUNTER_KEY, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to persist restart counter: %s",
                 esp_err_to_name(err));
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        s_restart_counter_loaded = true;
        s_restart_counter_value = value;
    }

    return err;
}

static void cancel_restart_counter_timer(void) {
    if (s_restart_counter_timer == NULL) {
        return;
    }

    esp_err_t err = esp_timer_stop(s_restart_counter_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "Failed to stop restart counter timer: %s",
                 esp_err_to_name(err));
    }
}

void lifecycle_reset_restart_counter(void) {
    cancel_restart_counter_timer();
    if (store_restart_counter(0) == ESP_OK) {
        ESP_LOGD(TAG, "Restart counter cleared");
    }
}

uint32_t lifecycle_get_restart_counter(void) {
    uint32_t value = 0;
    load_restart_counter(&value);
    return value;
}

static void restart_counter_timeout(void *arg) {
    (void)arg;
    ESP_LOGD(TAG,
             "No rapid restart detected within %d ms; clearing counter",
             CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS);
    lifecycle_reset_restart_counter();
}

static void ensure_restart_counter_timer_created(void) {
    if (s_restart_counter_timer != NULL) {
        return;
    }

    const esp_timer_create_args_t args = {
        .callback = &restart_counter_timeout,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lcm_rst",
        .skip_unhandled_events = false,
    };

    esp_err_t err = esp_timer_create(&args, &s_restart_counter_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to create restart counter timer: %s",
                 esp_err_to_name(err));
    }
}

static bool reason_counts_towards_threshold(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
        case ESP_RST_EXT:
            return true;
        default:
            return false;
    }
}

static void schedule_restart_counter_timeout(void) {
    ensure_restart_counter_timer_created();
    if (s_restart_counter_timer == NULL) {
        return;
    }

    cancel_restart_counter_timer();

    const uint64_t timeout_us =
        (uint64_t)CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS * 1000ULL;

    esp_err_t err = esp_timer_start_once(s_restart_counter_timer, timeout_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to start restart counter timer: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG,
                 "Restart counter timeout armed for %d ms",
                 CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS);
    }
}

void lifecycle_register_factory_reset_callback(lifecycle_factory_reset_callback_t cb,
                                               void *ctx) {
    s_factory_reset_cb = cb;
    s_factory_reset_ctx = ctx;
}

static void maybe_execute_factory_reset(void) {
    if (s_factory_reset_cb == NULL) {
        ESP_LOGW(TAG,
                 "Rapid restart threshold reached but no factory reset callback registered");
        return;
    }

    ESP_LOGW(TAG, "Executing factory reset callback");
    s_factory_reset_cb(s_factory_reset_ctx);
}

static void countdown_before_factory_reset(void) {
    int seconds = CONFIG_LCM_FACTORY_RESET_COUNTDOWN_SECONDS;
    if (seconds <= 0) {
        return;
    }

    for (int remaining = seconds; remaining > 0; --remaining) {
        ESP_LOGW(TAG, "Factory reset in %d", remaining);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void lifecycle_log_post_reset_state(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_to_string(reason));

    uint32_t counter = 0;
    load_restart_counter(&counter);

    if (!reason_counts_towards_threshold(reason)) {
        if (counter != 0) {
            ESP_LOGI(TAG,
                     "Reset reason does not count towards rapid restart window; clearing counter");
            lifecycle_reset_restart_counter();
        }
        return;
    }

    if (counter == UINT32_MAX) {
        counter = 0;
    }
    counter++;

    ESP_LOGI(TAG, "Consecutive rapid restarts: %" PRIu32, counter);
    store_restart_counter(counter);

    if (counter >= CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT) {
        ESP_LOGW(TAG,
                 "Rapid restart threshold (%d) reached; starting factory reset countdown",
                 CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT);
        countdown_before_factory_reset();
        lifecycle_reset_restart_counter();
        maybe_execute_factory_reset();
    } else {
        schedule_restart_counter_timeout();
    }
}

