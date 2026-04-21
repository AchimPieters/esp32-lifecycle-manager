/**
   Copyright 2026 Achim Pieters | StudioPieters®

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
#include <esp_wifi.h>
#include <esp_flash.h>
#include <soc/soc_caps.h>
#include <inttypes.h>
#include "github_update.h"
#include "led_indicator.h"
#include "lifecycle_manager.h"
#include "lifecycle_restart_counter.h"

#ifndef CONFIG_ESP_LED_GPIO
#define CONFIG_ESP_LED_GPIO 2
#endif

#ifndef CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS
#define CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS 10000
#endif

#ifndef CONFIG_LCM_REQUIRE_NVS_ENCRYPTION
#define CONFIG_LCM_REQUIRE_NVS_ENCRYPTION 0
#endif

static const char *TAG = "main";

#ifdef SOC_GPIO_PIN_COUNT
#define LCM_MAX_GPIO_PIN ((int)SOC_GPIO_PIN_COUNT - 1)
#else
#define LCM_MAX_GPIO_PIN 32
#endif

static const uint32_t RESTART_COUNTER_THRESHOLD_MIN = 10U;
static const uint32_t RESTART_COUNTER_THRESHOLD_MAX = 12U;
static const uint32_t RESTART_COUNTER_RESET_TIMEOUT_MS = CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS;

static const uint32_t MIN_FLASH_SIZE_BYTES = 4U * 1024U * 1024U;

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
    esp_err_t err = gpio_set_level(led_gpio, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio_set_level(%d) failed: %s", led_gpio, esp_err_to_name(err));
    }
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
        esp_err_t err = gpio_reset_pin(led_gpio);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_reset_pin(%d) failed: %s", led_gpio, esp_err_to_name(err));
        }
        err = gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_set_direction(%d) failed: %s", led_gpio, esp_err_to_name(err));
        }
        led_write(led_on);
        ESP_LOGD(TAG, "LED GPIO configured on pin %d", led_gpio);
    } else if (led_gpio >= 0) {
        esp_err_t err = gpio_reset_pin(led_gpio);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_reset_pin(%d) failed: %s", led_gpio, esp_err_to_name(err));
        }
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

    if (gpio > LCM_MAX_GPIO_PIN) {
        ESP_LOGW(TAG, "Requested LED GPIO %d out of range (max=%d); disabling indicator",
                 gpio, LCM_MAX_GPIO_PIN);
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

static void lifecycle_factory_reset_and_reboot(void);

static esp_err_t lifecycle_validate_hardware_requirements(void) {
    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(NULL, &flash_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read flash size: %s", esp_err_to_name(err));
        return err;
    }

    if (flash_size < MIN_FLASH_SIZE_BYTES) {
        ESP_LOGE(TAG, "Unsupported flash size: %lu bytes (minimum required: %lu bytes)",
                 (unsigned long)flash_size, (unsigned long)MIN_FLASH_SIZE_BYTES);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Flash size check passed: %lu bytes", (unsigned long)flash_size);
    return ESP_OK;
}

static esp_err_t lifecycle_validate_security_requirements(void) {
#if CONFIG_LCM_REQUIRE_NVS_ENCRYPTION
#if !CONFIG_NVS_ENCRYPTION
    ESP_LOGE(TAG, "Security requirement failed: CONFIG_NVS_ENCRYPTION is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_LOGI(TAG, "Security requirement passed: NVS encryption is enabled");
    return ESP_OK;
#endif
#else
    ESP_LOGW(TAG, "NVS encryption requirement is disabled by configuration");
    return ESP_OK;
#endif
}

// Task factory_reset
void factory_reset_task(void *pvParameter) {
    (void)pvParameter;
    esp_err_t err = lifecycle_factory_reset_execute();
    if (err != ESP_OK) {
        ESP_LOGE("RESET", "Factory reset failed: %s", esp_err_to_name(err));
        factory_reset_requested = false;
        vTaskDelete(NULL);
    }
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
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init requires recovery (%s), erasing NVS partition",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    err = lifecycle_validate_hardware_requirements();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware requirements not met; aborting startup");
        return;
    }
    err = lifecycle_validate_security_requirements();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Security requirements not met; aborting startup");
        return;
    }

    ESP_LOGI(TAG, "Powercycle threshold window: %" PRIu32 "-%" PRIu32 ", timeout=%" PRIu32 "ms",
             RESTART_COUNTER_THRESHOLD_MIN, RESTART_COUNTER_THRESHOLD_MAX, RESTART_COUNTER_RESET_TIMEOUT_MS);
    if (lifecycle_restart_counter_process(RESTART_COUNTER_THRESHOLD_MIN,
                                          RESTART_COUNTER_THRESHOLD_MAX,
                                          RESTART_COUNTER_RESET_TIMEOUT_MS,
                                          lifecycle_factory_reset_and_reboot)) {
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
