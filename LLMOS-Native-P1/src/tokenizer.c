#include "llmos/tokenizer.h"

uint32_t tokenizer_encode(const char *text, uint32_t *out_tokens, uint32_t cap) {
    if (!text || !out_tokens || cap < 2u) return 0;
    uint32_t n = 0;
    out_tokens[n++] = TOKEN_BOS;
    while (*text && n + 1u < cap) {
        out_tokens[n++] = (uint32_t)(uint8_t)*text++;
    }
    if (*text) return 0; /* would not fit */
    out_tokens[n++] = TOKEN_EOS;
    return n;
}

uint32_t tokenizer_decode(const uint32_t *tokens, uint32_t count, char *out_text, uint32_t cap) {
    if (!tokens || !out_text || !cap) return 0;
    uint32_t written = 0;
    for (uint32_t i = 0; i < count && written + 1u < cap; ++i) {
        uint32_t t = tokens[i];
        if (t == TOKEN_EOS) break;
        if (t == TOKEN_BOS || t == TOKEN_PAD) continue;
        if (t < TOKENIZER_BYTE_VOCAB) out_text[written++] = (char)(uint8_t)t;
    }
    out_text[written] = 0;
    return written;
}
