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
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <homekit/homekit.h>

#include "esp32-wifi.h"

static const char *TAG = "WIFI";

#define CHECK_ERROR(x) do {                      \
    esp_err_t __err_rc = (x);                    \
    if (__err_rc != ESP_OK) {                    \
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(__err_rc)); \
        return __err_rc;                         \
    }                                            \
} while(0)

static void (*s_on_ready_cb)(void) = NULL;
static bool s_started = false;

static const char *LIFECYCLE_TAG = "LIFECYCLE";

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
