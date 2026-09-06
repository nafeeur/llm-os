#ifndef LLMOS_TOKENIZER_H
#define LLMOS_TOKENIZER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Byte-level tokenizer: every raw byte value is its own token id (0..255).
 * Three special tokens extend the vocabulary. This is deliberately the
 * simplest tokenizer object that can still be described in an LMOF
 * tokenizer directory; a real BPE merge table is a later increment.
 */

#define TOKENIZER_BYTE_VOCAB 256u
#define TOKEN_BOS (TOKENIZER_BYTE_VOCAB + 0u)
#define TOKEN_EOS (TOKENIZER_BYTE_VOCAB + 1u)
#define TOKEN_PAD (TOKENIZER_BYTE_VOCAB + 2u)
#define TOKENIZER_VOCAB_SIZE (TOKENIZER_BYTE_VOCAB + 3u)

/* Encodes text as [BOS, byte tokens..., EOS]; returns token count, 0 on overflow. */
uint32_t tokenizer_encode(const char *text, uint32_t *out_tokens, uint32_t cap);

/* Decodes a token stream back to bytes, skipping BOS/EOS/PAD; returns byte count. */
uint32_t tokenizer_decode(const uint32_t *tokens, uint32_t count, char *out_text, uint32_t cap);

#endif
