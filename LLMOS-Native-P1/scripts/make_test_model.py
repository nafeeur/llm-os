#!/usr/bin/env python3
"""Builds a synthetic, deterministically-seeded LMOF package for the P2
compute-path vertical slice (see src/core.c's M2_* constants, which this
script's geometry must match exactly).

These are NOT trained weights -- there is no training here at all, just a
seeded PRNG filling int8 tensors -- so 'infer2' output is not a measure of
language quality. It exists to exercise the real code path: a VirtIO block
read, LMOF header/tensor-directory parsing, SHA-256 content-hash
verification, and a genuine RMSNorm/RoPE/GQA/SwiGLU/sampling forward pass.
"""
import hashlib
import struct
import random

DIM = 32
LAYERS = 2
HEADS = 4
KV_HEADS = 2
HEAD_DIM = 8
FFN_HIDDEN = 64
VOCAB = 259
CONTEXT_LIMIT = 64

LMOF_LAYER_GLOBAL = 0xffffffff
LMOF_QUANT_INT8 = 1
FX_ONE = 65536

HEADER_FMT = "<8sHHIQ32sIIQQQQQIIIIIIIII"
# magic,major,minor,header_bytes,package_bytes,content_hash,model_family,default_quant,
# tensor_directory,graph_directory,tokenizer_directory,variant_directory,integrity_root,
# tensor_count,dimension,layers,heads,kv_heads,head_dim,ffn_hidden,context_limit,vocab_size
HEADER_SIZE = struct.calcsize(HEADER_FMT)
RECORD_FMT = "<32sI4IQQiI"
RECORD_SIZE = struct.calcsize(RECORD_FMT)


def record_bytes(name, dims, offset, length, scale_fx, layer):
    name_b = name.encode("ascii")
    assert len(name_b) < 32
    name_b = name_b + b"\x00" * (32 - len(name_b))
    dims4 = (list(dims) + [0, 0, 0, 0])[:4]
    return struct.pack(RECORD_FMT, name_b, len(dims), *dims4, offset, length, scale_fx, layer)


def main():
    rng = random.Random(1234567)

    def int8_tensor(n):
        return bytes((rng.randrange(0, 256) - 128) & 0xff for _ in range(n))

    tensors = []  # (name, layer, dims, data_bytes, scale_fx)

    tensors.append(("token_embedding", LMOF_LAYER_GLOBAL, [VOCAB, DIM], int8_tensor(VOCAB * DIM), 3277))  # scale ~0.05
    tensors.append(("final_norm", LMOF_LAYER_GLOBAL, [DIM], int8_tensor(DIM), FX_ONE // 32))

    for layer in range(LAYERS):
        tensors.append(("attn_norm", layer, [DIM], int8_tensor(DIM), FX_ONE // 32))
        tensors.append(("wq", layer, [HEADS * HEAD_DIM, DIM], int8_tensor(HEADS * HEAD_DIM * DIM), 1024))
        tensors.append(("wk", layer, [KV_HEADS * HEAD_DIM, DIM], int8_tensor(KV_HEADS * HEAD_DIM * DIM), 1024))
        tensors.append(("wv", layer, [KV_HEADS * HEAD_DIM, DIM], int8_tensor(KV_HEADS * HEAD_DIM * DIM), 1024))
        tensors.append(("wo", layer, [DIM, HEADS * HEAD_DIM], int8_tensor(DIM * HEADS * HEAD_DIM), 1024))
        tensors.append(("ffn_norm", layer, [DIM], int8_tensor(DIM), FX_ONE // 32))
        tensors.append(("w1", layer, [FFN_HIDDEN, DIM], int8_tensor(FFN_HIDDEN * DIM), 512))
        tensors.append(("w3", layer, [FFN_HIDDEN, DIM], int8_tensor(FFN_HIDDEN * DIM), 512))
        tensors.append(("w2", layer, [DIM, FFN_HIDDEN], int8_tensor(DIM * FFN_HIDDEN), 512))

    tensor_count = len(tensors)
    dir_offset = HEADER_SIZE
    dir_bytes = tensor_count * RECORD_SIZE
    data_offset = dir_offset + dir_bytes

    records = []
    blob = b""
    offset = data_offset
    for name, layer, dims, data, scale_fx in tensors:
        records.append(record_bytes(name, dims, offset, len(data), scale_fx, layer))
        blob += data
        offset += len(data)

    package_bytes = offset

    header = bytearray(HEADER_SIZE)
    struct.pack_into(
        HEADER_FMT, header, 0,
        b"LMOSLMOF", 1, 0, HEADER_SIZE, package_bytes, b"\x00" * 32,
        1, LMOF_QUANT_INT8,
        dir_offset, 0, 0, 0, 0,
        tensor_count, DIM, LAYERS, HEADS, KV_HEADS, HEAD_DIM, FFN_HIDDEN, CONTEXT_LIMIT, VOCAB,
    )

    package = bytes(header) + b"".join(records) + blob
    assert len(package) == package_bytes, (len(package), package_bytes)

    hash_off = struct.calcsize("<8sHHIQ")  # offset of content_hash within the header
    digest = hashlib.sha256(package[:hash_off] + package[hash_off + 32:]).digest()
    package = package[:hash_off] + digest + package[hash_off + 32:]

    with open("build/models/nativelm2-test.lmof", "wb") as f:
        f.write(package)
    print(f"wrote build/models/nativelm2-test.lmof ({len(package)} bytes, {tensor_count} tensors)")


if __name__ == "__main__":
    import os
    os.makedirs("build/models", exist_ok=True)
    main()
