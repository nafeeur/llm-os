#include "llmos/platform.h"
#include <stdint.h>

#define COM1 0x3f8u

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void platform_early_init(uint64_t boot_arg) {
    (void)boot_arg;
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

void platform_putc(char c) {
    while ((inb(COM1 + 5) & 0x20u) == 0u) platform_relax();
    outb(COM1, (uint8_t)c);
}

int platform_getc_nonblock(void) {
    return (inb(COM1 + 5) & 0x01u) ? (int)inb(COM1) : -1;
}

uint64_t platform_cycles(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void platform_relax(void) { __asm__ volatile ("pause"); }
void platform_idle(void) { platform_relax(); }

void platform_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

const char *platform_name(void) { return "x86-64 BIOS/QEMU serial"; }
const char *platform_arch(void) { return "x86_64"; }
uint64_t platform_nominal_memory_bytes(void) { return 128ull * 1024ull * 1024ull; }
