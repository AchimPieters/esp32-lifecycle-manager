#include "ota.h"
#include "esp_crt_bundle.h"
#include <cJSON.h>
#include <ctype.h>
#include <driver/ledc.h>
#include <esp_http_client.h>
#include <esp_idf_version.h>
#include <esp_image_format.h>
#include <esp_log.h>
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

#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
#define mbedtls_sha512_starts_ret mbedtls_sha512_starts
#define mbedtls_sha512_update_ret mbedtls_sha512_update
#define mbedtls_sha512_finish_ret mbedtls_sha512_finish
#endif

static const char *TAG = "ota";

extern void led_write(bool on);

volatile bool ota_in_progress = false;

static bool ota_partition_has_valid_firmware(void) {
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  if (!part) {
    ESP_LOGW(TAG, "OTA partition ota_0 not found");
    return false;
  }
  esp_image_metadata_t data;
  const esp_partition_pos_t pos = {
      .offset = part->address,
      .size = part->size,
  };
  if (esp_image_verify(ESP_IMAGE_VERIFY, &pos, &data) == ESP_OK) {
    ESP_LOGI(TAG, "OTA partition ota_0 has valid firmware");
    return true;
  }
  ESP_LOGI(TAG, "OTA partition ota_0 lacks valid firmware");
  return false;
}

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY 5000

static TaskHandle_t led_task_handle = NULL;

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

  xTaskCreate(led_fade_task, "ota_led", 1024, NULL, 1, &led_task_handle);
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

static char *http_get(const char *url, const char *auth, int *out_status) {
  ESP_LOGI(TAG, "HTTP GET: %s", url);
  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 10000,
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
  esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
  esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
  if (auth && *auth) {
    char header[160];
    snprintf(header, sizeof(header), "token %s", auth);
    esp_http_client_set_header(client, "Authorization", header);
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection");
    esp_http_client_cleanup(client);
    return NULL;
  }

  int content_length = esp_http_client_fetch_headers(client);
  if (out_status)
    *out_status = esp_http_client_get_status_code(client);
  if (out_status && *out_status != 200) {
    ESP_LOGE(TAG, "HTTP status %d", *out_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }
  if (content_length <= 0)
    content_length = 1024; // default buffer

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
  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 10000,
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
    return false;
  }
  esp_http_client_set_header(client, "Accept", "application/octet-stream");
  esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
  if (auth && *auth) {
    char header[160];
    snprintf(header, sizeof(header), "token %s", auth);
    esp_http_client_set_header(client, "Authorization", header);
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection");
    esp_http_client_cleanup(client);
    return false;
  }
  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
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

static bool download_and_flash(const char *url, const uint8_t *expected_hash,
                               uint32_t expected_size, const char *auth) {
  ESP_LOGI(TAG, "OTA: Start download of main.bin from GitHub: %s", url);
  ota_led_start();

  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 10000,
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
    ota_led_stop();
    return false;
  }
  esp_http_client_set_header(client, "Accept", "application/octet-stream");
  esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
  if (auth && *auth) {
    char header[160];
    snprintf(header, sizeof(header), "token %s", auth);
    esp_http_client_set_header(client, "Authorization", header);
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection");
    esp_http_client_cleanup(client);
    ota_led_stop();
    return false;
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
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
    ESP_LOGW(TAG, "OTA: Signature mismatch or no newer version found");
    esp_ota_abort(ota_handle);
    ota_led_stop();
    return false;
  }
  if (memcmp(hash, expected_hash, 48) != 0) {
    ESP_LOGW(TAG, "OTA: Signature mismatch or no newer version found");
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
                          bool prerelease, bool force_update,
                          const char *auth) {
  ESP_LOGI(TAG, "Checking repository %s (prerelease=%d, force=%d)", repo_url,
           prerelease, force_update);
  ota_in_progress = true;
  char current_version[64] = {0};
  char *stored_version = nvs_get_string(handle, "current_version");
  if (stored_version) {
    sanitize_version_str(stored_version, current_version,
                         sizeof(current_version));
    ESP_LOGI(TAG, "Stored version %s", current_version);
    free(stored_version);
  } else {
    ESP_LOGI(TAG, "No current_version in NVS; forcing update");
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

  int status = 0;
  char *json = http_get(api_url, auth, &status);
  if (!json) {
    ESP_LOGE(TAG, "Failed to fetch release info (status %d)", status);
    ota_in_progress = false;
    return false;
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
    ota_in_progress = false;
    return false;
  }

  const cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
  if (!cJSON_IsString(tag)) {
    cJSON_Delete(root);
    free(json);
    ESP_LOGE(TAG, "No release found at GitHub API URL: %s", api_url);
    ota_in_progress = false;
    return false;
  }

  const char *tag_name = tag->valuestring;
  ESP_LOGI(TAG, "Latest tag %s", tag_name);
  if (!force_update && *current_version &&
      !is_version_newer(current_version, tag_name)) {
    ESP_LOGW(TAG, "OTA: Signature mismatch or no newer version found");
    cJSON_Delete(root);
    free(json);
    ota_in_progress = false;
    return false;
  }

  char sig_url[1024] = {0};
  char fw_url[1024];
  snprintf(fw_url, sizeof(fw_url),
           "https://github.com/%s/releases/download/%s/main.bin", repo_url,
           tag_name);
  ESP_LOGI(TAG, "OTA: Checking for update from %s", fw_url);
  cJSON *assets = cJSON_GetObjectItem(release, "assets");
  if (cJSON_IsArray(assets)) {
    cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
      cJSON *name = cJSON_GetObjectItem(asset, "name");
      if (cJSON_IsString(name) && strcasestr(name->valuestring, ".sig") != NULL) {
        cJSON *download_url =
            cJSON_GetObjectItem(asset, "browser_download_url");
        if (download_url && cJSON_IsString(download_url) &&
            download_url->valuestring) {
          strlcpy(sig_url, download_url->valuestring, sizeof(sig_url));
          ESP_LOGI(TAG, "Found signature asset: %s", name->valuestring);
          ESP_LOGI(TAG, "Signature download URL: %s", sig_url);
        }
      }
    }
  }
  if (!sig_url[0]) {
    snprintf(sig_url, sizeof(sig_url), "%s.sig", fw_url);
    ESP_LOGW(TAG, "No signature asset found; assuming %s", sig_url);
  }

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

  bool has_valid = ota_partition_has_valid_firmware();
  size_t dummy = 0;
  bool has_version =
      nvs_get_str(handle, "current_version", NULL, &dummy) == ESP_OK;

  char *token = nvs_get_string(handle, "github_token");

  if (!has_valid) {
    ESP_LOGI(TAG, "OTA partition empty; installing latest release");
    (void)perform_update(handle, repo_url, prerelease, true, token);
  } else if (!has_version) {
    ESP_LOGI(TAG, "No current_version in NVS; installing latest release");
    (void)perform_update(handle, repo_url, prerelease, true, token);
  } else {
    ESP_LOGI(TAG, "Valid firmware found; checking for updates");
    (void)perform_update(handle, repo_url, prerelease, false, token);
  }

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

  (void)perform_update(handle, repo_url, prerelease, false, token);

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
  ESP_LOGI(TAG, "Initial OTA check complete");
  firmware_update();
  ESP_LOGI(TAG, "OTA task finished");
  vTaskDelete(NULL);
}

void ota_start(void) {
  ESP_LOGI(TAG, "Starting OTA task");
  xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
}
