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

// GPIO-definities
#define LED_GPIO CONFIG_ESP_LED_GPIO
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define DEBOUNCE_TIME_MS 50

static const char *TAG = "main";

static void sntp_start_and_wait(void);
void wifi_ready(void);

bool led_on = false;

void led_write(bool on) {
    gpio_set_level(LED_GPIO, on ? 1 : 0);
}

void gpio_init() {
    // LED setup
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_write(led_on);

    // Knop setup
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

// Task factory_reset
void factory_reset_task(void *pvParameter) {
    ESP_LOGI("RESET", "Resetting WiFi Config");
    wifi_config_reset();

    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("RESTART", "Restarting system");
    esp_restart();

    vTaskDelete(NULL);
}

void factory_reset() {
    ESP_LOGI("RESET", "Resetting device configuration");
    xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 2, NULL);
}

// Task button
void button_task(void *pvParameter) {
    ESP_LOGI(TAG, "Button task started");
    bool last_state = true;

    while (1) {
        bool current_state = gpio_get_level(BUTTON_GPIO);

        // Detecteer overgang van HIGH naar LOW (knop ingedrukt)
        if (last_state && !current_state) {
            ESP_LOGW(TAG, "BUTTON PRESSED → RESETTING CONFIGURATION");
            factory_reset();
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    gpio_init();
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    wifi_config_init("LCM", NULL, wifi_ready);
}

static void sntp_start_and_wait(void){
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    time_t now=0; struct tm tm={0};
    for (int i=0; i<20 && tm.tm_year < (2016-1900); ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now); localtime_r(&now, &tm);
    }
}

void wifi_ready(void)
{
    ESP_LOGI("app", "WiFi ready; starting OTA check");
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("github_update", ESP_LOG_DEBUG);
    esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_CLIENT",   ESP_LOG_DEBUG);

    sntp_start_and_wait();

    char repo[96]={0}, fw[256]={0}, sig[256]={0};
    bool pre=false;
    if (!load_fw_config(repo, sizeof(repo), &pre, fw, sizeof(fw), sig, sizeof(sig))) {
        ESP_LOGW("app", "Geen firmware-config in NVS; configureer via web UI.");
        return;
    }
    github_update_if_needed(repo, pre);
    // alternatively: github_update_from_urls(fw, sig);
}
