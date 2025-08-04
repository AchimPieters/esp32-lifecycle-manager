#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

void ota_check_and_install(void);
void firmware_update(void);
void ota_start(void);

extern volatile bool ota_in_progress;

#endif // OTA_H
