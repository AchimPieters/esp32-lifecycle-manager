#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIFECYCLE_RESTART_COUNTER_THRESHOLD_MIN 10U
#define LIFECYCLE_RESTART_COUNTER_THRESHOLD_MAX 12U
#define LIFECYCLE_RESTART_COUNTER_RESET_TIMEOUT_MS 5000U

/**
 * @brief Returns true if the restart counter storage was successfully initialized.
 */
bool lifecycle_restart_counter_is_available(void);

/**
 * @brief Get the current restart counter value recorded at boot.
 */
uint32_t lifecycle_restart_counter_get(void);

/**
 * @brief Reset the restart counter to zero and persist the change.
 */
void lifecycle_restart_counter_reset(void);

/**
 * @brief Schedule automatic reset of the restart counter after the timeout.
 */
void lifecycle_restart_counter_schedule_reset(void);

#ifdef __cplusplus
}
#endif
