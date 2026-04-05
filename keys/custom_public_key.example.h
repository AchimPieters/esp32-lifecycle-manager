#pragma once

// Copy this file to `keys/custom_public_key.h` for custom publisher mode.
// Keep only a PUBLIC key in this header; never store private keys in git.
#define LCM_CUSTOM_PUBLIC_KEY_PEM \
"-----BEGIN PUBLIC KEY-----\n" \
"REPLACE_WITH_YOUR_BASE64_KEY_MATERIAL\n" \
"-----END PUBLIC KEY-----\n"
