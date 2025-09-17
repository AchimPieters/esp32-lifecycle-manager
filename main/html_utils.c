#include "html_utils.h"

#include <stdlib.h>
#include <string.h>

static size_t html_escape_required_len(const unsigned char *src)
{
        size_t required = 0;

        while (*src) {
                switch (*src++) {
                case '&':
                        required += 5;
                        break;
                case '<':
                case '>':
                        required += 4;
                        break;
                case '"':
                        required += 6;
                        break;
                case '\'':
                        required += 5;
                        break;
                default:
                        required += 1;
                        break;
                }
        }

        return required;
}

bool html_escape_into(const char *input, char *dst, size_t dst_len, size_t *out_len)
{
        if (!dst || dst_len == 0)
                return false;

        if (!input) {
                *dst = '\0';
                if (out_len)
                        *out_len = 0;
                return true;
        }

        const unsigned char *src = (const unsigned char *)input;
        size_t required = html_escape_required_len(src);

        if (required + 1 > dst_len) {
                if (out_len)
                        *out_len = required + 1;
                return false;
        }

        if (out_len)
                *out_len = required;

        char *write = dst;
        while (*src) {
                switch (*src) {
                case '&':
                        memcpy(write, "&amp;", 5);
                        write += 5;
                        break;
                case '<':
                        memcpy(write, "&lt;", 4);
                        write += 4;
                        break;
                case '>':
                        memcpy(write, "&gt;", 4);
                        write += 4;
                        break;
                case '"':
                        memcpy(write, "&quot;", 6);
                        write += 6;
                        break;
                case '\'':
                        memcpy(write, "&#39;", 5);
                        write += 5;
                        break;
                default:
                        *write++ = (char)*src;
                        break;
                }
                ++src;
        }

        *write = '\0';
        return true;
}

char *html_escape(const char *input)
{
        if (!input) {
                char *empty = malloc(1);
                if (empty)
                        empty[0] = '\0';
                return empty;
        }

        size_t required = html_escape_required_len((const unsigned char *)input);
        char *out = malloc(required + 1);
        if (!out)
                return NULL;

        if (!html_escape_into(input, out, required + 1, NULL)) {
                free(out);
                return NULL;
        }

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
