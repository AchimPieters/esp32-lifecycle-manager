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
#include <esp_err.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h" // Add this line

#include <button.h> // Add this line

#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO // Add this line

// LED control
#define LED_GPIO CONFIG_ESP_LED_GPIO
static const char *LED_TAG = "LED";
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
        ESP_LOGI(LED_TAG, "Setting LED %s", led_on ? "ON" : "OFF");
        led_write(led_on);
}

void button_callback(button_event_t event, void *context) {
        switch (event) {
        case button_event_single_press:
                ESP_LOGI("BUTTON", "Single press");
                lifecycle_request_update_and_reboot(); // Add this line
                break;
        case button_event_double_press:
                ESP_LOGI("BUTTON", "Double press");
                homekit_server_reset(); //Reset Homekit
                esp_restart();
                break;
        case button_event_long_press:
                ESP_LOGI("BUTTON", "Long press");
                lifecycle_factory_reset_and_reboot(); // Add this line
                break;
        default:
                ESP_LOGI("BUTTON", "Unknown button event: %d", event);
                break;
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
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, LIFECYCLE_DEFAULT_FW_VERSION); //Adjust this line
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER; // Add this line

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
                        &ota_trigger,  // Add this line
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

void on_wifi_ready() {
        static bool homekit_started = false;

        if (homekit_started) {
                ESP_LOGI("INFORMATION", "HomeKit server already running; skipping re-initialization");
                return;
        }

        ESP_LOGI("INFORMATION", "Starting HomeKit server...");
        homekit_server_init(&config);
        homekit_started = true;
}

void app_main(void) {
        ESP_ERROR_CHECK(lifecycle_nvs_init());  // Add this line
        lifecycle_log_post_reset_state("INFORMATION");
        ESP_ERROR_CHECK(lifecycle_configure_homekit(&revision, &ota_trigger, "INFORMATION"));  // Add this line

        gpio_init();

        button_config_t btn_cfg = button_config_default(button_active_low);  // Add this line
        btn_cfg.max_repeat_presses = 3;  // Add this line
        btn_cfg.long_press_time = 1000;  // Add this line

        if (button_create(BUTTON_GPIO, btn_cfg, button_callback, NULL)) {  // Add this line
                ESP_LOGE("BUTTON", "Failed to initialize button");  // Add this line
        }

        esp_err_t wifi_err = wifi_start(on_wifi_ready); // Add this line
        if (wifi_err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW("WIFI", "WiFi configuration not found; provisioning required");
        } else if (wifi_err != ESP_OK) {
                ESP_LOGE("WIFI", "Failed to start WiFi: %s", esp_err_to_name(wifi_err));
        }
}
