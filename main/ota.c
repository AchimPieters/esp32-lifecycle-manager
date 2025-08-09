#include "ota.h"
#include "version.h"
#include <cJSON.h>
#include <ctype.h>
#include <driver/ledc.h>
#include <esp_http_client.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha512.h>
#include <mbedtls/version.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
#error "This project requires ESP-IDF v5.4 or higher"
#endif

#define OTA_NAMESPACE "ota"

#define OTA_JSON_BUFFER_SIZE 4096

#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
#define mbedtls_sha512_starts_ret mbedtls_sha512_starts
#define mbedtls_sha512_update_ret mbedtls_sha512_update
#define mbedtls_sha512_finish_ret mbedtls_sha512_finish
#endif

static const char *TAG = "ota";

extern void led_write(bool on);

volatile bool ota_in_progress = false;

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY 5000

static TaskHandle_t led_task_handle = NULL;
static TaskHandle_t ota_task_handle = NULL;

static esp_err_t http_open_with_retry(esp_http_client_handle_t client) {
  const int max_tries = 5;
  int delay_ms = 500;
  for (int i = 0; i < max_tries; i++) {
    if (esp_http_client_open(client, 0) == ESP_OK)
      return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    delay_ms = delay_ms * 2 > 8000 ? 8000 : delay_ms * 2;
  }
  return ESP_FAIL;
}

static void led_fade_task(void *pv) {
  int duty = 0;
  int step = 5;
  int direction = 1;
  while (1) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    duty += step * direction;
    if (duty >= 255) {
      duty = 255;
      direction = -1;
    } else if (duty <= 0) {
      duty = 0;
      direction = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void ota_led_start(void) {
  if (led_task_handle)
    return;

  ledc_timer_config_t timer = {
      .speed_mode = LEDC_MODE,
      .timer_num = LEDC_TIMER,
      .duty_resolution = LEDC_DUTY_RES,
      .freq_hz = LEDC_FREQUENCY,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer);

  ledc_channel_config_t channel = {
      .speed_mode = LEDC_MODE,
      .channel = LEDC_CHANNEL,
      .timer_sel = LEDC_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = CONFIG_ESP_LED_GPIO,
      .duty = 0,
      .hpoint = 0,
  };
  ledc_channel_config(&channel);

  if (xTaskCreate(led_fade_task, "ota_led", 1024, NULL, 1,
                  &led_task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LED fade task");
    led_task_handle = NULL;
  }
}

static void ota_led_stop(void) {
  if (led_task_handle) {
    vTaskDelete(led_task_handle);
    led_task_handle = NULL;
  }
  ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
  led_write(false);
}

static char *nvs_get_string(nvs_handle_t handle, const char *key) {
  ESP_LOGD(TAG, "Reading NVS key '%s'", key);
  size_t required = 0;
  if (nvs_get_str(handle, key, NULL, &required) != ESP_OK || required == 0) {
    ESP_LOGD(TAG, "Key '%s' not found", key);
    return NULL;
  }
  char *value = malloc(required);
  if (!value) {
    ESP_LOGE(TAG, "Failed to allocate memory for key '%s'", key);
    return NULL;
  }
  if (nvs_get_str(handle, key, value, &required) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read key '%s'", key);
    free(value);
    return NULL;
  }
  ESP_LOGD(TAG, "Key '%s' value '%s'", key, value);
  return value;
}

static void sanitize_version_str(const char *in, char *out, size_t len) {
  while (*in && !isdigit((unsigned char)*in)) {
    in++;
  }
  strlcpy(out, in, len);
}

static void normalize_repo_api(const char *input, char *output, size_t len) {
  if (!input || !*input) {
    if (len)
      output[0] = '\0';
    return;
  }
  char temp[256];
  strlcpy(temp, input, sizeof(temp));
  size_t l = strlen(temp);
  while (l > 0 && temp[l - 1] == '/') {
    temp[--l] = '\0';
  }
  if (l > 4 && strcmp(&temp[l - 4], ".git") == 0) {
    temp[l - 4] = '\0';
    l -= 4;
  }
  const char *suffixes[] = {"/releases/latest", "/releases"};
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
    size_t sl = strlen(suffixes[i]);
    if (l >= sl && strcmp(&temp[l - sl], suffixes[i]) == 0) {
      temp[l - sl] = '\0';
      l -= sl;
      while (l > 0 && temp[l - 1] == '/') {
        temp[--l] = '\0';
      }
      break;
    }
  }
  const char *repo_part = temp;
  const char *p = NULL;
  if ((p = strstr(temp, "api.github.com/repos/"))) {
    strlcpy(output, temp, len);
    return;
  }
  if ((p = strstr(temp, "github.com/"))) {
    repo_part = p + strlen("github.com/");
  }

  char repo_part_limited[200];
  strlcpy(repo_part_limited, repo_part, sizeof(repo_part_limited));
  snprintf(output, len, "https://api.github.com/repos/%s", repo_part_limited);
}

static char *http_get(const char *url, const char *auth, const char *etag,
                      const char *last_modified, char **out_etag,
                      char **out_last_modified, int *out_status) {
  ESP_LOGI(TAG, "HTTP GET: %s", url);
  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 15000,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .skip_cert_common_name_check = false,
      .user_agent = "esp32-lcm",
      .disable_auto_redirect = false, // follow GitHub's 302 redirect to S3
      .keep_alive_enable = true,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return NULL;
  }
  esp_http_client_set_header(client, "User-Agent", "esp32-lcm");
  esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
  esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
  if (auth && *auth) {
    char header[160];
    snprintf(header, sizeof(header), "Bearer %s", auth);
    esp_http_client_set_header(client, "Authorization", header);
  }
  if (etag && *etag)
    esp_http_client_set_header(client, "If-None-Match", etag);
  if (last_modified && *last_modified)
    esp_http_client_set_header(client, "If-Modified-Since", last_modified);
  if (http_open_with_retry(client) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection");
    esp_http_client_cleanup(client);
    return NULL;
  }

  int content_length = esp_http_client_fetch_headers(client);
  if (out_status)
    *out_status = esp_http_client_get_status_code(client);
  char *resp_etag = NULL;
  esp_http_client_get_header(client, "ETag", &resp_etag);
  char *resp_last_mod = NULL;
  esp_http_client_get_header(client, "Last-Modified", &resp_last_mod);
  if (out_etag && resp_etag)
    *out_etag = strdup(resp_etag);
  if (out_last_modified && resp_last_mod)
    *out_last_modified = strdup(resp_last_mod);
  if (out_status && *out_status == 304) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }
  if (out_status && *out_status != 200) {
    ESP_LOGE(TAG, "HTTP status %d", *out_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }
  if (content_length < OTA_JSON_BUFFER_SIZE)
    content_length = OTA_JSON_BUFFER_SIZE; // default buffer

  char *buffer = malloc(content_length + 1);
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }

  int read_len = esp_http_client_read_response(client, buffer, content_length);
  if (read_len < 0) {
    ESP_LOGE(TAG, "HTTP read failed");
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }
  buffer[read_len] = '\0';
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "HTTP GET done (%d bytes)", read_len);
  return buffer;
}

static bool download_sig(const char *url, const char *auth, uint8_t *out_hash,
                         uint32_t *out_size) {
  ESP_LOGI(TAG, "Downloading signature");
  char current_url[1024];
  strlcpy(current_url, url, sizeof(current_url));

  for (int redirect = 0; redirect < 5; ++redirect) {
    esp_http_client_config_t config = {
        .url = current_url,
        .timeout_ms = 15000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
        .user_agent = "esp32-lcm",
        .disable_auto_redirect = true,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      ESP_LOGE(TAG, "Failed to init HTTP client");
      return false;
    }
    esp_http_client_set_header(client, "User-Agent", "esp32-lcm");
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    if (auth && *auth) {
      char header[160];
      snprintf(header, sizeof(header), "Bearer %s", auth);
      esp_http_client_set_header(client, "Authorization", header);
    }
    if (http_open_with_retry(client) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to open HTTP connection");
      esp_http_client_cleanup(client);
      return false;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status == 301 || status == 302 || status == 303 || status == 307 ||
        status == 308) {
      char *location = NULL;
      esp_http_client_get_header(client, "Location", &location);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (!location) {
        ESP_LOGE(TAG, "Redirect without Location header");
        return false;
      }
      char new_url[1024];
      if (location[0] == '/') {
        const char *scheme_end = strstr(current_url, "://");
        if (!scheme_end) {
          ESP_LOGE(TAG, "Invalid URL for redirect");
          return false;
        }
        const char *path_start = strchr(scheme_end + 3, '/');
        size_t base_len = path_start ? (size_t)(path_start - current_url) :
                                       strlen(current_url);
        snprintf(new_url, sizeof(new_url), "%.*s%s", (int)base_len, current_url,
                 location);
      } else {
        strlcpy(new_url, location, sizeof(new_url));
      }
      strlcpy(current_url, new_url, sizeof(current_url));
      continue;
    }

    if (status != 200) {
      ESP_LOGE(TAG, "HTTP status %d", status);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    const int sig_buf_len = 256;
    uint8_t *sig = malloc(sig_buf_len);
    if (!sig) {
      ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    int total_read = 0;
    while (total_read < 52) {
      int read_len = esp_http_client_read(client, (char *)sig + total_read,
                                          sig_buf_len - total_read);
      if (read_len < 0) {
        ESP_LOGE(TAG, "HTTP read failed");
        free(sig);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }
      if (read_len == 0)
        break;
      total_read += read_len;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Signature file received: %d bytes", total_read);
    if (total_read != 52) {
      ESP_LOGE(TAG, "Signature parse failed");
      free(sig);
      return false;
    }
    memcpy(out_hash, sig, 48);
    uint32_t size = ((uint32_t)sig[48]) | ((uint32_t)sig[49] << 8) |
                    ((uint32_t)sig[50] << 16) | ((uint32_t)sig[51] << 24);
    *out_size = size;
    free(sig);
    return true;
  }
  ESP_LOGE(TAG, "Too many HTTP redirects");
  return false;
}

static bool download_and_flash(const char *url, const uint8_t *expected_hash,
                               uint32_t expected_size, const char *auth) {
  ESP_LOGI(TAG, "OTA: Start download of main.bin from GitHub: %s", url);
  ota_led_start();

  char current_url[1024];
  strlcpy(current_url, url, sizeof(current_url));
  esp_http_client_handle_t client = NULL;
  int status = 0;

  for (int redirect = 0; redirect < 5; ++redirect) {
    esp_http_client_config_t config = {
        .url = current_url,
        .timeout_ms = 15000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
        .user_agent = "esp32-lcm",
        .disable_auto_redirect = true,
        .keep_alive_enable = true,
    };

    client = esp_http_client_init(&config);
    if (!client) {
      ESP_LOGE(TAG, "Failed to init HTTP client");
      ota_led_stop();
      return false;
    }
    esp_http_client_set_header(client, "User-Agent", "esp32-lcm");
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    if (auth && *auth) {
      char header[160];
      snprintf(header, sizeof(header), "Bearer %s", auth);
      esp_http_client_set_header(client, "Authorization", header);
    }
    if (http_open_with_retry(client) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to open HTTP connection");
      esp_http_client_cleanup(client);
      ota_led_stop();
      return false;
    }
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (status == 301 || status == 302 || status == 303 || status == 307 ||
        status == 308) {
      char *location = NULL;
      esp_http_client_get_header(client, "Location", &location);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (!location) {
        ESP_LOGE(TAG, "Redirect without Location header");
        ota_led_stop();
        return false;
      }
      char new_url[1024];
      if (location[0] == '/') {
        const char *scheme_end = strstr(current_url, "://");
        if (!scheme_end) {
          ESP_LOGE(TAG, "Invalid URL for redirect");
          ota_led_stop();
          return false;
        }
        const char *path_start = strchr(scheme_end + 3, '/');
        size_t base_len = path_start ? (size_t)(path_start - current_url) :
                                       strlen(current_url);
        snprintf(new_url, sizeof(new_url), "%.*s%s", (int)base_len, current_url,
                 location);
      } else {
        strlcpy(new_url, location, sizeof(new_url));
      }
      strlcpy(current_url, new_url, sizeof(current_url));
      continue;
    }
    break;
  }

  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status %d", status);
    if (client) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
    ota_led_stop();
    return false;
  }

  const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
  esp_ota_handle_t ota_handle;
  if (esp_ota_begin(update_part, expected_size, &ota_handle) != ESP_OK) {
    ESP_LOGE(TAG, "OTA begin failed");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ota_led_stop();
    return false;
  }

  const int buf_len = 4096;
  uint8_t *buf = malloc(buf_len);
  if (!buf) {
    ESP_LOGE(TAG, "Failed to allocate firmware buffer");
    esp_ota_abort(ota_handle);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ota_led_stop();
    return false;
  }

  mbedtls_sha512_context ctx;
  mbedtls_sha512_init(&ctx);
  mbedtls_sha512_starts_ret(&ctx, 1);

  int total = 0;
  while (1) {
    int read = esp_http_client_read(client, (char *)buf, buf_len);
    if (read < 0) {
      ESP_LOGE(TAG, "HTTP read failed");
      mbedtls_sha512_free(&ctx);
      free(buf);
      esp_ota_abort(ota_handle);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      ota_led_stop();
      return false;
    }
    if (read == 0)
      break;
    total += read;
    mbedtls_sha512_update_ret(&ctx, buf, read);
    if (esp_ota_write(ota_handle, buf, read) != ESP_OK) {
      ESP_LOGE(TAG, "OTA write failed");
      mbedtls_sha512_free(&ctx);
      free(buf);
      esp_ota_abort(ota_handle);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      ota_led_stop();
      return false;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  ESP_LOGI(TAG, "Firmware file received: %d bytes", total);

  uint8_t hash[48];
  mbedtls_sha512_finish_ret(&ctx, hash);
  mbedtls_sha512_free(&ctx);
  free(buf);

  if (expected_size && total != (int)expected_size) {
    ESP_LOGE(TAG, "OTA size mismatch: expected %u, got %d", expected_size,
             total);
    esp_ota_abort(ota_handle);
    ota_led_stop();
    return false;
  }
  if (memcmp(hash, expected_hash, 48) != 0) {
    ESP_LOGE(TAG, "OTA hash mismatch");
    esp_ota_abort(ota_handle);
    ota_led_stop();
    return false;
  }
  ESP_LOGI(TAG, "Firmware hash verified");

  if (esp_ota_end(ota_handle) != ESP_OK) {
    ESP_LOGE(TAG, "OTA end failed");
    ota_led_stop();
    return false;
  }
  if (esp_ota_set_boot_partition(update_part) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set boot partition");
    ota_led_stop();
    return false;
  }

  ota_led_stop();
  ESP_LOGI(TAG, "OTA update successful");
  return true;
}

static void parse_version(const char *str, int *major, int *minor, int *patch) {
  *major = *minor = *patch = 0;
  if (!str)
    return;
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
  if (lat_major > cur_major)
    return true;
  if (lat_major < cur_major)
    return false;
  if (lat_minor > cur_minor)
    return true;
  if (lat_minor < cur_minor)
    return false;
  if (lat_patch > cur_patch)
    return true;
  return false;
}

static bool perform_update(nvs_handle_t handle, const char *repo_url,
                           bool prerelease, const char *auth) {
  ESP_LOGI(TAG, "Checking repository %s (prerelease=%d)", repo_url, prerelease);
  esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
  ota_in_progress = true;
  char current_version[64] = APP_VERSION;
  ESP_LOGI(TAG, "Build version %s", current_version);
  char *stored_version = nvs_get_string(handle, "current_version");
  if (stored_version) {
    sanitize_version_str(stored_version, current_version,
                         sizeof(current_version));
    ESP_LOGI(TAG, "Stored version %s", current_version);
    free(stored_version);
  } else {
    ESP_LOGI(TAG, "No stored firmware version found (initial run)");
  }

  char api_base[512];
  normalize_repo_api(repo_url, api_base, sizeof(api_base));
  char api_url[1024];
  strlcpy(api_url, api_base, sizeof(api_url));
  const char *suffix = prerelease ? "/releases" : "/releases/latest";
  if (strlcat(api_url, suffix, sizeof(api_url)) >= sizeof(api_url)) {
    ESP_LOGE(TAG, "API URL truncated");
    ota_in_progress = false;
    return false;
  }
  ESP_LOGI(TAG, "GitHub API URL: %s", api_url);

  char *etag = nvs_get_string(handle, "etag");
  char *last_mod = nvs_get_string(handle, "last_modified");
  char *new_etag = NULL;
  char *new_last_mod = NULL;
  int status = 0;
  char *json =
      http_get(api_url, auth, etag, last_mod, &new_etag, &new_last_mod, &status);
  free(etag);
  free(last_mod);
  if (status == 304) {
    ESP_LOGI(TAG, "Release not modified since last check");
    free(new_etag);
    free(new_last_mod);
    ota_in_progress = false;
    return false;
  }
  if (!json) {
    ESP_LOGE(TAG, "Failed to fetch release info (status %d)", status);
    free(new_etag);
    free(new_last_mod);
    ota_in_progress = false;
    return false;
  }
  if (new_etag) {
    nvs_set_str(handle, "etag", new_etag);
    free(new_etag);
  }
  if (new_last_mod) {
    nvs_set_str(handle, "last_modified", new_last_mod);
    free(new_last_mod);
  }
  nvs_commit(handle);
  ESP_LOGI(TAG, "OTA: Received GitHub JSON:\n%s", json);

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
    ota_in_progress = false;
    return false;
  }

  const cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
  const char *tag_name = NULL;
  char parsed_tag[64];
  if (cJSON_IsString(tag)) {
    tag_name = tag->valuestring;
  } else {
    char *tag_pos = strstr(json, "\"tag_name\":");
    if (tag_pos) {
      tag_pos += strlen("\"tag_name\":");
      while (*tag_pos == ' ' || *tag_pos == '\"')
        tag_pos++;
      char *end = strchr(tag_pos, '\"');
      if (end) {
        size_t len = end - tag_pos;
        if (len >= sizeof(parsed_tag))
          len = sizeof(parsed_tag) - 1;
        memcpy(parsed_tag, tag_pos, len);
        parsed_tag[len] = '\0';
        tag_name = parsed_tag;
        ESP_LOGI(TAG, "OTA: Found GitHub release tag: %s", tag_name);
      }
    }
  }
  if (!tag_name) {
    cJSON_Delete(root);
    free(json);
    ESP_LOGE(TAG, "OTA: Failed to parse version from GitHub JSON");
    ota_in_progress = false;
    return false;
  }

  ESP_LOGI(TAG, "Latest tag %s", tag_name);
  if (strcmp(current_version, "0.0.0") == 0) {
    ESP_LOGI(TAG, "No firmware version detected – performing initial OTA...");
  } else if (!is_version_newer(current_version, tag_name)) {
    ESP_LOGI(TAG, "No newer firmware available");
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    return false;
  }

  char fw_url[1024] = {0};
  char sig_url[1024] = {0};
  cJSON *assets = cJSON_GetObjectItem(release, "assets");
  if (cJSON_IsArray(assets)) {
    cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
      cJSON *name = cJSON_GetObjectItem(asset, "name");
      cJSON *download_url =
          cJSON_GetObjectItem(asset, "browser_download_url");
      if (cJSON_IsString(name) && cJSON_IsString(download_url) &&
          download_url->valuestring) {
        if (strcmp(name->valuestring, "main.bin") == 0) {
          strlcpy(fw_url, download_url->valuestring, sizeof(fw_url));
          ESP_LOGI(TAG, "Found firmware asset: %s", fw_url);
        } else if (strcmp(name->valuestring, "main.bin.sig") == 0) {
          strlcpy(sig_url, download_url->valuestring, sizeof(sig_url));
          ESP_LOGI(TAG, "Found signature asset: %s", sig_url);
        }
      }
    }
  }
  if (!fw_url[0] || !sig_url[0]) {
    ESP_LOGE(TAG, "Required release assets not found");
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    return false;
  }

  ESP_LOGI(TAG, "OTA: Checking for update from %s", fw_url);

  uint8_t expected_hash[48];
  uint32_t expected_size = 0;
  if (!download_sig(sig_url, auth, expected_hash, &expected_size)) {
    ESP_LOGE(TAG, "Failed to download signature");
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    return false;
  }
  if (download_and_flash(fw_url, expected_hash, expected_size, auth)) {
    char cleaned_tag[64];
    sanitize_version_str(tag_name, cleaned_tag, sizeof(cleaned_tag));
    nvs_handle_t nvs;
    if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
      nvs_set_blob(nvs, "main_sig", expected_hash, sizeof(expected_hash));
      nvs_set_str(nvs, "current_version", cleaned_tag);
      nvs_commit(nvs);
      nvs_close(nvs);
    } else {
      ESP_LOGE(TAG, "Failed to open NVS to save current version");
    }
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    ESP_LOGI(TAG, "Rebooting to new firmware");
    esp_restart();
    return true;
  } else {
    ESP_LOGE(TAG, "OTA update failed");
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    return false;
  }
}

void ota_check_and_install(void) {
  ESP_LOGI(TAG, "Starting OTA update process...");
  nvs_handle_t handle;
  if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS");
    return;
  }

  char *repo_url = nvs_get_string(handle, "repo_url");
  if (!repo_url) {
    ESP_LOGW(TAG, "No repository URL set in NVS");
    nvs_close(handle);
    return;
  }
  ESP_LOGI(TAG, "Loaded OTA repository URL from NVS: %s", repo_url);

  char *prerelease_str = nvs_get_string(handle, "prerelease");
  bool prerelease = prerelease_str && strcmp(prerelease_str, "1") == 0;
  free(prerelease_str);

  char *token = nvs_get_string(handle, "github_token");

  (void)perform_update(handle, repo_url, prerelease, token);

  free(token);
  free(repo_url);
  nvs_close(handle);
}

void firmware_update(void) {
  ESP_LOGI(TAG, "Checking for new firmware...");
  nvs_handle_t handle;
  if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS");
    return;
  }

  char *repo_url = nvs_get_string(handle, "repo_url");
  if (!repo_url) {
    ESP_LOGW(TAG, "No repository URL set in NVS");
    nvs_close(handle);
    return;
  }
  ESP_LOGI(TAG, "Loaded OTA repository URL from NVS: %s", repo_url);

  char *prerelease_str = nvs_get_string(handle, "prerelease");
  bool prerelease = prerelease_str && strcmp(prerelease_str, "1") == 0;
  free(prerelease_str);
  char *token = nvs_get_string(handle, "github_token");

  (void)perform_update(handle, repo_url, prerelease, token);

  free(token);
  free(repo_url);
  nvs_close(handle);
}

static void ota_task(void *pv) {
  ESP_LOGI(TAG, "OTA task started");
  // Give WiFi a few seconds to stabilize before OTA
  vTaskDelay(pdMS_TO_TICKS(3000));
  ESP_LOGI(TAG, "Checking for firmware updates");
  ota_check_and_install();
  ESP_LOGI(TAG, "OTA task finished");
  ota_task_handle = NULL;
  vTaskDelete(NULL);
}

void ota_start(void) {
  ESP_LOGI(TAG, "Starting OTA task");
  if (ota_task_handle) {
    ESP_LOGW(TAG, "OTA task already running");
    return;
  }
  if (xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, &ota_task_handle) !=
      pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    ota_task_handle = NULL;
  }
}
