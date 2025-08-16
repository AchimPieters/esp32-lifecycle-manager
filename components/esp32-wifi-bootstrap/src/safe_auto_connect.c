// Compatibility shim for IDF v4/v5: esp_wifi_set_auto_connect() is removed in IDF v5.
#include <stdbool.h>
#include "esp_err.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR < 5
#include "esp_wifi.h"
#endif

esp_err_t safe_set_auto_connect(bool enable)
{
#if ESP_IDF_VERSION_MAJOR >= 5
    (void)enable;
    // No-op on IDF v5+, keep previous behaviour (always allow automatic reconnect)
    return ESP_OK;
#else
    return esp_wifi_set_auto_connect(enable);
#endif
}
