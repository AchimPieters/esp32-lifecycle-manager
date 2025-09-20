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
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include "github_update.h"
#include "led_indicator.h"

#if defined(__has_include)
#if __has_include(<homekit/homekit.h>)
#include <homekit/homekit.h>
#define LCM_HAVE_HOMEKIT 1
#else
#define LCM_HAVE_HOMEKIT 0
#endif
#else
#define LCM_HAVE_HOMEKIT 0
#endif

// GPIO-definities
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define DEBOUNCE_TIME_MS 50
#define RESET_HOLD_MS 3000
#define MULTI_CLICK_TIMEOUT_MS 400

static const char *TAG = "main";

static void sntp_start_and_wait(void);
void wifi_ready(void);
static void handle_button_press_count(int count);
static void request_lcm_update(void);
static void reset_homekit(void);

static int led_gpio = CONFIG_ESP_LED_GPIO;
static bool led_enabled = true;
static bool led_on = false;
static TaskHandle_t led_task = NULL;
static bool led_blinking = false;

#define LED_BLINK_ON_MS 500
#define LED_BLINK_OFF_MS 500

void led_write(bool on) {
    if (led_gpio < 0) return;
    ESP_LOGD(TAG, "Setting LED %s", on ? "ON" : "OFF");
    led_on = on;
    gpio_set_level(led_gpio, on ? 1 : 0);
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
    if (led_gpio >= 0) {
        gpio_reset_pin(led_gpio);
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        led_write(led_on);
        ESP_LOGD(TAG, "LED GPIO configured on pin %d", led_gpio);
    }

    // Knop setup
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    ESP_LOGD(TAG, "Button GPIO configured on pin %d", BUTTON_GPIO);
}

static bool factory_reset_requested = false;

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

static void handle_button_press_count(int count) {
    if (count <= 0) {
        return;
    }

    if (count == 1) {
        ESP_LOGI(TAG, "Single press detected (no action)");
        return;
    }

    if (count == 2) {
        ESP_LOGI(TAG, "Double press detected → request firmware update through LCM");
        request_lcm_update();
        return;
    }

    ESP_LOGI(TAG, "Triple press detected → reset HomeKit");
    reset_homekit();
}

// Task button
void button_task(void *pvParameter) {
    ESP_LOGI(TAG, "Button task started");
    const TickType_t debounce_ticks = pdMS_TO_TICKS(DEBOUNCE_TIME_MS);
    const TickType_t long_press_ticks = pdMS_TO_TICKS(RESET_HOLD_MS);
    const TickType_t multi_timeout_ticks = pdMS_TO_TICKS(MULTI_CLICK_TIMEOUT_MS);

    bool stable_state = gpio_get_level(BUTTON_GPIO) == 0;
    bool raw_state = stable_state;
    TickType_t last_raw_change = xTaskGetTickCount();
    TickType_t press_start = stable_state ? last_raw_change : 0;
    bool long_press_triggered = false;
    int press_count = 0;
    TickType_t last_release_tick = 0;
    bool waiting_for_multi = false;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        if (waiting_for_multi && (now - last_release_tick) > multi_timeout_ticks) {
            handle_button_press_count(press_count);
            press_count = 0;
            waiting_for_multi = false;
        }

        bool current_raw = gpio_get_level(BUTTON_GPIO) == 0;
        if (current_raw != raw_state) {
            raw_state = current_raw;
            last_raw_change = now;
        }

        if (stable_state != raw_state && (now - last_raw_change) >= debounce_ticks) {
            stable_state = raw_state;
            if (stable_state) {
                press_start = now;
                long_press_triggered = false;
            } else {
                if (!long_press_triggered) {
                    TickType_t press_duration = now - press_start;
                    if (press_duration >= long_press_ticks) {
                        long_press_triggered = true;
                        press_count = 0;
                        waiting_for_multi = false;
                        ESP_LOGW(TAG, "Long press detected → resetting configuration");
                        factory_reset();
                    } else {
                        if (press_count < 255) {
                            press_count++;
                        }
                        if (press_count >= 3) {
                            handle_button_press_count(press_count);
                            press_count = 0;
                            waiting_for_multi = false;
                        } else {
                            waiting_for_multi = true;
                            last_release_tick = now;
                        }
                    }
                }
            }
        } else if (stable_state && !long_press_triggered) {
            TickType_t held = now - press_start;
            if (held >= long_press_ticks) {
                long_press_triggered = true;
                press_count = 0;
                waiting_for_multi = false;
                ESP_LOGW(TAG, "Long press detected → resetting configuration");
                factory_reset();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void request_lcm_update(void) {
    ESP_LOGI(TAG, "Setting update request flag for Lifecycle Manager");

    nvs_handle_t handle;
    esp_err_t err = nvs_open("lcm", NVS_READWRITE, &handle);
    bool update_flag_set = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'lcm': %s", esp_err_to_name(err));
    } else {
        err = nvs_set_u8(handle, "do_update", 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set update flag: %s", esp_err_to_name(err));
        } else {
            err = nvs_commit(handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit update flag: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Lifecycle Manager will run on next boot to install firmware");
                update_flag_set = true;
            }
        }
        nvs_close(handle);
    }

    if (!update_flag_set) {
        ESP_LOGW(TAG, "Update flag could not be stored; aborting reboot to Lifecycle Manager");
        return;
    }

    const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        ESP_LOGE(TAG, "Factory partition not found; cannot hand off to Lifecycle Manager");
        return;
    }

    err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set factory partition for boot: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Rebooting into Lifecycle Manager to perform update");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void reset_homekit(void) {
#if LCM_HAVE_HOMEKIT
    ESP_LOGI(TAG, "Resetting HomeKit pairing information");
    homekit_server_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Rebooting after HomeKit reset");
    esp_restart();
#else
    ESP_LOGW(TAG, "HomeKit reset requested, but HomeKit support is not available");
#endif
}

void app_main(void) {
    ESP_LOGI(TAG, "Application start");
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
    load_led_config(&led_enabled, &led_gpio);
    gpio_init();
    if (xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
    }
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
    ESP_LOGI("app", "Checking for firmware update");
    github_update_if_needed(repo, pre);
    led_blinking_stop();
    ESP_LOGI("app", "Firmware update check complete");
}
