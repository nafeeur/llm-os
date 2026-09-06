#ifndef LLMOS_PLATFORM_H
#define LLMOS_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

void platform_early_init(uint64_t boot_arg);
void platform_putc(char c);
int platform_getc_nonblock(void);
uint64_t platform_cycles(void);
void platform_relax(void);
void platform_idle(void);
void platform_halt(void) __attribute__((noreturn));
const char *platform_name(void);
const char *platform_arch(void);
uint64_t platform_nominal_memory_bytes(void);

/* Block storage (VirtIO-blk on QEMU targets; not yet implemented on Pi 4 --
 * see docs/ROADMAP.md P2 SD/eMMC item). Sector size is fixed at 512 bytes. */
bool platform_block_present(void);
uint64_t platform_block_sector_count(void);
bool platform_block_read(uint64_t lba, void *buf, uint32_t sector_count);

#endif
