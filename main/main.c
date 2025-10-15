/**
   Copyright 2025 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   for more information visit https://www.studiopieters.nl
 **/

#include <stdio.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <wifi_config.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <inttypes.h>
#include <nvs.h>
#include "github_update.h"
#include "led_indicator.h"

static const char *TAG = "main";

static const char *RESTART_COUNTER_NAMESPACE = "lcm";
static const char *RESTART_COUNTER_KEY = "restart_count";
static const uint32_t RESTART_COUNTER_THRESHOLD_MIN = 10U;
static const uint32_t RESTART_COUNTER_THRESHOLD_MAX = 12U;

#ifndef CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS
#define CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS 0
#endif

static const uint32_t RESTART_COUNTER_RESET_TIMEOUT_MS =
        (uint32_t)CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS;

static esp_timer_handle_t restart_counter_timer = NULL;
static uint32_t restart_counter_value = 0U;

static void sntp_start_and_wait(void);
void wifi_ready(void);

static int led_gpio = CONFIG_ESP_LED_GPIO;
static bool led_enabled = true;
static bool led_on = false;
static bool led_active_high = false;
static TaskHandle_t led_task = NULL;
static bool led_blinking = false;
static void led_indicator_apply(bool enabled, int gpio, bool active_high);

#define LED_BLINK_ON_MS 500
#define LED_BLINK_OFF_MS 500

void led_write(bool on) {
    if (led_gpio < 0) return;
    ESP_LOGD(TAG, "Setting LED %s", on ? "ON" : "OFF");
    led_on = on;
    int level = (on == led_active_high) ? 1 : 0;
    gpio_set_level(led_gpio, level);
}

static void led_blink_task(void *pv) {
    ESP_LOGD(TAG, "LED blinking task starting on GPIO %d", led_gpio);

    led_write(false);
    while (led_blinking) {
        led_write(true);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_ON_MS));
        if (!led_blinking) break;
        led_write(false);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_OFF_MS));
    }

    led_write(false);
    led_task = NULL;
    ESP_LOGD(TAG, "LED blinking task exiting");
    vTaskDelete(NULL);
}

void led_blinking_start() {
    if (led_gpio < 0 || !led_enabled || led_blinking) return;
    led_blinking = true;
    BaseType_t res = xTaskCreate(led_blink_task, "led_blink", 2048, NULL, 5, &led_task);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED blinking task");
        led_task = NULL;
        led_blinking = false;
    }
}

void led_blinking_stop() {
    if (!led_blinking) return;
    led_blinking = false;
    if (led_task) {
        for (int i = 0; i < 10 && led_task; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (led_task) {
            vTaskDelete(led_task);
            led_task = NULL;
        }
    }
    led_write(false);
}

void gpio_init() {
    ESP_LOGD(TAG, "Initializing GPIO");
    // LED setup
    if (led_gpio >= 0 && led_enabled) {
        gpio_reset_pin(led_gpio);
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        led_write(led_on);
        ESP_LOGD(TAG, "LED GPIO configured on pin %d", led_gpio);
    } else if (led_gpio >= 0) {
        gpio_reset_pin(led_gpio);
        ESP_LOGD(TAG, "LED indicator disabled on GPIO %d", led_gpio);
    } else {
        ESP_LOGD(TAG, "LED indicator disabled");
    }
}

static void led_indicator_apply(bool enabled, int gpio, bool active_high) {
    bool resume_blinking = led_blinking;
    int previous_gpio = led_gpio;

    if (led_blinking) {
        led_blinking_stop();
    } else {
        led_write(false);
    }

    if (previous_gpio >= 0 && previous_gpio != gpio) {
        gpio_reset_pin(previous_gpio);
    }

    if (gpio > 32) {
        ESP_LOGW(TAG, "Requested LED GPIO %d out of range; disabling indicator", gpio);
        gpio = -1;
    }

    led_gpio = gpio;
    led_active_high = active_high;
    led_enabled = enabled && gpio >= 0;
    if (!led_enabled) {
        led_on = false;
    }

    gpio_init();

    if (led_gpio >= 0 && led_enabled) {
        led_write(false);
    }

    if (resume_blinking && led_enabled) {
        led_blinking_start();
    }
}

void led_indicator_reload(void) {
    bool enabled = true;
    int gpio = CONFIG_ESP_LED_GPIO;
    bool active_high = false;

    if (!load_led_config(&enabled, &gpio, &active_high)) {
        enabled = CONFIG_ESP_LED_GPIO >= 0;
        gpio = CONFIG_ESP_LED_GPIO;
        active_high = false;
    }

    led_indicator_apply(enabled, gpio, active_high);
}

static bool factory_reset_requested = false;

static esp_err_t restart_counter_store(uint32_t value) {
    restart_counter_value = value;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(RESTART_COUNTER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open restart counter namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, RESTART_COUNTER_KEY, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist restart counter: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

static uint32_t restart_counter_load(void) {
    nvs_handle_t handle;
    uint32_t value = 0;

    esp_err_t err = nvs_open(RESTART_COUNTER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to open restart counter namespace: %s", esp_err_to_name(err));
        }
        restart_counter_value = 0;
        return 0;
    }

    err = nvs_get_u32(handle, RESTART_COUNTER_KEY, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        err = ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read restart counter: %s", esp_err_to_name(err));
        value = 0;
    }

    nvs_close(handle);
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
    if (RESTART_COUNTER_RESET_TIMEOUT_MS == 0U) {
        ESP_LOGD(TAG,
                "Restart counter auto-reset timeout disabled; retaining power-cycle count until manual reset");
        return;
    }

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

    esp_err_t start_err = esp_timer_start_once(restart_counter_timer,
            (uint64_t)RESTART_COUNTER_RESET_TIMEOUT_MS * 1000ULL);
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start restart counter timer: %s", esp_err_to_name(start_err));
    } else {
        ESP_LOGD(TAG, "Restart counter timeout armed for %" PRIu32 " ms", RESTART_COUNTER_RESET_TIMEOUT_MS);
    }
}

static void lifecycle_factory_reset_and_reboot(void);

static bool handle_power_cycle_sequence(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_POWERON && reason != ESP_RST_EXT) {
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

    uint32_t persisted_count = count;
    if (count > RESTART_COUNTER_THRESHOLD_MAX) {
        ESP_LOGW(TAG,
                "Detected %" PRIu32 " consecutive power cycles; exceeding maximum window %" PRIu32 "; capping at %" PRIu32 " for factory reset",
                count, RESTART_COUNTER_THRESHOLD_MAX, RESTART_COUNTER_THRESHOLD_MAX);
        persisted_count = RESTART_COUNTER_THRESHOLD_MAX;
    }

    restart_counter_store(persisted_count);

    if (persisted_count >= RESTART_COUNTER_THRESHOLD_MIN) {
        ESP_LOGW(TAG,
                "Detected %" PRIu32 " consecutive power cycles within factory reset window (%" PRIu32 "-%" PRIu32 "); triggering factory reset",
                persisted_count, RESTART_COUNTER_THRESHOLD_MIN, RESTART_COUNTER_THRESHOLD_MAX);
        lifecycle_factory_reset_and_reboot();
        return true;
    }

    restart_counter_schedule_reset();
    return false;
}

static void clear_nvs_storage(void) {
    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGW("RESET", "nvs_flash_deinit failed: %s", esp_err_to_name(err));
    }

    err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE("RESET", "nvs_flash_erase failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("RESET", "NVS flash erased");
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW("RESET", "nvs_flash_init after erase failed: %s", esp_err_to_name(err));
    }
}

static void erase_otadata_partition(void) {
    const esp_partition_t *otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata == NULL) {
        ESP_LOGW("RESET", "OTA data partition not found");
        return;
    }

    ESP_LOGI("RESET", "Erasing OTA data partition '%s' (offset=0x%08x, size=%" PRIu32 ")",
            otadata->label, (unsigned int)otadata->address, (uint32_t)otadata->size);
    esp_err_t err = esp_partition_erase_range(otadata, 0, otadata->size);
    if (err != ESP_OK) {
        ESP_LOGE("RESET", "Failed to erase OTA data partition: %s", esp_err_to_name(err));
    }
}

static void erase_ota_app_partitions(void) {
    ESP_LOGI("RESET", "Erasing OTA firmware partitions");

    esp_partition_iterator_t it = esp_partition_find(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        esp_partition_iterator_t next = esp_partition_next(it);

        if (part->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                part->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            ESP_LOGI("RESET",
                    "Erasing partition '%s' (subtype=%d) at offset=0x%08x size=%" PRIu32 ")",
                    part->label, part->subtype, (unsigned int)part->address, (uint32_t)part->size);
            esp_err_t err = esp_partition_erase_range(part, 0, part->size);
            if (err != ESP_OK) {
                ESP_LOGE("RESET", "Failed to erase partition '%s': %s",
                        part->label, esp_err_to_name(err));
            }
        }

        esp_partition_iterator_release(it);
        it = next;
    }
}

// Task factory_reset
void factory_reset_task(void *pvParameter) {
    ESP_LOGI("RESET", "Performing factory reset (clearing WiFi and NVS)");

    esp_err_t wifi_err = esp_wifi_restore();
    if (wifi_err != ESP_OK) {
        ESP_LOGW("RESET", "esp_wifi_restore failed: %s", esp_err_to_name(wifi_err));
    } else {
        ESP_LOGI("RESET", "WiFi configuration restored to defaults");
    }

    clear_nvs_storage();
    erase_otadata_partition();
    erase_ota_app_partitions();

    ESP_LOGD("RESET", "Waiting before reboot");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("RESTART", "Restarting system");
    esp_restart();

    factory_reset_requested = false;
    ESP_LOGD("RESET", "factory_reset_task completed");
    vTaskDelete(NULL);
}

void factory_reset() {
    if (factory_reset_requested) {
        ESP_LOGW("RESET", "Factory reset already in progress");
        return;
    }

    ESP_LOGI("RESET", "Resetting device configuration");
    if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE("RESET", "Failed to create factory_reset task");
    } else {
        factory_reset_requested = true;
    }
}

static void lifecycle_factory_reset_and_reboot(void) {
    ESP_LOGW(TAG, "Triggering lifecycle factory reset and reboot");
    factory_reset();
}

void app_main(void) {
    ESP_LOGI(TAG, "Application start");
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }

    restart_counter_load();
    if (handle_power_cycle_sequence()) {
        return;
    }
    led_indicator_reload();
    wifi_config_init("LCM", NULL, wifi_ready);
}

static void sntp_start_and_wait(void){
    ESP_LOGD(TAG, "Starting SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    time_t now=0; struct tm tm={0};
    for (int i=0; i<20 && tm.tm_year < (2016-1900); ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now); localtime_r(&now, &tm);
        ESP_LOGD(TAG, "SNTP attempt %d, year=%d", i, tm.tm_year+1900);
    }
    ESP_LOGD(TAG, "SNTP sync completed");
}

void wifi_ready(void)
{
    ESP_LOGI("app", "WiFi ready; starting OTA check");
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("github_update", ESP_LOG_DEBUG);
    esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_CLIENT",   ESP_LOG_DEBUG);

    ESP_LOGI("app", "Starting SNTP synchronization");
    sntp_start_and_wait();
    ESP_LOGI("app", "SNTP synchronization complete");

    char repo[96]={0};
    bool pre=false;
    if (!load_fw_config(repo, sizeof(repo), &pre)) {
        ESP_LOGW("app", "Geen firmware-config in NVS; configureer via web UI.");
        led_blinking_stop();
        return;
    }
    ESP_LOGI("app", "Firmware config loaded: repo=%s pre=%d", repo, pre);
    led_blinking_start();
    ESP_LOGI("app", "Checking for firmware update");
    github_update_if_needed(repo, pre);
    led_blinking_stop();
    ESP_LOGI("app", "Firmware update check complete");
}
