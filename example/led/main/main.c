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
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "github_update.h"
#include "led_indicator.h"
#include "esp32-wifi.h"   // <— nieuwe wifi module

static const char *TAG = "INFORMATION";

// Custom error handling macro
#define CHECK_ERROR(x) do {                        \
                esp_err_t __err_rc = (x);          \
                if (__err_rc != ESP_OK) {          \
                        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(__err_rc)); \
                        handle_error(__err_rc);    \
                }                                  \
} while(0)

// LED control
#define LED_GPIO CONFIG_ESP_LED_GPIO
#define LED_BLINK_ON_MS 200
#define LED_BLINK_OFF_MS 200

// Boot button configuration
#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_ACTIVE_LEVEL 0
#define BUTTON_DEBOUNCE_MS 25
#define BUTTON_MIN_PRESS_MS 50
#define BUTTON_DOUBLE_CLICK_MS 400
#define BUTTON_LONG_PRESS_MS 3000

static bool led_on = false;
static TaskHandle_t s_led_blink_task = NULL;
static bool s_led_blinking = false;
static TaskHandle_t s_update_task = NULL;
static TaskHandle_t s_homekit_reset_task = NULL;
static TaskHandle_t s_factory_reset_task = NULL;

void handle_error(esp_err_t err) {
        // Simpel fallback: herstart device bij kritieke fouten
        ESP_LOGE("ERROR", "Critical error, restarting device... (%s)", esp_err_to_name(err));
        esp_restart();
}

static void led_write(bool on) {
        gpio_set_level(LED_GPIO, on ? 1 : 0);
        led_on = on;
}

static void led_blink_task(void *args) {
        (void)args;
        while (s_led_blinking) {
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_ON_MS));
                if (!s_led_blinking) {
                        break;
                }
                gpio_set_level(LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_OFF_MS));
        }

        s_led_blink_task = NULL;
        gpio_set_level(LED_GPIO, led_on ? 1 : 0);
        vTaskDelete(NULL);
}

void led_blinking_start(void) {
        if (s_led_blinking) {
                return;
        }
        s_led_blinking = true;
        if (xTaskCreate(led_blink_task, "led_blink", 2048, NULL, 5, &s_led_blink_task) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create LED blink task");
                s_led_blinking = false;
                s_led_blink_task = NULL;
        }
}

void led_blinking_stop(void) {
        if (!s_led_blinking) {
                return;
        }
        s_led_blinking = false;
        while (s_led_blink_task) {
                vTaskDelay(pdMS_TO_TICKS(10));
        }
        gpio_set_level(LED_GPIO, led_on ? 1 : 0);
}

static void reset_wifi_credentials(void) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "WiFi config namespace not found");
                return;
        }
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open wifi_cfg namespace: %s", esp_err_to_name(err));
                return;
        }

        err = nvs_erase_all(handle);
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to erase WiFi credentials: %s", esp_err_to_name(err));
                nvs_close(handle);
                return;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit WiFi credentials erase: %s", esp_err_to_name(err));
        } else {
                ESP_LOGI(TAG, "WiFi credentials cleared");
        }
        nvs_close(handle);
}

static void update_task(void *arg) {
        (void)arg;
        char repo[96] = {0};
        bool prerelease = false;

        ESP_LOGI(TAG, "Button single click → firmware update check");
        if (!load_fw_config(repo, sizeof(repo), &prerelease)) {
                ESP_LOGW(TAG, "No firmware configuration stored; skipping update");
        } else {
                esp_err_t err = github_update_if_needed(repo, prerelease);
                if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Firmware update failed: %s", esp_err_to_name(err));
                } else {
                        ESP_LOGI(TAG, "Firmware update completed");
                }
        }

        s_update_task = NULL;
        vTaskDelete(NULL);
}

static void start_update_task(void) {
        if (s_update_task) {
                ESP_LOGW(TAG, "Update already running");
                return;
        }
        if (xTaskCreate(update_task, "github_update", 8192, NULL, 5, &s_update_task) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create update task");
                s_update_task = NULL;
        }
}

static void homekit_reset_task(void *arg) {
        (void)arg;
        ESP_LOGI(TAG, "Button double click → reset HomeKit pairing");
        homekit_server_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_homekit_reset_task = NULL;
        ESP_LOGI(TAG, "Restarting after HomeKit reset");
        esp_restart();
        vTaskDelete(NULL);
}

static void start_homekit_reset_task(void) {
        if (s_homekit_reset_task) {
                ESP_LOGW(TAG, "HomeKit reset already running");
                return;
        }
        if (xTaskCreate(homekit_reset_task, "homekit_reset", 4096, NULL, 5, &s_homekit_reset_task) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create HomeKit reset task");
                s_homekit_reset_task = NULL;
        }
}

static void factory_reset_task(void *arg) {
        (void)arg;
        ESP_LOGI(TAG, "Button long press → factory reset");
        homekit_server_reset();
        reset_wifi_credentials();
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_factory_reset_task = NULL;
        ESP_LOGI(TAG, "Restarting after factory reset");
        esp_restart();
        vTaskDelete(NULL);
}

static void start_factory_reset_task(void) {
        if (s_factory_reset_task) {
                ESP_LOGW(TAG, "Factory reset already running");
                return;
        }
        if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 5, &s_factory_reset_task) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create factory reset task");
                s_factory_reset_task = NULL;
        }
}

static void button_task(void *pvParameter) {
        (void)pvParameter;
        bool pressed = false;
        TickType_t press_start = 0;
        TickType_t last_release = 0;
        uint8_t click_count = 0;
        bool long_press_triggered = false;

        const TickType_t debounce_ticks = pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS);
        const TickType_t min_press_ticks = pdMS_TO_TICKS(BUTTON_MIN_PRESS_MS);
        const TickType_t double_click_ticks = pdMS_TO_TICKS(BUTTON_DOUBLE_CLICK_MS);
        const TickType_t long_press_ticks = pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS);

        while (1) {
                bool level = gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE_LEVEL;
                TickType_t now = xTaskGetTickCount();

                if (level) {
                        if (!pressed) {
                                pressed = true;
                                press_start = now;
                                long_press_triggered = false;
                                ESP_LOGD(TAG, "Button press detected");
                        } else if (!long_press_triggered && (now - press_start) >= long_press_ticks) {
                                long_press_triggered = true;
                                click_count = 0;
                                start_factory_reset_task();
                        }
                } else {
                        if (pressed) {
                                pressed = false;
                                TickType_t press_duration = now - press_start;
                                ESP_LOGD(TAG, "Button released after %lu ticks", (unsigned long)press_duration);
                                if (!long_press_triggered && press_duration >= min_press_ticks) {
                                        click_count++;
                                        last_release = now;
                                        if (click_count >= 2) {
                                                click_count = 0;
                                                start_homekit_reset_task();
                                        }
                                }
                        } else if (click_count > 0 && (now - last_release) >= double_click_ticks) {
                                click_count = 0;
                                start_update_task();
                        }
                }

                vTaskDelay(debounce_ticks);
        }
}

// All GPIO Settings
static void gpio_init(void) {
        gpio_reset_pin(LED_GPIO); // Reset GPIO pin to avoid conflicts
        gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(LED_GPIO, led_on ? 1 : 0);

        gpio_reset_pin(BUTTON_GPIO);
        gpio_config_t btn_conf = {
                .pin_bit_mask = 1ULL << BUTTON_GPIO,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&btn_conf);
}

// Accessory identification
void accessory_identify_task(void *args) {
        (void)args;
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
        ESP_LOGI(TAG, "Accessory identify");
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
        led_write(value.bool_value);
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
        ESP_LOGI(TAG, "Starting HomeKit server...");
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

        // Start WiFi op basis van NVS en geef callback door
        CHECK_ERROR(wifi_start(on_wifi_ready));

        if (xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create button task");
        }
}
