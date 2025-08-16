#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mbedtls/md.h"
typedef struct { uint8_t sha384[48]; uint32_t fw_size; } sig_info_t;
typedef struct { mbedtls_md_context_t mdctx; uint32_t written; } sig_ctx_t;
bool sig_parse(const uint8_t* sig, size_t sig_len, sig_info_t* out);
void sig_ctx_init(sig_ctx_t* ctx);
void sig_ctx_update(sig_ctx_t* ctx, const uint8_t* data, size_t len);
bool sig_ctx_finish(sig_ctx_t* ctx, const sig_info_t* expected);
