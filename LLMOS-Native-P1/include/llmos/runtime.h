#ifndef LLMOS_RUNTIME_H
#define LLMOS_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

void *k_memset(void *dst, int value, size_t count);
void *k_memcpy(void *dst, const void *src, size_t count);
int k_memcmp(const void *a, const void *b, size_t count);
size_t k_strlen(const char *s);
int k_strcmp(const char *a, const char *b);
bool k_starts_with(const char *text, const char *prefix);
char *k_strncpy(char *dst, const char *src, size_t cap);
uint32_t k_hash32(const char *s);
uint32_t k_parse_u32(const char *s, bool *ok);
void console_putc(char c);
void console_write(const char *s);
void k_printf(const char *fmt, ...);
size_t console_readline(char *buf, size_t cap);

#endif
