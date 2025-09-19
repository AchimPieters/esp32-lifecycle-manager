#pragma once

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
    .description = "}FirmwareUpdate", \
    .format = homekit_format_bool, \
    .permissions = homekit_permissions_paired_read \
        | homekit_permissions_paired_write \
        | homekit_permissions_notify, \
    .value = HOMEKIT_BOOL_(_value), \
    ##__VA_ARGS__

#define API_OTA_TRIGGER HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_TRIGGER, false)

#endif /* __HOMEKIT_CUSTOM_CHARACTERISTICS__ */

#ifdef __cplusplus
extern "C" {
#endif

// Start WiFi STA op basis van NVS keys (namespace: wifi_cfg, keys: wifi_ssid, wifi_password).
// Roep 'on_ready' aan zodra IP is verkregen.
esp_err_t wifi_start(void (*on_ready)(void));

// Optioneel: stop WiFi netjes
esp_err_t wifi_stop(void);

// Lifecycle acties die ook door de BOOT-knop of HomeKit aangeroepen worden.
void lifecycle_request_update_and_reboot(void);
void lifecycle_reset_homekit_and_reboot(void);
void lifecycle_factory_reset_and_reboot(void);

#ifdef __cplusplus
}
#endif
