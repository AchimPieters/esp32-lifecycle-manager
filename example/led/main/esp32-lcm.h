#pragma once

#include <esp_err.h>
#include <driver/gpio.h>
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

#define LIFECYCLE_FW_REVISION_MAX_LEN 32

#endif /* __HOMEKIT_CUSTOM_CHARACTERISTICS__ */

#ifdef __cplusplus
extern "C" {
#endif

// Initialiseer NVS en voer automatische herstelactie uit wanneer er geen ruimte is of versie verandert.
esp_err_t lifecycle_nvs_init(void);

// Lifecycle acties die ook door de BOOT-knop of HomeKit aangeroepen worden.
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
                                  homekit_value_t value);

typedef enum {
    LIFECYCLE_BUTTON_EVENT_SINGLE = 0,
    LIFECYCLE_BUTTON_EVENT_DOUBLE,
    LIFECYCLE_BUTTON_EVENT_LONG,
} lifecycle_button_event_t;

typedef enum {
    LIFECYCLE_BUTTON_ACTION_NONE = 0,
    LIFECYCLE_BUTTON_ACTION_REQUEST_UPDATE,
    LIFECYCLE_BUTTON_ACTION_RESET_HOMEKIT,
    LIFECYCLE_BUTTON_ACTION_FACTORY_RESET,
} lifecycle_button_action_t;

typedef void (*lifecycle_button_event_cb_t)(lifecycle_button_event_t event, void *ctx);

typedef struct {
    gpio_num_t gpio;
    uint32_t debounce_us;
    uint32_t double_click_us;
    uint32_t long_press_us;
    lifecycle_button_action_t single_action;
    lifecycle_button_action_t double_action;
    lifecycle_button_action_t long_action;
    lifecycle_button_event_cb_t event_callback;
    void *event_context;
} lifecycle_button_config_t;

// Initialiseer de BOOT-knop state machine met de opgegeven acties en optionele callback.
esp_err_t lifecycle_button_init(const lifecycle_button_config_t *config);

// Start WiFi STA op basis van NVS keys (namespace: wifi_cfg, keys: wifi_ssid, wifi_password).
// Roep 'on_ready' aan zodra IP is verkregen.
esp_err_t wifi_start(void (*on_ready)(void));

// Optioneel: stop WiFi netjes.
esp_err_t wifi_stop(void);

#ifdef __cplusplus
}
#endif
