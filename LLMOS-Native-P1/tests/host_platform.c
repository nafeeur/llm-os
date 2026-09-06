#include "llmos/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void platform_early_init(uint64_t boot_arg) { (void)boot_arg; }
void platform_putc(char c) { putchar((unsigned char)c); }
int platform_getc_nonblock(void) { return -1; }
uint64_t platform_cycles(void) { return (uint64_t)clock(); }
void platform_relax(void) { }
void platform_idle(void) { }
void platform_halt(void) { exit(0); }
const char *platform_name(void) { return "host-test"; }
const char *platform_arch(void) { return "host"; }
uint64_t platform_nominal_memory_bytes(void) { return 1024ull * 1024ull * 1024ull; }

bool platform_block_present(void) { return false; }
uint64_t platform_block_sector_count(void) { return 0; }
bool platform_block_read(uint64_t lba, void *buf, uint32_t sector_count) {
    (void)lba; (void)buf; (void)sector_count;
    return false;
}
