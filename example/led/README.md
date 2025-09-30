# Lifecycle Manager LED Example

Deze map bevat een complete HomeKit LED-accessoire die de **Lifecycle Manager (LCM)** gebruikt. Hieronder leer je stap voor stap hoe je de oorspronkelijke versie van de demo (zonder LCM) omzet naar de nieuwe versie met LCM. De uitleg is geschreven voor beginners: per onderdeel lees je wat je moet toevoegen, waar het voor dient en wat het precies doet.

## 1. Basis begrijpen

| Onderdeel | Zonder LCM | Met LCM |
|-----------|------------|---------|
| WiFi-beheer | Handmatig zelf alle WiFi events afhandelen. | Gebruik `wifi_start()` uit de LCM om WiFi te starten en opnieuw te verbinden. |
| Opslag | Zelf NVS initialiseren en resetten. | `lifecycle_nvs_init()` doet dit en logt de reset-status. |
| OTA-updates & firmwareversies | Niet aanwezig. | LCM levert een OTA-trigger en zorgt dat de firmwareversie uit NVS komt. |
| Herstellen/resetten | Handmatig resetten via HomeKit. | Gebruik lifecycle-functies voor update, factory reset en automatische reset-teller. |

De rest van dit document laat zien hoe je de oude code omzet naar de nieuwe door elke stap toe te lichten.

## 2. Headers toevoegen

**Waarom:** De LCM en de knopbibliotheek leveren kant-en-klare functies. Door de juiste headers te importeren krijg je toegang tot die functies.

```c
#include "esp32-lcm.h"   // brengt de Lifecycle Manager binnen
#include <button.h>       // knop-events zonder extra code
```

**Wat doet het:**
- `esp32-lcm.h` geeft je alle lifecycle-, OTA- en WiFi-hulpfuncties.
- `button.h` maakt het eenvoudig om single/double/long presses te detecteren.

Vergeet ook niet om de `CONFIG_ESP_BUTTON_GPIO` macro te gebruiken zodat je de knop-pin via `menuconfig` kunt instellen.

## 3. HomeKit-kenmerken uitbreiden

**Waarom:** De LCM beheert de firmwareversie en levert een standaard OTA-trigger. Die kenmerken koppel je aan HomeKit zodat je ze vanuit de Home-app kunt gebruiken.

Vervang de handmatige firmwareversie door de constante van de LCM en voeg de OTA-trigger toe:

```c
homekit_characteristic_t revision =
    HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, LIFECYCLE_DEFAULT_FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;
```

Voeg daarna `&ota_trigger` toe aan de Lightbulb-service. Hierdoor kun je via de Home-app een update starten zonder op een knop te drukken.

## 4. Lifecycle initialiseren in `app_main`

**Waarom:** De LCM bewaakt de staat van het apparaat (reset-teller, firmwareversie, HomeKit-instellingen). Zonder deze initialisatie werken de lifecycle-functies niet.

```c
ESP_ERROR_CHECK(lifecycle_nvs_init());
lifecycle_log_post_reset_state("INFORMATION");
ESP_ERROR_CHECK(
    lifecycle_configure_homekit(&revision, &ota_trigger, "INFORMATION"));
```

**Wat doet het:**
- `lifecycle_nvs_init()` initialiseert NVS en zet de lifecycle-tabellen klaar.
- `lifecycle_log_post_reset_state()` logt of je bijvoorbeeld uit een brown-out of crash komt.
- `lifecycle_configure_homekit()` koppelt de OTA-trigger en firmwareversie aan HomeKit.

## 5. WiFi door de LCM laten opstarten

**Waarom:** In de oude code moest je zelf events registreren en `esp_wifi_*` functies aanroepen. De LCM neemt dat uit handen en houdt rekening met provisioning.

```c
esp_err_t wifi_err = wifi_start(on_wifi_ready);
```

**Wat doet het:**
- Start automatisch WiFi in station-modus.
- Roept `on_wifi_ready()` aan zodra er verbinding is.
- Geeft duidelijk aan of er nog provisioning nodig is (`ESP_ERR_NVS_NOT_FOUND`).

## 6. Knoppen voor update en factory reset

**Waarom:** Via hardwareknoppen kun je nu OTA-updates starten of een factory reset uitvoeren, zonder handmatig alle timers en debouncing te schrijven.

```c
button_config_t btn_cfg = button_config_default(button_active_low);
btn_cfg.max_repeat_presses = 3;
btn_cfg.long_press_time = 1000;

if (button_create(BUTTON_GPIO, btn_cfg, button_callback, NULL)) {
    ESP_LOGE("BUTTON", "Failed to initialize button");
}
```

In de callback handel je verschillende events af:

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

**Wat doet het:**
- **Single press:** vraagt de LCM om een OTA-update en herstart daarna.
- **Double press:** reset specifiek de HomeKit-pairing.
- **Long press:** voert een volledige factory reset uit (inclusief WiFi en HomeKit).

## 7. LED-besturing en HomeKit blijven gelijk

De basis-LED functies (`gpio_init`, `led_write`, `led_on_set`) blijven bijna hetzelfde. Wel log je nu extra informatie met `ESP_LOGI` zodat je in de seriële monitor ziet wanneer de LED aan of uit gaat.

## 8. Alles samengevoegd

Wanneer je alle bovenstaande stappen volgt, ziet het begin van je bestand er zo uit:

```c
#include "esp32-lcm.h"
#include <button.h>

#define BUTTON_GPIO CONFIG_ESP_BUTTON_GPIO
#define LED_GPIO    CONFIG_ESP_LED_GPIO
```

En in `app_main`:

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

## 9. Verwacht gedrag

- **HomeKit-kenmerken** tonen automatisch de juiste firmwareversie.
- **OTA** kan via de Home-app (Lifecycle-service) of via de knop (single press).
- **Factory reset** kan via een long press óf automatisch na 10 snelle herstarts (configureerbaar via `menuconfig`).
- **WiFi** start automatisch of vraagt provisioning als er geen gegevens zijn opgeslagen.

## 10. Wiring

Verbind de pinnen zoals hieronder beschreven (instelbaar via `menuconfig`):

| Naam | Omschrijving | Standaard |
|------|--------------|-----------|
| `CONFIG_ESP_LED_GPIO` | GPIO voor de LED | `2` |
| `CONFIG_ESP_BUTTON_GPIO` | GPIO voor de knop | `32` |

## 11. Schema

![HomeKit LED](https://raw.githubusercontent.com/AchimPieters/esp32-homekit-demo/refs/heads/main/examples/led/scheme.png)

## 12. Vereisten

- **idf versie:** `>=5.0`
- **espressif/mdns versie:** `1.8.0`
- **wolfssl/wolfssl versie:** `5.7.6`
- **achimpieters/esp32-homekit versie:** `1.0.0`
- **achimpieters/button versie:** `1.2.3`

## 13. Menuconfig-tips

- Kies je GPIO-nummers, WiFi-SSID en wachtwoord in het `StudioPieters` menu.
- Pas desgewenst de `HomeKit Setup Code` en `Setup ID` aan. Vergeet niet een nieuwe QR-code te genereren wanneer je dit doet.
- Stel in `Lifecycle Manager` de time-out voor de restart-teller in om automatische factory resets te sturen.

Met deze stappen heb je de originele LED-demo omgebouwd naar een versie die de Lifecycle Manager benut. Daarmee krijg je OTA-updates, consistente firmware-informatie en eenvoudige reset-scenario’s zonder complexe extra code.
