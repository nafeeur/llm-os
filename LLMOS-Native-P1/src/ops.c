#include "llmos/ops.h"
#include <stddef.h>
#include "llmos/sincos_table.h"
#include "llmos/exp_table.h"
#include "llmos/sigmoid_table.h"
#include "llmos/rope_freq_table.h"

fx_t fx_from_int(int32_t v) { return v << FX_SHIFT; }
int32_t fx_to_int(fx_t v) { return v >> FX_SHIFT; }

fx_t fx_mul(fx_t a, fx_t b) {
    return (fx_t)(((int64_t)a * (int64_t)b) >> FX_SHIFT);
}

fx_t fx_div(fx_t a, fx_t b) {
    if (b == 0) return 0;
    return (fx_t)(((int64_t)a << FX_SHIFT) / b);
}

static uint64_t isqrt64(uint64_t n) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        uint64_t val = res + bit;
        if (n >= val) {
            n -= val;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

fx_t fx_sqrt(fx_t v) {
    if (v <= 0) return 0;
    uint64_t scaled = (uint64_t)(uint32_t)v * (uint64_t)FX_ONE;
    return (fx_t)isqrt64(scaled);
}

fx_t fx_matvec_row(const fx_t *x, const int8_t *weight_row, uint32_t n, fx_t scale) {
    int64_t acc = 0;
    for (uint32_t i = 0; i < n; ++i)
        acc += (int64_t)x[i] * (int64_t)weight_row[i];
    return (fx_t)(((int64_t)scale * acc) >> FX_SHIFT);
}

void fx_matvec(const fx_t *x, const int8_t *weight, uint32_t out_dim, uint32_t in_dim,
               fx_t scale, fx_t *out) {
    for (uint32_t o = 0; o < out_dim; ++o)
        out[o] = fx_matvec_row(x, &weight[(size_t)o * in_dim], in_dim, scale);
}

void rmsnorm_fx(const fx_t *x, const fx_t *weight, uint32_t dim, fx_t eps, fx_t *out) {
    int64_t sumsq = 0;
    for (uint32_t i = 0; i < dim; ++i) sumsq += (int64_t)fx_mul(x[i], x[i]);
    fx_t mean_sq = (fx_t)(sumsq / (int32_t)dim);
    fx_t rms = fx_sqrt(mean_sq + eps);
    fx_t inv_rms = fx_div(FX_ONE, rms);
    for (uint32_t i = 0; i < dim; ++i)
        out[i] = fx_mul(fx_mul(x[i], inv_rms), weight[i]);
}

void sincos_fx(fx_t angle, fx_t *out_sin, fx_t *out_cos) {
    int64_t reduced = (int64_t)angle % FX_TWO_PI;
    if (reduced < 0) reduced += FX_TWO_PI;
    int64_t scaled = reduced * (int64_t)SINCOS_TABLE_N;
    uint32_t idx = (uint32_t)(scaled / FX_TWO_PI);
    if (idx >= SINCOS_TABLE_N) idx = SINCOS_TABLE_N - 1u;
    uint32_t idx_next = (idx + 1u) % SINCOS_TABLE_N;
    int64_t frac_num = scaled - (int64_t)idx * FX_TWO_PI;
    fx_t frac = (fx_t)((frac_num << FX_SHIFT) / FX_TWO_PI);
    fx_t s0 = sin_table[idx], s1 = sin_table[idx_next];
    fx_t c0 = cos_table[idx], c1 = cos_table[idx_next];
    *out_sin = s0 + fx_mul(s1 - s0, frac);
    *out_cos = c0 + fx_mul(c1 - c0, frac);
}

bool rope_apply_fx(fx_t *vec, uint32_t head_dim, uint32_t position) {
    if (head_dim != ROPE_FREQ_HEAD_DIM) return false;
    uint32_t half = head_dim / 2u;
    for (uint32_t i = 0; i < half; ++i) {
        int64_t angle64 = (int64_t)rope_freq_table[i] * (int64_t)position;
        fx_t angle = (fx_t)angle64; /* position bounded well below overflow for this table */
        fx_t s, c;
        sincos_fx(angle, &s, &c);
        fx_t a = vec[2u * i], b = vec[2u * i + 1u];
        vec[2u * i]      = fx_mul(a, c) - fx_mul(b, s);
        vec[2u * i + 1u] = fx_mul(a, s) + fx_mul(b, c);
    }
    return true;
}

static fx_t table_lookup_clamped(const fx_t *table, uint32_t n, fx_t lo, fx_t hi, fx_t x) {
    if (x <= lo) return table[0];
    if (x >= hi) return table[n - 1u];
    fx_t span = hi - lo;
    fx_t ratio = fx_div(x - lo, span);              /* in [0, FX_ONE] */
    fx_t scaled = fx_mul(ratio, fx_from_int((int32_t)(n - 1u))); /* in [0, n-1] */
    uint32_t idx = (uint32_t)fx_to_int(scaled);
    if (idx >= n - 1u) return table[n - 1u];
    fx_t frac = scaled - fx_from_int((int32_t)idx);
    return table[idx] + fx_mul(table[idx + 1u] - table[idx], frac);
}

fx_t softmax_exp_fx(fx_t x) {
    return table_lookup_clamped(exp_table, EXP_TABLE_N, FX_EXP_LO, 0, x);
}

fx_t sigmoid_fx(fx_t x) {
    return table_lookup_clamped(sigmoid_table, SIGMOID_TABLE_N, FX_SIGMOID_LO, FX_SIGMOID_HI, x);
}

fx_t silu_fx(fx_t x) { return fx_mul(x, sigmoid_fx(x)); }

void attention_head_fx(const fx_t *query, const fx_t *keys, const fx_t *values,
                        uint32_t kv_len, uint32_t head_dim, fx_t *out) {
    fx_t inv_scale = fx_div(FX_ONE, fx_sqrt(fx_from_int((int32_t)head_dim)));
    fx_t scores[256];
    if (kv_len > 256u) kv_len = 256u;

    fx_t max_score = 0;
    for (uint32_t j = 0; j < kv_len; ++j) {
        const fx_t *k = &keys[(size_t)j * head_dim];
        int64_t dot = 0;
        for (uint32_t d = 0; d < head_dim; ++d) dot += (int64_t)fx_mul(query[d], k[d]);
        fx_t score = fx_mul((fx_t)dot, inv_scale);
        scores[j] = score;
        if (j == 0 || score > max_score) max_score = score;
    }

    int64_t sum_exp = 0;
    for (uint32_t j = 0; j < kv_len; ++j) {
        fx_t e = softmax_exp_fx(scores[j] - max_score);
        scores[j] = e;
        sum_exp += e;
    }
    fx_t inv_sum = fx_div(FX_ONE, (fx_t)sum_exp);

    for (uint32_t d = 0; d < head_dim; ++d) out[d] = 0;
    for (uint32_t j = 0; j < kv_len; ++j) {
        fx_t weight = fx_mul(scores[j], inv_sum);
        const fx_t *v = &values[(size_t)j * head_dim];
        for (uint32_t d = 0; d < head_dim; ++d) out[d] += fx_mul(weight, v[d]);
    }
}

static uint32_t rng_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

uint32_t sample_fx(const fx_t *logits, uint32_t vocab, fx_t temperature, uint32_t *rng_state) {
    if (!vocab) return 0;
    fx_t max_logit = logits[0];
    for (uint32_t i = 1; i < vocab; ++i) if (logits[i] > max_logit) max_logit = logits[i];

    fx_t inv_temp = fx_div(FX_ONE, temperature > 0 ? temperature : FX_ONE);
    int64_t sum_exp = 0;
    for (uint32_t i = 0; i < vocab; ++i) {
        fx_t scaled = fx_mul(logits[i] - max_logit, inv_temp);
        sum_exp += softmax_exp_fx(scaled);
    }
    if (sum_exp <= 0) return 0;

    uint32_t draw32 = rng_next(rng_state);
    uint64_t threshold = ((uint64_t)draw32 * (uint64_t)sum_exp) >> 32;

    int64_t running = 0;
    for (uint32_t i = 0; i < vocab; ++i) {
        fx_t scaled = fx_mul(logits[i] - max_logit, inv_temp);
        running += softmax_exp_fx(scaled);
        if ((uint64_t)running > threshold) return i;
    }
    return vocab - 1u;
}
