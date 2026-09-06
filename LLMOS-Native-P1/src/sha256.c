#include "llmos/sha256.h"

/*
 * Freestanding, integer-only SHA-256 (FIPS 180-4). Used to validate LMOF
 * package content hashes. No dynamic allocation, no libc dependency.
 */

static const uint32_t k256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

static void sha256_process_block(Sha256Context *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (uint32_t i = 0; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
    }
    for (uint32_t i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotr32(w[i - 15u], 7) ^ rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotr32(w[i - 2u], 17) ^ rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + k256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(Sha256Context *ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->total_bytes = 0;
    ctx->block_used = 0;
}

void sha256_update(Sha256Context *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    ctx->total_bytes += (uint64_t)len;
    while (len) {
        uint32_t take = 64u - ctx->block_used;
        if ((size_t)take > len) take = (uint32_t)len;
        for (uint32_t i = 0; i < take; ++i) ctx->block[ctx->block_used + i] = p[i];
        ctx->block_used += take;
        p += take;
        len -= take;
        if (ctx->block_used == 64u) {
            sha256_process_block(ctx, ctx->block);
            ctx->block_used = 0;
        }
    }
}

void sha256_final(Sha256Context *ctx, uint8_t out_digest[32]) {
    uint64_t bit_len = ctx->total_bytes * 8u;
    uint8_t pad = 0x80u;
    sha256_update(ctx, &pad, 1u);
    uint8_t zero = 0x00u;
    while (ctx->block_used != 56u) sha256_update(ctx, &zero, 1u);
    uint8_t len_be[8];
    for (uint32_t i = 0; i < 8u; ++i) len_be[i] = (uint8_t)(bit_len >> (56u - 8u * i));
    /* Bypass sha256_update's total_bytes accounting for the length suffix. */
    for (uint32_t i = 0; i < 8u; ++i) ctx->block[ctx->block_used + i] = len_be[i];
    ctx->block_used += 8u;
    sha256_process_block(ctx, ctx->block);
    ctx->block_used = 0;
    for (uint32_t i = 0; i < 8u; ++i) {
        out_digest[i * 4u]      = (uint8_t)(ctx->state[i] >> 24);
        out_digest[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out_digest[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out_digest[i * 4u + 3u] = (uint8_t)(ctx->state[i]);
    }
}

void sha256_digest(const void *data, size_t len, uint8_t out_digest[32]) {
    Sha256Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out_digest);
}
