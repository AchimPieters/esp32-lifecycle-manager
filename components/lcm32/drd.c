#include <inttypes.h>
#include "drd.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

RTC_NOINIT_ATTR static uint64_t last_boot_us;
RTC_NOINIT_ATTR static uint32_t boot_counter;
static const char* TAG = "LCM32-DRD";
static uint32_t s_window_ms = 3000;

void lcm32_drd_set_window_ms(uint32_t window_ms) { s_window_ms = window_ms; }

bool lcm32_drd_was_triggered(void) {
    uint64_t now = esp_timer_get_time();
    bool triggered = false;
    if (last_boot_us != 0) {
        uint64_t delta_ms = (now + 1) / 1000;
        if (delta_ms < s_window_ms) {
            boot_counter++;
        } else {
            boot_counter = 1;
        }
        triggered = (boot_counter >= 2);
    } else {
        boot_counter = 1;
    }
    last_boot_us = 1;
    ESP_LOGI(TAG, "DRD counter=%" PRIu32 " (window %" PRIu32 "ms) -> %s",
             (uint32_t)boot_counter, (uint32_t)s_window_ms, triggered ? "TRIGGERED" : "no");
    return triggered;
}

uint32_t lcm32_drd_get_count(void) {
    return boot_counter;
}
