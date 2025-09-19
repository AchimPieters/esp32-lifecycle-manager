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
#include <stdbool.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "homekit_custom_characteristics.h"

#define BUTTON_GPIO GPIO_NUM_0
#define DEBOUNCE_US 2000
#define DOUBLE_CLICK_US 400000
#define LONG_PRESS_US 2000000

static const char *BUTTON_TAG = "BUTTON";
static const char *HOMEKIT_TAG = "HOMEKIT";

static QueueHandle_t button_evt_queue;
static volatile int64_t last_isr_time_us = 0;
static int press_count = 0;
static bool waiting_for_second_press = false;
static bool double_press_detected = false;
static int64_t press_start_time_ms = 0;
static int64_t last_release_time_ms = 0;

#include "esp32-wifi.h"   // <— nieuwe wifi module

static void ota_trigger_setter(homekit_value_t value);
static void button_task(void *pvParameter);
static void button_isr_handler(void *arg);
static void initialise_firmware_revision(void);
static void handle_single_press(void);
static void handle_double_press(void);
static void handle_long_press(void);

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

static char s_installed_fw_revision[32] = FW_VERSION;

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;

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
                HOMEKIT_SERVICE(CUSTOM_SETUP, .characteristics = (homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Lifecycle"),
                        &ota_trigger,
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

static void initialise_firmware_revision(void) {
        const esp_app_desc_t *desc = esp_app_get_description();
        const char *current_version = (desc && strlen(desc->version) > 0) ? desc->version : FW_VERSION;

        esp_err_t err = ESP_OK;
        nvs_handle_t handle;
        err = nvs_open("fwcfg", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
                size_t required = sizeof(s_installed_fw_revision);
                esp_err_t get_err = nvs_get_str(handle, "installed_ver", s_installed_fw_revision, &required);
                if (get_err == ESP_ERR_NVS_NOT_FOUND || s_installed_fw_revision[0] == '\0') {
                        strlcpy(s_installed_fw_revision, current_version, sizeof(s_installed_fw_revision));
                        esp_err_t set_err = nvs_set_str(handle, "installed_ver", s_installed_fw_revision);
                        if (set_err != ESP_OK) {
                                ESP_LOGW(HOMEKIT_TAG, "Failed to store firmware revision: %s", esp_err_to_name(set_err));
                        } else {
                                esp_err_t commit_err = nvs_commit(handle);
                                if (commit_err != ESP_OK) {
                                        ESP_LOGW(HOMEKIT_TAG, "Commit of firmware revision failed: %s", esp_err_to_name(commit_err));
                                }
                        }
                } else if (get_err == ESP_OK) {
                        if (strncmp(s_installed_fw_revision, current_version, sizeof(s_installed_fw_revision)) != 0) {
                                strlcpy(s_installed_fw_revision, current_version, sizeof(s_installed_fw_revision));
                                esp_err_t set_err = nvs_set_str(handle, "installed_ver", s_installed_fw_revision);
                                if (set_err == ESP_OK) {
                                        esp_err_t commit_err = nvs_commit(handle);
                                        if (commit_err != ESP_OK) {
                                                ESP_LOGW(HOMEKIT_TAG, "Commit of firmware revision failed: %s", esp_err_to_name(commit_err));
                                        }
                                } else {
                                        ESP_LOGW(HOMEKIT_TAG, "Failed to update stored firmware revision: %s", esp_err_to_name(set_err));
                                }
                        }
                } else {
                        ESP_LOGW(HOMEKIT_TAG, "Reading stored firmware revision failed: %s", esp_err_to_name(get_err));
                        strlcpy(s_installed_fw_revision, current_version, sizeof(s_installed_fw_revision));
                }
                nvs_close(handle);
        } else {
                ESP_LOGW(HOMEKIT_TAG, "Unable to open fwcfg namespace: %s", esp_err_to_name(err));
                strlcpy(s_installed_fw_revision, current_version, sizeof(s_installed_fw_revision));
        }

        revision.value.string_value = s_installed_fw_revision;
        revision.value.is_static = true;
}

static void handle_single_press(void) {
        ESP_LOGI(BUTTON_TAG, "Single press detected -> requesting update");
        lifecycle_request_update_and_reboot();
}

static void handle_double_press(void) {
        ESP_LOGI(BUTTON_TAG, "Double press detected -> resetting HomeKit");
        lifecycle_reset_homekit_and_reboot();
}

static void handle_long_press(void) {
        ESP_LOGI(BUTTON_TAG, "Long press detected -> factory reset");
        lifecycle_factory_reset_and_reboot();
}

static void ota_trigger_setter(homekit_value_t value) {
        if (value.format != homekit_format_bool) {
                ESP_LOGW(HOMEKIT_TAG, "Invalid OTA trigger format: %d", value.format);
                return;
        }

        bool requested = value.bool_value;
        ota_trigger.value.bool_value = false;
        homekit_characteristic_notify(&ota_trigger, HOMEKIT_BOOL(ota_trigger.value.bool_value));

        if (requested) {
                ESP_LOGI(HOMEKIT_TAG, "HomeKit requested firmware update");
                lifecycle_request_update_and_reboot();
        }
}

static void button_task(void *pvParameter) {
        const int double_window_ms = DOUBLE_CLICK_US / 1000;
        const int long_press_threshold_ms = LONG_PRESS_US / 1000;

        uint32_t io_num;
        ESP_LOGI(BUTTON_TAG, "Button task started");
        while (true) {
                if (xQueueReceive(button_evt_queue, &io_num, pdMS_TO_TICKS(10)) == pdTRUE) {
                        int64_t now_ms = esp_timer_get_time() / 1000;
                        bool level = gpio_get_level(io_num) == 0; // active low -> pressed when 0

                        if (level) {
                                press_start_time_ms = now_ms;
                                press_count++;
                                double_press_detected = false;
                                waiting_for_second_press = false;
                        } else {
                                if (press_start_time_ms == 0) {
                                        continue;
                                }
                                int64_t press_duration_ms = now_ms - press_start_time_ms;
                                press_start_time_ms = 0;

                                if (press_duration_ms >= long_press_threshold_ms) {
                                        waiting_for_second_press = false;
                                        press_count = 0;
                                        handle_long_press();
                                } else {
                                        if (press_count == 1) {
                                                waiting_for_second_press = true;
                                                last_release_time_ms = now_ms;
                                        } else if (press_count == 2) {
                                                int64_t diff_ms = now_ms - last_release_time_ms;
                                                if (diff_ms <= double_window_ms) {
                                                        double_press_detected = true;
                                                        handle_double_press();
                                                }
                                                waiting_for_second_press = false;
                                                press_count = 0;
                                        } else if (press_count > 2) {
                                                waiting_for_second_press = false;
                                                press_count = 0;
                                        }
                                }
                        }
                }

                if (waiting_for_second_press) {
                        int64_t now_ms = esp_timer_get_time() / 1000;
                        if ((now_ms - last_release_time_ms) > double_window_ms) {
                                waiting_for_second_press = false;
                                if (!double_press_detected && press_count == 1) {
                                        press_count = 0;
                                        handle_single_press();
                                } else {
                                        press_count = 0;
                                }
                        }
                }

                vTaskDelay(pdMS_TO_TICKS(10));
        }
}

static void IRAM_ATTR button_isr_handler(void *arg) {
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_isr_time_us) < DEBOUNCE_US) {
                return;
        }
        last_isr_time_us = now_us;

        uint32_t gpio_num = (uint32_t)arg;
        BaseType_t higher_wakeup = pdFALSE;
        if (xQueueSendFromISR(button_evt_queue, &gpio_num, &higher_wakeup) != pdPASS) {
                // queue full -> drop event
        }
        if (higher_wakeup == pdTRUE) {
                portYIELD_FROM_ISR();
        }
}
static void on_wifi_ready(void) {
        ESP_LOGI("INFORMATION", "Starting HomeKit server...");
        homekit_server_init(&config);
}

void app_main(void) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_LOGW("WARNING", "NVS flash initialization failed, erasing...");
                CHECK_ERROR(nvs_flash_erase());
                ret = nvs_flash_init();
        }
        CHECK_ERROR(ret);

        initialise_firmware_revision();
        ota_trigger.setter = ota_trigger_setter;
        ota_trigger.value.bool_value = false;

        gpio_init();

        button_evt_queue = xQueueCreate(10, sizeof(uint32_t));
        if (button_evt_queue == NULL) {
                ESP_LOGE(BUTTON_TAG, "Failed to create button event queue");
                handle_error(ESP_ERR_NO_MEM);
        }

        gpio_reset_pin(BUTTON_GPIO);
        gpio_config_t button_conf = {
                .pin_bit_mask = 1ULL << BUTTON_GPIO,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_ANYEDGE,
        };
        CHECK_ERROR(gpio_config(&button_conf));
        CHECK_ERROR(gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE));

        esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(BUTTON_TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_err));
                handle_error(isr_err);
        }

        CHECK_ERROR(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)BUTTON_GPIO));
        CHECK_ERROR(gpio_intr_enable(BUTTON_GPIO));

        last_isr_time_us = 0;

        if (xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL) != pdPASS) {
                ESP_LOGE(BUTTON_TAG, "Failed to create button task");
                handle_error(ESP_ERR_NO_MEM);
        }

        CHECK_ERROR(wifi_start(on_wifi_ready));
}
