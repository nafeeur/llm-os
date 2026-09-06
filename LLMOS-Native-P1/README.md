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

The C core currently builds into `build/x86_64/llmos-x86_64.img` — an x86-64
BIOS/QEMU disk image. (An earlier revision also targeted AArch64 QEMU and
Raspberry Pi 4; that work is on hold in favor of finishing one architecture
first — see `docs/ROADMAP.md`.)

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

`NativeLM-test` is a deterministic operator and scheduler test backend. It is
not represented as a trained model.

## What the P2 slice adds

- A legacy VirtIO block driver (PCI, legacy/transitional device 0x1af4:0x1001)
  with its own shared virtqueue implementation.
- LMOF (`docs/MODEL_FORMAT.md`): a SHA-256-content-hash-validated model
  package format — header, tensor directory, per-tensor quantization scale.
- A byte-level tokenizer.
- Q16.16 fixed-point RMSNorm, RoPE, grouped-query attention, SwiGLU and
  sampling kernels (the kernel build disables x87/SSE entirely, so there is
  no runtime float available).
- `blk` / `modelload` / `infer2` shell commands that load a package over the
  block device and run a genuine forward pass against it.

The test package (`scripts/make_test_model.py`) is seeded-random int8
weights, not a trained checkpoint — the point is exercising a real format,
real hash validation and real fixed-point transformer math end to end, not
producing meaningful text.

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

This builds the x86-64 target, validates the image structure, runs the host
integration tests, and runs AddressSanitizer plus UndefinedBehaviorSanitizer.

## Run in QEMU

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

To also exercise the P2 slice, generate the test model package and attach it
as a second (VirtIO) drive — `scripts/run-x86.sh` does this automatically
whenever `build/models/nativelm2-test.lmof` exists:

```bash
python3 scripts/make_test_model.py
make x86
./scripts/run-x86.sh
```

Then at the `llmos>` prompt: `blk`, `modelload`, `infer2 hello`.

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
blk
modelload
infer2 hello
selftest
halt
```

The failed-agent command performs inference and stages a write, then attempts
an irreversible action without the required capability. The action is denied
and the entire job is rolled back.

## Source layout

```text
arch/x86_64/           x86 boot, serial, PCI/VirtIO discovery and linker definition
include/llmos/         architecture-neutral contracts and generated fixed-point tables
src/runtime.c          freestanding runtime and serial console formatting
src/core.c             model, tensor, context, scheduler and agent executive
src/sha256.c           freestanding SHA-256
src/lmof.c             LMOF model package parser
src/tokenizer.c         byte-level tokenizer
src/ops.c              Q16.16 fixed-point tensor operators
src/virtio_blk.c       shared legacy VirtIO virtqueue driver
tests/                 host platform and integration test entry point
scripts/               QEMU launcher, table generator, test model packager
docs/                   architecture, model format and roadmap
```

## Design boundary

P1 is a bare-metal vertical slice, not yet a production microkernel. The model
executive, agent executive and kernel mechanisms currently share one privileged
address space. Later phases must add page-table-separated services, true
capability handles, interrupt-driven I/O, SMP, and a real trained model.

Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` before extending the system.
