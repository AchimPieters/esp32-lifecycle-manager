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
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-wifi.h"   // <— nieuwe wifi module

// Button handling (GPIO0 / BOOT)
#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_DEBOUNCE_US 10000
#define LONG_PRESS_US (2 * 1000 * 1000)
#define DOUBLE_CLICK_TIMEOUT_MS 400

typedef enum {
        BUTTON_EVENT_PRESS,
        BUTTON_EVENT_RELEASE,
        BUTTON_EVENT_SINGLE_TIMEOUT,
} button_event_type_t;

typedef struct {
        button_event_type_t type;
        int64_t time_us;
} button_event_t;

static QueueHandle_t button_event_queue;
static TimerHandle_t button_single_click_timer;
static int button_click_count;

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

static void request_lcm_update_and_reboot(void) {
        ESP_LOGI("BUTTON", "Single click detected: hand-off to factory updater");

        nvs_handle_t handle;
        CHECK_ERROR(nvs_open("lcm", NVS_READWRITE, &handle));
        CHECK_ERROR(nvs_set_u8(handle, "do_update", 1));
        CHECK_ERROR(nvs_commit(handle));
        nvs_close(handle);

        const esp_partition_t *factory = esp_partition_find_first(
                ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory == NULL) {
                ESP_LOGE("BUTTON", "Factory partition not found");
                handle_error(ESP_FAIL);
        }

        CHECK_ERROR(esp_ota_set_boot_partition(factory));
        ESP_LOGI("BUTTON", "Rebooting into factory partition for update");
        esp_restart();
}

static void homekit_reset_only_and_reboot(void) {
        ESP_LOGI("BUTTON", "Double click detected: resetting HomeKit and rebooting");
        homekit_server_reset();
        esp_restart();
}

static void factory_reset_all_and_reboot(void) {
        ESP_LOGI("BUTTON", "Long press detected: factory reset initiated");
        homekit_server_reset();
        CHECK_ERROR(esp_wifi_restore());
        esp_restart();
}

static void button_single_click_timeout_callback(TimerHandle_t timer) {
        button_event_t event = {
                .type = BUTTON_EVENT_SINGLE_TIMEOUT,
                .time_us = esp_timer_get_time(),
        };

        if (xQueueSend(button_event_queue, &event, 0) != pdPASS) {
                ESP_LOGW("BUTTON", "Single click timeout event queue full");
        }
}

static void IRAM_ATTR button_isr_handler(void *arg) {
        const int level = gpio_get_level(BUTTON_GPIO);
        button_event_t event = {
                .type = (level == 0) ? BUTTON_EVENT_PRESS : BUTTON_EVENT_RELEASE,
                .time_us = esp_timer_get_time(),
        };

        BaseType_t task_woken = pdFALSE;
        if (xQueueSendFromISR(button_event_queue, &event, &task_woken) != pdPASS) {
                // Queue full, drop event silently
        }

        if (task_woken == pdTRUE) {
                portYIELD_FROM_ISR();
        }
}

static void button_event_task(void *args) {
        button_event_t event;
        int64_t press_start_us = 0;
        button_event_type_t last_event_type = BUTTON_EVENT_RELEASE;
        int64_t last_event_time_us = 0;

        while (xQueueReceive(button_event_queue, &event, portMAX_DELAY) == pdTRUE) {
                if ((event.type == last_event_type) &&
                    ((event.time_us - last_event_time_us) < BUTTON_DEBOUNCE_US)) {
                        continue;
                }

                if (event.type == BUTTON_EVENT_PRESS || event.type == BUTTON_EVENT_RELEASE) {
                        last_event_type = event.type;
                        last_event_time_us = event.time_us;
                }

                switch (event.type) {
                case BUTTON_EVENT_PRESS:
                        press_start_us = event.time_us;
                        break;
                case BUTTON_EVENT_RELEASE: {
                        if (press_start_us == 0) {
                                break;
                        }

                        const int64_t press_duration = event.time_us - press_start_us;
                        press_start_us = 0;

                        if (press_duration < BUTTON_DEBOUNCE_US) {
                                break;
                        }

                        if (press_duration >= LONG_PRESS_US) {
                                button_click_count = 0;
                                xTimerStop(button_single_click_timer, 0);
                                factory_reset_all_and_reboot();
                                break;
                        }

                        button_click_count++;
                        if (button_click_count == 1) {
                                xTimerStop(button_single_click_timer, 0);
                                if (xTimerStart(button_single_click_timer, 0) != pdPASS) {
                                        ESP_LOGE("BUTTON", "Failed to start single click timer");
                                        handle_error(ESP_FAIL);
                                }
                        } else if (button_click_count == 2) {
                                xTimerStop(button_single_click_timer, 0);
                                button_click_count = 0;
                                homekit_reset_only_and_reboot();
                        }
                        break;
                }
                case BUTTON_EVENT_SINGLE_TIMEOUT:
                        if (button_click_count == 1) {
                                button_click_count = 0;
                                request_lcm_update_and_reboot();
                        } else {
                                button_click_count = 0;
                        }
                        break;
                }
        }

        vTaskDelete(NULL);
}

static void button_init(void) {
        button_click_count = 0;

        button_event_queue = xQueueCreate(10, sizeof(button_event_t));
        if (button_event_queue == NULL) {
                ESP_LOGE("BUTTON", "Failed to create button event queue");
                handle_error(ESP_ERR_NO_MEM);
        }

        button_single_click_timer = xTimerCreate(
                "btn_click", pdMS_TO_TICKS(DOUBLE_CLICK_TIMEOUT_MS), pdFALSE, NULL,
                button_single_click_timeout_callback);
        if (button_single_click_timer == NULL) {
                ESP_LOGE("BUTTON", "Failed to create button timer");
                handle_error(ESP_ERR_NO_MEM);
        }

        gpio_reset_pin(BUTTON_GPIO);

        gpio_config_t io_conf = {
                .pin_bit_mask = 1ULL << BUTTON_GPIO,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_ANYEDGE,
        };
        CHECK_ERROR(gpio_config(&io_conf));
        CHECK_ERROR(gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE));

        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE("BUTTON", "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
                handle_error(err);
        }

        CHECK_ERROR(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));
        CHECK_ERROR(gpio_intr_enable(BUTTON_GPIO));

        if (xTaskCreate(button_event_task, "button_task", 4096, NULL, 10, NULL) != pdPASS) {
                ESP_LOGE("BUTTON", "Failed to create button task");
                handle_error(ESP_ERR_NO_MEM);
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
        button_init();

        // Start WiFi op basis van NVS en geef callback door
        CHECK_ERROR(wifi_start(on_wifi_ready));
}
