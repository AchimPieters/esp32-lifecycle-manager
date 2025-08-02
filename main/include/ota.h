#ifndef OTA_H
#define OTA_H

void ota_check_and_install(void);
void firmware_update(void);
void ota_start(void);
void ota_install_latest_if_missing(void);

#endif // OTA_H
