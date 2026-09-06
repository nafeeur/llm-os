# LLMOS Native architecture

## Product definition

LLMOS is a small appliance OS designed around one job: keep one or more local
language models resident and safely multiplex them among autonomous agents with
minimum data movement, predictable token latency and reversible state changes.

The target stack is:

```text
Agent goals and native LLM applications
                 |
          Agent Executive
 capability budgets | context DAG | verifier | transaction
                 |
          Model Executive
 tokenizer | graph executor | KV/prefix cache | token scheduler
                 |
        Tensor Memory System
 weights | KV | scratch | placement | prefetch | compression
                 |
       Capability Nanokernel
 address spaces | channels | frames | IRQs | timers | IOMMU
                 |
       Platform + Operator HAL
 x86-64/Arm64 | UEFI/DTB | AVX/AMX/NEON/SVE | devices
```

Only the bottom two layers contain architecture- or board-specific code.

## Why this is not a conventional OS

A conventional OS exports processes, byte streams, files, sockets and generic
virtual memory. LLMOS instead exports a deliberately narrow object set:

| Native object | Purpose |
|---|---|
| Model image | Content-addressed immutable weights and execution metadata |
| Tensor extent | Typed, aligned, tier-aware memory range |
| Context | Tokens, KV pages, sampler state and deadline policy |
| Prefix | Shareable immutable context path |
| Agent | Goal, capabilities, budgets, verifier and context references |
| Channel | Bounded capability-secured message path |
| Transaction | Copy-on-write mutable-state snapshot |
| Device queue | Accelerator, storage or network submission/completion ring |
| Journal | Append-only decisions, actions, measurements and proofs |

There is no `fork()` operation. Context branching is `context_clone()`, which
shares KV pages and becomes copy-on-write only when one branch appends.

## Capability nanokernel

The production kernel remains small but is LLM-aware where that improves
policy. Its stable responsibilities are:

- create and isolate address spaces;
- own physical frames and IOMMU mappings;
- mint, transfer, attenuate and revoke capability handles;
- provide bounded shared-memory channels;
- route interrupts and timers;
- expose page-role and deadline hints to the policy services;
- enforce hard memory, token, energy and device budgets;
- recover or terminate a failed system service.

Model architectures and quantization algorithms do not become privileged
kernel code. They change too quickly and must remain replaceable components of
the Model Executive.

P1 has the object semantics but not the final isolation boundary: its services
share one privileged image. Page-table separation and protected channels are a
required next step, not an optional hardening feature.

## Tensor memory system

Every physical or accelerator-visible page carries metadata that generic page
caches lack:

```text
role: weight | kv | prefix | scratch | agent-state | journal
model identity
context or agent owner
reference count
tier: accelerator | RAM | cold storage
immutability and pinning
heat and last-use information
quantization/packing variant
eviction and recomputation cost
```

Policy differs by role:

- **Weights:** immutable, executable by operators, deduplicated globally,
  preferentially huge-page mapped and retained while demand exists.
- **KV:** grows incrementally, page-granular, reference-counted, branchable,
  compressible and evictable by context policy.
- **Prefixes:** immutable KV subgraphs promoted for cross-session reuse.
- **Scratch:** short lifetime tied to a batch or graph barrier; bulk reclaimed.
- **Agent state:** small, capability-owned and transaction-aware.
- **Journal:** append-only and integrity checked.

P1 implements an 8 MiB RAM arena with 2,048 real 4 KiB pages, page metadata,
reference counting, role accounting and copy-on-write. Production builds will
replace this early arena with discovered RAM zones, huge pages and device-local
allocators while preserving the object contract.

## Model executive

The Model Executive owns:

- model package validation;
- tokenizer execution;
- tensor graph planning;
- hardware-specific operator selection;
- model and adapter residency;
- KV/prefix allocation;
- prompt prefill;
- token decode;
- continuous batching;
- sampling;
- telemetry and admission control.

An application never initializes a separate copy of an inference framework.
All agents submit contexts to this one executive and therefore share weights,
compiled plans and reusable prefixes.

## Token scheduler

The scheduling unit is not a CPU thread. It is a phase-specific context quantum:

```text
prefill(context, token range)
decode(batch of compatible contexts, one or more token steps)
prefetch(model/layer/tensor extent)
compress-or-evict(context extent)
```

A production score combines:

- interactive first-token deadline;
- maximum permitted inter-token gap;
- agent priority and wait age;
- model residency and batch compatibility;
- KV capacity and eviction cost;
- thermal and energy budget;
- accelerator queue occupancy.

P1 implements separate prefill/decode states, bounded prefill chunks,
decode-biased priority-and-age selection and model-affine batches of four.
Weight-page reads are accounted once per batch, and avoided duplicate reads are
reported as a scheduler metric.

## Agent executive

An agent is intentionally smaller than a process:

```text
agent identity
goal and state machine
model/context references
capability table
token/page/time/energy budgets
transaction handle
verifier policy
journal cursor
```

Agents do not receive shell access by default. Native actions are typed object
operations. An irreversible external action requires a separate capability and
must cross an explicit commit boundary.

P1 supplies multiple agents with distinct priorities and budgets. A denied
irreversible action invalidates verification and rolls back staged state.

## Persistent state

The eventual storage system is not a POSIX root filesystem. It has three
classes:

1. **Immutable content-addressed objects:** model packages, tokenizer tables,
   operator plans and verified artifacts.
2. **Copy-on-write workspaces:** mutable agent output and application state.
3. **Append-only journals:** actions, authority changes, benchmark samples,
   verifier results and transaction commits.

A small read-only compatibility filesystem may later expose objects to a
maintenance environment. It is not the source of truth.

## Hardware model

Portability is achieved through narrow contracts, not by pretending devices are
identical:

- Boot contract: memory map, firmware tables/DTB, CPU topology and framebuffer.
- CPU contract: barriers, timers, atomics, cache maintenance and SIMD features.
- MMU contract: frame mapping, huge pages, protection and address-space switch.
- Interrupt contract: APIC/MSI on x86, GIC on Arm.
- Device queue contract: descriptor rings and completion interrupts.
- Operator contract: quantized matrix/vector kernels and tensor transforms.

An x86-64 UEFI machine and an Arm64 SBC use the same Model and Agent Executives,
but different boot, interrupt and device implementations.

## Current P1 code path

```text
firmware/QEMU
   -> architecture entry
   -> clear BSS and initialize serial
   -> initialize tensor arena
   -> install immutable NativeLM-test weight pages
   -> create capability-scoped agents
   -> accept serial commands
   -> create contexts
   -> prefill/decode scheduler
   -> paged KV allocation and copy-on-write
   -> verifier
   -> commit or rollback workspace state
```

The deterministic generator exists only to make every OS control path testable
before a trained model loader is present.
