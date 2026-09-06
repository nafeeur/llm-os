#include "llmos/platform.h"
#include <stdint.h>

#define PERIPHERAL_BASE 0xfe000000ull
#define GPIO_BASE       (PERIPHERAL_BASE + 0x00200000ull)
#define UART_BASE       (PERIPHERAL_BASE + 0x00201000ull)
#define GPFSEL1         0x04u
#define GPPUPPDN0       0xe4u
#define UART_DR         0x00u
#define UART_FR         0x18u
#define UART_IBRD       0x24u
#define UART_FBRD       0x28u
#define UART_LCRH       0x2cu
#define UART_CR         0x30u
#define UART_IMSC       0x38u
#define UART_ICR        0x44u

static inline void mmio_write(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}
static inline uint32_t mmio_read(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}
static inline void barrier(void) { __asm__ volatile ("dsb sy" ::: "memory"); }

void platform_early_init(uint64_t boot_arg) {
    (void)boot_arg;
    uint32_t select = mmio_read(GPIO_BASE + GPFSEL1);
    select &= ~((7u << 12) | (7u << 15));
    select |= (4u << 12) | (4u << 15); /* GPIO14/15 = ALT0 PL011 */
    mmio_write(GPIO_BASE + GPFSEL1, select);
    uint32_t pulls = mmio_read(GPIO_BASE + GPPUPPDN0);
    pulls &= ~((3u << 28) | (3u << 30));
    mmio_write(GPIO_BASE + GPPUPPDN0, pulls);
    barrier();

    mmio_write(UART_BASE + UART_CR, 0);
    mmio_write(UART_BASE + UART_ICR, 0x7ffu);
    mmio_write(UART_BASE + UART_IBRD, 26u);
    mmio_write(UART_BASE + UART_FBRD, 3u);
    mmio_write(UART_BASE + UART_LCRH, (3u << 5) | (1u << 4));
    mmio_write(UART_BASE + UART_IMSC, 0u);
    mmio_write(UART_BASE + UART_CR, (1u << 9) | (1u << 8) | 1u);
    barrier();
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
const char *platform_name(void) { return "Raspberry Pi 4 / PL011"; }
const char *platform_arch(void) { return "aarch64"; }
uint64_t platform_nominal_memory_bytes(void) { return 1024ull * 1024ull * 1024ull; }

/* SD/eMMC block path is a separate P2 roadmap item, not implemented yet. */
bool platform_block_present(void) { return false; }
uint64_t platform_block_sector_count(void) { return 0; }
bool platform_block_read(uint64_t lba, void *buf, uint32_t sector_count) {
    (void)lba; (void)buf; (void)sector_count;
    return false;
}
