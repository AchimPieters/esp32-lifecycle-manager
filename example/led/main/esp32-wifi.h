#pragma once
#include <stdint.h>

#include <esp_err.h>
#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start WiFi STA op basis van NVS keys (namespace: wifi_cfg, keys: wifi_ssid, wifi_password).
// Roep 'on_ready' aan zodra IP is verkregen.
esp_err_t wifi_start(void (*on_ready)(void));

// Optioneel: stop WiFi netjes
esp_err_t wifi_stop(void);

#ifndef ESP32_WIFI_BUTTON_TYPES_DEFINED
#define ESP32_WIFI_BUTTON_TYPES_DEFINED
typedef enum {
    BUTTON_EVENT_PRESS,
    BUTTON_EVENT_RELEASE,
    BUTTON_EVENT_SINGLE_TIMEOUT,
} button_event_type_t;

typedef struct {
    button_event_type_t type;
    int64_t time_us;
} button_event_t;
#endif

// Callback voor single-click timeout timer.
void button_single_click_timeout_callback(TimerHandle_t timer);

// Configureer BOOT-knop voor single/double/long press acties.
void button_init(gpio_num_t button_gpio,
                 int64_t debounce_us,
                 int64_t long_press_us,
                 QueueHandle_t event_queue,
                 TimerHandle_t single_click_timer,
                 int *click_count);

#ifdef __cplusplus
}
#endif
