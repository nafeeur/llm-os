# LLMOS Native P1

LLMOS Native is a from-scratch, bare-metal operating-system research project
whose only first-class workload is local language-model inference and
multi-agent execution.

It is deliberately **not**:

- a Linux distribution;
- a Python agent framework;
- an Ollama/vLLM wrapper;
- a POSIX-compatible general-purpose OS;
- a desktop automation layer.

The same architecture-neutral C core currently builds into:

- `build/x86_64/llmos-x86_64.img` — x86-64 BIOS/QEMU disk image;
- `build/aarch64-qemu/llmos-aarch64-qemu.bin` — Arm64 QEMU `virt` image;
- `build/rpi4/llmos-rpi4.img` — Raspberry Pi 4 / BCM2711 direct kernel image.

## What P1 demonstrates

- An 8 MiB, 4 KiB-granularity tensor arena with role metadata for weight, KV,
  prefix, scratch, agent-state and journal pages.
- Immutable, pinned model-weight pages shared by every inference context.
- Growing KV contexts with reference counting and page-level copy-on-write.
- Separate prefill and decode phases.
- Model-affine dynamic batches of up to four sessions.
- Decode-biased, priority-and-age-aware scheduling.
- Admission control based on context limits and available tensor pages.
- Multiple agents with token budgets, page budgets and capability masks.
- Transactional workspace writes with verify/commit/rollback behavior.
- A bounded audit journal.
- A native integer tensor microbenchmark.
- One common model/agent core across x86-64 and AArch64.

`NativeLM-test` is a deterministic operator and scheduler test backend. It is
not represented as a trained model. Loading and executing a real quantized
transformer is the P2 boundary.

## Build

Required tools:

- Clang 17 or newer;
- LLD;
- LLVM objcopy;
- GNU Make;
- a normal host C compiler for tests.

```bash
make test
```

This builds all three targets, validates the image structures, runs seven host
integration tests, and runs AddressSanitizer plus UndefinedBehaviorSanitizer.

## Run x86-64 in QEMU

```bash
make x86
./scripts/run-x86.sh
```

Equivalent command:

```bash
qemu-system-x86_64 \
  -machine pc -m 128M -smp 1 \
  -drive format=raw,file=build/x86_64/llmos-x86_64.img \
  -display none -serial stdio -monitor none -no-reboot
```

## Run Arm64 in QEMU

```bash
make arm64
./scripts/run-arm64.sh
```

Equivalent command:

```bash
qemu-system-aarch64 \
  -machine virt -cpu cortex-a72 -m 256M -smp 1 \
  -kernel build/aarch64-qemu/llmos-aarch64-qemu.bin \
  -nographic -monitor none -no-reboot
```

## Run on Raspberry Pi 4

See `pi4/README.txt`. The P1 Pi image uses the PL011 UART and needs a 3.3 V
serial adapter at 115200 8N1. It is a BCM2711-specific platform target; it is
not a Raspberry Pi 5 image.

## Commands

At the `llmos>` prompt:

```text
about
arch
models
mem
pages
infer explain shared model weights
sessions
fork 1 continue with a second agent
sched
batchdemo
agents
agent demo
workspace
cat alpha.txt
agent fail 4 attempt an unauthorized power action
journal
tensorbench
selftest
halt
```

The failed-agent command performs inference and stages a write, then attempts
an irreversible action without the required capability. The action is denied
and the entire job is rolled back.

## Source layout

```text
arch/x86_64/           x86 boot, serial and linker definition
arch/aarch64/          Arm64 entry, QEMU/Pi platforms and link definitions
include/llmos/         architecture-neutral contracts
src/runtime.c          freestanding runtime and serial console formatting
src/core.c             model, tensor, context, scheduler and agent executive
tests/                 host platform and integration test entry point
pi4/                   Raspberry Pi 4 boot configuration
scripts/               QEMU launchers
docs/                   architecture, portability, model format and roadmap
```

## Design boundary

P1 is a bare-metal vertical slice, not yet a production microkernel. The model
executive, agent executive and kernel mechanisms currently share one privileged
address space. P2/P3 must add page-table-separated services, true capability
handles, interrupt-driven I/O, SMP, persistent model storage, tokenizer support
and a real transformer backend.

Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` before extending the system.
