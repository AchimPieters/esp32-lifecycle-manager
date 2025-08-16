#include "lcm32.h"
#include "portal.h"
#include "drd.h"
#include "ota_sig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "LCM32";
static const char* NVS_NS = "ota";

static void nvs_get_str2(nvs_handle_t h, const char* key, char* out, size_t outlen) {
    size_t len = outlen;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = 0;
}

bool lcm32_get_config(lcm32_config_t* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    nvs_get_str2(h, "repo_url", out->repo_url, sizeof(out->repo_url));
    nvs_get_str2(h, "gh_token", out->gh_token, sizeof(out->gh_token));
    uint8_t pre = 0;
    nvs_get_u8(h, "allow_prerelease", &pre);
    out->allow_prerelease = pre ? true : false;
    nvs_close(h);
    return true;
}

bool lcm32_set_config(const lcm32_config_t* cfg) {
    if (!cfg) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_set_str(h, "repo_url", cfg->repo_url);
    esp_err_t e2 = nvs_set_str(h, "gh_token", cfg->gh_token);
    esp_err_t e3 = nvs_set_u8(h, "allow_prerelease", cfg->allow_prerelease ? 1 : 0);
    esp_err_t c = nvs_commit(h);
    nvs_close(h);
    return (e1 == ESP_OK && e2 == ESP_OK && e3 == ESP_OK && c == ESP_OK);
}

static bool sanitize_repo(const char* in, char* owner, size_t olen, char* repo, size_t rlen) {
    const char* p = strstr(in, "github.com/");
    const char* base = in;
    if (p) base = p + strlen("github.com/");
    const char* slash = strchr(base, '/');
    if (!slash) return false;
    size_t lo = slash - base;
    size_t lr = strlen(slash+1);
    if (lo == 0 || lr == 0) return false;
    if (lo >= olen) lo = olen - 1;
    if (lr >= rlen) lr = rlen - 1;
    memcpy(owner, base, lo); owner[lo] = 0;
    memcpy(repo, slash+1, lr); repo[lr] = 0;
    char* end = strpbrk(repo, "/#?");
    if (end) *end = 0;
    end = strstr(repo, ".git");
    if (end && end[4]==0) *end = 0;
    return true;
}

static esp_err_t http_fetch(const char* url, const char* token, char** out, size_t* outlen) {
    *out = NULL; *outlen = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", "LCM32/1.0 (+ESP-IDF)");
    if (token && token[0]) {
        char bearer[128]; snprintf(bearer, sizeof(bearer), "Bearer %s", token);
        esp_http_client_set_header(client, "Authorization", bearer);
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    int content_len = esp_http_client_fetch_headers(client);
    if (content_len < 0) content_len = 4096;
    size_t cap = (size_t)content_len; if (cap < 4096) cap = 4096;
    char* buf = (char*)malloc(cap+1); if (!buf) { esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
    int total = 0;
    while (1) {
        int r = esp_http_client_read(client, buf+total, cap-total);
        if (r <= 0) break;
        total += r;
        if ((size_t)total >= cap) {
            cap *= 2; char* n = (char*)realloc(buf, cap+1);
            if (!n) { free(buf); esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
            buf = n;
        }
    }
    buf[total] = 0; *out = buf; *outlen = (size_t)total;
    esp_http_client_close(client); esp_http_client_cleanup(client);
    return ESP_OK;
}

static esp_err_t http_stream_to_ota(const char* url, const char* token, const sig_info_t* expect) {
    esp_http_client_config_t cfg = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 20000 };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", "LCM32/1.0 (+ESP-IDF)");
    if (token && token[0]) { char bearer[128]; snprintf(bearer, sizeof(bearer), "Bearer %s", token); esp_http_client_set_header(client, "Authorization", bearer); }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part) { esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_FAIL; }
    esp_ota_handle_t ota = 0;
    err = esp_ota_begin(part, expect ? expect->fw_size : OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) { esp_http_client_close(client); esp_http_client_cleanup(client); return err; }
    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) { esp_ota_end(ota); esp_http_client_close(client); esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
    sig_ctx_t sctx; if (expect) sig_ctx_init(&sctx);
    while (1) {
        int r = esp_http_client_read(client, (char*)buf, 4096);
        if (r <= 0) break;
        err = esp_ota_write(ota, buf, r);
        if (err != ESP_OK) break;
        if (expect) sig_ctx_update(&sctx, buf, r);
    }
    free(buf);
    esp_http_client_close(client); esp_http_client_cleanup(client);
    if (err == ESP_OK) err = esp_ota_end(ota);
    if (err != ESP_OK) return err;
    if (expect) { if (!sig_ctx_finish(&sctx, expect)) { ESP_LOGE(TAG, "Signature or size mismatch"); return ESP_ERR_INVALID_CRC; } }
    err = esp_ota_set_boot_partition(part);
    if (err == ESP_OK) { ESP_LOGI(TAG, "OTA done, rebooting"); esp_restart(); }
    return err;
}

static bool parse_release_and_pick_assets(const char* json, bool allow_pre, char* tag, size_t taglen, char* bin_url, size_t blen, char* sig_url, size_t slen) {
    cJSON* root = cJSON_Parse(json); if (!root) return false;
    cJSON* list = NULL;
    if (cJSON_IsArray(root)) list = root;
    else { list = cJSON_CreateArray(); cJSON_AddItemToArray(list, cJSON_Duplicate(root, 1)); }
    bool found = false; cJSON* rel = NULL;
    cJSON_ArrayForEach(rel, list) {
        cJSON* prerelease = cJSON_GetObjectItem(rel, "prerelease");
        if (!allow_pre && cJSON_IsBool(prerelease) && prerelease->valueint) continue;
        cJSON* tag_name = cJSON_GetObjectItem(rel, "tag_name");
        cJSON* assets = cJSON_GetObjectItem(rel, "assets");
        if (!cJSON_IsString(tag_name) || !cJSON_IsArray(assets)) continue;
        cJSON* a = NULL; char burl[384] = {0}, surl[384] = {0};
        cJSON_ArrayForEach(a, assets) {
            cJSON* name = cJSON_GetObjectItem(a, "name");
            cJSON* dl = cJSON_GetObjectItem(a, "browser_download_url");
            if (!cJSON_IsString(name) || !cJSON_IsString(dl)) continue;
            if (strcmp(name->valuestring, "main.bin") == 0) snprintf(burl, sizeof(burl), "%s", dl->valuestring);
            if (strcmp(name->valuestring, "main.bin.sig") == 0) snprintf(surl, sizeof(surl), "%s", dl->valuestring);
        }
        if (burl[0] && surl[0]) {
            snprintf(tag, taglen, "%s", tag_name->valuestring);
            snprintf(bin_url, blen, "%s", burl);
            snprintf(sig_url, slen, "%s", surl);
            found = true; break;
        }
    }
    if (list != root) cJSON_Delete(list);
    cJSON_Delete(root);
    return found;
}

static int semver_cmp(const char* a, const char* b) {
    const char* pa = (*a=='v'||*a=='V')? a+1:a;
    const char* pb = (*b=='v'||*b=='V')? b+1:b;
    int ma=0, mi=0, pa_ = 0; int mb=0, mj=0, pb_ = 0;
    sscanf(pa, "%d.%d.%d", &ma, &mi, &pa_);
    sscanf(pb, "%d.%d.%d", &mb, &mj, &pb_);
    if (ma!=mb) return (ma<mb)?-1:1;
    if (mi!=mj) return (mi<mj)?-1:1;
    if (pa_!=pb_) return (pa_<pb_)?-1:1;
    return strcmp(pa, pb);
}
static bool is_newer_tag(const char* newtag, const char* current) {
    if (!current || !current[0]) return true;
    return semver_cmp(current, newtag) < 0;
}

bool lcm32_check_and_update(void) {
    lcm32_config_t cfg={0};
    if (!lcm32_get_config(&cfg) || cfg.repo_url[0]==0) {
        ESP_LOGW(TAG, "No repo configured; open /ota and set Owner/Repo");
        return false;
    }
    char owner[96]={0}, repo[96]={0};
    if (!sanitize_repo(cfg.repo_url, owner, sizeof(owner), repo, sizeof(repo))) {
        ESP_LOGE(TAG, "Invalid repo_url: %s", cfg.repo_url);
        return false;
    }
    ESP_LOGI(TAG, "OTA: Checking for update for %s/%s", owner, repo);
    char api_url[256];
    snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/%s/%s/releases", owner, repo);
    char* json=NULL; size_t jlen=0;
    if (http_fetch(api_url, cfg.gh_token, &json, &jlen) != ESP_OK || !json) {
        ESP_LOGE(TAG, "Failed to fetch releases");
        return false;
    }
    char tag[64]={0}, bin_url[384]={0}, sig_url[384]={0};
    if (!parse_release_and_pick_assets(json, cfg.allow_prerelease, tag, sizeof(tag), bin_url, sizeof(bin_url), sig_url, sizeof(sig_url))) {
        free(json);
        ESP_LOGW(TAG, "No suitable release found with main.bin and main.bin.sig");
        return false;
    }
    free(json);
    char current[64]={0};
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len=sizeof(current); nvs_get_str(h, "current_version", current, &len);
        nvs_close(h);
    }
    if (!cfg.force_update && !is_newer_tag(tag, current)) {
        ESP_LOGI(TAG, "No newer version (current=%s, latest=%s)", current[0]?current:"<none>", tag);
        return false;
    }
    ESP_LOGI(TAG, "OTA: Update found (%s -> %s), installing...", current[0]?current:"<none>", tag);
    char* sigbuf=NULL; size_t siglen=0;
    if (http_fetch(sig_url, cfg.gh_token, &sigbuf, &siglen) != ESP_OK || !sigbuf) {
        ESP_LOGE(TAG, "Failed to download signature");
        return false;
    }
    sig_info_t expect={0};
    bool ok = sig_parse((const uint8_t*)sigbuf, siglen, &expect);
    free(sigbuf);
    if (!ok) { ESP_LOGE(TAG, "Invalid signature file"); return false; }
    esp_err_t err = http_stream_to_ota(bin_url, cfg.gh_token, &expect);
    if (err != ESP_OK) { ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err)); return false; }
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "current_version", tag);
        nvs_commit(h); nvs_close(h);
    }
    return true;
}

void lcm32_erase_wifi_and_reboot(void) {
    ESP_LOGW(TAG, "Erasing Wi-Fi config and rebooting...");
    esp_wifi_restore(); esp_restart();
}

void lcm32_factory_reset_and_reboot(void) {
    ESP_LOGW(TAG, "Factory reset: erasing NVS namespaces 'wifi' and 'ota'");
    nvs_handle_t h;
    if (nvs_open_from_partition("nvs", "wifi", NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    esp_restart();
}

void lcm32_init(void) {
    static bool inited = false; if (inited) return; inited = true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    bool drd = lcm32_drd_was_triggered();
    if (drd) lcm32_portal_start();
}
