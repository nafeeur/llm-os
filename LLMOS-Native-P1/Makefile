SHELL := /bin/bash
CLANG ?= clang
LD_LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
HOST_CC ?= cc
PYTHON ?= python3
BUILD := build
DIST := dist
COMMON_SRC := src/runtime.c src/core.c src/sha256.c src/lmof.c src/tokenizer.c src/ops.c
INCLUDES := -Iinclude
COMMON_FLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-vectorize -fno-slp-vectorize $(INCLUDES)
X86_FLAGS := --target=x86_64-none-elf $(COMMON_FLAGS) -m64 -mno-red-zone -mcmodel=small \
	-mno-mmx -mno-sse -mno-sse2
ARM_FLAGS := --target=aarch64-none-elf $(COMMON_FLAGS) -mcpu=cortex-a53 -mgeneral-regs-only
# src/core.c miscompiles under this clang (observed: 22.1.8) at -O1/-O2 for
# aarch64 -mgeneral-regs-only: page_allocate() takes an early exception into
# an unpopulated vector table during llmos_initialize(), before any serial
# output, on both the QEMU virt and Pi 4 targets. Verified by bisection
# across optimization levels and object files -- -O0 for this one
# translation unit avoids it; every other file stays at -O2, as does the
# entire x86-64 build, which is unaffected. Revisit if the toolchain updates.
ARM_FLAGS_CORE := $(subst -O2,-O0,$(ARM_FLAGS))

.PHONY: all x86 arm64 pi4 check host-test sanitize test vm-test-x86 vm-test-arm64 run-x86 run-arm64 package clean
all: x86 arm64 pi4

$(BUILD):
	mkdir -p $(BUILD)

# x86-64 BIOS/QEMU target
$(BUILD)/x86_64:
	mkdir -p $@
$(BUILD)/x86_64/runtime.o: src/runtime.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/core.o: src/core.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/sha256.o: src/sha256.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/lmof.o: src/lmof.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/tokenizer.o: src/tokenizer.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/ops.o: src/ops.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/virtio_blk.o: src/virtio_blk.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/virtio_pci.o: arch/x86_64/virtio_pci.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/platform.o: arch/x86_64/platform.c | $(BUILD)/x86_64
	$(CLANG) $(X86_FLAGS) -c $< -o $@
$(BUILD)/x86_64/entry.o: arch/x86_64/entry.S | $(BUILD)/x86_64
	$(CLANG) --target=x86_64-none-elf -m64 -ffreestanding -fno-pie -c $< -o $@
X86_OBJS := $(BUILD)/x86_64/entry.o $(BUILD)/x86_64/runtime.o $(BUILD)/x86_64/core.o \
	$(BUILD)/x86_64/sha256.o $(BUILD)/x86_64/lmof.o $(BUILD)/x86_64/tokenizer.o \
	$(BUILD)/x86_64/ops.o $(BUILD)/x86_64/virtio_blk.o $(BUILD)/x86_64/virtio_pci.o \
	$(BUILD)/x86_64/platform.o
$(BUILD)/x86_64/kernel.elf: $(X86_OBJS) arch/x86_64/linker.ld
	$(LD_LLD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T arch/x86_64/linker.ld \
		$(X86_OBJS) -o $@
$(BUILD)/x86_64/kernel.bin: $(BUILD)/x86_64/kernel.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/x86_64/boot.bin: arch/x86_64/boot.S $(BUILD)/x86_64/kernel.bin | $(BUILD)/x86_64
	@sectors=$$(( ($$(stat -c%s $(BUILD)/x86_64/kernel.bin) + 511) / 512 )); \
	if (( sectors < 1 || sectors > 2048 )); then echo "x86 kernel requires $$sectors sectors; boot loader limit is 2048 (soft margin under the 16-bit LBA read count, not a hardware ceiling)" >&2; exit 1; fi; \
	$(CLANG) --target=x86_64-none-elf -m64 -ffreestanding -fno-pie -DKERNEL_SECTORS=$$sectors -c arch/x86_64/boot.S -o $(BUILD)/x86_64/boot.o; \
	$(LD_LLD) -m elf_x86_64 -e boot_start -Ttext 0x7c00 --image-base=0 --oformat binary $(BUILD)/x86_64/boot.o -o $@; \
	test "$$(stat -c%s $@)" -eq 512
$(BUILD)/x86_64/llmos-x86_64.img: $(BUILD)/x86_64/boot.bin $(BUILD)/x86_64/kernel.bin
	truncate -s 32M $@
	dd if=$(BUILD)/x86_64/boot.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/x86_64/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
x86: $(BUILD)/x86_64/llmos-x86_64.img

# AArch64 QEMU virt target
$(BUILD)/aarch64-qemu:
	mkdir -p $@
$(BUILD)/aarch64-qemu/runtime.o: src/runtime.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/core.o: src/core.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS_CORE) -c $< -o $@
$(BUILD)/aarch64-qemu/sha256.o: src/sha256.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/lmof.o: src/lmof.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/tokenizer.o: src/tokenizer.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/ops.o: src/ops.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/virtio_blk.o: src/virtio_blk.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/virtio_mmio.o: arch/aarch64/virtio_mmio.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/platform.o: arch/aarch64/platform_qemu.c | $(BUILD)/aarch64-qemu
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/aarch64-qemu/entry.o: arch/aarch64/entry.S | $(BUILD)/aarch64-qemu
	$(CLANG) --target=aarch64-none-elf -mcpu=cortex-a53 -ffreestanding -fno-pie -c $< -o $@
ARM_QEMU_OBJS := $(BUILD)/aarch64-qemu/entry.o $(BUILD)/aarch64-qemu/runtime.o $(BUILD)/aarch64-qemu/core.o \
	$(BUILD)/aarch64-qemu/sha256.o $(BUILD)/aarch64-qemu/lmof.o $(BUILD)/aarch64-qemu/tokenizer.o \
	$(BUILD)/aarch64-qemu/ops.o $(BUILD)/aarch64-qemu/virtio_blk.o $(BUILD)/aarch64-qemu/virtio_mmio.o \
	$(BUILD)/aarch64-qemu/platform.o
$(BUILD)/aarch64-qemu/llmos-aarch64-qemu.elf: $(ARM_QEMU_OBJS) arch/aarch64/linker_qemu.ld
	$(LD_LLD) -m aarch64elf -nostdlib -T arch/aarch64/linker_qemu.ld \
		$(ARM_QEMU_OBJS) -o $@
$(BUILD)/aarch64-qemu/llmos-aarch64-qemu.bin: $(BUILD)/aarch64-qemu/llmos-aarch64-qemu.elf
	$(OBJCOPY) -O binary $< $@
arm64: $(BUILD)/aarch64-qemu/llmos-aarch64-qemu.bin

# Raspberry Pi 4 direct 64-bit firmware target
$(BUILD)/rpi4:
	mkdir -p $@
$(BUILD)/rpi4/runtime.o: src/runtime.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/core.o: src/core.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS_CORE) -c $< -o $@
$(BUILD)/rpi4/sha256.o: src/sha256.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/lmof.o: src/lmof.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/tokenizer.o: src/tokenizer.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/ops.o: src/ops.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/platform.o: arch/aarch64/platform_pi4.c | $(BUILD)/rpi4
	$(CLANG) $(ARM_FLAGS) -c $< -o $@
$(BUILD)/rpi4/entry.o: arch/aarch64/entry.S | $(BUILD)/rpi4
	$(CLANG) --target=aarch64-none-elf -mcpu=cortex-a53 -ffreestanding -fno-pie -c $< -o $@
RPI4_OBJS := $(BUILD)/rpi4/entry.o $(BUILD)/rpi4/runtime.o $(BUILD)/rpi4/core.o \
	$(BUILD)/rpi4/sha256.o $(BUILD)/rpi4/lmof.o $(BUILD)/rpi4/tokenizer.o \
	$(BUILD)/rpi4/ops.o $(BUILD)/rpi4/platform.o
$(BUILD)/rpi4/llmos-rpi4.elf: $(RPI4_OBJS) arch/aarch64/linker_pi4.ld
	$(LD_LLD) -m aarch64elf -nostdlib -T arch/aarch64/linker_pi4.ld \
		$(RPI4_OBJS) -o $@
$(BUILD)/rpi4/llmos-rpi4.img: $(BUILD)/rpi4/llmos-rpi4.elf
	$(OBJCOPY) -O binary $< $@
pi4: $(BUILD)/rpi4/llmos-rpi4.img

host-test: | $(BUILD)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(INCLUDES) \
		tests/host_platform.c $(COMMON_SRC) tests/host_test.c -o $(BUILD)/host-test
	$(BUILD)/host-test

sanitize: | $(BUILD)
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror $(INCLUDES) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/host_platform.c $(COMMON_SRC) tests/host_test.c -o $(BUILD)/host-test-sanitize
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/host-test-sanitize

check: all
	@sig=$$(od -An -tx1 -j510 -N2 $(BUILD)/x86_64/boot.bin | tr -d ' \n'); \
	test "$$sig" = "55aa"; \
	test -z "$$(nm -u $(BUILD)/x86_64/kernel.elf)"; \
	test -z "$$(nm -u $(BUILD)/aarch64-qemu/llmos-aarch64-qemu.elf)"; \
	test -z "$$(nm -u $(BUILD)/rpi4/llmos-rpi4.elf)"; \
	echo "x86 kernel: $$(stat -c%s $(BUILD)/x86_64/kernel.bin) bytes"; \
	echo "arm64 qemu: $$(stat -c%s $(BUILD)/aarch64-qemu/llmos-aarch64-qemu.bin) bytes"; \
	echo "rpi4 image: $$(stat -c%s $(BUILD)/rpi4/llmos-rpi4.img) bytes"; \
	echo "structural checks: PASS"

test: check host-test sanitize

vm-test-x86: x86
	$(PYTHON) scripts/vm_smoke.py x86
vm-test-arm64: arm64
	$(PYTHON) scripts/vm_smoke.py arm64

run-x86: x86
	./scripts/run-x86.sh
run-arm64: arm64
	./scripts/run-arm64.sh

package: test
	mkdir -p $(DIST)
	rm -f $(DIST)/llmos-native-p1.zip
	zip -qr $(DIST)/llmos-native-p1.zip . -x 'dist/*' -x '.git/*'
	sha256sum $(DIST)/llmos-native-p1.zip > $(DIST)/llmos-native-p1.zip.sha256

clean:
	rm -rf $(BUILD) $(DIST)
