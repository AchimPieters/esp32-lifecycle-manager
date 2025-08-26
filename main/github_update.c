#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "github_update.h"

static const char *TAG = "github_update";

esp_err_t save_fw_config(const char *repo, bool pre) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("fwcfg", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err |= nvs_set_str(h, "repo", repo ? repo : "");
    err |= nvs_set_u8(h, "pre", pre ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool load_fw_config(char *repo, size_t repo_len, bool *pre) {
    nvs_handle_t h;
    if (nvs_open("fwcfg", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len;
    if (repo) { len = repo_len; if (nvs_get_str(h, "repo", repo, &len) != ESP_OK) { nvs_close(h); return false; } }
    uint8_t pre_u8; if (nvs_get_u8(h, "pre", &pre_u8) != ESP_OK) { nvs_close(h); return false; }
    if (pre) *pre = pre_u8 != 0;
    nvs_close(h);
    return true;
}

static esp_err_t download_string(const char *url, char *buf, size_t buf_len) {
    esp_http_client_config_t cfg = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    int r = esp_http_client_read_response(client, buf, buf_len-1);
    if (r < 0) { esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_FAIL; }
    buf[r] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

static bool parse_hex(const char *hex, uint8_t *out, size_t out_len) {
    size_t len = strlen(hex);
    if (len < out_len*2) return false;
    for (size_t i=0;i<out_len;i++) {
        char c1 = tolower(hex[i*2]);
        char c2 = tolower(hex[i*2+1]);
        if (!isxdigit(c1) || !isxdigit(c2)) return false;
        out[i] = ((c1 <= '9'? c1-'0': c1-'a'+10) <<4) |
                 (c2 <= '9'? c2-'0': c2-'a'+10);
    }
    return true;
}

esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url) {
    ESP_LOGI(TAG, "Downloading signature from %s", sig_url);
    char sig[128];
    ESP_ERROR_CHECK(download_string(sig_url, sig, sizeof(sig)));
    uint8_t expected[32];
    if (!parse_hex(sig, expected, sizeof(expected))) {
        ESP_LOGE(TAG, "Invalid signature format");
        return ESP_FAIL;
    }

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t http_cfg = {
        .url = fw_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    ESP_LOGI(TAG, "Starting OTA from %s", fw_url);
    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        return ret;
    }
    uint8_t actual[32];
    esp_partition_get_sha256(update_part, actual);
    if (memcmp(actual, expected, sizeof(actual)) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA successful, rebooting");
    esp_restart();
    return ESP_OK;
}

esp_err_t github_update_if_needed(const char *repo, bool prerelease) {
    char api[256];
    snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases%s", repo,
             prerelease ? "?per_page=5" : "/latest");
    esp_http_client_config_t cfg = { .url = api, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota" };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    int len = esp_http_client_fetch_headers(client);
    char *data = malloc(len + 1);
    if (!data) { esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
    int read = esp_http_client_read_response(client, data, len);
    data[read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return ESP_FAIL;

    cJSON *release = NULL;
    if (prerelease) {
        if (!cJSON_IsArray(json)) { cJSON_Delete(json); return ESP_FAIL; }
        release = cJSON_GetArrayItem(json, 0);
    } else {
        if (cJSON_IsArray(json)) {
            // fallback from /latest: pick first stable
            for (int i=0; i<cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) { release = rel; break; }
            }
        } else {
            release = json;
            cJSON *pre = cJSON_GetObjectItem(release, "prerelease");
            if (cJSON_IsTrue(pre)) release = NULL; // need to fetch list
        }
        if (!release) {
            // fallback to list
            cJSON_Delete(json);
            snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases?per_page=5", repo);
            cfg.url = api;
            client = esp_http_client_init(&cfg);
            if (!client) return ESP_FAIL;
            err = esp_http_client_open(client, 0);
            if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
            len = esp_http_client_fetch_headers(client);
            data = malloc(len + 1);
            if (!data) { esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
            read = esp_http_client_read_response(client, data, len);
            data[read] = '\0';
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            json = cJSON_Parse(data);
            free(data);
            if (!json || !cJSON_IsArray(json) || cJSON_GetArraySize(json)==0) { cJSON_Delete(json); return ESP_FAIL; }
            for (int i=0; i<cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) { release = rel; break; }
            }
        }
    }
    if (!release) { cJSON_Delete(json); ESP_LOGE(TAG, "No suitable release"); return ESP_FAIL; }
    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    if (!cJSON_IsArray(assets)) { cJSON_Delete(json); return ESP_FAIL; }
    const char *fw=NULL,*sig=NULL;
    cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
    for (int i=0;i<cJSON_GetArraySize(assets);++i){
        cJSON *a = cJSON_GetArrayItem(assets,i);
        cJSON *name = cJSON_GetObjectItem(a,"name");
        cJSON *url = cJSON_GetObjectItem(a,"browser_download_url");
        if (cJSON_IsString(name) && cJSON_IsString(url)) {
            if (!strcmp(name->valuestring, "main.bin")) fw = url->valuestring;
            else if (!strcmp(name->valuestring, "main.bin.sig")) sig = url->valuestring;
        }
    }
    if (!fw || !sig) { cJSON_Delete(json); ESP_LOGE(TAG, "Missing assets"); return ESP_FAIL; }
    ESP_LOGI(TAG, "Release %s selected", cJSON_IsString(tag)?tag->valuestring:"?");
    esp_err_t res = github_update_from_urls(fw, sig);
    cJSON_Delete(json);
    return res;
}
