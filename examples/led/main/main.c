#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>

#include <button.h>
#include <homekit/characteristics.h>
#include <homekit/homekit.h>
#include <homekit/tlv.h>

#include "esp32-lcm.h"

#define LED_GPIO CONFIG_ESP_LED_GPIO
#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO

static const char *TAG = "LCM_LED_EXAMPLE";

static bool led_state = false;

homekit_characteristic_t light_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t revision =
    HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, LIFECYCLE_DEFAULT_FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;

static void led_write(bool on)
{
    gpio_set_level(LED_GPIO, on ? 1 : 0);
}

static void led_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_write(false);
}

static void light_on_setter(const homekit_value_t value)
{
    if (value.format != homekit_format_bool) {
        ESP_LOGW(TAG, "ON setter received non-boolean format");
        return;
    }

    led_state = value.bool_value;
    led_write(led_state);
    light_on.value = HOMEKIT_BOOL(led_state);
    homekit_characteristic_notify(&light_on, light_on.value);
}

static void ota_trigger_setter(const homekit_value_t value)
{
    lifecycle_handle_ota_trigger(&ota_trigger, value);
}

static void accessory_identify(homekit_value_t _value)
{
    (void)_value;
    ESP_LOGI(TAG, "Identify requested");
}

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id = 1,
        .category = homekit_accessory_category_lightbulb,
        .services = (homekit_service_t *[]){
            HOMEKIT_SERVICE(
                ACCESSORY_INFORMATION,
                .characteristics = (homekit_characteristic_t *[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "Lifecycle LED"),
                    HOMEKIT_CHARACTERISTIC(MANUFACTURER, "StudioPieters"),
                    HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "LCM-LED-001"),
                    HOMEKIT_CHARACTERISTIC(MODEL, "LCM LED"),
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
                    &revision,
                    NULL,
                }),
            HOMEKIT_SERVICE(
                LIGHTBULB,
                .primary = true,
                .characteristics = (homekit_characteristic_t *[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "LED"),
                    &light_on,
                    &ota_trigger,
                    NULL,
                }),
            NULL,
        }),
    NULL,
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = CONFIG_ESP_SETUP_CODE,
    .setupId = CONFIG_ESP_SETUP_ID,
};

static void on_wifi_ready(void)
{
    ESP_LOGI(TAG, "Wi-Fi connected, starting HomeKit server");
    homekit_server_init(&config);
}

static void button_callback(button_event_t event, void *context)
{
    (void)context;

    switch (event) {
    case button_event_single_press:
        ESP_LOGI(TAG, "Button single press: request OTA update");
        lifecycle_request_update_and_reboot();
        break;
    case button_event_double_press:
        ESP_LOGI(TAG, "Button double press: reset HomeKit and reboot");
        lifecycle_reset_homekit_and_reboot();
        break;
    case button_event_long_press:
        ESP_LOGI(TAG, "Button long press: factory reset and reboot");
        lifecycle_factory_reset_and_reboot();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    light_on.setter_ex = light_on_setter;
    ota_trigger.setter_ex = ota_trigger_setter;

    ESP_ERROR_CHECK(lifecycle_nvs_init());
    lifecycle_log_post_reset_state(TAG);
    ESP_ERROR_CHECK(lifecycle_configure_homekit(&revision, &ota_trigger, TAG));

    led_init();

    button_config_t button_cfg = button_config_default(button_active_low);
    button_cfg.max_repeat_presses = 3;
    button_cfg.long_press_time = 1000;
    if (button_create(BUTTON_GPIO, button_cfg, button_callback, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to initialize button on GPIO %d", BUTTON_GPIO);
    }

    esp_err_t wifi_err = wifi_start(on_wifi_ready);
    if (wifi_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Wi-Fi credentials missing; provisioning required");
    } else if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(wifi_err));
    }
}
