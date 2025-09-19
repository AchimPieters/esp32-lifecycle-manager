#pragma once

#include <esp_err.h>

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
