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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_rom_gpio.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/portmacro.h>
#include <driver/gpio.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h"

static const char *WIFI_TAG = "WIFI";
static const char *BUTTON_TAG = "BUTTON";
static const char *LIFECYCLE_TAG = "LIFECYCLE";

#define WIFI_CHECK(call) do { \
    esp_err_t __wifi_err = (call); \
    if (__wifi_err != ESP_OK) { \
        ESP_LOGE(WIFI_TAG, "Error: %s", esp_err_to_name(__wifi_err)); \
        return __wifi_err; \
    } \
} while (0)

static void (*s_wifi_on_ready_cb)(void) = NULL;
static bool s_wifi_started = false;

#define BUTTON_QUEUE_LENGTH 10
#define DEFAULT_DEBOUNCE_US 2000
#define DEFAULT_DOUBLE_CLICK_US 400000
#define DEFAULT_LONG_PRESS_US 2000000

static char s_fw_revision[LIFECYCLE_FW_REVISION_MAX_LEN];
static bool s_fw_revision_initialized = false;

static lifecycle_button_config_t s_button_cfg = {0};
static bool s_button_initialized = false;
static QueueHandle_t s_button_evt_queue = NULL;
static volatile int64_t s_last_isr_time_us = 0;
static int64_t s_press_start_time_us = -1;
static int64_t s_last_release_time_us = 0;
static int s_press_count = 0;
static bool s_waiting_for_second_press = false;
static bool s_double_press_detected = false;

static esp_err_t nvs_load_wifi(char **out_ssid, char **out_pass) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "NVS open failed for namespace 'wifi_cfg': %s", esp_err_to_name(err));
        return err;
    }

    size_t len_ssid = 0;
    size_t len_pass = 0;
    err = nvs_get_str(handle, "wifi_ssid", NULL, &len_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "NVS key 'wifi_ssid' not found: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, "wifi_password", NULL, &len_pass);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        len_pass = 1;
    } else if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "NVS key 'wifi_password' read error: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    char *ssid = (char *)malloc(len_ssid);
    char *pass = (char *)malloc(len_pass);
    if (ssid == NULL || pass == NULL) {
        free(ssid);
        free(pass);
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(handle, "wifi_ssid", ssid, &len_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to read wifi_ssid: %s", esp_err_to_name(err));
        free(ssid);
        free(pass);
        nvs_close(handle);
        return err;
    }

    if (len_pass == 1) {
        pass[0] = '\0';
    } else {
        err = nvs_get_str(handle, "wifi_password", pass, &len_pass);
        if (err != ESP_OK) {
            ESP_LOGE(WIFI_TAG, "Failed to read wifi_password: %s", esp_err_to_name(err));
            free(ssid);
            free(pass);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);
    *out_ssid = ssid;
    *out_pass = pass;
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(WIFI_TAG, "STA start -> connect");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)data;
                ESP_LOGW(WIFI_TAG, "Disconnected (reason=%d). Reconnecting...", disc ? disc->reason : -1);
                esp_wifi_connect();
                break;
            }
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(WIFI_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_on_ready_cb != NULL) {
            s_wifi_on_ready_cb();
        }
    }
}

esp_err_t wifi_start(void (*on_ready)(void)) {
    if (s_wifi_started) {
        s_wifi_on_ready_cb = on_ready;
        ESP_LOGI(WIFI_TAG, "WiFi already started");
        return ESP_OK;
    }

    char *ssid = NULL;
    char *pass = NULL;
    esp_err_t err = nvs_load_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Kon WiFi config niet laden uit NVS");
        return err;
    }

    bool pass_empty = (pass[0] == '\0');
    wifi_config_t wc = (wifi_config_t){ 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    free(ssid);
    free(pass);

    if (pass_empty) {
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(WIFI_TAG, "Failed to init netif: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(WIFI_TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }
    esp_netif_create_default_wifi_sta();

    WIFI_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    WIFI_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_CHECK(esp_wifi_init(&cfg));
    WIFI_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    WIFI_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    WIFI_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    WIFI_CHECK(esp_wifi_start());

    s_wifi_on_ready_cb = on_ready;
    s_wifi_started = true;

    ESP_LOGI(WIFI_TAG, "WiFi start klaar (STA). Verbinden...");
    return ESP_OK;
}

esp_err_t wifi_stop(void) {
    if (!s_wifi_started) {
        return ESP_OK;
    }

    ESP_LOGI(WIFI_TAG, "WiFi stoppen...");
    esp_err_t stop_err = esp_wifi_stop();
    esp_err_t unregister_wifi = esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_err_t unregister_ip = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    s_wifi_started = false;
    s_wifi_on_ready_cb = NULL;

    if (stop_err != ESP_OK) {
        return stop_err;
    }
    if (unregister_wifi != ESP_OK) {
        return unregister_wifi;
    }
    if (unregister_ip != ESP_OK) {
        return unregister_ip;
    }

    return ESP_OK;
}

esp_err_t lifecycle_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(LIFECYCLE_TAG, "NVS init issue (%s), erasing...", esp_err_to_name(ret));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(LIFECYCLE_TAG, "Failed to erase NVS: %s", esp_err_to_name(erase_err));
            return erase_err;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(LIFECYCLE_TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t lifecycle_init_firmware_revision(homekit_characteristic_t *revision,
                                           const char *fallback_version) {
    if (revision == NULL || fallback_version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    const char *current_version = fallback_version;
    if (desc && strlen(desc->version) > 0) {
        current_version = desc->version;
    }
    if (current_version == NULL || current_version[0] == '\0') {
        current_version = "0.0.0";
    }

    strlcpy(s_fw_revision, current_version, sizeof(s_fw_revision));
    s_fw_revision_initialized = true;

    esp_err_t status = ESP_OK;
    bool used_stored_value = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("fwcfg", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        size_t required = sizeof(s_fw_revision);
        err = nvs_get_str(handle, "installed_ver", s_fw_revision, &required);
        if (err == ESP_OK && s_fw_revision[0] != '\0') {
            used_stored_value = true;
        } else if (err == ESP_ERR_NVS_NOT_FOUND || s_fw_revision[0] == '\0') {
            strlcpy(s_fw_revision, current_version, sizeof(s_fw_revision));
            esp_err_t set_err = nvs_set_str(handle, "installed_ver", s_fw_revision);
            if (set_err != ESP_OK) {
                ESP_LOGW(LIFECYCLE_TAG, "Failed to store firmware revision: %s",
                         esp_err_to_name(set_err));
                status = set_err;
            } else {
                esp_err_t commit_err = nvs_commit(handle);
                if (commit_err != ESP_OK) {
                    ESP_LOGW(LIFECYCLE_TAG, "Commit of firmware revision failed: %s",
                             esp_err_to_name(commit_err));
                    status = commit_err;
                }
            }
        } else {
            ESP_LOGW(LIFECYCLE_TAG, "Reading stored firmware revision failed: %s",
                     esp_err_to_name(err));
            strlcpy(s_fw_revision, current_version, sizeof(s_fw_revision));
        }
        nvs_close(handle);
    } else {
        ESP_LOGW(LIFECYCLE_TAG, "Unable to open fwcfg namespace: %s", esp_err_to_name(err));
        status = err;
    }

    revision->value.string_value = s_fw_revision;
    revision->value.is_static = true;

    ESP_LOGI(LIFECYCLE_TAG, "Firmware revision set to %s (%s)",
             s_fw_revision, used_stored_value ? "stored" : "runtime");

    return status;
}

const char *lifecycle_get_firmware_revision_string(void) {
    if (s_fw_revision_initialized && s_fw_revision[0] != '\0') {
        return s_fw_revision;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc && desc->version[0] != '\0') {
        return desc->version;
    }

    return NULL;
}

void lifecycle_handle_ota_trigger(homekit_characteristic_t *characteristic,
                                  homekit_value_t value) {
    if (characteristic == NULL) {
        return;
    }
    if (value.format != homekit_format_bool) {
        ESP_LOGW(LIFECYCLE_TAG, "Invalid OTA trigger format: %d", value.format);
        return;
    }

    bool requested = value.bool_value;
    characteristic->value.bool_value = false;
    homekit_characteristic_notify(characteristic, HOMEKIT_BOOL(characteristic->value.bool_value));

    if (requested) {
        ESP_LOGI(LIFECYCLE_TAG, "HomeKit requested firmware update");
        lifecycle_request_update_and_reboot();
    }
}

static void dispatch_button_event(lifecycle_button_event_t event) {
    if (!s_button_initialized) {
        return;
    }

    if (s_button_cfg.event_callback) {
        s_button_cfg.event_callback(event, s_button_cfg.event_context);
    }

    lifecycle_button_action_t action = LIFECYCLE_BUTTON_ACTION_NONE;
    switch (event) {
        case LIFECYCLE_BUTTON_EVENT_SINGLE:
            action = s_button_cfg.single_action;
            break;
        case LIFECYCLE_BUTTON_EVENT_DOUBLE:
            action = s_button_cfg.double_action;
            break;
        case LIFECYCLE_BUTTON_EVENT_LONG:
            action = s_button_cfg.long_action;
            break;
        default:
            break;
    }

    switch (action) {
        case LIFECYCLE_BUTTON_ACTION_NONE:
            break;
        case LIFECYCLE_BUTTON_ACTION_REQUEST_UPDATE:
            lifecycle_request_update_and_reboot();
            break;
        case LIFECYCLE_BUTTON_ACTION_RESET_HOMEKIT:
            lifecycle_reset_homekit_and_reboot();
            break;
        case LIFECYCLE_BUTTON_ACTION_FACTORY_RESET:
            lifecycle_factory_reset_and_reboot();
            break;
        default:
            break;
    }
}

static void IRAM_ATTR button_isr_handler(void *arg) {
    if (s_button_evt_queue == NULL) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_isr_time_us) < s_button_cfg.debounce_us) {
        return;
    }
    s_last_isr_time_us = now_us;

    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t higher_wakeup = pdFALSE;
    xQueueSendFromISR(s_button_evt_queue, &gpio_num, &higher_wakeup);
    if (higher_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void button_task(void *pvParameter) {
    const int64_t double_window_us = s_button_cfg.double_click_us;
    const int64_t long_press_threshold_us = s_button_cfg.long_press_us;

    uint32_t io_num;
    ESP_LOGI(BUTTON_TAG, "Button task started on GPIO %d", s_button_cfg.gpio);
    while (true) {
        if (xQueueReceive(s_button_evt_queue, &io_num, pdMS_TO_TICKS(10)) == pdTRUE) {
            int64_t now_us = esp_timer_get_time();
            bool pressed = gpio_get_level(io_num) == 0;

            if (pressed) {
                s_press_start_time_us = now_us;
                s_press_count++;
                s_double_press_detected = false;
                s_waiting_for_second_press = false;
            } else {
                if (s_press_start_time_us < 0) {
                    continue;
                }

                int64_t press_duration_us = now_us - s_press_start_time_us;
                s_press_start_time_us = -1;

                if (press_duration_us >= long_press_threshold_us) {
                    s_waiting_for_second_press = false;
                    s_press_count = 0;
                    ESP_LOGI(BUTTON_TAG, "Long press detected");
                    dispatch_button_event(LIFECYCLE_BUTTON_EVENT_LONG);
                } else {
                    if (s_press_count == 1) {
                        s_waiting_for_second_press = true;
                        s_last_release_time_us = now_us;
                    } else if (s_press_count == 2) {
                        int64_t diff_us = now_us - s_last_release_time_us;
                        if (diff_us <= double_window_us) {
                            s_double_press_detected = true;
                            ESP_LOGI(BUTTON_TAG, "Double press detected");
                            dispatch_button_event(LIFECYCLE_BUTTON_EVENT_DOUBLE);
                        }
                        s_waiting_for_second_press = false;
                        s_press_count = 0;
                    } else if (s_press_count > 2) {
                        s_waiting_for_second_press = false;
                        s_press_count = 0;
                    }
                }
            }
        }

        if (s_waiting_for_second_press) {
            int64_t now_us = esp_timer_get_time();
            if ((now_us - s_last_release_time_us) > s_button_cfg.double_click_us) {
                s_waiting_for_second_press = false;
                if (!s_double_press_detected && s_press_count == 1) {
                    s_press_count = 0;
                    ESP_LOGI(BUTTON_TAG, "Single press detected");
                    dispatch_button_event(LIFECYCLE_BUTTON_EVENT_SINGLE);
                } else {
                    s_press_count = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t lifecycle_button_init(const lifecycle_button_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_button_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_button_cfg = *config;
    if (s_button_cfg.debounce_us == 0) {
        s_button_cfg.debounce_us = DEFAULT_DEBOUNCE_US;
    }
    if (s_button_cfg.double_click_us == 0) {
        s_button_cfg.double_click_us = DEFAULT_DOUBLE_CLICK_US;
    }
    if (s_button_cfg.long_press_us == 0) {
        s_button_cfg.long_press_us = DEFAULT_LONG_PRESS_US;
    }

    s_button_evt_queue = xQueueCreate(BUTTON_QUEUE_LENGTH, sizeof(uint32_t));
    if (s_button_evt_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_reset_pin(s_button_cfg.gpio);
    esp_rom_gpio_pad_select_gpio(s_button_cfg.gpio);

    gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << s_button_cfg.gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&button_conf);
    if (err != ESP_OK) {
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return err;
    }

    err = gpio_set_intr_type(s_button_cfg.gpio, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return err;
    }

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return err;
    }

    bool handler_added = false;
    err = gpio_isr_handler_add(s_button_cfg.gpio, button_isr_handler, (void *)s_button_cfg.gpio);
    if (err == ESP_OK) {
        handler_added = true;
    } else {
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return err;
    }

    err = gpio_intr_enable(s_button_cfg.gpio);
    if (err != ESP_OK) {
        if (handler_added) {
            gpio_isr_handler_remove(s_button_cfg.gpio);
        }
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return err;
    }

    s_last_isr_time_us = 0;
    s_press_start_time_us = -1;
    s_press_count = 0;
    s_waiting_for_second_press = false;
    s_double_press_detected = false;

    if (xTaskCreate(button_task, "lifecycle_button", 4096, NULL, 10, NULL) != pdPASS) {
        gpio_intr_disable(s_button_cfg.gpio);
        if (handler_added) {
            gpio_isr_handler_remove(s_button_cfg.gpio);
        }
        vQueueDelete(s_button_evt_queue);
        s_button_evt_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_button_initialized = true;
    ESP_LOGI(BUTTON_TAG, "Lifecycle button initialised on GPIO %d", s_button_cfg.gpio);
    return ESP_OK;
}

void lifecycle_request_update_and_reboot(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Requesting Lifecycle Manager update and reboot");

    nvs_handle_t handle;
    esp_err_t err = nvs_open("lcm", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(LIFECYCLE_TAG, "Failed to open NVS namespace 'lcm': %s", esp_err_to_name(err));
    } else {
        err = nvs_set_u8(handle, "do_update", 1);
        if (err != ESP_OK) {
            ESP_LOGE(LIFECYCLE_TAG, "Failed to set do_update flag: %s", esp_err_to_name(err));
        } else {
            err = nvs_commit(handle);
            if (err != ESP_OK) {
                ESP_LOGE(LIFECYCLE_TAG, "Failed to commit update flag: %s", esp_err_to_name(err));
            }
        }
        nvs_close(handle);
    }

    const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(LIFECYCLE_TAG, "Factory partition not found, rebooting to current app");
        esp_restart();
        return;
    }

    err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(LIFECYCLE_TAG, "Failed to set factory partition for boot: %s", esp_err_to_name(err));
        esp_restart();
        return;
    }

    ESP_LOGI(LIFECYCLE_TAG, "Rebooting into factory partition for update");
    esp_restart();
    return;
}

void lifecycle_reset_homekit_and_reboot(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Resetting HomeKit state and rebooting");
    homekit_server_reset();
    esp_restart();
    return;
}

static void erase_wifi_credentials(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Clearing Wi-Fi credentials from NVS namespace 'wifi_cfg'");

    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to open wifi_cfg namespace: %s", esp_err_to_name(err));
        return;
    }

    esp_err_t erase_err = nvs_erase_key(handle, "wifi_ssid");
    if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to erase wifi_ssid: %s", esp_err_to_name(erase_err));
    }

    erase_err = nvs_erase_key(handle, "wifi_password");
    if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to erase wifi_password: %s", esp_err_to_name(erase_err));
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to commit Wi-Fi credential erase: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

void lifecycle_factory_reset_and_reboot(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Performing factory reset (HomeKit + Wi-Fi)");
    homekit_server_reset();
    erase_wifi_credentials();
    esp_err_t err = esp_wifi_restore();
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "esp_wifi_restore failed: %s", esp_err_to_name(err));
    }
    esp_restart();
    return;
}
