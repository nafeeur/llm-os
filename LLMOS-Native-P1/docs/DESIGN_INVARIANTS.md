# LLMOS design invariants

These rules define the product. Features that violate them do not belong in
the native system.

1. **The model is a system object, not an application process.** Installing a
   model creates immutable weight extents, tokenizer state, execution plans,
   quantization metadata and hardware-specific packed variants.
2. **An inference context is the primary schedulable entity.** Threads are an
   implementation detail. The scheduler reasons about prefill, decode, token
   deadlines, model affinity, KV pressure and energy.
3. **Weight memory is mapped once and shared.** An agent never receives a
   private copy of a base model merely because it is a separate task.
4. **KV state is a graph.** Prefixes and branches share pages until mutation;
   copying an agent context does not copy its entire history.
5. **Agents have explicit budgets and capabilities.** There is no ambient
   authority inherited from a logged-in desktop user.
6. **Mutable work is transactional.** Agent-created state is staged, verified
   and atomically committed or rolled back.
7. **There is no mandatory POSIX layer.** Files, fork/exec, signals and sockets
   are not the native interface. Compatibility may exist later in an isolated
   domain, but cannot shape the core.
8. **No Python or framework runtime is required to boot or infer.** Native
   model execution is freestanding and statically controlled.
9. **Hardware differences stop at the platform and operator layers.** Model,
   context, scheduler, capability, agent and transaction logic is shared across
   x86-64 and AArch64.
10. **Performance claims require measurement.** Deterministic test generation
    is never described as a trained language model, and structural validation
    is never described as a hardware boot test.
