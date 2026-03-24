#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"

esp_err_t nvs_store_open_rw(const char *ns, nvs_handle_t *out_handle);
esp_err_t nvs_store_open_ro(const char *ns, nvs_handle_t *out_handle);
esp_err_t nvs_store_commit_and_close(nvs_handle_t handle);
void nvs_store_close(nvs_handle_t handle);

esp_err_t nvs_store_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
esp_err_t nvs_store_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_store_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value);
esp_err_t nvs_store_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_store_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_store_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_store_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_store_set_i32(nvs_handle_t handle, const char *key, int32_t value);
