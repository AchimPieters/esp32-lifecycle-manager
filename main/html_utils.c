#include "html_utils.h"

#include <stdlib.h>
#include <string.h>

char *html_escape(const char *input)
{
        if (!input) {
                char *empty = malloc(1);
                if (empty)
                        empty[0] = '\0';
                return empty;
        }

        const unsigned char *src = (const unsigned char *)input;
        size_t len = 0;
        size_t extra = 0;
        for (const unsigned char *p = src; *p; ++p) {
                ++len;
                switch (*p) {
                case '&':
                        extra += 4; // "&amp;" replaces 1 char with 5 chars (delta 4)
                        break;
                case '<':
                case '>':
                        extra += 3; // "&lt;" or "&gt;"
                        break;
                case '"':
                        extra += 5; // "&quot;"
                        break;
                case '\'':
                        extra += 4; // "&#39;"
                        break;
                default:
                        break;
                }
        }

        size_t out_len = len + extra;
        char *out = malloc(out_len + 1);
        if (!out)
                return NULL;

        char *dst = out;
        for (size_t i = 0; i < len; ++i) {
                unsigned char c = src[i];
                switch (c) {
                case '&':
                        memcpy(dst, "&amp;", 5);
                        dst += 5;
                        break;
                case '<':
                        memcpy(dst, "&lt;", 4);
                        dst += 4;
                        break;
                case '>':
                        memcpy(dst, "&gt;", 4);
                        dst += 4;
                        break;
                case '"':
                        memcpy(dst, "&quot;", 6);
                        dst += 6;
                        break;
                case '\'':
                        memcpy(dst, "&#39;", 5);
                        dst += 5;
                        break;
                default:
                        *dst++ = (char)c;
                        break;
                }
        }

        *dst = '\0';
        return out;
}

size_t sanitize_ssid_bytes(const uint8_t *src, size_t src_len, char *dst, size_t dst_len)
{
        if (!dst || dst_len == 0)
                return 0;

        dst[0] = '\0';
        if (!src)
                return 0;

        size_t max_copy = dst_len - 1;
        if (src_len > max_copy)
                src_len = max_copy;

        size_t out_len = 0;
        for (size_t i = 0; i < src_len; ++i) {
                unsigned char c = src[i];
                if (c == '\0')
                        break;
                if (c < 0x20 || c == 0x7f)
                        c = '?';
                dst[out_len++] = (char)c;
        }

        dst[out_len] = '\0';
        return out_len;
}
