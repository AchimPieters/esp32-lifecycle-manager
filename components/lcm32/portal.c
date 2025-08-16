#include "portal.h"
#include "lcm32.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>

static const char* TAG = "LCM32-PORTAL";
static httpd_handle_t s_server = NULL;

static const char* HTML_FORM =
"<!doctype html><html><head><meta charset='utf-8'><title>LCM32</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{font-family:sans-serif;margin:2rem;max-width:640px}label{display:block;margin:.5rem 0}input[type=text],input[type=password]{width:100%;padding:.5rem}</style>"
"</head><body><h2>ESP32 Lifecycle Manager</h2>"
"<form method='POST' action='/ota/save'>"
"<label>GitHub Repo (Owner/Repo)</label><input name='repo' type='text' placeholder='Owner/Repo'>"
"<label>GitHub Token (optional)</label><input name='token' type='password' placeholder='ghp_xxx'>"
"<label><input name='pre' type='checkbox' value='1'> Allow prereleases</label>"
"<button type='submit'>Save</button>"
"</form></body></html>";

static esp_err_t ota_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void parse_kv(const char* buf, size_t len, const char* key, char* out, size_t outlen) {
    char pattern[32]; snprintf(pattern, sizeof(pattern), "%s=", key);
    const char* p = strstr(buf, pattern); if (!p) return; p += strlen(pattern);
    const char* e = strchr(p, '&'); size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static esp_err_t ota_save_handler(httpd_req_t* req) {
    char buf[512]; int received = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (received <= 0) return ESP_FAIL;
    buf[received] = 0;
    lcm32_config_t cfg = {0}; lcm32_get_config(&cfg);
    parse_kv(buf, received, "repo", cfg.repo_url, sizeof(cfg.repo_url));
    parse_kv(buf, received, "token", cfg.gh_token, sizeof(cfg.gh_token));
    cfg.allow_prerelease = (strstr(buf, "pre=1") != NULL);
    if (!lcm32_set_config(&cfg)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    const char* ok = "<html><body><p>Saved. Reboot device to check for updates.</p><a href='/ota'>Back</a></body></html>";
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Saved repo='%s' pre=%d token=%s", cfg.repo_url, (int)cfg.allow_prerelease, cfg.gh_token[0]?"***":"<none>");
    return ESP_OK;
}

void lcm32_portal_start(void) {
    if (s_server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t get = { .uri="/ota", .method=HTTP_GET, .handler=ota_get_handler };
        httpd_register_uri_handler(s_server, &get);
        httpd_uri_t save = { .uri="/ota/save", .method=HTTP_POST, .handler=ota_save_handler };
        httpd_register_uri_handler(s_server, &save);
    }
}

void lcm32_portal_stop(void) { if (s_server) { httpd_stop(s_server); s_server = NULL; } }
