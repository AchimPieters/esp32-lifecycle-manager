#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void lcm32_drd_set_window_ms(uint32_t window_ms);
bool lcm32_drd_was_triggered(void);
#ifdef __cplusplus
}
#endif
