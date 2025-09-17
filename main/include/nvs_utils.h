#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS flash, erasing and retrying if recovery is needed.
 *
 * @return ESP_OK on success, otherwise an error code from the NVS APIs.
 */
esp_err_t nvs_init_with_recovery(void);

#ifdef __cplusplus
}
#endif

