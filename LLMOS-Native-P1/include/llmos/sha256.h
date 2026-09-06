#ifndef LLMOS_SHA256_H
#define LLMOS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    uint32_t block_used;
} Sha256Context;

void sha256_init(Sha256Context *ctx);
void sha256_update(Sha256Context *ctx, const void *data, size_t len);
void sha256_final(Sha256Context *ctx, uint8_t out_digest[32]);
void sha256_digest(const void *data, size_t len, uint8_t out_digest[32]);

#endif
