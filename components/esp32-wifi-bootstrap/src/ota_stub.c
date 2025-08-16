// Weak OTA check task stub to satisfy references when OTA isn't implemented.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
__attribute__((weak)) void ota_check_task(void *arg) {
    // Immediately exit the task
    vTaskDelete(NULL);
}
