#pragma once

#include <sdkconfig.h>

#include <esp_err.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#ifndef __HOMEKIT_CUSTOM_CHARACTERISTICS__
#define __HOMEKIT_CUSTOM_CHARACTERISTICS__

#define HOMEKIT_CUSTOM_UUID(value) (value "-0e36-4a42-ad11-745a73b84f2b")

#define HOMEKIT_SERVICE_CUSTOM_SETUP HOMEKIT_CUSTOM_UUID("000000FF")

#define HOMEKIT_CHARACTERISTIC_CUSTOM_OTA_TRIGGER HOMEKIT_CUSTOM_UUID("F0000001")
#define HOMEKIT_DECLARE_CHARACTERISTIC_CUSTOM_OTA_TRIGGER(_value, ...) \
    .type = HOMEKIT_CHARACTERISTIC_CUSTOM_OTA_TRIGGER, \
    .description = "FirmwareUpdate", \
    .format = homekit_format_bool, \
    .permissions = homekit_permissions_paired_read \
        | homekit_permissions_paired_write \
        | homekit_permissions_notify, \
    .value = HOMEKIT_BOOL_(_value), \
    ##__VA_ARGS__

#define API_OTA_TRIGGER HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_TRIGGER, false)

#ifndef LIFECYCLE_DEFAULT_FW_VERSION
#ifdef CONFIG_APP_PROJECT_VER
#define LIFECYCLE_DEFAULT_FW_VERSION CONFIG_APP_PROJECT_VER
#else
#define LIFECYCLE_DEFAULT_FW_VERSION "0.0.1"
#endif
#endif

#define LIFECYCLE_FW_REVISION_MAX_LEN 32

#endif /* __HOMEKIT_CUSTOM_CHARACTERISTICS__ */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIFECYCLE_POST_RESET_NONE = 0,
    LIFECYCLE_POST_RESET_REASON_HOMEKIT = 1,
    LIFECYCLE_POST_RESET_REASON_FACTORY = 2,
    LIFECYCLE_POST_RESET_REASON_UPDATE = 3,
} lifecycle_post_reset_reason_t;

void lifecycle_log_post_reset_state(const char *log_tag);

// Initialiseer NVS en voer automatische herstelactie uit wanneer er geen ruimte is of versie verandert.
esp_err_t lifecycle_nvs_init(void);

// Lifecycle acties die ook door externe triggers aangeroepen kunnen worden.
void lifecycle_request_update_and_reboot(void);
void lifecycle_reset_homekit_and_reboot(void);
void lifecycle_factory_reset_and_reboot(void);

// Koppel de firmware versie karakteristiek aan de opgeslagen versie in NVS.
esp_err_t lifecycle_init_firmware_revision(homekit_characteristic_t *revision,
                                           const char *fallback_version);

// Retrieve the cached firmware revision string. Returns NULL if no revision
// has been initialised yet.
const char *lifecycle_get_firmware_revision_string(void);

// Verwerk de custom HomeKit OTA trigger. Gebruik dit als setter van de characteristic.
void lifecycle_handle_ota_trigger(homekit_characteristic_t *characteristic,
                                  const homekit_value_t value);

// Initialise the HomeKit-facing lifecycle characteristics using defaults and
// stored NVS values. Logs using the provided tag (falls back to the lifecycle
// tag when NULL) and returns the status from the firmware revision
// initialisation.
esp_err_t lifecycle_configure_homekit(homekit_characteristic_t *revision,
                                      homekit_characteristic_t *ota_trigger,
                                      const char *log_tag);

// Start WiFi STA op basis van NVS keys (namespace: wifi_cfg, keys: wifi_ssid, wifi_password).
// Roep 'on_ready' aan zodra IP is verkregen.
esp_err_t wifi_start(void (*on_ready)(void));

// Optioneel: stop WiFi netjes.
esp_err_t wifi_stop(void);

#ifdef __cplusplus
}
#endif
