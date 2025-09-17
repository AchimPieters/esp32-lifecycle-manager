/**
   Copyright 2025 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   for more information visit https://www.studiopieters.nl
 **/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "form_urlencoded.h"

static const char *TAG = "form_urlencoded";

static inline int hex_to_val(unsigned char c)
{
        if (c >= '0' && c <= '9')
                return c - '0';
        c = (unsigned char)toupper(c);
        if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
        return -1;
}


char *url_unescape(const char *buffer, size_t size) {
        ESP_LOGD(TAG, "Decoding URL-escaped string of size %d", (int)size);
        if (!buffer || size == 0) {
                char *empty = malloc(1);
                if (empty)
                        empty[0] = '\0';
                return empty;
        }

        char *result = malloc(size + 1);
        if (!result) {
                ESP_LOGE(TAG, "malloc failed in url_unescape");
                return NULL;
        }

        size_t i = 0, j = 0;
        while (i < size) {
                unsigned char c = (unsigned char)buffer[i];
                if (c == '+') {
                        result[j++] = ' ';
                        i++;
                        continue;
                }
                if (c == '%' && i + 2 < size) {
                        int hi = hex_to_val((unsigned char)buffer[i + 1]);
                        int lo = hex_to_val((unsigned char)buffer[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                                result[j++] = (char)((hi << 4) | lo);
                                i += 3;
                                continue;
                        }
                }
                result[j++] = (char)c;
                i++;
        }

        result[j] = '\0';
        ESP_LOGD(TAG, "Decoded string: %s", result);
        return result;
}


form_param_t *form_params_parse(const char *s) {
        if (!s) {
                ESP_LOGW(TAG, "form_params_parse called with NULL input");
                return NULL;
        }

        form_param_t *params = NULL;

        size_t i = 0;
        while (s[i]) {
                size_t name_start = i;
                while (s[i] && s[i] != '=' && s[i] != '&')
                        i++;
                size_t name_len = i - name_start;
                if (name_len == 0) {
                        if (s[i] == '&') {
                                i++;
                                continue;
                        }
                        break;
                }

                form_param_t *param = calloc(1, sizeof(form_param_t));
                if (!param) {
                        ESP_LOGE(TAG, "calloc failed in form_params_parse");
                        form_params_free(params);
                        return NULL;
                }

                param->name = url_unescape(s + name_start, name_len);
                if (!param->name) {
                        free(param);
                        form_params_free(params);
                        return NULL;
                }
                ESP_LOGD(TAG, "Parsed param name=%s", param->name);

                if (s[i] == '=') {
                        i++;
                        size_t value_start = i;
                        while (s[i] && s[i] != '&')
                                i++;
                        size_t value_len = i - value_start;
                        if (value_len > 0) {
                                param->value = url_unescape(s + value_start, value_len);
                                if (!param->value) {
                                        free(param->name);
                                        free(param);
                                        form_params_free(params);
                                        return NULL;
                                }
                                ESP_LOGD(TAG, "Param %s value=%s", param->name, param->value);
                        }
                }

                param->next = params;
                params = param;

                if (s[i] == '&')
                        i++;
        }

        return params;
}


form_param_t *form_params_find(form_param_t *params, const char *name) {
        while (params) {
                if (!strcmp(params->name, name)) {
                        ESP_LOGD(TAG, "Found param %s", name);
                        return params;
                }
                params = params->next;
        }

        ESP_LOGD(TAG, "Param %s not found", name);
        return NULL;
}


void form_params_free(form_param_t *params) {
        while (params) {
                form_param_t *next = params->next;
                ESP_LOGD(TAG, "Freeing param %s", params->name ? params->name : "(null)");
                if (params->name)
                        free(params->name);
                if (params->value)
                        free(params->value);
                free(params);

                params = next;
        }
}
