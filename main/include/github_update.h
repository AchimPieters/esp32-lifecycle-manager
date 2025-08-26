#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t save_fw_config(const char *repo, bool pre, const char *fw_url, const char *sig_url);
bool load_fw_config(char *repo, size_t repo_len, bool *pre, char *fw_url, size_t fw_len, char *sig_url, size_t sig_len);

esp_err_t github_update_if_needed(const char *repo, bool prerelease);
esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url);
