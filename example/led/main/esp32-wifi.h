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

// Verwijder opgeslagen WiFi-instellingen uit NVS (wifi_cfg namespace).
// Het is geen fout wanneer er geen opgeslagen configuratie aanwezig is.
esp_err_t wifi_reset_settings(void);

#ifdef __cplusplus
}
#endif
