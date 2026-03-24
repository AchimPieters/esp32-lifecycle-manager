#include "nvs_store.h"

#include "esp_log.h"

static const char *TAG = "nvs_store";

esp_err_t nvs_store_open_rw(const char *ns, nvs_handle_t *out_handle) {
    if (!ns || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_open(ns, NVS_READWRITE, out_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s", ns, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_open_ro(const char *ns, nvs_handle_t *out_handle) {
    if (!ns || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_open(ns, NVS_READONLY, out_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(ro:%s) failed: %s", ns, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_commit_and_close(nvs_handle_t handle) {
    esp_err_t err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
    return err;
}

void nvs_store_close(nvs_handle_t handle) {
    nvs_close(handle);
}

esp_err_t nvs_store_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value) {
    if (!key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_get_u32(handle, key, out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_u32(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value) {
    if (!key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_get_u8(handle, key, out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_u8(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value) {
    if (!key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_get_i32(handle, key, out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_i32(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length) {
    if (!key || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_get_str(handle, key, out_value, length);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_str(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_set_str(nvs_handle_t handle, const char *key, const char *value) {
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_set_u32(nvs_handle_t handle, const char *key, uint32_t value) {
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_u32(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_u8(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_u8(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_store_set_i32(nvs_handle_t handle, const char *key, int32_t value) {
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_i32(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}
