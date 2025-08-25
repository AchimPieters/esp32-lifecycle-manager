#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <mbedtls/md.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "github_update.h"

static const char *TAG = "github_update";

static bool file_exists(const char *path)
{
        struct stat st;
        return stat(path, &st) == 0;
}

static esp_err_t download_to_mem(const char *url, char **out, size_t *len)
{
        esp_http_client_config_t cfg = {
                .url = url,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return ESP_FAIL;
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
                esp_http_client_cleanup(client);
                return err;
        }
        int total = esp_http_client_fetch_headers(client);
        if (total <= 0) {
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
        }
        char *buf = malloc(total + 1);
        if (!buf) {
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
        }
        int read = esp_http_client_read_response(client, buf, total);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (read != total) {
                free(buf);
                return ESP_FAIL;
        }
        buf[total] = '\0';
        *out = buf;
        *len = total;
        return ESP_OK;
}

static esp_err_t verify_sig(const uint8_t *bin, size_t bin_len, const char *sig)
{
        unsigned char hash[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (!info) {
                mbedtls_md_free(&ctx);
                return ESP_FAIL;
        }
        mbedtls_md_setup(&ctx, info, 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, bin, bin_len);
        mbedtls_md_finish(&ctx, hash);
        mbedtls_md_free(&ctx);
        char computed[65];
        for (int i = 0; i < 32; i++) {
                sprintf(computed + i * 2, "%02x", hash[i]);
        }
        computed[64] = '\0';
        return strncasecmp(computed, sig, 64) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t fetch_release(const char *repo, bool pre, char **bin_url, char **sig_url)
{
        char api[256];
        if (pre) {
                snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases?per_page=1", repo);
        } else {
                snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases/latest", repo);
        }
        char *json;
        size_t json_len;
        esp_err_t err = download_to_mem(api, &json, &json_len);
        if (err != ESP_OK) return err;
        cJSON *root = cJSON_ParseWithLength(json, json_len);
        free(json);
        if (!root) return ESP_FAIL;
        cJSON *release = root;
        if (pre) {
                release = cJSON_GetArrayItem(root, 0);
        }
        if (!release) {
                cJSON_Delete(root);
                return ESP_FAIL;
        }
        cJSON *assets = cJSON_GetObjectItem(release, "assets");
        if (!assets) {
                cJSON_Delete(root);
                return ESP_FAIL;
        }
        cJSON *asset;
        cJSON_ArrayForEach(asset, assets) {
                const char *name = cJSON_GetObjectItem(asset, "name")->valuestring;
                const char *url = cJSON_GetObjectItem(asset, "browser_download_url")->valuestring;
                if (strcmp(name, "main.bin") == 0) {
                        *bin_url = strdup(url);
                } else if (strcmp(name, "main.bin.sig") == 0) {
                        *sig_url = strdup(url);
                }
        }
        cJSON_Delete(root);
        return (*bin_url && *sig_url) ? ESP_OK : ESP_FAIL;
}

esp_err_t github_update_if_needed(const char *repo, bool pre_release)
{
        char version[16] = {0};
        size_t size = sizeof(version);
        nvs_handle_t h;
        if (nvs_open("app", NVS_READONLY, &h) == ESP_OK) {
                nvs_get_str(h, "version", version, &size);
                nvs_close(h);
        }
        bool need = !file_exists("/spiffs/main.bin") || strcmp(version, "0.0.0") == 0 || version[0] == '\0';
        if (!need) return ESP_OK;

        char *bin_url = NULL;
        char *sig_url = NULL;
        esp_err_t err = fetch_release(repo, pre_release, &bin_url, &sig_url);
        if (err != ESP_OK) {
                free(bin_url);
                free(sig_url);
                return err;
        }

        char *sig;
        size_t sig_len;
        err = download_to_mem(sig_url, &sig, &sig_len);
        if (err != ESP_OK) {
                free(bin_url);
                free(sig_url);
                return err;
        }

        char *bin;
        size_t bin_len;
        err = download_to_mem(bin_url, &bin, &bin_len);
        if (err != ESP_OK) {
                free(sig);
                free(bin_url);
                free(sig_url);
                return err;
        }

        err = verify_sig((const uint8_t *)bin, bin_len, sig);
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "Signature mismatch");
                free(sig);
                free(bin);
                free(bin_url);
                free(sig_url);
                return err;
        }
        free(sig);
        free(bin);

        esp_http_client_config_t client_cfg = {
                .url = bin_url,
        };
        esp_https_ota_config_t ota_cfg = {
                .http_config = &client_cfg,
        };
        err = esp_https_ota(&ota_cfg);
        free(bin_url);
        free(sig_url);
        return err;
}
