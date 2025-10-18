#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "bootloader_flash_priv.h"
#include "esp_flash_encrypt.h"
#include "soc/rtc.h"
#include "soc/reset_reasons.h"
#include "hal/wdt_hal.h"
#include "sdkconfig.h"

#if defined(__GNUC__)
#define LCM_ALIGNED32 __attribute__((aligned(32)))
#elif defined(_MSC_VER)
#define LCM_ALIGNED32 __declspec(align(32))
#else
#define LCM_ALIGNED32
#endif

#ifndef CONFIG_LCM_RESTART_THRESHOLD
#define CONFIG_LCM_RESTART_THRESHOLD 10
#endif

#ifndef CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS
#define CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS 5000
#endif

static const char *TAG = "lcm_boot";
static wdt_hal_context_t s_rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();

#define LCM_STATE_MAGIC 0x4C434D52u
#define NVS_PART_OFFSET 0x9000
#define NVS_PART_SIZE   0x5000
#define LCM_STATE_OFFSET 0x11000
#define LCM_STATE_SIZE   0x1000
#define OTADATA_OFFSET   0xE000
#define OTADATA_SIZE     0x2000
#define OTA0_OFFSET      0x120000
#define OTA1_OFFSET      0x220000
#define OTA_PART_SIZE    0x100000

enum {
    LCM_STATE_FLASH_BYTES = 32,
    LCM_STATE_RESERVED_BYTES = LCM_STATE_FLASH_BYTES - (3 * sizeof(uint32_t) + sizeof(uint64_t)),
};

typedef struct {
    uint32_t magic;
    uint32_t restart_count;
    uint64_t last_timestamp_us;
    uint32_t checksum;
    uint8_t reserved[LCM_STATE_RESERVED_BYTES];
} lcm_restart_state_t;

#if __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(lcm_restart_state_t) == LCM_STATE_FLASH_BYTES, "restart state must match flash write size");
#else
typedef char lcm_restart_state_t_buffer_wrong_size[(sizeof(lcm_restart_state_t) == LCM_STATE_FLASH_BYTES) ? 1 : -1];
#endif

void bootloader_hooks_include(void) {}

static uint64_t boot_time_us(void)
{
    const uint64_t rtc_ticks = rtc_time_get();
    const uint32_t slow_clk_hz = rtc_clk_slow_freq_get_hz();
    if (slow_clk_hz == 0) {
        return 0;
    }
    return (rtc_ticks * 1000000ULL) / slow_clk_hz;
}

static uint32_t compute_state_checksum(const lcm_restart_state_t *state)
{
    uint32_t sum = state->magic ^ state->restart_count;
    sum ^= (uint32_t)(state->last_timestamp_us & 0xFFFFFFFFULL);
    sum ^= (uint32_t)((state->last_timestamp_us >> 32) & 0xFFFFFFFFULL);
    return sum;
}

static bool lcm_state_flash_encryption_active(void)
{
#if defined(CONFIG_SECURE_FLASH_ENC_ENABLED) && CONFIG_SECURE_FLASH_ENC_ENABLED
    return esp_flash_encryption_enabled();
#else
    return false;
#endif
}

static bool load_restart_state_from_flash(lcm_restart_state_t *out)
{
    lcm_restart_state_t state = {0};
    const bool allow_decrypt = lcm_state_flash_encryption_active();
    const esp_err_t read_err = bootloader_flash_read(LCM_STATE_OFFSET, &state, sizeof(state), allow_decrypt);
    if (read_err != ESP_OK) {
        ESP_LOGW(TAG, "read restart state failed (%d)", (int)read_err);
        return false;
    }
    if (state.magic != LCM_STATE_MAGIC) {
        ESP_LOGI(TAG, "restart state magic invalid (0x%08x)", (unsigned)state.magic);
        return false;
    }

    if (state.checksum != compute_state_checksum(&state)) {
        ESP_LOGW(TAG, "restart state checksum mismatch");
        return false;
    }

    *out = state;
    return true;
}

static esp_err_t store_restart_state_to_flash(const lcm_restart_state_t *state)
{
    lcm_restart_state_t snapshot = *state;
    memset(snapshot.reserved, 0, sizeof(snapshot.reserved));
    snapshot.magic = LCM_STATE_MAGIC;
    snapshot.checksum = compute_state_checksum(&snapshot);

    esp_err_t err = bootloader_flash_erase_range(LCM_STATE_OFFSET, LCM_STATE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "erase restart state failed (%d)", (int)err);
        return err;
    }

    const bool flash_encryption_active = lcm_state_flash_encryption_active();

    LCM_ALIGNED32 uint8_t write_buf[LCM_STATE_FLASH_BYTES] = {0};
    memcpy(write_buf, &snapshot, sizeof(snapshot));

    err = bootloader_flash_write(LCM_STATE_OFFSET, write_buf, sizeof(write_buf), flash_encryption_active);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write restart state failed (%d)", (int)err);
        return err;
    }

    lcm_restart_state_t verify = {0};
    err = bootloader_flash_read(LCM_STATE_OFFSET, &verify, sizeof(verify), flash_encryption_active);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "verify restart state read failed (%d)", (int)err);
        return err;
    }

    if (memcmp(&verify, &snapshot, sizeof(snapshot)) != 0) {
        ESP_LOGW(TAG, "verify restart state mismatch (magic=0x%08x count=%u timestamp=%llu checksum=0x%08x)",
                 (unsigned)verify.magic,
                 (unsigned)verify.restart_count,
                 (unsigned long long)verify.last_timestamp_us,
                 (unsigned)verify.checksum);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "wrote restart counter %u to flash", (unsigned)snapshot.restart_count);
    return ESP_OK;
}

static bool is_supported_reset_reason(soc_reset_reason_t reason)
{
    switch (reason) {
    case RESET_REASON_CHIP_POWER_ON:
#ifdef RESET_REASON_CHIP_BROWN_OUT
    case RESET_REASON_CHIP_BROWN_OUT:
#endif
#ifdef RESET_REASON_SYS_BROWN_OUT
    case RESET_REASON_SYS_BROWN_OUT:
#endif
        return true;
    default:
        return false;
    }
}

static void erase_partition_range(uint32_t offset, uint32_t size, const char *label)
{
    const esp_err_t err = bootloader_flash_erase_range(offset, size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "erased %s", label);
    } else {
        ESP_LOGW(TAG, "erase %s failed (%d)", label, (int)err);
    }
}

static void erase_factory_state(void)
{
    ESP_LOGW(TAG, "wiping NVS/OTA data");
    erase_partition_range(NVS_PART_OFFSET, NVS_PART_SIZE, "nvs");
    erase_partition_range(LCM_STATE_OFFSET, LCM_STATE_SIZE, "lcm_state");
    erase_partition_range(OTADATA_OFFSET, OTADATA_SIZE, "otadata");
    erase_partition_range(OTA0_OFFSET, OTA_PART_SIZE, "ota_0");
    erase_partition_range(OTA1_OFFSET, OTA_PART_SIZE, "ota_1");
}

static void perform_factory_reset(void)
{
    ESP_LOGW(TAG, "restart threshold reached (%u)", CONFIG_LCM_RESTART_THRESHOLD);

    for (int i = CONFIG_LCM_RESTART_THRESHOLD; i >= 0; --i) {
        ESP_LOGW(TAG, "factory reset in %d", i);
        wdt_hal_feed(&s_rwdt_ctx);
        esp_rom_delay_us(1000000);
    }

    erase_factory_state();
    ESP_LOGW(TAG, "NVS/OTA wiped");
    ESP_LOGI(TAG, "booting factory image");
    esp_rom_delay_us(1000000);
    esp_rom_software_reset_system();
    while (true) {
        ;
    }
}

void bootloader_after_init(void)
{
    const soc_reset_reason_t reason = esp_rom_get_reset_reason(0);
    const uint64_t now_us = boot_time_us();

    lcm_restart_state_t state = {0};
    bool state_loaded = load_restart_state_from_flash(&state);
    uint32_t previous_count = state_loaded ? state.restart_count : 0;
    uint64_t last_us = state_loaded ? state.last_timestamp_us : 0;
    const uint32_t timeout_ms = CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS;

    if (!is_supported_reset_reason(reason)) {
        if (previous_count != 0) {
            ESP_LOGI(TAG, "reset reason %d -> counter reset", reason);
            state.restart_count = 0;
            state.last_timestamp_us = now_us;
            (void)store_restart_state_to_flash(&state);
        }
        return;
    }

    uint32_t new_count = 1;
    bool elapsed_valid = false;
    uint64_t elapsed_ms = 0;

    if (last_us != 0 && now_us >= last_us) {
        elapsed_ms = (now_us - last_us) / 1000ULL;
        elapsed_valid = true;
    }

    if (elapsed_valid && elapsed_ms <= (uint64_t)timeout_ms) {
        if (previous_count < UINT32_MAX) {
            new_count = previous_count + 1;
        } else {
            new_count = UINT32_MAX;
        }
        ESP_LOGI(TAG, "power cycles=%u (elapsed=%llu ms)", new_count, (unsigned long long)elapsed_ms);
    } else {
        if (state_loaded && previous_count != 0 && elapsed_valid) {
            ESP_LOGI(TAG, "restart timeout (%llu ms) -> counter reset", (unsigned long long)elapsed_ms);
        }
        new_count = 1;
        ESP_LOGI(TAG, "power cycles=%u", new_count);
    }

    state.restart_count = new_count;
    state.last_timestamp_us = now_us;
    if (store_restart_state_to_flash(&state) != ESP_OK) {
        ESP_LOGW(TAG, "persist restart counter failed");
    }

    if (new_count >= CONFIG_LCM_RESTART_THRESHOLD) {
        perform_factory_reset();
    }
}
