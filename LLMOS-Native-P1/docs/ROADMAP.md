# LLMOS implementation roadmap

The ordering is intentional. Framework integrations and a desktop are excluded.

## P1 — portable model-centric foundation (this release)

- freestanding x86-64 build (AArch64 QEMU and Raspberry Pi 4 builds existed
  in an earlier revision and were dropped to concentrate validation effort
  on one architecture; the core stays architecture-neutral so re-adding a
  port is a platform-layer exercise, not a rewrite);
- architecture-neutral model, tensor, context, scheduler and agent code;
- immutable shared weight pages;
- paged copy-on-write KV state;
- multiple budgeted agents;
- capability checks, journal and transactional workspace;
- deterministic native backend and integer tensor benchmark.

## P2 — first real in-guest language model

- VirtIO block driver on QEMU (done for x86-64; SD/eMMC and other-board
  block paths are future platform-layer work);
- LMOF package parser and content-hash validation (done);
- byte tokenizer (done; BPE merge table is a later increment);
- RMSNorm, RoPE, grouped-query attention, SwiGLU and sampling operators in
  Q16.16 fixed point (done, scalar reference only);
- x86 AVX2 packed kernels (not started; the build currently disables
  SSE/AVX entirely to avoid FPU state complexity without preemption);
- a small deterministic, seeded-random int8 test package (done; not a
  trained model — see `scripts/make_test_model.py`);
- measured tokens/second, time to first token and bytes moved per token
  (not started);
- persistent model and prefix objects (not started).

P2 is complete only when x86-64 QEMU generates text without Linux, Ollama,
Python, a host bridge or a network service — which the `infer2` command
already does end to end, with the caveats above on what's still missing.

## P3 — real capability microkernel and multicore execution

- physical frame discovery and allocator;
- x86 APIC and Arm GIC interrupt paths;
- per-service page tables and user-mode execution;
- capability handles that cannot be forged by a service;
- bounded shared-memory IPC;
- service restart and crash containment;
- SMP bring-up and CPU topology;
- separate compute, I/O and control cores where hardware permits;
- preemption at token, layer and safe graph boundaries.

## P4 — appliance storage, power and unattended agents

- copy-on-write object store;
- append-only integrity journal;
- crash-consistent transaction commit;
- context checkpoint/suspend/resume;
- model and KV tiering between RAM and NVMe;
- thermal and energy-aware admission/scheduling;
- watchdog and automatic recovery;
- signed system/model updates with A/B rollback.

## P5 — accelerators

- generic accelerator queue ABI;
- IOMMU/SMMU isolation;
- zero-copy tensor mappings;
- native drivers for documented GPU/NPU hardware;
- layer/tensor placement across CPU, GPU and NPU;
- asynchronous prefetch and completion-driven scheduling;
- device-loss recovery without corrupting agent transactions.

## Production exit criteria

A release is not production-ready until it has:

- real page-table isolation;
- fuzzed model/package parsers;
- power-loss-safe persistent transactions;
- measured boot and inference behavior on at least one x86-64 appliance and two
  Arm64 boards;
- a threat model and capability audit;
- long-duration multi-agent stress tests;
- deterministic recovery from model, storage and agent-service failure;
- reproducible signed builds.
