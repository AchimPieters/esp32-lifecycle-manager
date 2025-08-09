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

#include "ota.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include "apps/esp_sntp.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <wifi_config.h>

// GPIO-definities
#define LED_GPIO CONFIG_ESP_LED_GPIO
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define DEBOUNCE_TIME_MS 50

static const char *TAG = "main";

bool led_on = false;

void led_write(bool on) { gpio_set_level(LED_GPIO, on ? 1 : 0); }

static bool sntp_started = false;

static void on_time_synced(struct timeval *tv) {
  ESP_LOGI("TIME", "Systeemtijd gesynchroniseerd");
}

static bool s_time_ready(void) {
  time_t now = 0;
  struct tm tm_info = {0};
  time(&now);
  localtime_r(&now, &tm_info);
  return (tm_info.tm_year + 1900) >= 2024;
}

static void start_time_sync(void) {
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  // In ESP-IDF 5.4+, the DHCP SNTP server configuration API is
  // esp_sntp_servermode_dhcp(), which is only available when
  // LWIP_DHCP_GET_NTP_SRV is enabled. Guard the call so the code
  // builds cleanly regardless of that configuration.
#if LWIP_DHCP_GET_NTP_SRV
  esp_sntp_servermode_dhcp(true);
#endif
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(on_time_synced);
  esp_sntp_init();
  sntp_started = true;

  const int timeout_ms = 15000;
  int waited = 0;
  while (!s_time_ready() && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(250));
    waited += 250;
  }
  if (!s_time_ready()) {
    ESP_LOGW("TIME", "SNTP timeout; ga toch door (TLS kan falen)");
  }
}

void gpio_init() {
  ESP_LOGI(TAG, "Initializing GPIOs");
  // LED setup
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  led_write(led_on);

  // Knop setup
  gpio_config_t io_conf = {.pin_bit_mask = 1ULL << BUTTON_GPIO,
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);
  ESP_LOGI(TAG, "GPIOs initialized");
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
  if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 2, NULL) ==
      pdPASS) {
    ESP_LOGI("RESET", "Factory reset task created");
  } else {
    ESP_LOGE("RESET", "Failed to create factory reset task");
  }
}

// Task button
void button_task(void *pvParameter) {
  ESP_LOGI(TAG, "Button task started");
  TickType_t pressed_ts = 0;
  bool was_pressed = false;

  while (1) {
    bool current = (gpio_get_level(BUTTON_GPIO) == 0);
    if (current && !was_pressed) {
      was_pressed = true;
      pressed_ts = xTaskGetTickCount();
    } else if (!current && was_pressed) {
      TickType_t held = xTaskGetTickCount() - pressed_ts;
      was_pressed = false;
      if (held >= pdMS_TO_TICKS(3000)) {
        ESP_LOGW(TAG, "LONG PRESS -> FACTORY RESET");
        factory_reset();
      } else {
        ESP_LOGI(TAG, "Short press ignored (safety)");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
  }
}

static void on_got_ip(void) {
  ESP_LOGI("MAIN", "WiFi connected, synchronizing time…");
  if (esp_sntp_enabled())
    esp_sntp_stop();
  if (!sntp_started) {
    start_time_sync();
  }
  ESP_LOGI("MAIN", "Starting OTA task…");
  ota_start();
}

void app_main(void) {
  ESP_LOGI(TAG, "Application start");
  ESP_LOGI(TAG, "Initializing NVS");
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "NVS initialized");
  gpio_init();
  ESP_LOGI(TAG, "GPIO initialized");
  if (xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL) == pdPASS) {
    ESP_LOGI(TAG, "Button task created");
  } else {
    ESP_LOGE(TAG, "Failed to create button task");
  }
  ESP_LOGI(TAG, "Initializing WiFi config");
  wifi_config_init("LCM", NULL, on_got_ip);
  ESP_LOGI(TAG, "WiFi config initialized");
}
