#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked when the rapid power-cycle threshold is reached.
 *
 * Implementations should perform the desired factory reset routine and restart
 * the device when finished. The @p ctx pointer matches the value registered via
 * ::lifecycle_register_factory_reset_callback.
 */
typedef void (*lifecycle_factory_reset_callback_t)(void *ctx);

/**
 * @brief Ensure NVS is initialised for the lifecycle manager component.
 *
 * This helper mirrors the standard esp-idf pattern: if NVS lacks free pages or
 * an old version is present the storage is erased and re-initialised. Call this
 * before interacting with lifecycle APIs that touch NVS directly.
 */
esp_err_t lifecycle_nvs_init(void);

/**
 * @brief Log the previous reset reason and update the rapid restart counter.
 *
 * Call this very early in @c app_main so the component can detect consecutive
 * power cycles. When the configured threshold is reached the registered factory
 * reset callback is executed after the optional countdown.
 */
void lifecycle_log_post_reset_state(void);

/**
 * @brief Register the handler executed once the rapid restart threshold hits.
 *
 * Passing @c NULL disables the callback. The @p ctx argument is forwarded to the
 * callback when it executes.
 */
void lifecycle_register_factory_reset_callback(lifecycle_factory_reset_callback_t cb,
                                               void *ctx);

/**
 * @brief Retrieve the currently persisted restart counter value.
 */
uint32_t lifecycle_get_restart_counter(void);

/**
 * @brief Clear the stored restart counter immediately.
 *
 * This also cancels any pending timeout used to reset the counter automatically.
 */
void lifecycle_reset_restart_counter(void);

#ifdef __cplusplus
}
#endif

