#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t save_fw_config(const char *repo, bool pre);
bool load_fw_config(char *repo, size_t repo_len, bool *pre);
esp_err_t save_led_config(bool enabled, int gpio);
bool load_led_config(bool *enabled, int *gpio);
void led_config_update(bool enabled, int gpio);

esp_err_t github_update_if_needed(const char *repo, bool prerelease);
esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url);
