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
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#define BUTTON_GPIO GPIO_NUM_0
#define DEBOUNCE_US 2000
#define DOUBLE_CLICK_US 400000
#define LONG_PRESS_US 2000000

#ifndef ESP32_WIFI_BUTTON_TYPES_DEFINED
#define ESP32_WIFI_BUTTON_TYPES_DEFINED
typedef enum {
        BUTTON_EVENT_PRESS,
        BUTTON_EVENT_RELEASE,
        BUTTON_EVENT_SINGLE_TIMEOUT,
} button_event_type_t;

typedef struct {
        button_event_type_t type;
        int64_t time_us;
} button_event_t;
#endif

static QueueHandle_t button_event_queue;
static TimerHandle_t button_single_click_timer;
static int button_click_count;

#include "esp32-wifi.h"   // <— nieuwe wifi module

// Custom error handling macro
#define CHECK_ERROR(x) do {                        \
                esp_err_t __err_rc = (x);                  \
                if (__err_rc != ESP_OK) {                  \
                        ESP_LOGE("INFORMATION", "Error: %s", esp_err_to_name(__err_rc)); \
                        handle_error(__err_rc);            \
                }                                          \
} while(0)

void handle_error(esp_err_t err) {
        // Simpel fallback: herstart device bij kritieke fouten
        ESP_LOGE("ERROR", "Critical error, restarting device... (%s)", esp_err_to_name(err));
        esp_restart();
}

// LED control
#define LED_GPIO CONFIG_ESP_LED_GPIO
bool led_on = false;

void led_write(bool on) {
        gpio_set_level(LED_GPIO, on ? 1 : 0);
}

// All GPIO Settings
void gpio_init() {
        gpio_reset_pin(LED_GPIO); // Reset GPIO pin to avoid conflicts
        gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
        led_write(led_on);
}


// Accessory identification
void accessory_identify_task(void *args) {
        for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 2; j++) {
                        led_write(true);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        led_write(false);
                        vTaskDelay(pdMS_TO_TICKS(100));
                }
                vTaskDelay(pdMS_TO_TICKS(250));
        }
        led_write(led_on);
        vTaskDelete(NULL);
}

void accessory_identify(homekit_value_t _value) {
        ESP_LOGI("INFORMATION", "Accessory identify");
        xTaskCreate(accessory_identify_task, "Accessory identify", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
}

homekit_value_t led_on_get() {
        return HOMEKIT_BOOL(led_on);
}

void led_on_set(homekit_value_t value) {
        if (value.format != homekit_format_bool) {
                ESP_LOGE("ERROR", "Invalid value format: %d", value.format);
                return;
        }
        led_on = value.bool_value;
        led_write(led_on);
}

// HomeKit characteristics
#define DEVICE_NAME "HomeKit LED"
#define DEVICE_MANUFACTURER "StudioPieters®"
#define DEVICE_SERIAL "NLDA4SQN1466"
#define DEVICE_MODEL "SD466NL/A"
#define FW_VERSION "0.0.1"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, FW_VERSION);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_lighting, .services = (homekit_service_t*[]) {
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t*[]) {
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "HomeKit LED"),
                        HOMEKIT_CHARACTERISTIC(ON, false, .getter = led_on_get, .setter = led_on_set),
                        NULL
                }),
                NULL
        }),
        NULL
};
#pragma GCC diagnostic pop

homekit_server_config_t config = {
        .accessories = accessories,
        .password = CONFIG_ESP_SETUP_CODE,
        .setupId = CONFIG_ESP_SETUP_ID,
};

static void on_wifi_ready(void) {
        ESP_LOGI("INFORMATION", "Starting HomeKit server...");
        homekit_server_init(&config);
}

void app_main(void) {
        // NVS init (vereist voor WiFi module om keys te lezen)
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_LOGW("WARNING", "NVS flash initialization failed, erasing...");
                CHECK_ERROR(nvs_flash_erase());
                ret = nvs_flash_init();
        }
        CHECK_ERROR(ret);

        // Start GPIO / LED
        gpio_init();

        // Configure BOOT button handling (single/double/long press)
        button_event_queue = xQueueCreate(10, sizeof(button_event_t));
        if (button_event_queue == NULL) {
                ESP_LOGE("BUTTON", "Failed to create button event queue");
                handle_error(ESP_ERR_NO_MEM);
        }

        button_single_click_timer = xTimerCreate(
                "btn_click",
                pdMS_TO_TICKS(DOUBLE_CLICK_US / 1000),
                pdFALSE,
                NULL,
                button_single_click_timeout_callback);
        if (button_single_click_timer == NULL) {
                ESP_LOGE("BUTTON", "Failed to create button timer");
                handle_error(ESP_ERR_NO_MEM);
        }

        esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE("BUTTON", "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_err));
                handle_error(isr_err);
        }

        button_click_count = 0;
        button_init(BUTTON_GPIO, DEBOUNCE_US, LONG_PRESS_US,
                    button_event_queue, button_single_click_timer, &button_click_count);

        // Start WiFi op basis van NVS en geef callback door
        CHECK_ERROR(wifi_start(on_wifi_ready));
}
