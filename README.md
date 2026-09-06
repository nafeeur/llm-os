LLMOS Native is a from-scratch, bare-metal operating system research project whose sole purpose is running local LLM inference and multi-agent execution directly on hardware. No Linux, no Python, no Ollama/vLLM wrapper underneath.

## What's here

All of the actual project — source, docs, build system — lives in [`LLMOS-Native-P1/`](LLMOS-Native-P1). See [`LLMOS-Native-P1/README.md`](LLMOS-Native-P1/README.md) for the full write-up; the short version:

- A shared C core (model, tensor, context, scheduler, agent executive) that builds identically for x86-64, AArch64 QEMU, and Raspberry Pi 4.
- An 8 MiB paged tensor arena with immutable shared model weights, copy-on-write KV contexts, capability-scoped agents, and transactional workspace writes with rollback.
- A legacy VirtIO block driver (PCI on x86-64, MMIO on AArch64), a SHA-256-validated LMOF model package format, a byte tokenizer, and Q16.16 fixed-point RMSNorm / RoPE / grouped-query attention / SwiGLU / sampling kernels — a real (not simulated) forward pass, since the kernel build disables x87/SSE/NEON entirely and has no runtime float.
- A serial console shell (`llmos>`) for poking at all of the above: `infer`, `sessions`, `fork`, `sched`, `agent demo`, `workspace`, `journal`, `blk`, `modelload`, `infer2`.

Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` inside `LLMOS-Native-P1/` before extending the system.

## Quick start

```bash
cd LLMOS-Native-P1
make test        # build all three targets + host integration tests
make x86 && ./scripts/run-x86.sh
make arm64 && ./scripts/run-arm64.sh
```

## Status

`NativeLM-test` (the P1 scheduler demo) is a deterministic operator/scheduler test backend, not a trained model. The newer VirtIO/LMOF/fixed-point-transformer path (`blk` / `modelload` / `infer2`) loads a small synthetic, untrained int8 test package and runs a genuine forward pass — verified working end-to-end on the x86-64 QEMU target; the AArch64 target boots and passes most self-tests but has an outstanding issue in the fixed-point attention path still being debugged.
