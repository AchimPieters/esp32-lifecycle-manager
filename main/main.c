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
#include "github_update.h"
#include <driver/ledc.h>

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
static bool led_breathing = false;

void led_write(bool on) {
    if (led_gpio < 0) return;
    ESP_LOGD(TAG, "Setting LED %s", on ? "ON" : "OFF");
    gpio_set_level(led_gpio, on ? 1 : 0);
}

static void led_breath_task(void *pv) {
    int duty = 0;
    bool up = true;
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t cconf = {
        .gpio_num = led_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&cconf);
    while (led_breathing) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (up) {
            duty++;
            if (duty >= 255) { duty = 255; up = false; }
        } else {
            duty--;
            if (duty <= 0) { duty = 0; up = true; }
        }
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelete(NULL);
}

void led_breathing_start() {
    if (led_gpio < 0 || !led_enabled || led_breathing) return;
    led_breathing = true;
    xTaskCreate(led_breath_task, "led_breath", 2048, NULL, 5, &led_task);
}

void led_breathing_stop() {
    if (!led_breathing) return;
    led_breathing = false;
    if (led_task) {
        vTaskDelete(led_task);
        led_task = NULL;
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
    if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE("RESET", "Failed to create factory_reset task");
    }
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
            } else if (xTaskGetTickCount() - press_start >= pdMS_TO_TICKS(RESET_HOLD_MS)) {
                ESP_LOGW(TAG, "Button held for %dms → resetting configuration", RESET_HOLD_MS);
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
        return;
    }
    ESP_LOGI("app", "Firmware config loaded: repo=%s pre=%d", repo, pre);
    ESP_LOGI("app", "Checking for firmware update");
    github_update_if_needed(repo, pre);
    ESP_LOGI("app", "Firmware update check complete");
}
