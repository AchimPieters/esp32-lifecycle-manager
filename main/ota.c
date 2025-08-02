#include "ota.h"
#include <string.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_https_ota.h>
#include <esp_app_desc.h>
#include <cJSON.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <ctype.h>

#define OTA_NAMESPACE "ota"

#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

static const char *TAG = "ota";

static char *nvs_get_string(nvs_handle_t handle, const char *key) {
    size_t required = 0;
    if (nvs_get_str(handle, key, NULL, &required) != ESP_OK || required == 0) {
        return NULL;
    }
    char *value = malloc(required);
    if (!value) return NULL;
    if (nvs_get_str(handle, key, value, &required) != ESP_OK) {
        free(value);
        return NULL;
    }
    return value;
}

static char *http_get(const char *url) {
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .user_agent = "esp32-lcm",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) content_length = 1024; // default buffer

    char *buffer = malloc(content_length + 1);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int read_len = esp_http_client_read_response(client, buffer, content_length);
    if (read_len < 0) {
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }
    buffer[read_len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return buffer;
}

static bool download_sig(const char *url, uint8_t *out_hash) {
    char *sig = http_get(url);
    if (!sig) return false;
    bool ok = false;
    if (strlen(sig) >= 64) {
        for (int i = 0; i < 32; i++) {
            sscanf(sig + 2 * i, "%2hhx", &out_hash[i]);
        }
        ok = true;
    }
    free(sig);
    return ok;
}

typedef struct {
    mbedtls_sha256_context sha_ctx;
} ota_hash_ctx_t;

static void sanitize_version_str(const char *in, char *out, size_t len) {
    while (*in && !isdigit((unsigned char)*in)) {
        in++;
    }
    strlcpy(out, in, len);
}

static void normalize_repo_api(const char *input, char *output, size_t len) {
    if (!input || !*input) {
        if (len) output[0] = '\0';
        return;
    }
    const char *repo_part = input;
    const char *p = NULL;
    if ((p = strstr(input, "api.github.com/repos/"))) {
        strlcpy(output, input, len);
        return;
    }
    if ((p = strstr(input, "github.com/"))) {
        repo_part = p + strlen("github.com/");
    }
    snprintf(output, len, "https://api.github.com/repos/%s", repo_part);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        ota_hash_ctx_t *ctx = (ota_hash_ctx_t *)evt->user_data;
        mbedtls_sha256_update_ret(&ctx->sha_ctx, (const unsigned char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

static bool download_and_flash(const char *bin_url, const uint8_t *expected_hash) {
    ota_hash_ctx_t hash_ctx;
    mbedtls_sha256_init(&hash_ctx.sha_ctx);
    mbedtls_sha256_starts_ret(&hash_ctx.sha_ctx, 0);

    esp_http_client_config_t http_config = {
        .url = bin_url,
        .timeout_ms = 10000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .user_agent = "esp32-lcm",
        .event_handler = http_event_handler,
        .user_data = &hash_ctx,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    if (esp_https_ota_begin(&ota_config, &https_ota_handle) != ESP_OK) {
        mbedtls_sha256_free(&hash_ctx.sha_ctx);
        return false;
    }

    esp_err_t err;
    do {
        err = esp_https_ota_perform(https_ota_handle);
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during OTA perform: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        mbedtls_sha256_free(&hash_ctx.sha_ctx);
        return false;
    }

    uint8_t hash[32];
    mbedtls_sha256_finish_ret(&hash_ctx.sha_ctx, hash);
    mbedtls_sha256_free(&hash_ctx.sha_ctx);

    if (memcmp(hash, expected_hash, 32) != 0) {
        ESP_LOGE(TAG, "Firmware hash mismatch");
        esp_https_ota_abort(https_ota_handle);
        return false;
    }

    if (esp_https_ota_finish(https_ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed");
        return false;
    }

    ESP_LOGI(TAG, "OTA update successful");
    return true;
}

static void parse_version(const char *str, int *major, int *minor, int *patch) {
    *major = *minor = *patch = 0;
    if (!str) return;
    while (*str && !isdigit((unsigned char)*str)) {
        str++;
    }
    sscanf(str, "%d.%d.%d", major, minor, patch);
}

static bool is_version_newer(const char *current, const char *latest) {
    int cur_major, cur_minor, cur_patch;
    int lat_major, lat_minor, lat_patch;
    parse_version(current, &cur_major, &cur_minor, &cur_patch);
    parse_version(latest, &lat_major, &lat_minor, &lat_patch);
    if (lat_major > cur_major) return true;
    if (lat_major < cur_major) return false;
    if (lat_minor > cur_minor) return true;
    if (lat_minor < cur_minor) return false;
    if (lat_patch > cur_patch) return true;
    return false;
}

static void perform_update(nvs_handle_t handle, const char *repo_url, bool prerelease) {
    char current_version[64] = {0};
    char *stored_version = nvs_get_string(handle, "current_version");
    if (stored_version) {
        sanitize_version_str(stored_version, current_version, sizeof(current_version));
        if (strcmp(stored_version, current_version) != 0) {
            nvs_set_str(handle, "current_version", current_version);
            nvs_commit(handle);
        }
        free(stored_version);
    } else {
        const esp_app_desc_t *desc = esp_app_get_description();
        if (desc) {
            sanitize_version_str(desc->version, current_version, sizeof(current_version));
            nvs_set_str(handle, "current_version", current_version);
            nvs_commit(handle);
        }
    }

    char api_base[256];
    normalize_repo_api(repo_url, api_base, sizeof(api_base));
    char api_url[256];
    strlcpy(api_url, api_base, sizeof(api_url));
    const char *suffix = prerelease ? "/releases" : "/releases/latest";
    if (strlcat(api_url, suffix, sizeof(api_url)) >= sizeof(api_url)) {
        ESP_LOGE(TAG, "API URL truncated");
        return;
    }

    char *json = http_get(api_url);
    if (!json) {
        ESP_LOGE(TAG, "Failed to fetch release info");
        return;
    }

    cJSON *root = cJSON_Parse(json);
    cJSON *release = NULL;
    if (prerelease) {
        if (cJSON_IsArray(root)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, root) {
                cJSON *pre = cJSON_GetObjectItem(item, "prerelease");
                if (cJSON_IsBool(pre) && cJSON_IsTrue(pre)) {
                    release = item;
                    break;
                }
            }
            if (!release) {
                release = cJSON_GetArrayItem(root, 0);
            }
        }
    } else {
        release = root;
    }

    if (!release) {
        cJSON_Delete(root);
        free(json);
        ESP_LOGE(TAG, "Invalid release data");
        return;
    }

    const cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
    if (!cJSON_IsString(tag)) {
        cJSON_Delete(root);
        free(json);
        ESP_LOGE(TAG, "tag_name missing");
        return;
    }

    const char *tag_name = tag->valuestring;
    if (*current_version && !is_version_newer(current_version, tag_name)) {
        ESP_LOGI(TAG, "Geen update beschikbaar");
        cJSON_Delete(root);
        free(json);
        return;
    }

    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    const char *bin_url = NULL;
    const char *sig_url = NULL;
    if (cJSON_IsArray(assets)) {
        cJSON *asset = NULL;
        cJSON_ArrayForEach(asset, assets) {
            cJSON *name = cJSON_GetObjectItem(asset, "name");
            cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
            if (cJSON_IsString(name) && cJSON_IsString(url)) {
                if (strcmp(name->valuestring, "main.bin") == 0) {
                    bin_url = url->valuestring;
                } else if (strcmp(name->valuestring, "main.bin.sig") == 0) {
                    sig_url = url->valuestring;
                }
            }
        }
    }
    if (!bin_url || !sig_url) {
        ESP_LOGE(TAG, "Required assets not found");
        cJSON_Delete(root);
        free(json);
        return;
    }

    uint8_t expected_hash[32];
    if (!download_sig(sig_url, expected_hash)) {
        ESP_LOGE(TAG, "Failed to download signature");
        cJSON_Delete(root);
        free(json);
        return;
    }

    if (download_and_flash(bin_url, expected_hash)) {
        char cleaned_tag[64];
        sanitize_version_str(tag_name, cleaned_tag, sizeof(cleaned_tag));
        nvs_set_str(handle, "current_version", cleaned_tag);
        nvs_set_str(handle, "installed", "1");
        nvs_commit(handle);
        ESP_LOGI(TAG, "Rebooting to new firmware");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed");
    }

    cJSON_Delete(root);
    free(json);
}

void ota_check_and_install(void) {
    nvs_handle_t handle;
    if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return;
    }

    char *repo_url = nvs_get_string(handle, "repo_url");
    if (!repo_url) {
        ESP_LOGW(TAG, "ota.repo_url not set");
        nvs_close(handle);
        return;
    }

    char *prerelease_str = nvs_get_string(handle, "prerelease");
    bool prerelease = prerelease_str && strcmp(prerelease_str, "1") == 0;
    free(prerelease_str);

    bool installed = false;
    size_t dummy = 0;
    if (nvs_get_str(handle, "installed", NULL, &dummy) == ESP_OK) {
        installed = true;
    }

    if (!installed) {
        ESP_LOGI(TAG, "No firmware installed; performing initial OTA");
        perform_update(handle, repo_url, prerelease);
    } else {
        ESP_LOGI(TAG, "Existing firmware present; skipping initial OTA");
    }

    free(repo_url);
    nvs_close(handle);
}

void firmware_update(void) {
    nvs_handle_t handle;
    if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return;
    }

    char *repo_url = nvs_get_string(handle, "repo_url");
    if (!repo_url) {
        ESP_LOGW(TAG, "ota.repo_url not set");
        nvs_close(handle);
        return;
    }

    char *prerelease_str = nvs_get_string(handle, "prerelease");
    bool prerelease = prerelease_str && strcmp(prerelease_str, "1") == 0;
    free(prerelease_str);

    perform_update(handle, repo_url, prerelease);

    free(repo_url);
    nvs_close(handle);
}

