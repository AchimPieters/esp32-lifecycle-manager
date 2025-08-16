#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void lcm32_drd_set_window_ms(uint32_t window_ms);
bool lcm32_drd_was_triggered(void);
uint32_t lcm32_drd_get_count(void);
#ifdef __cplusplus
}
#endif
