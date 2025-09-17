#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Allocate and return a new string containing an HTML-escaped version of
 * `input`. The caller is responsible for freeing the returned buffer with
 * `free()`. The function replaces the characters &, <, >, " and ' with their
 * corresponding HTML entities and returns an empty string when `input` is NULL.
 */
char *html_escape(const char *input);

/**
 * Copy at most `src_len` bytes from `src` into `dst`, replacing control
 * characters with '?' and ensuring the result is NUL-terminated. The return
 * value is the number of bytes written to `dst` (excluding the terminator).
 */
size_t sanitize_ssid_bytes(const uint8_t *src, size_t src_len, char *dst, size_t dst_len);

