#ifndef GITHUB_UPDATE_H
#define GITHUB_UPDATE_H

#include <stdbool.h>
#include <esp_err.h>

/*
 * Check if a firmware update is needed and, if so, download the latest
 * firmware from the specified GitHub repository. When pre_release is true,
 * pre-releases are considered, otherwise only stable releases are used.
 *
 * The function expects to find assets named "main.bin" and "main.bin.sig"
 * in the selected release. The signature file should contain the SHA256
 * hash of the firmware in hexadecimal.
 */
esp_err_t github_update_if_needed(const char *repo, bool pre_release);

#endif /* GITHUB_UPDATE_H */
