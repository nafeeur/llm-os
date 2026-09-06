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

.PHONY: all x86 check host-test sanitize test vm-test-x86 run-x86 package clean
all: x86

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
	echo "x86 kernel: $$(stat -c%s $(BUILD)/x86_64/kernel.bin) bytes"; \
	echo "structural checks: PASS"

test: check host-test sanitize

vm-test-x86: x86
	$(PYTHON) scripts/vm_smoke.py x86

run-x86: x86
	./scripts/run-x86.sh

package: test
	mkdir -p $(DIST)
	rm -f $(DIST)/llmos-native-p1.zip
	zip -qr $(DIST)/llmos-native-p1.zip . -x 'dist/*' -x '.git/*'
	sha256sum $(DIST)/llmos-native-p1.zip > $(DIST)/llmos-native-p1.zip.sha256

clean:
	rm -rf $(BUILD) $(DIST)
