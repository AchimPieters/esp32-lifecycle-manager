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

#include <stdbool.h>
#include <stdio.h>

#include <esp_err.h>
#include <esp_log.h>
#include <string.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h"

// Boot/lifecycle button wired to ground, so keep the internal pull-up enabled
// and treat a LOW level as a pressed state.
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define LED_GPIO CONFIG_ESP_LED_GPIO

#define DEVICE_NAME "HomeKit LED"
#define DEVICE_MANUFACTURER "StudioPieters®"
#define DEVICE_SERIAL "NLDA4SQN1466"
#define DEVICE_MODEL "SD466NL/A"

#ifndef CONFIG_APP_PROJECT_VER
#define CONFIG_APP_PROJECT_VER "0.0.1"
#endif

static char fw_version_buffer[LIFECYCLE_FW_REVISION_MAX_LEN] = CONFIG_APP_PROJECT_VER;

#define FW_VERSION fw_version_buffer

static const char *HOMEKIT_TAG = "HOMEKIT";

static bool led_on = false;

static void led_write(bool on) {
    gpio_set_level(LED_GPIO, on ? 1 : 0);
}

static const char *lifecycle_button_event_to_string(lifecycle_button_event_t event) {
    switch (event) {
        case LIFECYCLE_BUTTON_EVENT_SINGLE:
            return "single";
        case LIFECYCLE_BUTTON_EVENT_DOUBLE:
            return "double";
        case LIFECYCLE_BUTTON_EVENT_TRIPLE:
            return "triple";
        case LIFECYCLE_BUTTON_EVENT_LONG:
            return "long";
        default:
            return "unknown";
    }
}

static void lifecycle_button_event_logger(lifecycle_button_event_t event, void *ctx) {
    const char *ctx_str = ctx != NULL ? (const char *)ctx : "<no context>";
    int level = gpio_get_level(BUTTON_GPIO);
    ESP_LOGI(HOMEKIT_TAG,
             "Lifecycle button callback -> event=%s (%d), gpio level=%d, context=%s",
             lifecycle_button_event_to_string(event),
             event,
             level,
             ctx_str);
}

static void accessory_identify_task(void *args) {
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

static void accessory_identify(homekit_value_t _value) {
    ESP_LOGI(HOMEKIT_TAG, "Accessory identify");
    xTaskCreate(accessory_identify_task, "Accessory identify", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
}

static homekit_value_t led_on_get() {
    return HOMEKIT_BOOL(led_on);
}

static void led_on_set(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        ESP_LOGW(HOMEKIT_TAG, "Invalid value format: %d", value.format);
        return;
    }
    led_on = value.bool_value;
    led_write(led_on);
}

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id = 1,
        .category = homekit_accessory_category_lighting,
        .services = (homekit_service_t*[]) {
            HOMEKIT_SERVICE(
                ACCESSORY_INFORMATION,
                .characteristics = (homekit_characteristic_t*[]) {
                    &name,
                    &manufacturer,
                    &serial,
                    &model,
                    &revision,
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
                    NULL
                }
            ),
            HOMEKIT_SERVICE(
                LIGHTBULB,
                .primary = true,
                .characteristics = (homekit_characteristic_t*[]) {
                    HOMEKIT_CHARACTERISTIC(NAME, "HomeKit LED"),
                    HOMEKIT_CHARACTERISTIC(ON, false, .getter = led_on_get, .setter = led_on_set),
                    NULL
                }
            ),
            HOMEKIT_SERVICE(
                CUSTOM_SETUP,
                .characteristics = (homekit_characteristic_t*[]) {
                    HOMEKIT_CHARACTERISTIC(NAME, "Lifecycle"),
                    &ota_trigger,
                    NULL
                }
            ),
            NULL
        }
    ),
    NULL
};
#pragma GCC diagnostic pop

homekit_server_config_t config = {
    .accessories = accessories,
    .password = CONFIG_ESP_SETUP_CODE,
    .setupId = CONFIG_ESP_SETUP_ID,
};

static void on_wifi_ready(void) {
    ESP_LOGI(HOMEKIT_TAG, "Starting HomeKit server...");
    homekit_server_init(&config);
}

void app_main(void) {
    ESP_ERROR_CHECK(lifecycle_nvs_init());

    esp_err_t rev_err = lifecycle_init_firmware_revision(&revision, FW_VERSION);
    const char *resolved_version = lifecycle_get_firmware_revision_string();
    if (resolved_version && resolved_version[0] != '\0') {
        strlcpy(fw_version_buffer, resolved_version, sizeof(fw_version_buffer));
        ESP_LOGI(HOMEKIT_TAG, "Lifecycle Manager firmware version (NVS): %s", fw_version_buffer);
    } else {
        ESP_LOGW(HOMEKIT_TAG,
                 "Lifecycle Manager firmware version not found in NVS, using fallback: %s",
                 fw_version_buffer);
    }

    revision.value.string_value = fw_version_buffer;
    revision.value.is_static = true;
    if (rev_err != ESP_OK) {
        ESP_LOGW(HOMEKIT_TAG, "Firmware revision init failed: %s", esp_err_to_name(rev_err));
    }

    ota_trigger.setter = NULL;
    ota_trigger.setter_ex = lifecycle_handle_ota_trigger;
    ota_trigger.value.bool_value = false;

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_write(led_on);

    const lifecycle_button_config_t button_cfg = {
        .gpio = BUTTON_GPIO,
        .single_action = LIFECYCLE_BUTTON_ACTION_NONE,
        .double_action = LIFECYCLE_BUTTON_ACTION_REQUEST_UPDATE,
        .triple_action = LIFECYCLE_BUTTON_ACTION_RESET_HOMEKIT,
        .long_action = LIFECYCLE_BUTTON_ACTION_FACTORY_RESET,
        .event_callback = lifecycle_button_event_logger,
        .event_context = (void *)"app_main",
    };
    ESP_LOGI(HOMEKIT_TAG,
             "Configuring lifecycle button on GPIO %d (active low to GND)",
             button_cfg.gpio);
    ESP_ERROR_CHECK(lifecycle_button_init(&button_cfg));

    int button_level = gpio_get_level(BUTTON_GPIO);
    ESP_LOGI(HOMEKIT_TAG,
             "Lifecycle button initial level: %s (0=pressed, 1=released)",
             button_level == 0 ? "pressed" : "released");

    ESP_ERROR_CHECK(wifi_start(on_wifi_ready));
}
