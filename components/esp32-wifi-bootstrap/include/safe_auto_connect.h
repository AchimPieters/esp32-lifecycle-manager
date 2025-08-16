#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t safe_set_auto_connect(bool enable);
#ifdef __cplusplus
}
#endif
