#ifndef LLMOS_OPS_H
#define LLMOS_OPS_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Q16.16 fixed-point tensor operators. The kernel build disables x87/SSE and
 * NEON entirely (see Makefile), so there is no hardware float path available
 * to a freestanding, non-preemptible P1/P2 kernel; every operator here is
 * plain 32/64-bit integer arithmetic against build-time-generated tables.
 * This is the scalar reference path described in docs/ROADMAP.md; AVX2/NEON
 * packed kernels are a later increment once FPU state has an owner.
 */

typedef int32_t fx_t;

#define FX_SHIFT 16
#define FX_ONE   (1 << FX_SHIFT)

fx_t fx_from_int(int32_t v);
int32_t fx_to_int(fx_t v);
fx_t fx_mul(fx_t a, fx_t b);
fx_t fx_div(fx_t a, fx_t b);
fx_t fx_sqrt(fx_t v);

/* Dequantizes and accumulates x[0..n) (fx) dotted with an int8 row, scaled
 * by a per-tensor fixed-point scale, producing a single fx result. */
fx_t fx_matvec_row(const fx_t *x, const int8_t *weight_row, uint32_t n, fx_t scale);

/* out[o] = fx_matvec_row(x, &weight[o*in_dim], in_dim, scale) for o in [0,out_dim) */
void fx_matvec(const fx_t *x, const int8_t *weight, uint32_t out_dim, uint32_t in_dim,
               fx_t scale, fx_t *out);

void rmsnorm_fx(const fx_t *x, const fx_t *weight, uint32_t dim, fx_t eps, fx_t *out);

/* Applies rotary position embedding in place to a head_dim-wide vector using
 * the build-time frequency table (see rope_freq_table.h); head_dim must
 * equal ROPE_FREQ_HEAD_DIM. */
bool rope_apply_fx(fx_t *vec, uint32_t head_dim, uint32_t position);

void sincos_fx(fx_t angle, fx_t *out_sin, fx_t *out_cos);
fx_t softmax_exp_fx(fx_t x);
fx_t sigmoid_fx(fx_t x);
fx_t silu_fx(fx_t x);

/*
 * Causal grouped-query attention for one query position against a KV cache
 * of `kv_len` prior positions (key/value vectors already RoPE-applied where
 * applicable). Layout: keys/values are [kv_len][head_dim] for one kv head.
 * group_query writes the head_dim-wide attention output for one query head.
 */
void attention_head_fx(const fx_t *query, const fx_t *keys, const fx_t *values,
                        uint32_t kv_len, uint32_t head_dim, fx_t *out);

/* argmax-with-temperature sampler: temperature==FX_ONE is neutral; a simple
 * xorshift-driven top-k-free categorical sample over softmax(logits). */
uint32_t sample_fx(const fx_t *logits, uint32_t vocab, fx_t temperature, uint32_t *rng_state);

#endif
