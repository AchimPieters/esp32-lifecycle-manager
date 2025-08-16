# ESP32 Lifecycle Manager (ESP-IDF 5.4)

Deze bundel bevat een gepatchte versie die buildt met ESP-IDF 5.4. Belangrijkste fixes:
- **esp32-wifi-bootstrap** declareert nu `lcm32` als `PRIV_REQUIRES` zodat `lcm32.h` gevonden wordt.
- **lcm32/portal.c**: misleidende-indentation opgelost (Werror).
- **wifi_config.c**: forward declaration `static void ota_check_task(void* arg);` toegevoegd, zodat calls compileren.

## Bouwen (Docker, aanbevolen)

```bash
docker run -it --rm -v "$PWD":/project -w /project espressif/idf:v5.4 bash -lc '
  rm -rf build dependencies.lock &&
  idf.py reconfigure &&
  idf.py build
'
```

## Flashen

Na een succesvolle build vind je binaries in `build/`. Je kan flashen met:

```bash
./flash.sh /dev/tty.usbserial-xxxxxxxx   # macOS/Linux
# of
./flash.ps1 COM5                          # Windows PowerShell
```

Beide scripts flashen: bootloader, partition table, en app.

---
_Bundel gegenereerd op 2025-08-16T08:13:07_
