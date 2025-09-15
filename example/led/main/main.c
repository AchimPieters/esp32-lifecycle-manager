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
#include <esp_system.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h"   // <— nieuwe wifi module

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
#define UPDATE_BUTTON_GPIO GPIO_NUM_15
#define UPDATE_POLL_INTERVAL_MS 50
#define UPDATE_DEBOUNCE_MS 50
#define LED_BLINK_INTERVAL_MS 200
bool led_on = false;

static TaskHandle_t led_blink_task_handle = NULL;
static bool led_blinking = false;

static void led_blink_task(void *arg);
void led_blinking_start(void);
void led_blinking_stop(void);
static void lcm_update_task(void *arg);

void led_write(bool on) {
        gpio_set_level(LED_GPIO, on ? 1 : 0);
}

static void led_blink_task(void *arg) {
        while (led_blinking) {
                led_write(true);
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_INTERVAL_MS));
                led_write(false);
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_INTERVAL_MS));
        }

        led_blink_task_handle = NULL;
        if (!led_blinking) {
                led_write(led_on);
        }
        vTaskDelete(NULL);
}

void led_blinking_start(void) {
        if (led_blinking) {
                return;
        }

        led_blinking = true;
        if (xTaskCreate(led_blink_task, "led_blink", 2048, NULL, 5,
                        &led_blink_task_handle) != pdPASS) {
                ESP_LOGE("LCM", "Failed to create led_blink task");
                led_blinking = false;
                led_blink_task_handle = NULL;
        }
}

void led_blinking_stop(void) {
        if (!led_blinking) {
                return;
        }

        led_blinking = false;
        while (led_blink_task_handle != NULL) {
                vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!led_blinking) {
                led_write(led_on);
        }
}

// All GPIO Settings
void gpio_init() {
        gpio_reset_pin(LED_GPIO); // Reset GPIO pin to avoid conflicts
        gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
        if (!led_blinking) {
                led_write(led_on);
        }

        gpio_reset_pin(UPDATE_BUTTON_GPIO);
        gpio_config_t button_conf = {
                .pin_bit_mask = 1ULL << UPDATE_BUTTON_GPIO,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        CHECK_ERROR(gpio_config(&button_conf));
}

static void lcm_update_task(void *arg) {
        bool last_pressed = false;

        while (1) {
                bool pressed = gpio_get_level(UPDATE_BUTTON_GPIO) == 0;

                if (pressed && !last_pressed) {
                        vTaskDelay(pdMS_TO_TICKS(UPDATE_DEBOUNCE_MS));
                        if (gpio_get_level(UPDATE_BUTTON_GPIO) == 0) {
                                ESP_LOGI("LCM", "Update button pressed – checking for updates");
                                esp_err_t update_res = lcm_update();
                                if (update_res == ESP_OK) {
                                        ESP_LOGI("LCM", "Update successful, restarting device");
                                        vTaskDelay(pdMS_TO_TICKS(100));
                                        esp_restart();
                                } else {
                                        ESP_LOGE("LCM", "Update failed: %s", esp_err_to_name(update_res));
                                }

                                while (gpio_get_level(UPDATE_BUTTON_GPIO) == 0) {
                                        vTaskDelay(pdMS_TO_TICKS(UPDATE_POLL_INTERVAL_MS));
                                }
                        }
                }

                last_pressed = pressed;
                vTaskDelay(pdMS_TO_TICKS(UPDATE_POLL_INTERVAL_MS));
        }
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
        if (!led_blinking) {
                led_write(led_on);
        }
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
        if (!led_blinking) {
                led_write(led_on);
        }
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

        if (xTaskCreate(lcm_update_task, "lcm_update", 4096, NULL, 5, NULL) != pdPASS) {
                ESP_LOGE("LCM", "Failed to create lcm_update task");
        }

        // Start WiFi op basis van NVS en geef callback door
        CHECK_ERROR(wifi_start(on_wifi_ready));
}
