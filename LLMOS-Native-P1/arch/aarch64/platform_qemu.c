#include "llmos/platform.h"
#include <stdint.h>

#define UART_BASE 0x09000000ull
#define UART_DR   0x00u
#define UART_FR   0x18u
#define UART_IBRD 0x24u
#define UART_FBRD 0x28u
#define UART_LCRH 0x2cu
#define UART_CR   0x30u
#define UART_IMSC 0x38u
#define UART_ICR  0x44u

static inline void mmio_write(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}
static inline uint32_t mmio_read(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}

void platform_early_init(uint64_t boot_arg) {
    (void)boot_arg;
    mmio_write(UART_BASE + UART_CR, 0);
    mmio_write(UART_BASE + UART_ICR, 0x7ffu);
    mmio_write(UART_BASE + UART_IBRD, 13u);
    mmio_write(UART_BASE + UART_FBRD, 1u);
    mmio_write(UART_BASE + UART_LCRH, (3u << 5) | (1u << 4));
    mmio_write(UART_BASE + UART_IMSC, 0u);
    mmio_write(UART_BASE + UART_CR, (1u << 9) | (1u << 8) | 1u);
}

void platform_putc(char c) {
    while (mmio_read(UART_BASE + UART_FR) & (1u << 5)) platform_relax();
    mmio_write(UART_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

int platform_getc_nonblock(void) {
    if (mmio_read(UART_BASE + UART_FR) & (1u << 4)) return -1;
    return (int)(mmio_read(UART_BASE + UART_DR) & 0xffu);
}

uint64_t platform_cycles(void) {
    uint64_t value;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}
void platform_relax(void) { __asm__ volatile ("yield"); }
void platform_idle(void) { platform_relax(); }
void platform_halt(void) { for (;;) __asm__ volatile ("msr daifset, #0xf; wfe"); }
const char *platform_name(void) { return "QEMU virt / PL011"; }
const char *platform_arch(void) { return "aarch64"; }
uint64_t platform_nominal_memory_bytes(void) { return 256ull * 1024ull * 1024ull; }
