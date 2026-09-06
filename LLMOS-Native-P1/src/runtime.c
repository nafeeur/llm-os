#include "llmos/runtime.h"
#include "llmos/platform.h"

void *k_memset(void *dst, int value, size_t count) {
    uint8_t *p = (uint8_t *)dst;
    while (count--) *p++ = (uint8_t)value;
    return dst;
}

void *k_memcpy(void *dst, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (count--) *d++ = *s++;
    return dst;
}

int k_memcmp(const void *a, const void *b, size_t count) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    while (count--) {
        if (*x != *y) return (int)*x - (int)*y;
        ++x;
        ++y;
    }
    return 0;
}

size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

int k_strcmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

bool k_starts_with(const char *text, const char *prefix) {
    while (prefix && *prefix) {
        if (!text || *text++ != *prefix++) return false;
    }
    return true;
}

char *k_strncpy(char *dst, const char *src, size_t cap) {
    if (!cap) return dst;
    size_t i = 0;
    while (i + 1 < cap && src && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
    return dst;
}

uint32_t k_hash32(const char *s) {
    uint32_t h = 2166136261u;
    while (s && *s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

uint32_t k_parse_u32(const char *s, bool *ok) {
    uint32_t value = 0;
    bool any = false;
    while (s && *s == ' ') ++s;
    while (s && *s >= '0' && *s <= '9') {
        any = true;
        value = value * 10u + (uint32_t)(*s - '0');
        ++s;
    }
    if (ok) *ok = any;
    return value;
}

void console_putc(char c) {
    if (c == '\n') platform_putc('\r');
    platform_putc(c);
}

void console_write(const char *s) {
    while (s && *s) console_putc(*s++);
}

static void print_unsigned(uint64_t value, unsigned base, unsigned width, char pad) {
    char buf[32];
    static const char digits[] = "0123456789abcdef";
    unsigned n = 0;
    do {
        buf[n++] = digits[value % base];
        value /= base;
    } while (value && n < sizeof(buf));
    while (n < width && n < sizeof(buf)) buf[n++] = pad;
    while (n) console_putc(buf[--n]);
}

void k_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (fmt && *fmt) {
        if (*fmt != '%') { console_putc(*fmt++); continue; }
        ++fmt;
        char pad = ' ';
        unsigned width = 0;
        if (*fmt == '0') { pad = '0'; ++fmt; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10u + (unsigned)(*fmt++ - '0'); }
        bool longlong = false;
        if (*fmt == 'l') { ++fmt; if (*fmt == 'l') { longlong = true; ++fmt; } }
        switch (*fmt ? *fmt++ : 0) {
            case '%': console_putc('%'); break;
            case 'c': console_putc((char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                console_write(s ? s : "(null)");
                break;
            }
            case 'u': {
                uint64_t v = longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
                print_unsigned(v, 10u, width, pad);
                break;
            }
            case 'd': {
                int64_t v = longlong ? va_arg(ap, long long) : va_arg(ap, int);
                if (v < 0) { console_putc('-'); v = -v; }
                print_unsigned((uint64_t)v, 10u, width, pad);
                break;
            }
            case 'x': {
                uint64_t v = longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
                print_unsigned(v, 16u, width, pad);
                break;
            }
            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(ap, void *);
                console_write("0x");
                print_unsigned((uint64_t)v, 16u, (unsigned)(sizeof(uintptr_t) * 2u), '0');
                break;
            }
            default: console_putc('?'); break;
        }
    }
    va_end(ap);
}

size_t console_readline(char *buf, size_t cap) {
    size_t n = 0;
    if (!cap) return 0;
    for (;;) {
        int value = platform_getc_nonblock();
        if (value < 0) { platform_idle(); continue; }
        char c = (char)value;
        if (c == '\r' || c == '\n') {
            console_putc('\n');
            buf[n] = 0;
            return n;
        }
        if (c == 8 || c == 127) {
            if (n) { --n; console_write("\b \b"); }
            continue;
        }
        if (c >= 32 && c < 127 && n + 1 < cap) {
            buf[n++] = c;
            console_putc(c);
        }
    }
}
