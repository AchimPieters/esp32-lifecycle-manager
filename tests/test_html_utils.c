#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html_utils.h"
#include "index.html.h"

int main(void)
{
        const char *malicious = "\"/><script>alert('x')</script>&";
        char *escaped = html_escape(malicious);
        if (!escaped) {
                fprintf(stderr, "html_escape returned NULL\n");
                return 1;
        }

        const char *expected = "&quot;/&gt;&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt;&amp;";
        if (strcmp(escaped, expected) != 0) {
                fprintf(stderr, "Unexpected escape result: %s\n", escaped);
                free(escaped);
                return 1;
        }

        char into_buf[128];
        size_t out_len = 0;
        if (!html_escape_into(malicious, into_buf, sizeof(into_buf), &out_len)) {
                fprintf(stderr, "html_escape_into reported failure\n");
                free(escaped);
                return 1;
        }
        if (strcmp(into_buf, expected) != 0 || out_len != strlen(expected)) {
                fprintf(stderr, "html_escape_into produced unexpected result: %s (len=%zu)\n", into_buf, out_len);
                free(escaped);
                return 1;
        }
        char small_buf[8];
        size_t needed_len = 0;
        if (html_escape_into(malicious, small_buf, sizeof(small_buf), &needed_len)) {
                fprintf(stderr, "html_escape_into succeeded unexpectedly with small buffer\n");
                free(escaped);
                return 1;
        }
        if (needed_len != strlen(expected) + 1) {
                fprintf(stderr, "html_escape_into reported wrong required length: %zu\n", needed_len);
                free(escaped);
                return 1;
        }

        size_t needed = (size_t)snprintf(NULL, 0, html_network_item, "secure", escaped);
        char *buffer = malloc(needed + 1);
        if (!buffer) {
                free(escaped);
                return 1;
        }

        snprintf(buffer, needed + 1, html_network_item, "secure", escaped);
        if (strstr(buffer, "<script") != NULL) {
                fprintf(stderr, "Unescaped script tag found: %s\n", buffer);
                free(buffer);
                free(escaped);
                return 1;
        }
        free(buffer);

        const uint8_t noisy_bytes[] = { 'A', '\n', 'B', 0 };
        char sanitized[sizeof(noisy_bytes)];
        sanitize_ssid_bytes(noisy_bytes, sizeof(noisy_bytes), sanitized, sizeof(sanitized));
        if (strcmp(sanitized, "A?B") != 0) {
                fprintf(stderr, "Unexpected sanitization result: %s\n", sanitized);
                free(escaped);
                return 1;
        }

        free(escaped);

        char *null_escape = html_escape(NULL);
        if (!null_escape || null_escape[0] != '\0') {
                fprintf(stderr, "Null input not handled correctly\n");
                free(null_escape);
                return 1;
        }
        free(null_escape);

        return 0;
}
