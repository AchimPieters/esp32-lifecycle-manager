#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call dit zo VROEG mogelijk in de boot (bv. in app_main() als eerste regel).
void lcm_fast_reset_init(void);

// Optioneel: parameters tunen vanuit je app
typedef struct {
    const char* nvs_namespace; // default "lcm_rst"
    int threshold;             // default 10 (aantal snelle cycli)
    int64_t stable_ms;         // default 8000 (ms: >= dit = teller wissen)
} lcm_fast_reset_cfg_t;

void lcm_fast_reset_init_with_cfg(const lcm_fast_reset_cfg_t* cfg);

#ifdef __cplusplus
}
#endif
