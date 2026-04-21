#pragma once

#include <stdbool.h>
#include <stdint.h>

bool lifecycle_restart_counter_process(uint32_t threshold_min,
                                       uint32_t threshold_max,
                                       uint32_t reset_timeout_ms,
                                       void (*on_factory_reset)(void));
