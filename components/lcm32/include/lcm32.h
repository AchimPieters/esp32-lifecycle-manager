#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char repo_url[160];
    char gh_token[80];
    bool allow_prerelease;
    bool force_update;
} lcm32_config_t;
void lcm32_init(void);
bool lcm32_get_config(lcm32_config_t* out);
bool lcm32_set_config(const lcm32_config_t* cfg);
bool lcm32_check_and_update(void);
void lcm32_erase_wifi_and_reboot(void);
void lcm32_factory_reset_and_reboot(void);
void lcm32_drd_set_window_ms(uint32_t window_ms);
bool lcm32_drd_was_triggered(void);
void lcm32_portal_start(void);
void lcm32_portal_stop(void);
#ifdef __cplusplus
}
#endif
