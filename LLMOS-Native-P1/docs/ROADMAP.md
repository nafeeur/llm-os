# LLMOS implementation roadmap

The ordering is intentional. Framework integrations and a desktop are excluded.

## P1 — portable model-centric foundation (this release)

- freestanding x86-64 and AArch64 builds;
- QEMU images and a Raspberry Pi 4 image;
- architecture-neutral model, tensor, context, scheduler and agent code;
- immutable shared weight pages;
- paged copy-on-write KV state;
- multiple budgeted agents;
- capability checks, journal and transactional workspace;
- deterministic native backend and integer tensor benchmark.

## P2 — first real in-guest language model

- VirtIO block driver on QEMU and SD/eMMC block path for Pi;
- LMOF package parser and content-hash validation;
- byte/BPE tokenizer object;
- RMSNorm, RoPE, grouped-query attention, SwiGLU and sampling operators;
- scalar reference operators plus x86 AVX2 and Arm NEON kernels;
- a small openly licensed quantized decoder model included as a test package;
- measured tokens/second, time to first token and bytes moved per token;
- persistent model and prefix objects.

P2 is complete only when both x86-64 QEMU and Arm64 QEMU generate text without
Linux, Ollama, Python, a host bridge or a network service.

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
