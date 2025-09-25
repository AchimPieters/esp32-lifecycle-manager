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
#include <nvs.h>
#include "github_update.h"
#include "led_indicator.h"

static const char *TAG = "main";

static void sntp_start_and_wait(void);
void wifi_ready(void);

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

void app_main(void) {
    ESP_LOGI(TAG, "Application start");
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
    load_led_config(&led_enabled, &led_gpio);
    gpio_init();
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
