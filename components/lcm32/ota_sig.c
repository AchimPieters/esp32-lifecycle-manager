#include "ota_sig.h"
#include "mbedtls/md.h"
#include <string.h>
bool sig_parse(const uint8_t* sig, size_t sig_len, sig_info_t* out) {
    if (!sig || sig_len < 52 || !out) return false;
    memcpy(out->sha384, sig, 48);
    out->fw_size = (uint32_t)sig[48] | ((uint32_t)sig[49] << 8) | ((uint32_t)sig[50] << 16) | ((uint32_t)sig[51] << 24);
    return true;
}
void sig_ctx_init(sig_ctx_t* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    mbedtls_md_init(&ctx->mdctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA384);
    mbedtls_md_setup(&ctx->mdctx, info, 0);
    mbedtls_md_starts(&ctx->mdctx);
}
void sig_ctx_update(sig_ctx_t* ctx, const uint8_t* data, size_t len) {
    ctx->written += len;
    mbedtls_md_update(&ctx->mdctx, data, len);
}
bool sig_ctx_finish(sig_ctx_t* ctx, const sig_info_t* expected) {
    uint8_t out[48]; mbedtls_md_finish(&ctx->mdctx, out);
    bool ok = (ctx->written == expected->fw_size) && (memcmp(out, expected->sha384, 48) == 0);
    mbedtls_md_free(&ctx->mdctx);
    return ok;
}
