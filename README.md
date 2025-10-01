# ESP32 Lifecycle Manager (LCM)

LCM is een kleine maar krachtige firmwarelaag die het volledige levenscyclusbeheer
van een ESP32-apparaat automatiseert. Zie het als de "beheerder" die je hardware
klaarmaakt voor gebruik, nieuwe firmware ophaalt, updates controleert en het
apparaat terugbrengt naar de fabrieksinstellingen wanneer dat nodig is. Ook als je
nog nooit met ESP-IDF hebt gewerkt, helpt LCM je om in een paar stappen van een
verse module naar een volledig beheerd apparaat te gaan.

## Wat doet LCM precies?

LCM start bij elke boot en bepaalt of het apparaat in de gebruiksmodus kan
doorgaan of dat er onderhoud nodig is. De firmware beheert onder meer het
vastleggen van netwerkgegevens, het downloaden en valideren van nieuwe software
versies en het uitvoeren van resets. Daarmee hoef je zelf geen afzonderlijke
scripts of tools te bouwen; LCM bundelt alle basisfuncties die je nodig hebt om
ESP32-apparaten op afstand te beheren.

## Belangrijkste functies

### AP-modus
LCM kan een tijdelijk access point (hotspot) starten zodat je apparaat altijd
bereikbaar is, zelfs wanneer er nog geen Wi-Fi-instellingen zijn opgeslagen. Je
verbindt eenvoudig met deze hotspot om het apparaat te configureren.

### CNA (Captive Portal)
Wanneer je verbindt met het access point opent er automatisch een captive
portal. Hierin kun je:

- **Wi-Fi selecteren** – Kies een zichtbaar netwerk en vul het wachtwoord in.
- **Handmatig een netwerk toevoegen** – Ideaal voor verborgen SSID's of
  netwerken die niet in de lijst verschijnen.
- **GitHub-repository instellen** – Geef de URL op van de release die LCM moet
  monitoren voor updates (`main.bin` en `main.bin.sig`).
- **LED-identificatie activeren** – Laat de status-LED knipperen zodat je het
  juiste apparaat herkent tijdens installatie of service.

### Download en installatie van `main.bin`
Na provisioning haalt LCM zowel `main.bin` als `main.bin.sig` op, valideert de
SHA-384 handtekening én bestandsgrootte en installeert de firmware alleen als de
controle slaagt.

### Software-update (`ota_trigger`)
Met een OTA-trigger (bijvoorbeeld via een webrequest) kun je op afstand een
firmware-update starten zodra er een nieuwe release beschikbaar is.

### Hardware-update (bijv. knop)
LCM ondersteunt fysieke bedieningen zoals een knop om een updateproces te
starten of te bevestigen. Ideaal voor apparaten die zonder computer moeten
kunnen updaten.

### Resets en herstelfuncties
- **Software factory reset** – Roept via de API de resetroutine aan die alle
  opgeslagen configuratie wist en het apparaat opnieuw start.
- **Hardware factory reset** – Door het apparaat 10 keer snel achter elkaar te
  resetten (power-cycles) triggert LCM automatisch een fabrieksreset.

### Automatische versie-instelling
Tijdens installatie of update schrijft LCM automatisch de waarde van
`LIFECYCLE_DEFAULT_FW_VERSION` weg, zodat je op afstand kunt controleren welke
firmware momenteel actief is.

## Werkstroom van een nieuwe installatie

1. **Eerste boot** – LCM start in AP-modus en biedt de captive portal aan.
2. **Netwerkconfiguratie** – De gebruiker koppelt het apparaat aan een Wi-Fi-netwerk
   en slaat ook de GitHub-repository op waar updates klaarstaan.
3. **Firmware ophalen** – LCM downloadt `main.bin` en `main.bin.sig`, controleert de
   handtekening en de bestandsgrootte en activeert vervolgens de firmware.
4. **Normale werking** – Het apparaat draait nu de applicatiefirmware; LCM blijft op
   de achtergrond beschikbaar voor OTA en reset-functionaliteit.

## Zelf compileren met ESP-IDF

1. Installeer ESP-IDF volgens de officiële handleiding van Espressif.
2. Selecteer het juiste doelplatform:
   ```bash
   idf.py set-target esp32        # of esp32s2, esp32s3, esp32c3, ...
   ```
3. Bouw de lifecycle-manager firmware:
   ```bash
   idf.py build
   ```
   De output verschijnt in `build/` als `main.bin`.

## Firmware ondertekenen (`main.bin` en `main.bin.sig`)

Na het bouwen moet elke firmwareversie voorzien worden van een signature-bestand.
Voer de onderstaande stappen uit in de projectroot:

```bash
openssl sha384 -binary -out build/main.bin.sig build/main.bin
printf "%08x" "$(wc -c < build/main.bin)" | xxd -r -p >> build/main.bin.sig
```

> Tip: gebruik het meegeleverde script `./generate_sig.sh` om deze stappen
> automatisch uit te voeren.

Publiceer beide bestanden in een release op GitHub, bijvoorbeeld onder versie
`0.0.1`. Volg [Semantic Versioning](https://semver.org/) voor het kiezen van het
versienummer:

- **MAJOR** verhogen bij niet-compatibele API-wijzigingen.
- **MINOR** verhogen bij nieuwe, achterwaarts-compatibele features.
- **PATCH** verhogen bij bugfixes die achterwaarts compatibel zijn.

## Flash-instructies per chipset

Gebruik `esptool.py` (beschikbaar via `pip install esptool`) en vervang de
bestandsnamen door de output van jouw build:

### ESP32
```bash
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 esp32-bootloader.bin \
  0x8000 esp32-partition-table.bin \
  0xe000 esp32-ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-S2
```bash
python -m esptool --chip esp32s2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 esp32s2-bootloader.bin \
  0x8000 esp32s2-partition-table.bin \
  0xe000 esp32s2-ota_data_initial.bin \
  0x20000 esp32s2-lifecycle-manager.bin
```

### ESP32-S3
```bash
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32s3-bootloader.bin \
  0x8000 esp32s3-partition-table.bin \
  0xe000 esp32s3-ota_data_initial.bin \
  0x20000 esp32s3-lifecycle-manager.bin
```

### ESP32-C2
```bash
python -m esptool --chip esp32c2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 60m \
  0x0    esp32c2-bootloader.bin \
  0x8000 esp32c2-partition_table/partition-table.bin \
  0xe000 esp32c2-ota_data_initial.bin \
  0x20000 esp32c2-lifecycle-manager.bin
```

### ESP32-C3
```bash
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32c3-bootloader.bin \
  0x8000 esp32c3-partition-table.bin \
  0xe000 esp32c3-ota_data_initial.bin \
  0x20000 esp32c3-lifecycle-manager.bin
```

### ESP32-C5
```bash
python -m esptool --chip esp32c5 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x2000 esp32c5-bootloader.bin \
  0x8000 esp32c5-partition-table.bin \
  0xe000 esp32c5-ota_data_initial.bin \
  0x20000 esp32c5-lifecycle-manager.bin
```

### ESP32-C6 / ESP32-C61
```bash
python -m esptool --chip esp32c6 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32c6-bootloader.bin \
  0x8000 esp32c6-partition-table.bin \
  0xe000 esp32c6-ota_data_initial.bin \
  0x20000 esp32c6-lifecycle-manager.bin
```

> Gebruik voor de ESP32-C61 dezelfde offsets en vervang uitsluitend de bestandsnamen.

## Resetten en herstellen

- **Software reset naar fabrieksinstellingen** – Aanroep van de LCM API zet alle
  opgeslagen configuratie terug en start het apparaat opnieuw op.
- **Hardware reset naar fabrieksinstellingen** – Zet het apparaat 10 keer snel
  achter elkaar uit en weer aan. Zodra LCM het patroon detecteert, volgt een
  aftelvenster van ~11 seconden waarna de fabrieksreset wordt uitgevoerd.

Met deze stappen kun je als beginner snel en veilig een ESP32-device provisionen,
voorzien van jouw eigen firmware en op afstand beheren met behulp van LCM.
