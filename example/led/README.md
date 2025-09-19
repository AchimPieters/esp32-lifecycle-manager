# Example for `Led`

## What it does

It's a "Hello World" example for the HomeKit Demo. This code is for an ESP32-based HomeKit-compatible smart LED. It connects the ESP32 to WiFi and allows users to control an LED’s on/off state via Apple HomeKit.

## Key Functions:
- WiFi Management: Handles connection, reconnection, and IP assignment.
- LED Control: Uses a GPIO pin to turn an LED on or off.
- HomeKit Integration: Defines HomeKit characteristics for power state control.
- Accessory Identification: Implements a blinking pattern for device identification.

## Wiring

Connect `LED` pin to the following pin:

| Name | Description | Defaults |
|------|-------------|----------|
| `CONFIG_ESP_LED_GPIO` | GPIO number for `LED` pin | "2" Default |

## Scheme

![HomeKit LED](https://raw.githubusercontent.com/AchimPieters/esp32-homekit-demo/refs/heads/main/examples/led/scheme.png)

## Requirements

- **idf version:** `>=5.0`
- **espressif/mdns version:** `1.8.0`
- **wolfssl/wolfssl version:** `5.7.6`
- **achimpieters/esp32-homekit version:** `1.0.0`

## Notes

- Choose your GPIO number under `StudioPieters` in `menuconfig`. The default is `2` (On an ESP32 WROOM 32D).
- Set your `WiFi SSID` and `WiFi Password` under `StudioPieters` in `menuconfig`.
- **Optional:** You can change `HomeKit Setup Code` and `HomeKit Setup ID` under `StudioPieters` in `menuconfig`. _(Note: you need to make a new QR-CODE to make it work.)_

## BOOT button actions

The BOOT button (GPIO0, active low) controls the device lifecycle without reflashing:

- **Single press (<0.4 s):** Flag the Lifecycle Manager (LCM) update in NVS, boot the factory partition and let the LCM perform the OTA update before rebooting into the new firmware.
- **Double press (two clicks within 400 ms):** Reset HomeKit pairing information via `homekit_server_reset()` and reboot so the accessory can be paired again.
- **Long press (≥2 s):** Perform a factory reset by clearing HomeKit state, removing Wi-Fi credentials stored in the `wifi_cfg` namespace, calling `esp_wifi_restore()`, and rebooting into provisioning/AP mode.

The firmware logs each action ("Button task started", "Single click", etc.) so behaviour can be verified over the serial console.
