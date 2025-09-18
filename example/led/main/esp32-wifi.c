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
 
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

#include <driver/gpio.h>

#include <homekit/homekit.h>

static const char *TAG = "WIFI";
static const char *BUTTON_TAG = "BUTTON";

#define CHECK_ERROR(x) do {                      \
    esp_err_t __err_rc = (x);                    \
    if (__err_rc != ESP_OK) {                    \
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(__err_rc)); \
        return __err_rc;                         \
    }                                            \
} while(0)

static void restart_on_fatal(esp_err_t err) {
    ESP_LOGE(BUTTON_TAG, "Critical error, restarting device... (%s)", esp_err_to_name(err));
    esp_restart();
}

#define CHECK_FATAL(x) do {                                      \
    esp_err_t __err_rc = (x);                                    \
    if (__err_rc != ESP_OK) {                                    \
        ESP_LOGE(BUTTON_TAG, "Error: %s", esp_err_to_name(__err_rc)); \
        restart_on_fatal(__err_rc);                              \
    }                                                            \
} while(0)

static void (*s_on_ready_cb)(void) = NULL;
static bool s_started = false;

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

static esp_err_t nvs_load_wifi(char **out_ssid, char **out_pass) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed for namespace 'wifi_cfg': %s", esp_err_to_name(err));
        return err;
    }

    size_t len_ssid = 0, len_pass = 0;
    err = nvs_get_str(h, "wifi_ssid", NULL, &len_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS key 'wifi_ssid' not found: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }
    // password is allowed to be empty (open networks). If missing, treat as empty string.
    err = nvs_get_str(h, "wifi_password", NULL, &len_pass);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        len_pass = 1; // for terminating '\0'
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS key 'wifi_password' read error: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    char *ssid  = (char *)malloc(len_ssid);
    char *pass  = (char *)malloc(len_pass);
    if (!ssid || !pass) {
        if (ssid) free(ssid);
        if (pass) free(pass);
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }

    CHECK_ERROR(nvs_get_str(h, "wifi_ssid", ssid, &len_ssid));
    if (len_pass == 1) {
        pass[0] = '\0';
    } else {
        CHECK_ERROR(nvs_get_str(h, "wifi_password", pass, &len_pass));
    }

    nvs_close(h);
    *out_ssid = ssid;
    *out_pass = pass;
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA start -> connect");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t*)data;
                ESP_LOGW(TAG, "Disconnected (reason=%d). Reconnecting...", disc ? disc->reason : -1);
                esp_wifi_connect();
                break;
            }
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_on_ready_cb) {
            s_on_ready_cb();
        }
    }
}

esp_err_t wifi_start(void (*on_ready)(void)) {
    if (s_started) {
        s_on_ready_cb = on_ready; // update cb
        ESP_LOGI(TAG, "WiFi already started");
        return ESP_OK;
    }

    // laad SSID/PASS uit NVS
    char *ssid = NULL, *pass = NULL;
    esp_err_t err = nvs_load_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Kon WiFi config niet laden uit NVS");
        return err;
    }

    // netif + event loop
    CHECK_ERROR(esp_netif_init());
    CHECK_ERROR(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // events
    CHECK_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    CHECK_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // wifi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    CHECK_ERROR(esp_wifi_init(&cfg));
    CHECK_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    CHECK_ERROR(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wc = { 0 };
    // let op: wifi_config_t expects fixed-size arrays
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);

    // authmode: open indien password leeg, anders WPA2 threshold
    if (pass[0] == '\0') {
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    free(ssid);
    free(pass);

    CHECK_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc));
    CHECK_ERROR(esp_wifi_start());

    s_on_ready_cb = on_ready;
    s_started = true;

    ESP_LOGI(TAG, "WiFi start klaar (STA). Verbinden...");
    return ESP_OK;
}

esp_err_t wifi_stop(void) {
    if (!s_started) return ESP_OK;
    ESP_LOGI(TAG, "WiFi stoppen...");
    esp_err_t r1 = esp_wifi_stop();
    esp_err_t r2 = esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_err_t r3 = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler);
    s_started = false;
    if (r1 != ESP_OK) return r1;
    if (r2 != ESP_OK) return r2;
    if (r3 != ESP_OK) return r3;
    return ESP_OK;
}

static void request_lcm_update_and_reboot(void) {
    ESP_LOGI(BUTTON_TAG, "Single click detected: hand-off to factory updater");

    nvs_handle_t handle;
    CHECK_FATAL(nvs_open("lcm", NVS_READWRITE, &handle));
    CHECK_FATAL(nvs_set_u8(handle, "do_update", 1));
    CHECK_FATAL(nvs_commit(handle));
    nvs_close(handle);

    const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(BUTTON_TAG, "Factory partition not found");
        restart_on_fatal(ESP_FAIL);
    }

    CHECK_FATAL(esp_ota_set_boot_partition(factory));
    ESP_LOGI(BUTTON_TAG, "Rebooting into factory partition for update");
    esp_restart();
}

static void homekit_reset_only_and_reboot(void) {
    ESP_LOGI(BUTTON_TAG, "Double click detected: resetting HomeKit and rebooting");
    homekit_server_reset();
    esp_restart();
}

static void factory_reset_all_and_reboot(void) {
    ESP_LOGI(BUTTON_TAG, "Long press detected: factory reset initiated");
    homekit_server_reset();
    CHECK_FATAL(esp_wifi_restore());
    esp_restart();
}

static void button_single_click_timeout_callback(TimerHandle_t timer) {
    button_event_t event = {
        .type = BUTTON_EVENT_SINGLE_TIMEOUT,
        .time_us = esp_timer_get_time(),
    };

    if (xQueueSend(button_event_queue, &event, 0) != pdPASS) {
        ESP_LOGW(BUTTON_TAG, "Single click timeout event queue full");
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
                    ESP_LOGE(BUTTON_TAG, "Failed to start single click timer");
                    restart_on_fatal(ESP_FAIL);
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

void button_init(void) {
    button_click_count = 0;

    button_event_queue = xQueueCreate(10, sizeof(button_event_t));
    if (button_event_queue == NULL) {
        ESP_LOGE(BUTTON_TAG, "Failed to create button event queue");
        restart_on_fatal(ESP_ERR_NO_MEM);
    }

    button_single_click_timer = xTimerCreate(
            "btn_click", pdMS_TO_TICKS(DOUBLE_CLICK_TIMEOUT_MS), pdFALSE, NULL,
            button_single_click_timeout_callback);
    if (button_single_click_timer == NULL) {
        ESP_LOGE(BUTTON_TAG, "Failed to create button timer");
        restart_on_fatal(ESP_ERR_NO_MEM);
    }

    gpio_reset_pin(BUTTON_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    CHECK_FATAL(gpio_config(&io_conf));
    CHECK_FATAL(gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE));

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BUTTON_TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        restart_on_fatal(err);
    }

    CHECK_FATAL(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));
    CHECK_FATAL(gpio_intr_enable(BUTTON_GPIO));

    if (xTaskCreate(button_event_task, "button_task", 4096, NULL, 10, NULL) != pdPASS) {
        ESP_LOGE(BUTTON_TAG, "Failed to create button task");
        restart_on_fatal(ESP_ERR_NO_MEM);
    }
}
