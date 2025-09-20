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

#ifdef __cplusplus
}
#endif
