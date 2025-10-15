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
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <mdns.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h"

static const char *WIFI_TAG = "WIFI";
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
static esp_netif_t *s_wifi_netif = NULL;

static const uint32_t k_post_reset_magic = 0xC0DEC0DE;
#ifndef CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS
#define CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS 5000
#endif

#if CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS <= 0
#error "CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS must be a positive value"
#endif

static const uint64_t k_restart_counter_timeout_us =
    ((uint64_t)CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS) * 1000ULL;
static const uint64_t k_restart_counter_timeout_ms =
    k_restart_counter_timeout_us / 1000ULL;
static const char *k_restart_counter_namespace = "lcm";
static const char *k_restart_counter_key = "restart_count";

RTC_DATA_ATTR static struct {
    uint32_t magic;
    uint32_t reason;
    uint32_t restart_count;
} s_post_reset_state;

static char s_fw_revision[LIFECYCLE_FW_REVISION_MAX_LEN];
static bool s_fw_revision_initialized = false;
static esp_timer_handle_t s_restart_counter_timer = NULL;
static bool s_nvs_initialized = false;

void wifi_config_shutdown(void) __attribute__((weak));

static void lifecycle_log_step(const char *step);
static void lifecycle_mark_post_reset(lifecycle_post_reset_reason_t reason);
static lifecycle_post_reset_reason_t lifecycle_peek_post_reset_reason(void);
static void lifecycle_clear_post_reset_state(void);
static uint32_t lifecycle_increment_restart_counter(void);
static void lifecycle_reset_restart_counter(void);
static void lifecycle_restart_counter_timeout(void *arg);
static void lifecycle_schedule_restart_counter_timeout(const char *log_tag);
static void lifecycle_shutdown_homekit(bool reset_store);
static void lifecycle_stop_provisioning_servers(void);
static void lifecycle_perform_common_shutdown(bool reset_homekit_store);
static esp_err_t load_restart_counter_from_nvs(uint32_t *out_value, const char *log_tag);
static esp_err_t save_restart_counter_to_nvs(uint32_t value, const char *log_tag);
static esp_err_t lifecycle_ensure_nvs_initialized(const char *log_tag);

static esp_err_t nvs_load_wifi(char **out_ssid, char **out_pass) {
    esp_err_t init_err = lifecycle_ensure_nvs_initialized(WIFI_TAG);
    if (init_err != ESP_OK) {
        return init_err;
    }

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

static void lifecycle_log_step(const char *step) {
    if (step == NULL) {
        return;
    }
    ESP_LOGI(LIFECYCLE_TAG, "[lifecycle] %s", step);
}

static void lifecycle_mark_post_reset(lifecycle_post_reset_reason_t reason) {
    s_post_reset_state.magic = k_post_reset_magic;
    s_post_reset_state.reason = (uint32_t)reason;
}

static lifecycle_post_reset_reason_t lifecycle_peek_post_reset_reason(void) {
    if (s_post_reset_state.magic != k_post_reset_magic) {
        return LIFECYCLE_POST_RESET_NONE;
    }

    lifecycle_post_reset_reason_t reason =
            (lifecycle_post_reset_reason_t)s_post_reset_state.reason;
    if (reason < LIFECYCLE_POST_RESET_NONE ||
            reason > LIFECYCLE_POST_RESET_REASON_UPDATE) {
        return LIFECYCLE_POST_RESET_NONE;
    }

    return reason;
}

static void lifecycle_clear_post_reset_state(void) {
    s_post_reset_state.magic = 0;
    s_post_reset_state.reason = LIFECYCLE_POST_RESET_NONE;
}

static esp_err_t lifecycle_ensure_nvs_initialized(const char *log_tag) {
    if (s_nvs_initialized) {
        return ESP_OK;
    }

    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(tag,
                 "[lifecycle] NVS init issue (%s); attempting erase",
                 esp_err_to_name(ret));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(tag,
                     "[lifecycle] Failed to erase NVS while recovering init: %s",
                     esp_err_to_name(erase_err));
            return erase_err;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to initialise NVS: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    s_nvs_initialized = true;
    return ESP_OK;
}

static esp_err_t load_restart_counter_from_nvs(uint32_t *out_value, const char *log_tag) {
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    nvs_handle_t handle;
    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;

    esp_err_t init_err = lifecycle_ensure_nvs_initialized(tag);
    if (init_err != ESP_OK) {
        *out_value = 0;
        return init_err;
    }

    esp_err_t err = nvs_open(k_restart_counter_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to open NVS namespace '%s' for restart counter: %s",
                 k_restart_counter_namespace,
                 esp_err_to_name(err));
        *out_value = 0;
        return err;
    }

    err = nvs_get_u32(handle, k_restart_counter_key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to read restart counter from NVS: %s",
                 esp_err_to_name(err));
    }

    nvs_close(handle);
    *out_value = value;
    return err;
}

static esp_err_t save_restart_counter_to_nvs(uint32_t value, const char *log_tag) {
    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;

    esp_err_t init_err = lifecycle_ensure_nvs_initialized(tag);
    if (init_err != ESP_OK) {
        return init_err;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_restart_counter_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to open NVS namespace '%s' for restart counter: %s",
                 k_restart_counter_namespace,
                 esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, k_restart_counter_key, value);
    if (err != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to store restart counter in NVS: %s",
                 esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    esp_err_t commit_err = nvs_commit(handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(tag,
                 "[lifecycle] Failed to commit restart counter to NVS: %s",
                 esp_err_to_name(commit_err));
        err = commit_err;
    }

    nvs_close(handle);
    return err;
}

static uint32_t lifecycle_increment_restart_counter(void) {
    uint32_t previous = s_post_reset_state.restart_count;
    if (s_post_reset_state.restart_count == UINT32_MAX) {
        s_post_reset_state.restart_count = 0;
    }

    s_post_reset_state.restart_count++;
    ESP_LOGD(LIFECYCLE_TAG,
            "[lifecycle] restart counter incremented (previous=%" PRIu32 ", current=%" PRIu32 ")",
            previous,
            s_post_reset_state.restart_count);
    return s_post_reset_state.restart_count;
}

static void lifecycle_reset_restart_counter(void) {
    if (s_post_reset_state.restart_count != 0U) {
        ESP_LOGD(LIFECYCLE_TAG, "[lifecycle] restart counter reset");
    }
    s_post_reset_state.restart_count = 0;

    esp_err_t err = save_restart_counter_to_nvs(0, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG,
                 "[lifecycle] Failed to reset restart counter in NVS: %s",
                 esp_err_to_name(err));
    }
}

static void lifecycle_restart_counter_timeout(void *arg) {
    const char *tag = (const char *)arg;
    if (tag == NULL) {
        tag = LIFECYCLE_TAG;
    }

    if (s_post_reset_state.restart_count != 0U) {
        ESP_LOGI(tag,
                "[lifecycle] No restart detected within %llu ms; clearing counter",
                (unsigned long long)k_restart_counter_timeout_ms);
    }

    lifecycle_reset_restart_counter();
}

static void lifecycle_schedule_restart_counter_timeout(const char *log_tag) {
    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;

    if (s_restart_counter_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = lifecycle_restart_counter_timeout,
            .arg = (void *)LIFECYCLE_TAG,
            .name = "restart_cnt_reset",
        };

        esp_err_t err = esp_timer_create(&timer_args, &s_restart_counter_timer);
        if (err != ESP_OK) {
            ESP_LOGE(tag,
                    "[lifecycle] Failed to create restart counter timer: %s",
                    esp_err_to_name(err));
            return;
        }
    }

    esp_err_t err = esp_timer_stop(s_restart_counter_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(tag,
                "[lifecycle] Failed to stop restart counter timer: %s",
                esp_err_to_name(err));
    }

    err = esp_timer_start_once(s_restart_counter_timer, k_restart_counter_timeout_us);
    if (err != ESP_OK) {
        ESP_LOGE(tag,
                "[lifecycle] Failed to start restart counter timer: %s",
                esp_err_to_name(err));
        return;
    }

    ESP_LOGD(tag,
            "[lifecycle] Restart counter timeout armed for %llu ms",
            (unsigned long long)k_restart_counter_timeout_ms);
}

void lifecycle_log_post_reset_state(const char *log_tag) {
    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;
    uint32_t persisted_count = 0;
    esp_err_t load_err = load_restart_counter_from_nvs(&persisted_count, tag);
    if (load_err == ESP_OK) {
        if (persisted_count > s_post_reset_state.restart_count) {
            s_post_reset_state.restart_count = persisted_count;
        }
    } else {
        ESP_LOGW(tag,
                 "[lifecycle] Failed to load restart counter from NVS (err=%s); using RTC value",
                 esp_err_to_name(load_err));
    }

    uint32_t restart_count = lifecycle_increment_restart_counter();

    esp_err_t save_err = save_restart_counter_to_nvs(restart_count, tag);
    if (save_err != ESP_OK) {
        ESP_LOGW(tag,
                 "[lifecycle] Failed to persist restart counter to NVS (err=%s)",
                 esp_err_to_name(save_err));
    }

    ESP_LOGI(tag, "[lifecycle] consecutive_restart_count=%" PRIu32, restart_count);

    lifecycle_schedule_restart_counter_timeout(tag);

    if (restart_count >= 10U) {
        ESP_LOGW(tag, "[lifecycle] Detected 10 consecutive restarts; performing factory reset countdown");
        for (int i = 10; i >= 0; --i) {
            ESP_LOGW(tag, "[lifecycle] Factory reset in %d", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        lifecycle_reset_restart_counter();
        lifecycle_factory_reset_and_reboot();
        return;
    }

    lifecycle_post_reset_reason_t reason = lifecycle_peek_post_reset_reason();
    const char *reason_str = "none";

    switch (reason) {
        case LIFECYCLE_POST_RESET_NONE:
            reason_str = "none";
            break;
        case LIFECYCLE_POST_RESET_REASON_HOMEKIT:
            reason_str = "homekit";
            break;
        case LIFECYCLE_POST_RESET_REASON_FACTORY:
            reason_str = "factory";
            break;
        case LIFECYCLE_POST_RESET_REASON_UPDATE:
            reason_str = "update";
            break;
        default:
            reason_str = "unknown";
            break;
    }

    ESP_LOGI(tag, "[lifecycle] post_reset_flag=%s", reason_str);
    lifecycle_clear_post_reset_state();
}

static void lifecycle_shutdown_homekit(bool reset_store) {
    lifecycle_log_step("stop_homekit");
    ESP_LOGD(LIFECYCLE_TAG,
             "HomeKit stop requested; relying on network teardown for active sessions");

    lifecycle_log_step("wait_hap_clients");
    vTaskDelay(pdMS_TO_TICKS(100));

    lifecycle_log_step("stop_mdns");
    esp_err_t mdns_err = mdns_service_remove("_hap", "_tcp");
    if (mdns_err != ESP_OK && mdns_err != ESP_ERR_NOT_FOUND &&
            mdns_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to remove mDNS service: %s",
                 esp_err_to_name(mdns_err));
    }

    mdns_free();

    if (reset_store) {
        lifecycle_log_step("reset_homekit_store");
        homekit_server_reset();
    }
}

static void lifecycle_stop_provisioning_servers(void) {
    if (wifi_config_shutdown) {
        lifecycle_log_step("stop_provisioning");
        wifi_config_shutdown();
    } else {
        ESP_LOGD(LIFECYCLE_TAG,
                 "No provisioning shutdown handler registered; skipping");
    }
}

static void lifecycle_perform_common_shutdown(bool reset_homekit_store) {
    lifecycle_shutdown_homekit(reset_homekit_store);
    lifecycle_stop_provisioning_servers();

    lifecycle_log_step("stop_wifi");
    esp_err_t stop_err = wifi_stop();
    if (stop_err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "wifi_stop failed: %s", esp_err_to_name(stop_err));
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
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(WIFI_TAG, "WiFi configuratie niet gevonden in NVS; provisioning vereist");
        return err;
    } else if (err != ESP_OK) {
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
    if (s_wifi_netif == NULL) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_netif == NULL) {
            ESP_LOGE(WIFI_TAG, "Failed to create default WiFi STA interface");
            return ESP_ERR_NO_MEM;
        }
    }

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

    esp_err_t result = ESP_OK;

    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err != ESP_OK && disconnect_err != ESP_ERR_WIFI_NOT_STARTED &&
            disconnect_err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(WIFI_TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(disconnect_err));
        if (result == ESP_OK) {
            result = disconnect_err;
        }
    }

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(WIFI_TAG, "esp_wifi_stop failed: %s", esp_err_to_name(stop_err));
        if (result == ESP_OK) {
            result = stop_err;
        }
    }

    esp_err_t unregister_wifi = esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    if (unregister_wifi != ESP_OK && unregister_wifi != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(WIFI_TAG, "Failed to unregister WiFi event handler: %s",
                 esp_err_to_name(unregister_wifi));
        if (result == ESP_OK) {
            result = unregister_wifi;
        }
    }

    esp_err_t unregister_ip = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    if (unregister_ip != ESP_OK && unregister_ip != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(WIFI_TAG, "Failed to unregister IP event handler: %s",
                 esp_err_to_name(unregister_ip));
        if (result == ESP_OK) {
            result = unregister_ip;
        }
    }

    esp_err_t deinit_err = esp_wifi_deinit();
    if (deinit_err != ESP_OK && deinit_err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(WIFI_TAG, "esp_wifi_deinit failed: %s", esp_err_to_name(deinit_err));
        if (result == ESP_OK) {
            result = deinit_err;
        }
    }

    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }

    s_wifi_started = false;
    s_wifi_on_ready_cb = NULL;

    ESP_LOGI(WIFI_TAG, "WiFi driver stopped");
    return result;
}

esp_err_t lifecycle_nvs_init(void) {
    return lifecycle_ensure_nvs_initialized(LIFECYCLE_TAG);
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

    esp_err_t init_err = lifecycle_ensure_nvs_initialized(LIFECYCLE_TAG);
    if (init_err != ESP_OK) {
        return init_err;
    }

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
                                  const homekit_value_t value) {
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

esp_err_t lifecycle_configure_homekit(homekit_characteristic_t *revision,
                                      homekit_characteristic_t *ota_trigger,
                                      const char *log_tag) {
    if (revision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *tag = (log_tag != NULL) ? log_tag : LIFECYCLE_TAG;
    const char *fallback_version = LIFECYCLE_DEFAULT_FW_VERSION;

    esp_err_t rev_err = lifecycle_init_firmware_revision(revision, fallback_version);
    const char *resolved_version = lifecycle_get_firmware_revision_string();
    if (resolved_version != NULL && resolved_version[0] != '\0') {
        ESP_LOGI(tag, "Lifecycle Manager firmware version (NVS): %s", resolved_version);
    } else {
        ESP_LOGW(tag,
                 "Lifecycle Manager firmware version not found in NVS, using fallback: %s",
                 fallback_version);
    }

    if (rev_err != ESP_OK) {
        ESP_LOGW(tag, "Firmware revision init failed: %s", esp_err_to_name(rev_err));
    }

    if (ota_trigger != NULL) {
        ota_trigger->setter = NULL;
        ota_trigger->setter_ex = lifecycle_handle_ota_trigger;
        ota_trigger->value.bool_value = false;
    }

    return rev_err;
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

    bool factory_boot_selected = false;

    const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(LIFECYCLE_TAG, "Factory partition not found, rebooting to current app");
    } else {
        lifecycle_log_step("set_boot=factory");
        err = esp_ota_set_boot_partition(factory);
        if (err != ESP_OK) {
            ESP_LOGE(LIFECYCLE_TAG, "Failed to set factory partition for boot: %s", esp_err_to_name(err));
        } else {
            lifecycle_log_step("set_post_reset_flag=update");
            lifecycle_mark_post_reset(LIFECYCLE_POST_RESET_REASON_UPDATE);
            factory_boot_selected = true;
        }
    }

    lifecycle_perform_common_shutdown(false);

    lifecycle_log_step("delay_before_reset");
    vTaskDelay(pdMS_TO_TICKS(100));

    lifecycle_log_step("reboot");
    if (factory_boot_selected) {
        ESP_LOGI(LIFECYCLE_TAG, "Rebooting into factory partition for update");
    } else {
        ESP_LOGI(LIFECYCLE_TAG, "Rebooting to continue update workflow");
    }
    esp_restart();
}

void lifecycle_reset_homekit_and_reboot(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Resetting HomeKit state and rebooting");
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        lifecycle_log_step("set_boot=current");
        esp_err_t err = esp_ota_set_boot_partition(running);
        if (err != ESP_OK) {
            ESP_LOGW(LIFECYCLE_TAG, "Failed to re-select running partition: %s",
                     esp_err_to_name(err));
        }
    }

    lifecycle_log_step("set_post_reset_flag=homekit");
    lifecycle_mark_post_reset(LIFECYCLE_POST_RESET_REASON_HOMEKIT);

    lifecycle_perform_common_shutdown(true);

    lifecycle_log_step("delay_before_reset");
    vTaskDelay(pdMS_TO_TICKS(100));

    lifecycle_log_step("reboot");
    esp_restart();
}

static void erase_wifi_credentials(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Clearing Wi-Fi credentials from NVS namespace 'wifi_cfg'");

    esp_err_t init_err = lifecycle_ensure_nvs_initialized(LIFECYCLE_TAG);
    if (init_err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG,
                 "Failed to initialise NVS while clearing Wi-Fi credentials: %s",
                 esp_err_to_name(init_err));
        return;
    }

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

static void erase_nvs_partition(void) {
    lifecycle_log_step("erase_nvs_partition");

    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGW(LIFECYCLE_TAG, "nvs_flash_deinit failed: %s", esp_err_to_name(err));
    }

    s_nvs_initialized = false;

    err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(LIFECYCLE_TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
    }
}

static void clear_nvs_namespace(const char *namespace, const char *description) {
    if (namespace == NULL || description == NULL) {
        return;
    }

    ESP_LOGI(LIFECYCLE_TAG, "Clearing %s in NVS namespace '%s'", description, namespace);

    esp_err_t init_err = lifecycle_ensure_nvs_initialized(LIFECYCLE_TAG);
    if (init_err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG,
                 "Failed to initialise NVS while clearing namespace '%s': %s",
                 namespace,
                 esp_err_to_name(init_err));
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to open namespace '%s' for clearing: %s", namespace, esp_err_to_name(err));
        return;
    }

    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "Failed to erase namespace '%s': %s", namespace, esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGW(LIFECYCLE_TAG, "Failed to commit erase of namespace '%s': %s", namespace, esp_err_to_name(err));
        }
    }

    nvs_close(handle);
}

static void clear_lcm_namespace(void) {
    clear_nvs_namespace("lcm", "Lifecycle Manager state");
}

static void clear_fwcfg_namespace(void) {
    clear_nvs_namespace("fwcfg", "firmware configuration");
}

static void erase_otadata_partition(void) {
    const esp_partition_t *otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata == NULL) {
        ESP_LOGW(LIFECYCLE_TAG, "OTA data partition not found");
        return;
    }

    ESP_LOGI(LIFECYCLE_TAG,
            "Erasing OTA data partition '%s' at offset 0x%08" PRIx32 " (size=%" PRIu32 ")",
            otadata->label, otadata->address, (uint32_t)otadata->size);
    esp_err_t err = esp_partition_erase_range(otadata, 0, otadata->size);
    if (err != ESP_OK) {
        ESP_LOGE(LIFECYCLE_TAG, "Failed to erase OTA data partition: %s", esp_err_to_name(err));
    }
}

static bool erase_ota_partition_by_label(const char *label) {
    if (label == NULL) {
        return false;
    }

    bool erased = false;

    esp_partition_iterator_t it = esp_partition_find(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        esp_partition_iterator_t next = esp_partition_next(it);

        if (part != NULL && strncmp(part->label, label, sizeof(part->label)) == 0) {
            if (part->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                    part->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
                ESP_LOGI(LIFECYCLE_TAG,
                        "Erasing OTA partition '%s' at offset 0x%08" PRIx32 " (size=%" PRIu32 ")",
                        part->label, part->address, (uint32_t)part->size);
                esp_err_t err = esp_partition_erase_range(part, 0, part->size);
                if (err != ESP_OK) {
                    ESP_LOGE(LIFECYCLE_TAG, "Failed to erase partition '%s': %s",
                            part->label, esp_err_to_name(err));
                } else {
                    erased = true;
                }
            } else {
                ESP_LOGW(LIFECYCLE_TAG,
                        "Partition '%s' found but subtype %d is not an OTA application",
                        part->label, part->subtype);
            }
        }

        esp_partition_iterator_release(it);
        it = next;
    }

    return erased;
}

static void erase_ota_app_partitions(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Erasing OTA application partitions");

    const char *labels[] = {
        "ota_1",
        "ota_2",
        "ota_0",
    };

    bool any_erased = false;
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        bool erased = erase_ota_partition_by_label(labels[i]);
        if (!erased) {
            ESP_LOGW(LIFECYCLE_TAG, "OTA partition '%s' not found or already empty", labels[i]);
        }
        any_erased |= erased;
    }

    if (!any_erased) {
        ESP_LOGW(LIFECYCLE_TAG, "No OTA partitions were erased");
    }
}

void lifecycle_factory_reset_and_reboot(void) {
    ESP_LOGI(LIFECYCLE_TAG, "Performing factory reset (HomeKit + Wi-Fi)");

    lifecycle_reset_restart_counter();

    bool factory_boot_selected = false;

    const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(LIFECYCLE_TAG, "Factory partition not found, rebooting to current app");
    } else {
        lifecycle_log_step("set_boot=factory");
        esp_err_t boot_err = esp_ota_set_boot_partition(factory);
        if (boot_err != ESP_OK) {
            ESP_LOGE(LIFECYCLE_TAG, "Failed to select factory partition after reset: %s", esp_err_to_name(boot_err));
        } else {
            lifecycle_log_step("set_post_reset_flag=factory");
            lifecycle_mark_post_reset(LIFECYCLE_POST_RESET_REASON_FACTORY);
            factory_boot_selected = true;
        }
    }

    lifecycle_log_step("reset_homekit_store");
    homekit_server_reset();

    lifecycle_perform_common_shutdown(false);

    lifecycle_log_step("erase_wifi_credentials");
    erase_wifi_credentials();

    lifecycle_log_step("clear_fw_config");
    clear_fwcfg_namespace();

    lifecycle_log_step("clear_lcm_state");
    clear_lcm_namespace();

    lifecycle_log_step("erase_otadata");
    erase_otadata_partition();

    lifecycle_log_step("erase_ota_apps");
    erase_ota_app_partitions();

    lifecycle_log_step("restore_wifi_defaults");
    esp_err_t wifi_restore_err = esp_wifi_restore();
    if (wifi_restore_err != ESP_OK) {
        ESP_LOGW(LIFECYCLE_TAG, "esp_wifi_restore failed: %s", esp_err_to_name(wifi_restore_err));
    }

    erase_nvs_partition();

    lifecycle_log_step("delay_before_reset");
    vTaskDelay(pdMS_TO_TICKS(100));

    lifecycle_log_step("reboot");
    if (factory_boot_selected) {
        ESP_LOGI(LIFECYCLE_TAG, "Factory reset complete, rebooting into factory partition");
    } else {
        ESP_LOGI(LIFECYCLE_TAG, "Factory reset complete, rebooting current firmware");
    }
    esp_restart();
}
