#include "lcm_key_provider.h"

#include "official_public_key.h"

#if CONFIG_LCM_PUBLISHER_MODE_CUSTOM
#if defined(__has_include)
#if __has_include("custom_public_key.h")
#include "custom_public_key.h"
#else
#error "CONFIG_LCM_PUBLISHER_MODE_CUSTOM requires keys/custom_public_key.h"
#endif
#else
#include "custom_public_key.h"
#endif
#endif

const char *lcm_trusted_public_key_pem(void) {
#if CONFIG_LCM_PUBLISHER_MODE_CUSTOM
    return LCM_CUSTOM_PUBLIC_KEY_PEM;
#else
    return LCM_OFFICIAL_PUBLIC_KEY_PEM;
#endif
}

const char *lcm_trusted_publisher_mode(void) {
#if CONFIG_LCM_PUBLISHER_MODE_CUSTOM
    return "Custom";
#else
    return "Official";
#endif
}
