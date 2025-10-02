# Lifecycle Manager LED Example

This folder contains a complete HomeKit LED accessory that uses the **Lifecycle Manager (LCM)**. The guide below walks you step by step through converting the original demo (without LCM) into the new LCM-enabled version. It is written for beginners: for every section you learn what to add, why it matters, and what it does.

## 1. Understand the basics

| Component | Without LCM | With LCM |
|-----------|-------------|----------|
| Wi-Fi management | Manually handle every Wi-Fi event yourself. | Call `wifi_start()` from the LCM to start Wi-Fi and reconnect automatically. |
| Storage | Initialize and reset NVS manually. | `lifecycle_nvs_init()` performs the setup and logs the reset state. |
| OTA updates & firmware versions | Not available. | The LCM exposes an OTA trigger and reads the firmware version from NVS. |
| Restore/reset | Reset manually through HomeKit. | Use lifecycle functions for updates, factory resets, and the automatic reboot counter. |

The rest of this document shows how to update the old code to the new version, with an explanation at each step.

## 2. Include the headers

**Why:** The LCM and the button library provide ready-to-use functions. Import the right headers to access them.

```c
#include "esp32-lcm.h"   // pull in the Lifecycle Manager
#include <button.h>       // button events without extra boilerplate
```

**What it does:**
- `esp32-lcm.h` gives you the lifecycle, OTA, and Wi-Fi helper functions.
- `button.h` makes it easy to detect single, double, and long presses.

Do not forget to use the `CONFIG_ESP_BUTTON_GPIO` macro so the button pin can be set through `menuconfig`.

## 3. Extend the HomeKit characteristics

**Why:** The LCM manages the firmware version and exposes a default OTA trigger. Add these characteristics to HomeKit so you can use them from the Home app.

Replace the manual firmware version with the LCM constant and add the OTA trigger:

```c
homekit_characteristic_t revision =
    HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, LIFECYCLE_DEFAULT_FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;
```

Then add `&ota_trigger` to the Lightbulb service. This allows you to start an update from the Home app without pressing a hardware button.

## 4. Initialize the lifecycle in `app_main`

**Why:** The LCM tracks the device state (reboot counter, firmware version, HomeKit data). Without this initialization the lifecycle features do not work.

```c
ESP_ERROR_CHECK(lifecycle_nvs_init());
lifecycle_log_post_reset_state("INFORMATION");
ESP_ERROR_CHECK(
    lifecycle_configure_homekit(&revision, &ota_trigger, "INFORMATION"));
```

**What it does:**
- `lifecycle_nvs_init()` initializes NVS and prepares the lifecycle tables.
- `lifecycle_log_post_reset_state()` logs whether you booted after a brownout, crash, etc.
- `lifecycle_configure_homekit()` links the OTA trigger and firmware version to HomeKit.

## 5. Let the LCM start Wi-Fi

**Why:** In the old code you had to register events and call `esp_wifi_*` functions manually. The LCM handles this and takes provisioning into account.

```c
esp_err_t wifi_err = wifi_start(on_wifi_ready);
```

**What it does:**
- Starts Wi-Fi in station mode automatically.
- Calls `on_wifi_ready()` when the connection is established.
- Makes it clear whether provisioning is still required (`ESP_ERR_NVS_NOT_FOUND`).

## 6. Buttons for updates and factory reset

**Why:** Hardware buttons can now trigger OTA updates or factory resets without writing your own timers and debouncing code.

```c
button_config_t btn_cfg = button_config_default(button_active_low);
btn_cfg.max_repeat_presses = 3;
btn_cfg.long_press_time = 1000;

if (button_create(BUTTON_GPIO, btn_cfg, button_callback, NULL)) {
    ESP_LOGE("BUTTON", "Failed to initialize button");
}
```

Handle the different events in the callback:

```c
void button_callback(button_event_t event, void *context) {
    switch (event) {
    case button_event_single_press:
        lifecycle_request_update_and_reboot();
        break;
    case button_event_double_press:
        homekit_server_reset();
        esp_restart();
        break;
    case button_event_long_press:
        lifecycle_factory_reset_and_reboot();
        break;
    }
}
```

**What it does:**
- **Single press:** requests an OTA update from the LCM and restarts afterwards.
- **Double press:** resets the HomeKit pairing.
- **Long press:** performs a full factory reset (Wi-Fi and HomeKit included).

## 7. LED control and HomeKit logic

The core LED helpers (`gpio_init`, `led_write`, `led_on_set`) stay almost the same. Add extra `ESP_LOGI` messages so the serial monitor shows when the LED toggles.

## 8. Putting it all together

Once you follow the steps above, the start of your file should look like this:

```c
#include "esp32-lcm.h"
#include <button.h>

#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define LED_GPIO    CONFIG_ESP_LED_GPIO
```

And the `app_main` function:

```c
void app_main(void) {
    ESP_ERROR_CHECK(lifecycle_nvs_init());
    lifecycle_log_post_reset_state("INFORMATION");
    ESP_ERROR_CHECK(lifecycle_configure_homekit(&revision, &ota_trigger,
                                                "INFORMATION"));

    gpio_init();

    button_config_t btn_cfg = button_config_default(button_active_low);
    btn_cfg.max_repeat_presses = 3;
    btn_cfg.long_press_time = 1000;
    if (button_create(BUTTON_GPIO, btn_cfg, button_callback, NULL)) {
        ESP_LOGE("BUTTON", "Failed to initialize button");
    }

    esp_err_t wifi_err = wifi_start(on_wifi_ready);
    if (wifi_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("WIFI", "WiFi configuration not found; provisioning required");
    } else if (wifi_err != ESP_OK) {
        ESP_LOGE("WIFI", "Failed to start WiFi: %s", esp_err_to_name(wifi_err));
    }
}
```

## 9. Expected behavior

- **HomeKit characteristics** automatically show the correct firmware version.
- **OTA** can be triggered from the Home app (Lifecycle service) or via the button (single press).
- **Factory reset** is available through a long press or automatically after 10 quick restarts (configurable through `menuconfig`).
- **Wi-Fi** starts automatically or requests provisioning if no credentials are stored.

## 10. Wiring

Connect the pins as described below (configurable via `menuconfig`):

| Name | Description | Default |
|------|-------------|---------|
| `CONFIG_ESP_LED_GPIO` | GPIO for the LED | `2` |
| `CONFIG_ESP_BUTTON_GPIO` | GPIO for the button | `32` |

## 11. Schematic

![HomeKit LED](https://github.com/AchimPieters/esp32-lifecycle-manager/blob/main/examples/led/scheme.png)

## 12. Requirements

- **idf version:** `>=5.0`
- **espressif/mdns version:** `1.8.0`
- **wolfssl/wolfssl version:** `5.7.6`
- **achimpieters/esp32-homekit version:** `1.0.0`
- **achimpieters/button version:** `1.2.3`

## 13. Menuconfig tips

- Set your GPIO numbers, Wi-Fi SSID, and password in the `StudioPieters` menu.
- Adjust the `HomeKit Setup Code` and `Setup ID` if needed. Remember to generate a new QR code when you do.
- Configure the reboot counter timeout in `Lifecycle Manager` to control automatic factory resets.

By following these steps, you convert the original LED demo into a version that leverages the Lifecycle Manager. You gain OTA updates, consistent firmware information, and straightforward reset scenarios without complex extra code.
