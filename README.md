LLMOS Native is a from-scratch, bare-metal operating system research project whose sole purpose is running local LLM inference and multi-agent execution directly on hardware. No Linux, no Python, no Ollama/vLLM wrapper underneath.

## What's here

All of the actual project — source, docs, build system — lives in [`LLMOS-Native-P1/`](LLMOS-Native-P1). See [`LLMOS-Native-P1/README.md`](LLMOS-Native-P1/README.md) for the full write-up; the short version:

- A C core (model, tensor, context, scheduler, agent executive) targeting x86-64. (An earlier revision also built for AArch64 QEMU and Raspberry Pi 4; that's on hold to concentrate validation effort on one architecture — see `docs/ROADMAP.md`.)
- An 8 MiB paged tensor arena with immutable shared model weights, copy-on-write KV contexts, capability-scoped agents, and transactional workspace writes with rollback.
- A legacy VirtIO-blk (PCI) driver, a SHA-256-validated LMOF model package format, a byte tokenizer, and Q16.16 fixed-point RMSNorm / RoPE / grouped-query attention / SwiGLU / sampling kernels — a real (not simulated) forward pass, since the kernel build disables x87/SSE entirely and has no runtime float.
- A serial console shell (`llmos>`) for poking at all of the above: `infer`, `sessions`, `fork`, `sched`, `agent demo`, `workspace`, `journal`, `blk`, `modelload`, `infer2`.

Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` inside `LLMOS-Native-P1/` before extending the system.

## Quick start

```bash
cd LLMOS-Native-P1
make test        # build the x86-64 target + host integration tests
make x86 && ./scripts/run-x86.sh
```

## Status

`NativeLM-test` (the P1 scheduler demo) is a deterministic operator/scheduler test backend, not a trained model. The VirtIO/LMOF/fixed-point-transformer path (`blk` / `modelload` / `infer2`) loads a small synthetic, untrained int8 test package and runs a genuine forward pass — verified working end-to-end in QEMU on x86-64, the only target currently maintained.
