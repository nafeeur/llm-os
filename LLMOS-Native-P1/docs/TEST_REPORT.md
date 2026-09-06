# LLMOS Native P1 test report

## Build validation

The release is built with Clang/LLD for ELF64 x86-64, entry `0x10000`. The
final flat binary is on the order of 60-70 KiB. The tensor arena and other
large structures are zero-initialized BSS and therefore do not inflate the
boot image.

Structural checks verify:

- the boot sector is exactly 512 bytes;
- the boot signature is `55 aa`;
- the kernel remains within the current 2048-sector loader limit (a soft
  margin under the boot loader's 16-bit LBA read count, not a hardware
  ceiling);
- the ELF image has no unresolved symbols;
- the build completes with warnings treated as errors.

## Integration tests

Thirteen common-core integration tests pass on the host harness, under
AddressSanitizer and UndefinedBehaviorSanitizer:

1. tensor-page allocation/reference/release lifecycle;
2. immutable resident model weights;
3. KV context copy-on-write;
4. model-affine dynamic batching;
5. capability denial for a restricted agent;
6. direct transaction rollback;
7. rollback of a job that attempts an irreversible denied action;
8. SHA-256 known-answer vector;
9. tokenizer round-trip;
10. fixed-point RoPE preserves vector norm;
11. fixed-point attention over a single KV position returns the value vector;
12. fixed-point sigmoid midpoint;
13. LMOF parser detects content-hash corruption.

## Live QEMU verification

Unlike the initial P1 cut, this has been verified with an actual QEMU boot,
not just structural/host-side validation. `qemu-system-x86_64` boots the
image, all 13 self-tests pass at the `llmos>` prompt, and the P2 slice
(`blk` / `modelload` / `infer2`) has been confirmed end to end: the legacy
VirtIO-blk driver discovers and reads from a second attached drive, the LMOF
parser validates its SHA-256 content hash, and the fixed-point forward pass
generates tokens.

## Scope note

AArch64 QEMU and Raspberry Pi 4 targets existed in an earlier revision but
have been dropped to concentrate validation effort on one architecture; see
`docs/ROADMAP.md`.
