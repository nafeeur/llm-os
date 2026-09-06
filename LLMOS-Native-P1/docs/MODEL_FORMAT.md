# LMOF: LLMOS Model Object Format

This is the planned on-disk contract for native model packages. P1 defines the
object model but does not yet parse LMOF from block storage.

## Goals

- map weights without deserializing framework objects;
- validate the complete package before execution;
- permit several hardware-specific packed variants in one package;
- support base-model plus adapter overlays;
- make tensors independently prefetchable and evictable;
- avoid copying immutable data;
- support content-addressed deduplication.

## Container layout

```text
LMOF header
manifest
string table
tokenizer object directory
execution graph directory
tensor directory
packing-variant directory
aligned tensor extents
optional adapter extents
integrity tree and package signature
```

## Header sketch

```c
struct lmof_header {
    uint8_t  magic[8];          // "LMOSLMOF"
    uint16_t major;
    uint16_t minor;
    uint32_t header_bytes;
    uint64_t package_bytes;
    uint8_t  content_hash[32];
    uint32_t model_family;
    uint32_t default_quant;
    uint64_t tensor_directory;
    uint64_t graph_directory;
    uint64_t tokenizer_directory;
    uint64_t variant_directory;
    uint64_t integrity_root;
};
```

## Tensor directory record

Each tensor record contains:

- semantic name or stable tensor ID;
- rank and dimensions;
- logical element type;
- quantization block geometry;
- byte offset, stored length and logical length;
- required alignment;
- layer and execution-stage affinity;
- recomputation cost;
- integrity hash;
- references to alternate packings.

## Packing variants

A package can contain or derive variants such as:

```text
portable scalar Q4
x86 AVX2 Q4
x86 AVX-512/VNNI Q4
x86 AMX INT8
Arm NEON Q4
Arm SVE2 Q4
vendor NPU graph blob
```

The installer chooses or creates the best local variant once. Normal boots map
that variant directly.

## Context compatibility

The model manifest declares KV geometry independently of the stored weights:

```text
layers
KV heads
head dimension
KV element format
page token capacity
supported KV compression modes
maximum logical context
```

This lets the Tensor Memory System allocate and migrate KV pages without
parsing model-framework internals.
