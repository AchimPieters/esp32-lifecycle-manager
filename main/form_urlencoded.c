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


char *url_unescape(const char *buffer, size_t size) {
        ESP_LOGD(TAG, "Decoding URL-escaped string of size %d", (int)size);
        int len = 0;

        int ishex(int c) {
                c = toupper(c);
                return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F');
        }

        int hexvalue(int c) {
                c = toupper(c);
                if ('0' <= c && c <= '9')
                        return c - '0';
                else
                        return c - 'A' + 10;
        }

        int i = 0, j;
        while (i < size) {
                len++;
                if (buffer[i] == '%') {
                        i += 3;
                } else {
                        i++;
                }
        }

        char *result = malloc(len+1);
        if (!result) {
                ESP_LOGE(TAG, "malloc failed in url_unescape");
                return NULL;
        }
        i = j = 0;
        while (i < size) {
                if (buffer[i] == '+') {
                        result[j++] = ' ';
                        i++;
                } else if (buffer[i] != '%') {
                        result[j++] = buffer[i++];
                } else {
                        if (i+2 < size && ishex(buffer[i+1]) && ishex(buffer[i+2])) {
                                result[j++] = hexvalue(buffer[i+1])*16 + hexvalue(buffer[i+2]);
                                i += 3;
                        } else {
                                result[j++] = buffer[i++];
                        }
                }
        }
        result[j] = 0;
        ESP_LOGD(TAG, "Decoded string: %s", result);
        return result;
}


form_param_t *form_params_parse(const char *s) {
        if (!s) {
                ESP_LOGW(TAG, "form_params_parse called with NULL input");
                return NULL;
        }

        form_param_t *params = NULL;

        int i = 0;
        while (1) {
                int pos = i;
                while (s[i] && s[i] != '=' && s[i] != '&') i++;
                if (i == pos) {
                        i++;
                        continue;
                }

                form_param_t *param = malloc(sizeof(form_param_t));
                if (!param) {
                        ESP_LOGE(TAG, "malloc failed in form_params_parse");
                        form_params_free(params);
                        return NULL;
                }
                param->name = url_unescape(s+pos, i-pos);
                param->value = NULL;
                param->next = params;
                params = param;
                ESP_LOGD(TAG, "Parsed param name=%s", param->name);

                if (s[i] == '=') {
                        i++;
                        pos = i;
                        while (s[i] && s[i] != '&') i++;
                        if (i != pos) {
                                param->value = url_unescape(s+pos, i-pos);
                                ESP_LOGD(TAG, "Param %s value=%s", param->name, param->value ? param->value : "(null)");
                        }
                }

                if (!s[i])
                        break;
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
