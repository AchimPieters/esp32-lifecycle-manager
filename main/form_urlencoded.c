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

static size_t url_decode_inplace(char *buffer)
{
        if (!buffer)
                return 0;

        char *src = buffer;
        char *dst = buffer;

        while (*src) {
                if (*src == '+') {
                        *dst++ = ' ';
                        ++src;
                } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
                        int hi = hex_to_val((unsigned char)src[1]);
                        int lo = hex_to_val((unsigned char)src[2]);
                        if (hi >= 0 && lo >= 0) {
                                *dst++ = (char)((hi << 4) | lo);
                                src += 3;
                                continue;
                        }
                        *dst++ = *src++;
                } else {
                        *dst++ = *src++;
                }
        }

        *dst = '\0';
        return (size_t)(dst - buffer);
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

        memcpy(result, buffer, size);
        result[size] = '\0';
        url_decode_inplace(result);
        ESP_LOGD(TAG, "Decoded string: %s", result);
        return result;
}


form_param_t *form_params_parse(char *s) {
        if (!s) {
                ESP_LOGW(TAG, "form_params_parse called with NULL input");
                return NULL;
        }

        form_param_t *params = NULL;

        while (*s) {
                char *name = s;
                while (*s && *s != '=' && *s != '&')
                        ++s;

                char separator = *s;
                if (separator == '=' || separator == '&')
                        *s++ = '\0';

                char *value = NULL;
                if (separator == '=') {
                        value = s;
                        while (*s && *s != '&')
                                ++s;
                        if (*s == '&')
                                *s++ = '\0';
                }

                url_decode_inplace(name);
                if (value)
                        url_decode_inplace(value);

                if (*name == '\0')
                        continue;

                form_param_t *param = calloc(1, sizeof(form_param_t));
                if (!param) {
                        ESP_LOGE(TAG, "calloc failed in form_params_parse");
                        form_params_free(params);
                        return NULL;
                }

                param->name = name;
                param->value = value;
                ESP_LOGD(TAG, "Parsed param name=%s value=%s", param->name, param->value ? param->value : "(null)");

                param->next = params;
                params = param;
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
                free(params);

                params = next;
        }
}
