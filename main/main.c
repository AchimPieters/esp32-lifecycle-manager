/**
   Copyright 2025 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

   for more information visit https://www.studiopieters.nl
 **/

#include "github_update.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <wifi_config.h>

// GPIO-definities
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define DEBOUNCE_TIME_MS 50
#define RESET_HOLD_MS 3000

static const char *TAG = "main";

static void sntp_start_and_wait(void);
void wifi_ready(void);

static int led_gpio = CONFIG_ESP_LED_GPIO;
static bool led_enabled = false;
static bool led_configured = false;
static bool led_on = false;
static TaskHandle_t led_task = NULL;
static bool led_blinking = false;

void led_write(bool on) {
    if (led_gpio < 0)
        return;
    ESP_LOGD(TAG, "Setting LED %s", on ? "ON" : "OFF");
    gpio_set_level(led_gpio, on ? 1 : 0);
}

static void led_apply_idle_state(void) {
    if (!led_enabled || led_gpio < 0) {
        led_write(false);
        return;
    }
    led_write(led_on);
}

static void led_blink_task(void *pv) {
    (void)pv;
    const TickType_t half_period = pdMS_TO_TICKS(500);
    TickType_t last_toggle = xTaskGetTickCount();

    while (led_blinking) {
        led_write(true);
        vTaskDelayUntil(&last_toggle, half_period);
        if (!led_blinking)
            break;
        led_write(false);
        vTaskDelayUntil(&last_toggle, half_period);
    }

    led_write(false);
    led_task = NULL;
    vTaskDelete(NULL);
}

void led_blinking_start() {
    if (led_gpio < 0 || !led_enabled || led_blinking)
        return;
    led_blinking = true;
    BaseType_t rc =
        xTaskCreate(led_blink_task, "led_blink", 2048, NULL, 5, &led_task);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED blink task");
        led_task = NULL;
        led_blinking = false;
    }
}

void led_blinking_stop() {
    if (!led_blinking)
        return;
    led_blinking = false;
    TaskHandle_t task = led_task;
    led_task = NULL;
    if (task)
        vTaskDelete(task);
    led_apply_idle_state();
}

void gpio_init() {
    ESP_LOGD(TAG, "Initializing GPIO");
    // LED setup
    if (led_gpio >= 0) {
        gpio_reset_pin(led_gpio);
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        led_apply_idle_state();
        ESP_LOGD(TAG, "LED GPIO configured on pin %d", led_gpio);
    }

    // Knop setup
    gpio_config_t io_conf = {.pin_bit_mask = 1ULL << BUTTON_GPIO,
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
    ESP_LOGD(TAG, "Button GPIO configured on pin %d", BUTTON_GPIO);
}

// Task factory_reset
void factory_reset_task(void *pvParameter) {
    ESP_LOGI("RESET", "Resetting WiFi Config");
    wifi_config_reset();

    ESP_LOGD("RESET", "Waiting before reboot");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("RESTART", "Restarting system");
    esp_restart();

    ESP_LOGD("RESET", "factory_reset_task completed");
    vTaskDelete(NULL);
}

void factory_reset() {
    ESP_LOGI("RESET", "Resetting device configuration");
    if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 2, NULL) !=
        pdPASS) {
        ESP_LOGE("RESET", "Failed to create factory_reset task");
    }
}

void led_config_update(bool enabled, int gpio) {
    led_enabled = enabled;
    led_on = enabled && gpio >= 0;
    led_blinking_stop();
    led_gpio = gpio;
    gpio_init();
}

// Task button
void button_task(void *pvParameter) {
    ESP_LOGI(TAG, "Button task started");
    bool pressed = false;
    TickType_t press_start = 0;

    while (1) {
        bool state = gpio_get_level(BUTTON_GPIO) == 0; // active low
        ESP_LOGD(TAG, "Button state: %d", state);

        if (state) {
            if (!pressed) {
                press_start = xTaskGetTickCount();
                pressed = true;
                ESP_LOGD(TAG, "Button press detected");
            } else if (xTaskGetTickCount() - press_start >=
                       pdMS_TO_TICKS(RESET_HOLD_MS)) {
                ESP_LOGW(TAG, "Button held for %dms → resetting configuration",
                         RESET_HOLD_MS);
                factory_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
                pressed = false; // prevent retrigger before reboot
            }
        } else if (pressed) {
            pressed = false;
            ESP_LOGD(TAG, "Button released");
        }

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Application start");
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
    led_configured = load_led_config(&led_enabled, &led_gpio);
    if (led_configured) {
        led_on = led_enabled && led_gpio >= 0;
    }
    gpio_init();
    if (xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
    }
    wifi_config_init("LCM", NULL, wifi_ready);
}

static void sntp_start_and_wait(void) {
    ESP_LOGD(TAG, "Starting SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    time_t now = 0;
    struct tm tm = {0};
    for (int i = 0; i < 20 && tm.tm_year < (2016 - 1900); ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &tm);
        ESP_LOGD(TAG, "SNTP attempt %d, year=%d", i, tm.tm_year + 1900);
    }
    ESP_LOGD(TAG, "SNTP sync completed");
}

void wifi_ready(void) {
    ESP_LOGI("app", "WiFi ready; starting OTA check");
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("github_update", ESP_LOG_DEBUG);
    esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);

    ESP_LOGI("app", "Starting SNTP synchronization");
    sntp_start_and_wait();
    ESP_LOGI("app", "SNTP synchronization complete");

    char repo[96] = {0};
    bool pre = false;
    if (!load_fw_config(repo, sizeof(repo), &pre)) {
        ESP_LOGW("app", "Geen firmware-config in NVS; configureer via web UI.");
        return;
    }
    ESP_LOGI("app", "Firmware config loaded: repo=%s pre=%d", repo, pre);
    ESP_LOGI("app", "Checking for firmware update");
    github_update_if_needed(repo, pre);
    ESP_LOGI("app", "Firmware update check complete");
}
