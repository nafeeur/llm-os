#ifndef LLMOS_CORE_H
#define LLMOS_CORE_H

#include <stdint.h>
#include <stdbool.h>

void llmos_initialize(bool interactive);
int llmos_run_selftests(bool verbose);
void llmos_dispatch(char *line);
void kernel_main(uint64_t boot_arg);

#endif
