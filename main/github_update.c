#include "github_update.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha512.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "github_update";

// LED blinking control provided by main.c
extern void led_blinking_start(void);
extern void led_blinking_stop(void);

static bool parse_version(const char *str, int *maj, int *min, int *pat) {
    *maj = *min = *pat = 0;
    if (!str) {
        ESP_LOGE(TAG, "Version string is null");
        return false;
    }
    if (str[0] == 'v' || str[0] == 'V')
        str++;
    int parsed = sscanf(str, "%d.%d.%d", maj, min, pat);
    if (parsed < 3) {
        ESP_LOGE(TAG, "Invalid version string: %s", str);
        *maj = *min = *pat = 0;
        return false;
    }
    return true;
}

static int cmp_version(int aMaj, int aMin, int aPat, int bMaj, int bMin,
                       int bPat) {
    if (aMaj != bMaj)
        return aMaj - bMaj;
    if (aMin != bMin)
        return aMin - bMin;
    return aPat - bPat;
}

esp_err_t save_fw_config(const char *repo, bool pre) {
    ESP_LOGD(TAG, "Saving firmware config repo=%s pre=%d",
             repo ? repo : "(null)", pre);
    nvs_handle_t h;
    esp_err_t err = nvs_open("fwcfg", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    if ((err = nvs_set_str(h, "repo", repo ? repo : "")) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    if ((err = nvs_set_u8(h, "pre", pre ? 1 : 0)) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8 failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Firmware config saved");
    }
    nvs_close(h);
    return err;
}

bool load_fw_config(char *repo, size_t repo_len, bool *pre) {
    ESP_LOGD(TAG, "Loading firmware config");
    nvs_handle_t h;
    esp_err_t err = nvs_open("fwcfg", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fwcfg namespace not found");
        return false;
    }

    if (repo) {
        size_t len = repo_len;
        err = nvs_get_str(h, "repo", repo, &len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "nvs_get_str(repo) failed: %s", esp_err_to_name(err));
            nvs_close(h);
            return false;
        }
    }

    uint8_t pre_u8;
    err = nvs_get_u8(h, "pre", &pre_u8);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_u8(pre) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return false;
    }
    if (pre)
        *pre = pre_u8 != 0;
    nvs_close(h);
    ESP_LOGD(TAG, "Loaded firmware config repo=%s pre=%d",
             repo ? repo : "(null)", pre ? *pre : pre_u8);
    return true;
}

esp_err_t save_led_config(bool enabled, int gpio) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("fwcfg", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    if ((err = nvs_set_u8(h, "led_en", enabled ? 1 : 0)) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8(led_en) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    if ((err = nvs_set_i32(h, "led_gpio", gpio)) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_i32(led_gpio) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
    return err;
}

bool load_led_config(bool *enabled, int *gpio) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("fwcfg", NVS_READONLY, &h);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t en;
    int32_t pin;
    err = nvs_get_u8(h, "led_en", &en);
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    err = nvs_get_i32(h, "led_gpio", &pin);
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    if (enabled)
        *enabled = en != 0;
    if (gpio)
        *gpio = (int)pin;
    nvs_close(h);
    return true;
}

static esp_err_t download_signature(const char *url, uint8_t *buf,
                                    size_t buf_len, size_t *out_len) {
    ESP_LOGD(TAG, "Initializing HTTP client for %s", url);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp32-ota",
        // GitHub release assets use redirects with extremely long Location
        // headers (currently >5k and sometimes exceeding 8k), so give the HTTP
        // client a generously sized buffer to ensure the Location header fits
        // entirely.
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
        esp_err_t url_err =
            esp_http_client_get_url(client, cur_url, sizeof(cur_url));
        ESP_LOGD(TAG, "Attempt %d URL: %s", redirects + 1,
                 url_err == ESP_OK ? cur_url : "(null)");
        ESP_LOGD(TAG, "Opening HTTP connection");
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_http_client_open failed: %s",
                     esp_err_to_name(err));
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
        if (status == 301 || status == 302 || status == 303 || status == 307 ||
            status == 308) {
            ESP_LOGD(TAG, "Handling HTTP redirect");
            esp_err_t redir = esp_http_client_set_redirection(client);
            if (redir != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set redirection: %s",
                         esp_err_to_name(redir));
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            if (esp_http_client_get_url(client, cur_url, sizeof(cur_url)) ==
                ESP_OK) {
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

static esp_err_t partition_sha384(const esp_partition_t *part, uint32_t len,
                                  uint8_t *out) {
    mbedtls_sha512_context ctx;
    uint8_t *buf = malloc(4096);
    if (!buf)
        return ESP_ERR_NO_MEM;
    mbedtls_sha512_init(&ctx);
    mbedtls_sha512_starts(&ctx, 1); // 1 -> SHA-384
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t to_read = len - offset;
        if (to_read > 4096)
            to_read = 4096;
        esp_err_t r = esp_partition_read(part, offset, buf, to_read);
        if (r != ESP_OK) {
            free(buf);
            mbedtls_sha512_free(&ctx);
            return r;
        }
        mbedtls_sha512_update(&ctx, buf, to_read);
        offset += to_read;
    }
    mbedtls_sha512_finish(&ctx, out);
    mbedtls_sha512_free(&ctx);
    free(buf);
    return ESP_OK;
}

esp_err_t github_update_from_urls(const char *fw_url, const char *sig_url) {
    ESP_LOGI(TAG, "Downloading signature from %s", sig_url);
    uint8_t sig[52];
    size_t sig_len = 0;
    esp_err_t sig_res = download_signature(sig_url, sig, sizeof(sig), &sig_len);
    if (sig_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download signature: %s",
                 esp_err_to_name(sig_res));
        return sig_res;
    }
    ESP_LOGI(TAG, "Downloaded signature (%u bytes)", (unsigned)sig_len);
    if (sig_len != sizeof(sig)) {
        ESP_LOGE(TAG, "Signature length %u unexpected", (unsigned)sig_len);
        return ESP_FAIL;
    }

    const esp_partition_t *update_part =
        esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Using OTA partition at 0x%lx (%lu bytes)",
             (unsigned long)update_part->address,
             (unsigned long)update_part->size);
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
    esp_err_t ret = esp_https_ota(&ota_cfg);
    ESP_LOGD(TAG, "esp_https_ota -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "OTA download complete");
    esp_partition_pos_t pos = {.offset = update_part->address,
                               .size = update_part->size};
    esp_image_metadata_t meta;
    esp_err_t meta_res = esp_image_get_metadata(&pos, &meta);
    ESP_LOGD(TAG, "esp_image_get_metadata -> %s", esp_err_to_name(meta_res));
    if (meta_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get image metadata: %s",
                 esp_err_to_name(meta_res));
        return meta_res;
    }
    uint8_t expected_hash[48];
    memcpy(expected_hash, sig, sizeof(expected_hash));
    uint32_t expected_len = ((uint32_t)sig[48] << 24) |
                            ((uint32_t)sig[49] << 16) |
                            ((uint32_t)sig[50] << 8) | (uint32_t)sig[51];
    if (expected_len != meta.image_len) {
        ESP_LOGE(TAG, "Image length mismatch: expected %u got %u",
                 (unsigned)expected_len, (unsigned)meta.image_len);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Image length verified (%u bytes)", (unsigned)meta.image_len);
    uint8_t actual[48];
    esp_err_t hash_res = partition_sha384(update_part, meta.image_len, actual);
    ESP_LOGD(TAG, "partition_sha384 -> %s", esp_err_to_name(hash_res));
    ESP_LOGD(TAG, "Image SHA384:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, actual, sizeof(actual), ESP_LOG_DEBUG);
    if (memcmp(actual, expected_hash, sizeof(actual)) != 0) {
        ESP_LOGE(TAG, "SHA384 mismatch");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Signature verified, rebooting");
    esp_restart();
    return ESP_OK;
}

esp_err_t github_update_if_needed(const char *repo, bool prerelease) {
    char api[256];
    snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases%s",
             repo, prerelease ? "?per_page=5" : "/latest");
    esp_http_client_config_t cfg = {.url = api,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .user_agent = "esp32-ota"};
    ESP_LOGI(TAG, "Checking updates for repo %s (pre=%d)", repo, prerelease);
    ESP_LOGD(TAG, "Release API URL: %s", api);
    const esp_app_desc_t *cur = esp_app_get_description();
    int curMaj, curMin, curPat;
    bool curValid =
        parse_version(cur ? cur->version : NULL, &curMaj, &curMin, &curPat);
    ESP_LOGI(TAG, "Current firmware version %d.%d.%d", curMaj, curMin, curPat);
    if (!curValid) {
        ESP_LOGE(TAG, "Invalid current firmware version, assuming 0.0.0");
    }
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
    int len = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "Header length %d", len);
    if (len < 0) {
        ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: errno=%d",
                 esp_http_client_get_errno(client));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    char *data = malloc(len + 1);
    if (!data) {
        ESP_LOGE(TAG, "malloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int read = esp_http_client_read_response(client, data, len);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
    if (read <= 0) {
        ESP_LOGE(TAG, "esp_http_client_read_response failed: errno=%d",
                 esp_http_client_get_errno(client));
        free(data);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    data[read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) {
        ESP_LOGE(TAG, "cJSON_Parse failed");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Release JSON parsed");

    cJSON *release = NULL;
    if (prerelease) {
        ESP_LOGD(TAG, "Prerelease mode: using first release from list");
        if (!cJSON_IsArray(json)) {
            cJSON_Delete(json);
            return ESP_FAIL;
        }
        release = cJSON_GetArrayItem(json, 0);
    } else {
        if (cJSON_IsArray(json)) {
            ESP_LOGD(TAG, "Array returned, searching for stable release");
            for (int i = 0; i < cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) {
                    release = rel;
                    break;
                }
            }
        } else {
            ESP_LOGD(TAG, "Object returned from /latest endpoint");
            release = json;
            cJSON *pre = cJSON_GetObjectItem(release, "prerelease");
            if (cJSON_IsTrue(pre))
                release = NULL; // need to fetch list
        }
        if (!release) {
            ESP_LOGW(TAG, "Falling back to full release list");
            cJSON_Delete(json);
            snprintf(api, sizeof(api),
                     "https://api.github.com/repos/%s/releases?per_page=5",
                     repo);
            cfg.url = api;
            client = esp_http_client_init(&cfg);
            if (!client)
                return ESP_FAIL;
            err = esp_http_client_open(client, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_http_client_open failed: %s",
                         esp_err_to_name(err));
                esp_http_client_cleanup(client);
                return err;
            }
            ESP_LOGD(TAG, "esp_http_client_open -> %s", esp_err_to_name(err));
            len = esp_http_client_fetch_headers(client);
            ESP_LOGD(TAG, "Header length %d", len);
            if (len < 0) {
                ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: errno=%d",
                         esp_http_client_get_errno(client));
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
            data = malloc(len + 1);
            if (!data) {
                ESP_LOGE(TAG, "malloc failed");
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            read = esp_http_client_read_response(client, data, len);
            status = esp_http_client_get_status_code(client);
            ESP_LOGD(TAG, "Read %d bytes with HTTP status %d", read, status);
            if (read <= 0) {
                ESP_LOGE(TAG, "esp_http_client_read_response failed: errno=%d",
                         esp_http_client_get_errno(client));
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
            if (!json || !cJSON_IsArray(json) ||
                cJSON_GetArraySize(json) == 0) {
                ESP_LOGE(TAG, "No releases in list");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
            for (int i = 0; i < cJSON_GetArraySize(json); ++i) {
                cJSON *rel = cJSON_GetArrayItem(json, i);
                cJSON *pre = cJSON_GetObjectItem(rel, "prerelease");
                if (!cJSON_IsTrue(pre)) {
                    release = rel;
                    break;
                }
            }
        }
    }
    if (!release) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "No suitable release");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Release selected");
    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    if (!cJSON_IsArray(assets)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    const char *fw = NULL, *sig = NULL;
    cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
    int relMaj, relMin, relPat;
    bool relValid = parse_version(cJSON_IsString(tag) ? tag->valuestring : NULL,
                                  &relMaj, &relMin, &relPat);
    ESP_LOGI(TAG, "Latest release version %d.%d.%d", relMaj, relMin, relPat);
    if (!relValid) {
        ESP_LOGE(TAG, "Invalid release version, assuming 0.0.0");
    }
    if (relValid && curValid &&
        cmp_version(relMaj, relMin, relPat, curMaj, curMin, curPat) <= 0) {
        ESP_LOGI(TAG, "Firmware already up to date");
        cJSON_Delete(json);
        return ESP_OK;
    }
    ESP_LOGD(TAG, "Scanning assets for firmware and signature");
    for (int i = 0; i < cJSON_GetArraySize(assets); ++i) {
        cJSON *a = cJSON_GetArrayItem(assets, i);
        cJSON *name = cJSON_GetObjectItem(a, "name");
        cJSON *url = cJSON_GetObjectItem(a, "browser_download_url");
        if (cJSON_IsString(name) && cJSON_IsString(url)) {
            if (!strcmp(name->valuestring, "main.bin"))
                fw = url->valuestring;
            else if (!strcmp(name->valuestring, "main.bin.sig"))
                sig = url->valuestring;
        }
    }
    if (!fw || !sig) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "Missing assets");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Firmware URL: %s", fw);
    ESP_LOGD(TAG, "Signature URL: %s", sig);
    ESP_LOGI(TAG, "Release %s selected",
             cJSON_IsString(tag) ? tag->valuestring : "?");
    led_blinking_start();
    esp_err_t res = github_update_from_urls(fw, sig);
    led_blinking_stop();
    ESP_LOGD(TAG, "github_update_from_urls -> %s", esp_err_to_name(res));
    cJSON_Delete(json);
    return res;
}
