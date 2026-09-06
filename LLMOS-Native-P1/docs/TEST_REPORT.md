# LLMOS Native P1 test report

## Build validation

The release was built with Clang/LLD for:

- ELF64 x86-64, entry `0x10000`;
- ELF64 AArch64 QEMU, entry `0x40080000`;
- ELF64 AArch64 Raspberry Pi 4, entry `0x80000`.

The final flat binaries are approximately 33–42 KiB. The tensor arena and
other large structures are zero-initialized BSS and therefore do not inflate
the boot image.

Structural checks verify:

- x86 boot sector is exactly 512 bytes;
- x86 boot signature is `55 aa`;
- the x86 kernel remains within the current 127-sector loader limit;
- all three ELF images have no unresolved symbols;
- all three architecture builds complete with warnings treated as errors.

## Integration tests

Seven common-core integration tests pass:

1. tensor-page allocation/reference/release lifecycle;
2. immutable resident model weights;
3. KV context copy-on-write;
4. model-affine dynamic batching;
5. capability denial for a restricted agent;
6. direct transaction rollback;
7. rollback of a job that attempts an irreversible denied action.

The same tests pass under AddressSanitizer and UndefinedBehaviorSanitizer in the
host harness.

## Limitations of validation

QEMU was not installed in the build environment and package installation had no
network access. Therefore this report does **not** claim an observed VM boot.
The images are compiled and structurally validated, and launch scripts are
included for live verification on a machine with QEMU.

The Raspberry Pi image has not been represented as physically boot-tested. It
must be validated through the PL011 serial console on a BCM2711 Raspberry Pi 4.
