#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "github_update.h"
#include "ota_pubkey.h"

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

static esp_err_t download_signature(const char *url, uint8_t *buf, size_t buf_len, size_t *out_len) {
    ESP_LOGD(TAG, "Initializing HTTP client for %s", url);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota"
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
        ESP_LOGD(TAG, "Opening HTTP connection");
        err = esp_http_client_open(client, 0);
        ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            return err;
        }
        int status = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "HTTP status %d", status);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const char *loc = esp_http_client_get_header(client, "Location");
            ESP_LOGD(TAG, "Redirect location: %s", loc ? loc : "(none)");
            if (!loc) {
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            esp_http_client_set_url(client, loc);
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
        const char *ctype = esp_http_client_get_content_type(client);
        ESP_LOGD(TAG, "Content-Type: %s", ctype ? ctype : "(none)");
        if (ctype && (strstr(ctype, "text/") || strstr(ctype, "json"))) {
            ESP_LOGE(TAG, "Unexpected content type");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        int r = esp_http_client_read_response(client, (char *)buf, buf_len);
        ESP_LOGD(TAG, "download_signature read %d bytes", r);
        if (r <= 0) {
            ESP_LOGE(TAG, "Signature download failed");
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

static int verify_sig_der(const uint8_t *hash, const uint8_t *sig_der, size_t sig_len,
                          const unsigned char *pubkey_pem, size_t pubkey_pem_len) {
    int ret; mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    if ((ret = mbedtls_pk_parse_public_key(&pk, pubkey_pem, pubkey_pem_len)) != 0) goto out;
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 0, sig_der, sig_len);
out:
    mbedtls_pk_free(&pk);
    return ret;
}

esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url) {
    ESP_LOGI(TAG, "Downloading signature from %s", sig_url);
    uint8_t sig[80];
    size_t sig_len = 0;
    esp_err_t sig_res = download_signature(sig_url, sig, sizeof(sig), &sig_len);
    if (sig_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download signature");
        return sig_res;
    }
    ESP_LOGI(TAG, "Downloaded signature (%u bytes)", (unsigned)sig_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Update partition at 0x%lx, size %lu", (unsigned long)update_part->address, (unsigned long)update_part->size);
    esp_http_client_config_t http_cfg = {
        .url = fw_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    ESP_LOGI(TAG, "Starting OTA from %s", fw_url);
    esp_err_t ret = esp_https_ota(&ota_cfg);
    ESP_LOGD(TAG, "esp_https_ota -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        return ret;
    }
    uint8_t actual[32];
    esp_err_t hash_res = esp_partition_get_sha256(update_part, actual);
    ESP_LOGD(TAG, "esp_partition_get_sha256 -> %s", esp_err_to_name(hash_res));
    ESP_LOGD(TAG, "Image SHA256:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, actual, sizeof(actual), ESP_LOG_DEBUG);
    if (verify_sig_der(actual, sig, sig_len, ota_pubkey_pem, ota_pubkey_pem_len) != 0) {
        ESP_LOGE(TAG, "Invalid signature");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Signature verified, rebooting");
    esp_restart();
    return ESP_OK;
}

esp_err_t github_update_if_needed(const char *repo, bool prerelease) {
    char api[256];
    snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases%s", repo,
             prerelease ? "?per_page=5" : "/latest");
    esp_http_client_config_t cfg = { .url = api, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota" };
    ESP_LOGI(TAG, "Checking updates for repo %s (pre=%d)", repo, prerelease);
    ESP_LOGD(TAG, "Release API URL: %s", api);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "esp_http_client_init failed"); return ESP_FAIL; }
    esp_err_t err = esp_http_client_open(client, 0);
    ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    int len = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "Header length %d", len);
    char *data = malloc(len + 1);
    if (!data) { ESP_LOGE(TAG, "malloc failed"); esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
    int read = esp_http_client_read_response(client, data, len);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
    data[read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) { ESP_LOGE(TAG, "cJSON_Parse failed"); return ESP_FAIL; }
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
            ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
            if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
            len = esp_http_client_fetch_headers(client);
            ESP_LOGD(TAG, "Header length %d", len);
            data = malloc(len + 1);
            if (!data) { ESP_LOGE(TAG, "malloc failed"); esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
            read = esp_http_client_read_response(client, data, len);
            status = esp_http_client_get_status_code(client);
            ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
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
    if (!release) { cJSON_Delete(json); ESP_LOGE(TAG, "No suitable release"); return ESP_FAIL; }
    ESP_LOGD(TAG, "Release selected");
    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    if (!cJSON_IsArray(assets)) { cJSON_Delete(json); return ESP_FAIL; }
    ESP_LOGD(TAG, "Scanning assets for firmware and signature");
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
    ESP_LOGD(TAG, "Firmware URL: %s", fw);
    ESP_LOGD(TAG, "Signature URL: %s", sig);
    ESP_LOGI(TAG, "Release %s selected", cJSON_IsString(tag)?tag->valuestring:"?");
    esp_err_t res = github_update_from_urls(fw, sig);
    ESP_LOGD(TAG, "github_update_from_urls -> %s", esp_err_to_name(res));
    cJSON_Delete(json);
    return res;
}
