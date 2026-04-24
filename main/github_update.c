/**
   Copyright 2026 Achim Pieters | StudioPieters®

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
#include <time.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "soc/soc_caps.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "esp_image_format.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "github_update.h"
#include "led_indicator.h"
#include "nvs_keys.h"
#include "nvs_store.h"

static const char *TAG = "github_update";

#ifdef SOC_GPIO_PIN_COUNT
#define LCM_MAX_GPIO_PIN ((int)SOC_GPIO_PIN_COUNT - 1)
#else
#define LCM_MAX_GPIO_PIN 32
#endif

#ifndef ESP_PARTITION_LABEL_MAX_LEN
#define ESP_PARTITION_LABEL_MAX_LEN 16
#endif

#define INSTALLED_VER_MAX_LEN 32
#define OTA_SIG_MAGIC 0x4C434D53UL
#define OTA_SIG_VERSION 1U
#define OTA_SIG_ALGO_ECDSA_P256_SHA256 1U
#define OTA_SIG_MAX_LEN 512U
#define INSTALLED_LABEL_MAX_LEN (ESP_PARTITION_LABEL_MAX_LEN + 1)
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING_RELEASE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_STAGING,
    OTA_STATE_ACTIVATING,
    OTA_STATE_FAILED,
    OTA_STATE_REBOOTING,
} ota_state_t;

static const char *OTA_REASON_NONE = "none";
static const char *OTA_REASON_RELEASE_API_FAILURE = "release_api_failure";
static const char *OTA_REASON_MISSING_ASSET = "missing_asset";
static const char *OTA_REASON_INVALID_SIGNATURE = "invalid_signature";
static const char *OTA_REASON_INVALID_IMAGE_LENGTH = "invalid_image_length";
static const char *OTA_REASON_PARTITION_UNAVAILABLE = "partition_unavailable";
static const char *OTA_REASON_BOOT_PARTITION_SET_FAILURE = "boot_partition_set_failure";
static const char *OTA_REASON_HTTP_FAILURE = "http_failure";

static const char *ota_state_to_str(ota_state_t state) {
    switch (state) {
    case OTA_STATE_IDLE: return "IDLE";
    case OTA_STATE_CHECKING_RELEASE: return "CHECKING_RELEASE";
    case OTA_STATE_DOWNLOADING: return "DOWNLOADING";
    case OTA_STATE_VERIFYING: return "VERIFYING";
    case OTA_STATE_STAGING: return "STAGING";
    case OTA_STATE_ACTIVATING: return "ACTIVATING";
    case OTA_STATE_FAILED: return "FAILED";
    case OTA_STATE_REBOOTING: return "REBOOTING";
    default: return "UNKNOWN";
    }
}

static bool ota_state_from_str(const char *state_str, ota_state_t *out_state) {
    if (!state_str || !out_state) {
        return false;
    }
    if (strcmp(state_str, "IDLE") == 0) {
        *out_state = OTA_STATE_IDLE;
        return true;
    }
    if (strcmp(state_str, "CHECKING_RELEASE") == 0) {
        *out_state = OTA_STATE_CHECKING_RELEASE;
        return true;
    }
    if (strcmp(state_str, "DOWNLOADING") == 0) {
        *out_state = OTA_STATE_DOWNLOADING;
        return true;
    }
    if (strcmp(state_str, "VERIFYING") == 0) {
        *out_state = OTA_STATE_VERIFYING;
        return true;
    }
    if (strcmp(state_str, "STAGING") == 0) {
        *out_state = OTA_STATE_STAGING;
        return true;
    }
    if (strcmp(state_str, "ACTIVATING") == 0) {
        *out_state = OTA_STATE_ACTIVATING;
        return true;
    }
    if (strcmp(state_str, "FAILED") == 0) {
        *out_state = OTA_STATE_FAILED;
        return true;
    }
    if (strcmp(state_str, "REBOOTING") == 0) {
        *out_state = OTA_STATE_REBOOTING;
        return true;
    }
    return false;
}

static bool ota_transition_allowed(ota_state_t current, ota_state_t next) {
    switch (current) {
    case OTA_STATE_IDLE:
        return next == OTA_STATE_CHECKING_RELEASE;
    case OTA_STATE_CHECKING_RELEASE:
        return next == OTA_STATE_IDLE || next == OTA_STATE_DOWNLOADING || next == OTA_STATE_FAILED;
    case OTA_STATE_DOWNLOADING:
        return next == OTA_STATE_VERIFYING || next == OTA_STATE_FAILED;
    case OTA_STATE_VERIFYING:
        return next == OTA_STATE_STAGING || next == OTA_STATE_FAILED;
    case OTA_STATE_STAGING:
        return next == OTA_STATE_ACTIVATING || next == OTA_STATE_FAILED;
    case OTA_STATE_ACTIVATING:
        return next == OTA_STATE_REBOOTING || next == OTA_STATE_FAILED;
    case OTA_STATE_FAILED:
        return next == OTA_STATE_CHECKING_RELEASE || next == OTA_STATE_IDLE;
    case OTA_STATE_REBOOTING:
        return false;
    default:
        return false;
    }
}

static esp_err_t ota_persist_state(ota_state_t state, esp_err_t last_error,
                                   const char *repo, const char *last_seen_version,
                                   const char *last_success_version,
                                   const char *failure_reason) {
    nvs_handle_t h;
    esp_err_t err = nvs_store_open_rw(NVS_NS_FWCFG, &h);
    if (err != ESP_OK) {
        return err;
    }

    char current_state_str[16];
    size_t current_len = sizeof(current_state_str);
    esp_err_t current_err = nvs_store_get_str(h, NVS_KEY_OTA_STATE, current_state_str, &current_len);
    if (current_err == ESP_OK) {
        ota_state_t current_state = OTA_STATE_IDLE;
        if (ota_state_from_str(current_state_str, &current_state) &&
            !ota_transition_allowed(current_state, state)) {
            ESP_LOGW(TAG, "Rejected invalid OTA transition: %s -> %s",
                     current_state_str, ota_state_to_str(state));
            nvs_store_close(h);
            return ESP_ERR_INVALID_STATE;
        }
    } else if (current_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load current OTA state: %s", esp_err_to_name(current_err));
    }

    err = nvs_store_set_str(h, NVS_KEY_OTA_STATE, ota_state_to_str(state));
    if (err == ESP_OK) {
        err = nvs_store_set_i32(h, NVS_KEY_OTA_LAST_ERR, (int32_t)last_error);
    }
    if (err == ESP_OK) {
        err = nvs_store_set_u32(h, NVS_KEY_OTA_LAST_ATTEMPT, (uint32_t)time(NULL));
    }
    if (err == ESP_OK && repo && repo[0] != '\0') {
        err = nvs_store_set_str(h, NVS_KEY_OTA_SOURCE_REPO, repo);
    }
    if (err == ESP_OK && last_seen_version && last_seen_version[0] != '\0') {
        err = nvs_store_set_str(h, NVS_KEY_OTA_LAST_SEEN_VER, last_seen_version);
    }
    if (err == ESP_OK && last_success_version && last_success_version[0] != '\0') {
        err = nvs_store_set_str(h, NVS_KEY_OTA_LAST_OK_VER, last_success_version);
    }
    if (err == ESP_OK && failure_reason && failure_reason[0] != '\0') {
        err = nvs_store_set_str(h, NVS_KEY_OTA_LAST_REASON, failure_reason);
    }
    if (err == ESP_OK) {
        err = nvs_store_commit_and_close(h);
    } else {
        nvs_store_close(h);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist OTA state %s: %s",
                 ota_state_to_str(state), esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA state => %s (err=%s, reason=%s)",
             ota_state_to_str(state), esp_err_to_name(last_error),
             failure_reason ? failure_reason : OTA_REASON_NONE);
    return ESP_OK;
}

static bool parse_version(const char *str, int *maj, int *min, int *pat) {
    *maj = *min = *pat = 0;
    if (!str) {
        ESP_LOGE(TAG, "Version string is null");
        return false;
    }
    if (str[0] == 'v' || str[0] == 'V') str++;
    int parsed = sscanf(str, "%d.%d.%d", maj, min, pat);
    if (parsed < 3) {
        ESP_LOGE(TAG, "Invalid version string: %s", str);
        *maj = *min = *pat = 0;
        return false;
    }
    return true;
}

static int cmp_version(int aMaj, int aMin, int aPat,
                       int bMaj, int bMin, int bPat) {
    if (aMaj != bMaj) return aMaj - bMaj;
    if (aMin != bMin) return aMin - bMin;
    return aPat - bPat;
}

static esp_err_t store_installed_version_if_needed(const char *version,
                                                   const char *partition_label) {
    if (version == NULL || version[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char truncated[INSTALLED_VER_MAX_LEN];
    strlcpy(truncated, version, sizeof(truncated));

    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_rw(NVS_NS_FWCFG, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open_rw(fwcfg) failed when storing version: %s", esp_err_to_name(err));
        return err;
    }

    bool version_changed = false;
    bool label_changed = false;

    char existing[INSTALLED_VER_MAX_LEN];
    size_t existing_len = sizeof(existing);
    esp_err_t get_err = nvs_store_get_str(handle, NVS_KEY_INSTALLED_VER, existing, &existing_len);
    if (get_err == ESP_OK) {
        if (strcmp(existing, truncated) != 0) {
            err = nvs_store_set_str(handle, NVS_KEY_INSTALLED_VER, truncated);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "set_str(installed_ver) failed: %s", esp_err_to_name(err));
                nvs_store_close(handle);
                return err;
            }
            version_changed = true;
        }
    } else {
        if (get_err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "get_str(installed_ver) failed: %s", esp_err_to_name(get_err));
        }
        err = nvs_store_set_str(handle, NVS_KEY_INSTALLED_VER, truncated);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_str(installed_ver) failed: %s", esp_err_to_name(err));
            nvs_store_close(handle);
            return err;
        }
        version_changed = true;
    }

    if (partition_label && partition_label[0] != '\0') {
        char existing_label[INSTALLED_LABEL_MAX_LEN];
        size_t label_len = sizeof(existing_label);
        esp_err_t label_err = nvs_store_get_str(handle, NVS_KEY_INSTALLED_PART, existing_label, &label_len);
        if (label_err == ESP_OK) {
            if (strcmp(existing_label, partition_label) != 0) {
                err = nvs_store_set_str(handle, NVS_KEY_INSTALLED_PART, partition_label);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "set_str(%s) failed: %s", NVS_KEY_INSTALLED_PART, esp_err_to_name(err));
                    nvs_store_close(handle);
                    return err;
                }
                label_changed = true;
            }
        } else {
            if (label_err != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "get_str(%s) failed: %s", NVS_KEY_INSTALLED_PART, esp_err_to_name(label_err));
            }
            err = nvs_store_set_str(handle, NVS_KEY_INSTALLED_PART, partition_label);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "set_str(%s) failed: %s", NVS_KEY_INSTALLED_PART, esp_err_to_name(err));
                nvs_store_close(handle);
                return err;
            }
            label_changed = true;
        }
    }

    if (version_changed || label_changed) {
        err = nvs_store_commit_and_close(handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "commit(installed metadata) failed: %s", esp_err_to_name(err));
            return err;
        }
        if (label_changed && partition_label && partition_label[0] != '\0') {
            ESP_LOGI(TAG, "Stored installed firmware version %s on partition %s",
                     truncated, partition_label);
        } else {
            ESP_LOGI(TAG, "Stored installed firmware version %s", truncated);
        }
    } else {
        ESP_LOGD(TAG, "Installed firmware metadata unchanged");
    }

    nvs_store_close(handle);
    return ESP_OK;
}

static bool load_installed_version(char *version, size_t version_len) {
    if (version == NULL || version_len == 0) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_ro(NVS_NS_FWCFG, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Unable to open fwcfg namespace for installed version: %s",
                 esp_err_to_name(err));
        return false;
    }

    size_t required = version_len;
    err = nvs_store_get_str(handle, NVS_KEY_INSTALLED_VER, version, &required);
    nvs_store_close(handle);
    if (err == ESP_OK) {
        return true;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_str(installed_ver) failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Installed firmware version not stored in NVS");
    }
    return false;
}

static bool load_installed_partition_label(char *label, size_t label_len) {
    if (label == NULL || label_len == 0) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_ro(NVS_NS_FWCFG, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Unable to open fwcfg namespace for partition label: %s",
                 esp_err_to_name(err));
        return false;
    }

    size_t required = label_len;
    err = nvs_store_get_str(handle, NVS_KEY_INSTALLED_PART, label, &required);
    nvs_store_close(handle);
    if (err == ESP_OK) {
        return true;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_str(%s) failed: %s", NVS_KEY_INSTALLED_PART, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Installed partition label not stored in NVS");
    }
    return false;
}

static bool read_update_request_flag(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_ro(NVS_NS_LCM, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No update request flag present: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t flag = 0;
    err = nvs_store_get_u8(handle, NVS_KEY_DO_UPDATE, &flag);
    nvs_store_close(handle);
    if (err == ESP_OK) {
        return flag != 0;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_u8(do_update) failed: %s", esp_err_to_name(err));
    }
    return false;
}

static esp_err_t write_update_request_flag(bool value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_store_open_rw(NVS_NS_LCM, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t current = 0;
    esp_err_t get_err = nvs_store_get_u8(handle, NVS_KEY_DO_UPDATE, &current);
    if (get_err == ESP_OK && current == (value ? 1 : 0)) {
        nvs_store_close(handle);
        return ESP_OK;
    }

    err = nvs_store_set_u8(handle, NVS_KEY_DO_UPDATE, value ? 1 : 0);
    if (err != ESP_OK) {
        nvs_store_close(handle);
        return err;
    }

    err = nvs_store_commit_and_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGD(TAG, "Update request flag set to %d", value ? 1 : 0);
    return ESP_OK;
}

static const esp_partition_t *find_partition_for_version(int maj, int min, int pat) {
    for (esp_partition_subtype_t subtype = ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
         subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX; ++subtype) {
        const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
        if (!part) {
            continue;
        }

        esp_app_desc_t desc;
        esp_err_t desc_err = esp_ota_get_partition_description(part, &desc);
        if (desc_err != ESP_OK) {
            ESP_LOGD(TAG, "Failed to read descriptor for partition %s: %s",
                     part->label, esp_err_to_name(desc_err));
            continue;
        }

        int pMaj, pMin, pPat;
        if (!parse_version(desc.version, &pMaj, &pMin, &pPat)) {
            ESP_LOGD(TAG, "Partition %s has invalid version string '%s'",
                     part->label, desc.version);
            continue;
        }

        if (pMaj == maj && pMin == min && pPat == pat) {
            return part;
        }
    }

    return NULL;
}

static esp_err_t set_boot_partition_for_installed_firmware(int maj, int min, int pat,
                                                           const char *version_str,
                                                           bool *partition_changed) {
    const esp_partition_t *target = NULL;
    char label[INSTALLED_LABEL_MAX_LEN];

    if (load_installed_partition_label(label, sizeof(label))) {
        target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
        if (!target) {
            ESP_LOGW(TAG, "Stored partition label %s not found", label);
        } else if (target->type != ESP_PARTITION_TYPE_APP ||
                   target->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
                   target->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            ESP_LOGW(TAG, "Stored partition label %s is not an OTA partition", label);
            target = NULL;
        }
    }

    if (!target) {
        target = find_partition_for_version(maj, min, pat);
    }

    if (!target) {
        ESP_LOGE(TAG, "Unable to locate partition for installed firmware version %s",
                 version_str ? version_str : "(unknown)");
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *current_boot = esp_ota_get_boot_partition();
    if (current_boot && current_boot->address == target->address) {
        ESP_LOGI(TAG, "Boot partition already set to %s; skipping change",
                 target->label);
        if (partition_changed) {
            *partition_changed = false;
        }
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Restoring boot partition to %s at offset 0x%lx", target->label,
             (unsigned long)target->address);
    esp_err_t set_err = esp_ota_set_boot_partition(target);
    if (partition_changed && set_err == ESP_OK) {
        *partition_changed = true;
    }
    return set_err;
}

esp_err_t save_fw_config(const char *repo, bool pre) {
    ESP_LOGD(TAG, "Saving firmware config repo=%s pre=%d", repo ? repo : "(null)", pre);
    nvs_handle_t h;
    esp_err_t err = nvs_store_open_rw(NVS_NS_FWCFG, &h);
    if (err != ESP_OK) {
        return err;
    }

    if ((err = nvs_store_set_str(h, NVS_KEY_FW_REPO, repo ? repo : "")) != ESP_OK) {
        nvs_store_close(h);
        return err;
    }

    if ((err = nvs_store_set_u8(h, NVS_KEY_FW_PRE, pre ? 1 : 0)) != ESP_OK) {
        nvs_store_close(h);
        return err;
    }

    err = nvs_store_commit_and_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Firmware config saved");
    }
    return err;
}

bool load_fw_config(char *repo, size_t repo_len, bool *pre) {
    ESP_LOGD(TAG, "Loading firmware config");
    nvs_handle_t h;
    esp_err_t err = nvs_store_open_ro(NVS_NS_FWCFG, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fwcfg namespace not found");
        return false;
    }

    if (repo) {
        size_t len = repo_len;
        err = nvs_store_get_str(h, NVS_KEY_FW_REPO, repo, &len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "nvs_get_str(repo) failed: %s", esp_err_to_name(err));
            nvs_store_close(h);
            return false;
        }
    }

    uint8_t pre_u8;
    err = nvs_store_get_u8(h, NVS_KEY_FW_PRE, &pre_u8);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_u8(pre) failed: %s", esp_err_to_name(err));
        nvs_store_close(h);
        return false;
    }
    if (pre) *pre = pre_u8 != 0;
    nvs_store_close(h);
    ESP_LOGD(TAG, "Loaded firmware config repo=%s pre=%d", repo ? repo : "(null)", pre ? *pre : pre_u8);
    return true;
}

esp_err_t save_led_config(bool enabled, int gpio, bool active_high) {
    nvs_handle_t h;
    esp_err_t err = nvs_store_open_rw(NVS_NS_FWCFG, &h);
    if (err != ESP_OK) {
        return err;
    }

    if (gpio < 0) {
        gpio = -1;
    } else if (gpio > LCM_MAX_GPIO_PIN) {
        ESP_LOGW(TAG, "LED GPIO %d out of range (max=%d); disabling indicator",
                 gpio, LCM_MAX_GPIO_PIN);
        gpio = -1;
    }

    if ((err = nvs_store_set_u8(h, NVS_KEY_LED_ENABLED, enabled ? 1 : 0)) != ESP_OK) {
        nvs_store_close(h);
        return err;
    }

    if ((err = nvs_store_set_i32(h, NVS_KEY_LED_GPIO, gpio)) != ESP_OK) {
        nvs_store_close(h);
        return err;
    }

    if ((err = nvs_store_set_u8(h, NVS_KEY_LED_LEVEL, active_high ? 1 : 0)) != ESP_OK) {
        nvs_store_close(h);
        return err;
    }

    err = nvs_store_commit_and_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    ESP_LOGD(TAG, "Saved LED config enabled=%d gpio=%d active_high=%d", enabled, gpio, active_high);
    return err;
}

bool load_led_config(bool *enabled, int *gpio, bool *active_high) {
    nvs_handle_t h;
    esp_err_t err = nvs_store_open_ro(NVS_NS_FWCFG, &h);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t en;
    int32_t pin;
    uint8_t level = 0;
    err = nvs_store_get_u8(h, NVS_KEY_LED_ENABLED, &en);
    if (err != ESP_OK) {
        nvs_store_close(h);
        return false;
    }
    err = nvs_store_get_i32(h, NVS_KEY_LED_GPIO, &pin);
    if (err != ESP_OK) {
        nvs_store_close(h);
        return false;
    }
    err = nvs_store_get_u8(h, NVS_KEY_LED_LEVEL, &level);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            level = 0;
        } else {
            nvs_store_close(h);
            return false;
        }
    }
    if (pin > LCM_MAX_GPIO_PIN) {
        ESP_LOGW(TAG, "Stored LED GPIO %ld out of range (max=%d); using disabled",
                 (long)pin, LCM_MAX_GPIO_PIN);
        pin = -1;
    }
    if (enabled) *enabled = en != 0;
    if (gpio) *gpio = (int)pin;
    if (active_high) *active_high = level != 0;
    nvs_store_close(h);
    return true;
}

static esp_err_t download_signature(const char *url, uint8_t *buf, size_t buf_len, size_t *out_len) {
    ESP_LOGD(TAG, "Initializing HTTP client for %s", url);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota",
        // GitHub release assets use redirects with extremely long Location headers
        // (currently >5k and sometimes exceeding 8k), so give the HTTP client a
        // generously sized buffer to ensure the Location header fits entirely.
        .buffer_size = 32768,
        .buffer_size_tx = 32768,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_err_t err;
    int redirects = 0;
    while (redirects < 5) {
        char cur_url[512];
        esp_err_t url_err = esp_http_client_get_url(client, cur_url, sizeof(cur_url));
        ESP_LOGD(TAG, "Attempt %d URL: %s", redirects + 1,
                 url_err == ESP_OK ? cur_url : "(null)");
        ESP_LOGD(TAG, "Opening HTTP connection");
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }
        ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
        int fetch = esp_http_client_fetch_headers(client);
        ESP_LOGD(TAG, "esp_http_client_fetch_headers -> %d", fetch);
        if (fetch < 0) {
            int e = esp_http_client_get_errno(client);
            ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: errno=%d", e);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        int status = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "HTTP status %d", status);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            ESP_LOGD(TAG, "Handling HTTP redirect");
            esp_err_t redir = esp_http_client_set_redirection(client);
            if (redir != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set redirection: %s", esp_err_to_name(redir));
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            if (esp_http_client_get_url(client, cur_url, sizeof(cur_url)) == ESP_OK) {
                ESP_LOGD(TAG, "Following redirect to %s", cur_url);
            }
            esp_http_client_close(client);
            redirects++;
            continue;
        }
        if (status != 200) {
            ESP_LOGE(TAG, "Unexpected HTTP status %d", status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        char *ctype = NULL;
        esp_http_client_get_header(client, "Content-Type", &ctype);
        ESP_LOGD(TAG, "Content-Type: %s", ctype ? ctype : "(none)");
        if (ctype && (strstr(ctype, "text/") || strstr(ctype, "json"))) {
            ESP_LOGE(TAG, "Unexpected content type: %s", ctype);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        int r = esp_http_client_read_response(client, (char *)buf, buf_len);
        ESP_LOGD(TAG, "download_signature read %d bytes", r);
        if (r <= 0) {
            int e = esp_http_client_get_errno(client);
            ESP_LOGE(TAG, "Signature download failed: errno=%d", e);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        *out_len = r;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Too many redirects");
    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

static esp_err_t partition_sha256(const esp_partition_t *part, uint32_t len, uint8_t *out)
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t *buf = malloc(4096);
    if (!buf) return ESP_ERR_NO_MEM;
    mbedtls_md_init(&ctx);
    if (md_info == NULL || mbedtls_md_setup(&ctx, md_info, 0) != 0 || mbedtls_md_starts(&ctx) != 0) {
        free(buf);
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }

    uint32_t offset = 0;
    while (offset < len) {
        uint32_t to_read = len - offset;
        if (to_read > 4096) to_read = 4096;
        esp_err_t r = esp_partition_read(part, offset, buf, to_read);
        if (r != ESP_OK) {
            free(buf);
            mbedtls_md_free(&ctx);
            return r;
        }
        if (mbedtls_md_update(&ctx, buf, to_read) != 0) {
            free(buf);
            mbedtls_md_free(&ctx);
            return ESP_FAIL;
        }
        offset += to_read;
    }

    if (mbedtls_md_finish(&ctx, out) != 0) {
        free(buf);
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    mbedtls_md_free(&ctx);
    free(buf);
    return ESP_OK;
}

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t algorithm;
    uint16_t reserved;
    uint32_t firmware_length;
    uint8_t firmware_hash[32];
    uint16_t signature_length;
} ota_sig_header_t;

static const char OTA_PUBLIC_KEY_PEM[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAELpaqUF+4W/Dp/gFIW1N3QLR5jvoE\n"
"HoNeU/zdv6O7cJf4JfeC7C/lUHBMSZJah9GD3l0HAKjY5CRNU3aFE/J45A==\n"
"-----END PUBLIC KEY-----\n";

static esp_err_t verify_signature_blob(const uint8_t *sig_blob, size_t sig_len,
                                       uint32_t image_len, const uint8_t image_hash[32]) {
    if (sig_len < sizeof(ota_sig_header_t)) {
        ESP_LOGE(TAG, "Signature file too short: %u", (unsigned)sig_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const ota_sig_header_t *header = (const ota_sig_header_t *)sig_blob;
    if (header->magic != OTA_SIG_MAGIC) {
        ESP_LOGE(TAG, "Signature magic mismatch: 0x%08lx", (unsigned long)header->magic);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header->version != OTA_SIG_VERSION) {
        ESP_LOGE(TAG, "Unsupported signature version: %u", (unsigned)header->version);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (header->algorithm != OTA_SIG_ALGO_ECDSA_P256_SHA256) {
        ESP_LOGE(TAG, "Unsupported signature algorithm id: %u", (unsigned)header->algorithm);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (header->firmware_length != image_len) {
        ESP_LOGE(TAG, "Image length mismatch: expected %lu got %lu",
                 (unsigned long)header->firmware_length, (unsigned long)image_len);
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(header->firmware_hash, image_hash, 32) != 0) {
        ESP_LOGE(TAG, "Image hash mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    const size_t expected_total = sizeof(ota_sig_header_t) + header->signature_length;
    if (header->signature_length == 0 || expected_total != sig_len) {
        ESP_LOGE(TAG, "Signature length invalid: %u (total %u)",
                 (unsigned)header->signature_length, (unsigned)sig_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *signature = sig_blob + sizeof(ota_sig_header_t);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int parse_res = mbedtls_pk_parse_public_key(&pk,
            (const unsigned char *)OTA_PUBLIC_KEY_PEM,
            strlen(OTA_PUBLIC_KEY_PEM) + 1);
    if (parse_res != 0) {
        ESP_LOGE(TAG, "Public key parse failed: -0x%04x", -parse_res);
        mbedtls_pk_free(&pk);
        return ESP_FAIL;
    }

    int verify_res = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                                       image_hash, 32,
                                       signature, header->signature_length);
    mbedtls_pk_free(&pk);

    if (verify_res != 0) {
        ESP_LOGE(TAG, "Signature verification failed: -0x%04x", -verify_res);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "Cryptographic signature verified");
    return ESP_OK;
}

esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url,
                                  const char *release_version) {
    (void)ota_persist_state(OTA_STATE_DOWNLOADING, ESP_OK, NULL, release_version, NULL, OTA_REASON_NONE);
    ESP_LOGI(TAG, "Downloading signature from %s", sig_url);
    uint8_t sig[OTA_SIG_MAX_LEN];
    size_t sig_len = 0;
    esp_err_t sig_res = download_signature(sig_url, sig, sizeof(sig), &sig_len);
    if (sig_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download signature: %s", esp_err_to_name(sig_res));
        return sig_res;
    }
    ESP_LOGI(TAG, "Downloaded signature (%u bytes)", (unsigned)sig_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, NULL, release_version, NULL, OTA_REASON_PARTITION_UNAVAILABLE);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Using OTA partition at 0x%lx (%lu bytes)",
             (unsigned long)update_part->address, (unsigned long)update_part->size);
    esp_http_client_config_t http_cfg = {
        .url = fw_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota",
        // GitHub release assets use redirects with extremely long headers
        // (sometimes over 8 KB), so enlarge the HTTP client buffers to avoid
        // `HTTP_CLIENT: Out of buffer` errors when fetching the image.
        .buffer_size = 32768,
        .buffer_size_tx = 32768,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    ESP_LOGI(TAG, "Starting OTA from %s", fw_url);
    bool led_active = false;
    esp_err_t ret = ESP_OK;
    const char *failure_reason = OTA_REASON_HTTP_FAILURE;
    led_blinking_start();
    led_active = true;
    ret = esp_https_ota(&ota_cfg);
    ESP_LOGD(TAG, "esp_https_ota -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_LOGI(TAG, "OTA download complete");
    (void)ota_persist_state(OTA_STATE_VERIFYING, ESP_OK, NULL, release_version, NULL, OTA_REASON_NONE);
    esp_partition_pos_t pos = { .offset = update_part->address, .size = update_part->size };
    esp_image_metadata_t meta;
    esp_err_t meta_res = esp_image_get_metadata(&pos, &meta);
    ESP_LOGD(TAG, "esp_image_get_metadata -> %s", esp_err_to_name(meta_res));
    if (meta_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get image metadata: %s", esp_err_to_name(meta_res));
        ret = meta_res;
        failure_reason = OTA_REASON_INVALID_IMAGE_LENGTH;
        (void)ota_persist_state(OTA_STATE_FAILED, ret, NULL, release_version, NULL, OTA_REASON_INVALID_IMAGE_LENGTH);
        goto cleanup;
    }
    (void)ota_persist_state(OTA_STATE_STAGING, ESP_OK, NULL, release_version, NULL, OTA_REASON_NONE);
    uint8_t actual[32];
    esp_err_t hash_res = partition_sha256(update_part, meta.image_len, actual);
    ESP_LOGD(TAG, "partition_sha256 -> %s", esp_err_to_name(hash_res));
    if (hash_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to compute image hash: %s", esp_err_to_name(hash_res));
        ret = hash_res;
        failure_reason = OTA_REASON_INVALID_IMAGE_LENGTH;
        (void)ota_persist_state(OTA_STATE_FAILED, ret, NULL, release_version, NULL, OTA_REASON_INVALID_IMAGE_LENGTH);
        goto cleanup;
    }

    ret = verify_signature_blob(sig, sig_len, meta.image_len, actual);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Signature validation failed: %s", esp_err_to_name(ret));
        failure_reason = OTA_REASON_INVALID_SIGNATURE;
        (void)ota_persist_state(OTA_STATE_FAILED, ret, NULL, release_version, NULL, OTA_REASON_INVALID_SIGNATURE);
        goto cleanup;
    }
    led_blinking_stop();
    led_active = false;
    const char *version_to_store = release_version;
    if (!version_to_store || version_to_store[0] == '\0') {
        esp_app_desc_t new_desc;
        esp_err_t desc_err = esp_ota_get_partition_description(update_part, &new_desc);
        if (desc_err == ESP_OK) {
            if (new_desc.version[0] != '\0') {
                version_to_store = new_desc.version;
                ESP_LOGD(TAG, "Persisting firmware version from image descriptor: %s",
                         version_to_store);
            } else {
                ESP_LOGW(TAG, "Image descriptor version string is empty; nothing to persist");
            }
        } else {
            ESP_LOGW(TAG, "Failed to read image descriptor for version persistence: %s",
                     esp_err_to_name(desc_err));
        }
    }
    if (version_to_store && version_to_store[0] != '\0') {
        (void)ota_persist_state(OTA_STATE_ACTIVATING, ESP_OK, NULL, release_version, version_to_store, OTA_REASON_NONE);
        esp_err_t store_err = store_installed_version_if_needed(version_to_store,
                                                                update_part->label);
        if (store_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist installed version %s: %s",
                     version_to_store, esp_err_to_name(store_err));
        }
    } else {
        ESP_LOGW(TAG, "Skipping persistence of installed version because no value is available");
    }

    ESP_LOGI(TAG, "Signed image verified, rebooting");
    (void)ota_persist_state(OTA_STATE_REBOOTING, ESP_OK, NULL, release_version, version_to_store, OTA_REASON_NONE);
    esp_err_t flag_err = write_update_request_flag(false);
    if (flag_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear update request flag: %s", esp_err_to_name(flag_err));
    }
    esp_restart();
    return ESP_OK;

cleanup:
    if (ret != ESP_OK) {
        (void)ota_persist_state(OTA_STATE_FAILED, ret, NULL, release_version, NULL, failure_reason);
    }
    if (led_active) {
        led_blinking_stop();
    }
    return ret;
}

esp_err_t github_update_if_needed(const char *repo, bool prerelease) {
    (void)ota_persist_state(OTA_STATE_CHECKING_RELEASE, ESP_OK, repo, NULL, NULL, OTA_REASON_NONE);
    char api[256];
    snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases%s", repo,
             prerelease ? "?per_page=5" : "/latest");
    esp_http_client_config_t cfg = { .url = api, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota" };
    ESP_LOGI(TAG, "Checking updates for repo %s (pre=%d)", repo, prerelease);
    ESP_LOGD(TAG, "Release API URL: %s", api);
    bool update_requested = read_update_request_flag();
    if (update_requested) {
        ESP_LOGI(TAG, "Update request flag detected");
    }

    char installed_version[INSTALLED_VER_MAX_LEN] = {0};
    bool using_stored_version = load_installed_version(installed_version, sizeof(installed_version));
    int curMaj = 0, curMin = 0, curPat = 0;
    bool curValid = false;
    if (using_stored_version) {
        curValid = parse_version(installed_version, &curMaj, &curMin, &curPat);
        if (!curValid) {
            ESP_LOGW(TAG, "Stored installed version '%s' is invalid; ignoring",
                     installed_version);
            installed_version[0] = '\0';
            using_stored_version = false;
        }
    }

    const esp_app_desc_t *running_desc = esp_app_get_description();
    if (!curValid) {
        curValid = parse_version(running_desc ? running_desc->version : NULL,
                                 &curMaj, &curMin, &curPat);
        if (!curValid) {
            ESP_LOGE(TAG, "Invalid current firmware version, assuming 0.0.0");
        }
    }

    if (curValid) {
        snprintf(installed_version, sizeof(installed_version), "%d.%d.%d",
                 curMaj, curMin, curPat);
    }

    ESP_LOGI(TAG, "Current firmware version %d.%d.%d (%s)", curMaj, curMin, curPat,
             using_stored_version ? "installed" : "factory");
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_RELEASE_API_FAILURE);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        (void)ota_persist_state(OTA_STATE_FAILED, err, repo, NULL, NULL, OTA_REASON_HTTP_FAILURE);
        return err;
    }
    ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
    int len = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "Header length %d", len);
    if (len < 0) {
        ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: errno=%d", esp_http_client_get_errno(client));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_HTTP_FAILURE);
        return ESP_FAIL;
    }
    char *data = malloc(len + 1);
    if (!data) { ESP_LOGE(TAG, "malloc failed"); esp_http_client_close(client); esp_http_client_cleanup(client); (void)ota_persist_state(OTA_STATE_FAILED, ESP_ERR_NO_MEM, repo, NULL, NULL, OTA_REASON_RELEASE_API_FAILURE); return ESP_ERR_NO_MEM; }
    int read = esp_http_client_read_response(client, data, len);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
    if (read <= 0) {
        ESP_LOGE(TAG, "esp_http_client_read_response failed: errno=%d", esp_http_client_get_errno(client));
        free(data);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_HTTP_FAILURE);
        return ESP_FAIL;
    }
    data[read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) { ESP_LOGE(TAG, "cJSON_Parse failed"); (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_RELEASE_API_FAILURE); return ESP_FAIL; }
    ESP_LOGD(TAG, "Release JSON parsed");

    cJSON *release = NULL;
    if (prerelease) {
        ESP_LOGD(TAG, "Prerelease mode: using first release from list");
        if (!cJSON_IsArray(json)) { cJSON_Delete(json); return ESP_FAIL; }
        release = cJSON_GetArrayItem(json, 0);
    } else {
        if (cJSON_IsArray(json)) {
            ESP_LOGD(TAG, "Array returned, searching for stable release");
            for (int i=0; i<cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) { release = rel; break; }
            }
        } else {
            ESP_LOGD(TAG, "Object returned from /latest endpoint");
            release = json;
            cJSON *pre = cJSON_GetObjectItem(release, "prerelease");
            if (cJSON_IsTrue(pre)) release = NULL; // need to fetch list
        }
        if (!release) {
            ESP_LOGW(TAG, "Falling back to full release list");
            cJSON_Delete(json);
            snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases?per_page=5", repo);
            cfg.url = api;
            client = esp_http_client_init(&cfg);
            if (!client) return ESP_FAIL;
            err = esp_http_client_open(client, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                return err;
            }
            ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
            len = esp_http_client_fetch_headers(client);
            ESP_LOGD(TAG, "Header length %d", len);
            if (len < 0) {
                ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: errno=%d", esp_http_client_get_errno(client));
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            data = malloc(len + 1);
            if (!data) { ESP_LOGE(TAG, "malloc failed"); esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
            read = esp_http_client_read_response(client, data, len);
            status = esp_http_client_get_status_code(client);
            ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
            if (read <= 0) {
                ESP_LOGE(TAG, "esp_http_client_read_response failed: errno=%d", esp_http_client_get_errno(client));
                free(data);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            data[read] = '\0';
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            json = cJSON_Parse(data);
            free(data);
            if (!json || !cJSON_IsArray(json) || cJSON_GetArraySize(json)==0) { ESP_LOGE(TAG, "No releases in list"); cJSON_Delete(json); return ESP_FAIL; }
            for (int i=0; i<cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) { release = rel; break; }
            }
        }
    }
    if (!release) { cJSON_Delete(json); ESP_LOGE(TAG, "No suitable release"); (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_RELEASE_API_FAILURE); return ESP_FAIL; }
    ESP_LOGD(TAG, "Release selected");
    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    if (!cJSON_IsArray(assets)) { cJSON_Delete(json); (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, NULL, NULL, OTA_REASON_MISSING_ASSET); return ESP_FAIL; }
    const char *fw=NULL,*sig=NULL;
    cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
    const char *raw_tag = cJSON_IsString(tag) ? tag->valuestring : NULL;
    int relMaj, relMin, relPat;
    bool relValid = parse_version(cJSON_IsString(tag)?tag->valuestring:NULL, &relMaj, &relMin, &relPat);
    char sanitized_version[INSTALLED_VER_MAX_LEN] = {0};
    if (relValid) {
        snprintf(sanitized_version, sizeof(sanitized_version), "%d.%d.%d",
                 relMaj, relMin, relPat);
        (void)ota_persist_state(OTA_STATE_CHECKING_RELEASE, ESP_OK, repo, sanitized_version, NULL, OTA_REASON_NONE);
    }
    ESP_LOGI(TAG, "Latest release version %d.%d.%d", relMaj, relMin, relPat);
    if (!relValid) {
        ESP_LOGE(TAG, "Invalid release version, assuming 0.0.0");
    }
    if (relValid && curValid && cmp_version(relMaj, relMin, relPat, curMaj, curMin, curPat) <= 0) {
        const esp_partition_t *release_partition = find_partition_for_version(relMaj, relMin, relPat);
        const char *version_to_store = sanitized_version[0] ? sanitized_version : raw_tag;

        if (!release_partition) {
            ESP_LOGW(TAG,
                     "Version %d.%d.%d not found in OTA partitions; firmware will be downloaded",
                     relMaj, relMin, relPat);
        } else {
            ESP_LOGI(TAG, "Firmware already up to date on partition %s", release_partition->label);
            if (version_to_store && version_to_store[0] != '\0') {
                esp_err_t store_err = store_installed_version_if_needed(version_to_store,
                                                                        release_partition->label);
                if (store_err != ESP_OK) {
                    ESP_LOGW(TAG, "Unable to refresh stored firmware version: %s",
                             esp_err_to_name(store_err));
                }
            }
            if (update_requested) {
                if (curValid) {
                    ESP_LOGI(TAG, "Update was requested but version %d.%d.%d is already installed",
                             curMaj, curMin, curPat);
                    bool partition_changed = false;
                    esp_err_t boot_err = set_boot_partition_for_installed_firmware(
                            curMaj, curMin, curPat,
                            installed_version[0] != '\0' ? installed_version : version_to_store,
                            &partition_changed);
                    if (boot_err == ESP_OK && partition_changed) {
                        esp_err_t clear_err = write_update_request_flag(false);
                        if (clear_err != ESP_OK) {
                            ESP_LOGW(TAG, "Failed to clear update request flag: %s",
                                     esp_err_to_name(clear_err));
                        }
                        cJSON_Delete(json);
                        ESP_LOGI(TAG, "Rebooting into previously installed firmware");
                        esp_restart();
                        return ESP_OK;
                    } else if (boot_err == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "Boot partition already pointed at installed firmware; "
                                 "skipping reboot");
                    }
                    ESP_LOGE(TAG, "Failed to select installed firmware partition: %s",
                             esp_err_to_name(boot_err));
                    (void)ota_persist_state(OTA_STATE_FAILED, boot_err, repo, version_to_store, NULL,
                                            OTA_REASON_BOOT_PARTITION_SET_FAILURE);
                } else {
                    ESP_LOGW(TAG, "Update was requested but installed version could not be determined");
                }
            }
            cJSON_Delete(json);
            (void)ota_persist_state(OTA_STATE_IDLE, ESP_OK, repo, version_to_store, version_to_store, OTA_REASON_NONE);
            return ESP_OK;
        }
    }
    ESP_LOGD(TAG, "Scanning assets for firmware and signature");
    for (int i=0;i<cJSON_GetArraySize(assets);++i){
        cJSON *a = cJSON_GetArrayItem(assets,i);
        cJSON *name = cJSON_GetObjectItem(a,"name");
        cJSON *url = cJSON_GetObjectItem(a,"browser_download_url");
        if (cJSON_IsString(name) && cJSON_IsString(url)) {
            if (!strcmp(name->valuestring, "main.bin")) fw = url->valuestring;
            else if (!strcmp(name->valuestring, "main.bin.sig")) sig = url->valuestring;
        }
    }
    if (!fw || !sig) { cJSON_Delete(json); ESP_LOGE(TAG, "Missing assets"); (void)ota_persist_state(OTA_STATE_FAILED, ESP_FAIL, repo, sanitized_version, NULL, OTA_REASON_MISSING_ASSET); return ESP_FAIL; }
    ESP_LOGD(TAG, "Firmware URL: %s", fw);
    ESP_LOGD(TAG, "Signature URL: %s", sig);
    ESP_LOGI(TAG, "Release %s selected", cJSON_IsString(tag)?tag->valuestring:"?");
    const char *version_argument = sanitized_version[0] ? sanitized_version : raw_tag;
    esp_err_t res = github_update_from_urls(fw, sig,
                                            version_argument && version_argument[0] ? version_argument : NULL);
    ESP_LOGD(TAG, "github_update_from_urls -> %s", esp_err_to_name(res));
    if (res == ESP_OK) {
        (void)ota_persist_state(OTA_STATE_REBOOTING, ESP_OK, repo, version_argument, version_argument, OTA_REASON_NONE);
    } else {
        (void)ota_persist_state(OTA_STATE_FAILED, res, repo, version_argument, NULL, OTA_REASON_HTTP_FAILURE);
    }
    cJSON_Delete(json);
    return res;
}
