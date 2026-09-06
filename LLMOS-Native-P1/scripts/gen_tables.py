#!/usr/bin/env python3
"""Generates Q16.16 fixed-point lookup tables for the freestanding compute
path (src/ops.c). The kernel build disables SSE/NEON/x87 entirely, so there
is no libm at runtime; these tables are the one place double precision is
used, at build time, on the host.
"""
import math

SCALE = 65536

def fx(v):
    r = round(v * SCALE)
    if r > 0x7fffffff or r < -0x80000000:
        raise ValueError(f"overflow: {v}")
    return r

def emit_array(f, name, values):
    f.write(f"static const int32_t {name}[{len(values)}] = {{\n")
    for i in range(0, len(values), 8):
        f.write("    " + ", ".join(str(v) for v in values[i:i+8]) + ",\n")
    f.write("};\n\n")

def gen_sincos(path, n=1024):
    sin_vals = [fx(math.sin(2 * math.pi * i / n)) for i in range(n)]
    cos_vals = [fx(math.cos(2 * math.pi * i / n)) for i in range(n)]
    with open(path, "w") as f:
        f.write("#ifndef LLMOS_SINCOS_TABLE_H\n#define LLMOS_SINCOS_TABLE_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define SINCOS_TABLE_N {n}u\n")
        f.write(f"#define FX_TWO_PI {fx(2 * math.pi)}\n\n")
        emit_array(f, "sin_table", sin_vals)
        emit_array(f, "cos_table", cos_vals)
        f.write("#endif\n")

def gen_exp(path, n=1024, lo=-16.0):
    # exp(x) for x in [lo, 0]; softmax always evaluates exp of (score - max) <= 0.
    vals = [fx(math.exp(lo * (1 - i / (n - 1)))) for i in range(n)]
    with open(path, "w") as f:
        f.write("#ifndef LLMOS_EXP_TABLE_H\n#define LLMOS_EXP_TABLE_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define EXP_TABLE_N {n}u\n")
        f.write(f"#define FX_EXP_LO {fx(lo)}\n\n")
        emit_array(f, "exp_table", vals)
        f.write("#endif\n")

def gen_sigmoid(path, n=1024, lo=-8.0, hi=8.0):
    vals = [fx(1.0 / (1.0 + math.exp(-(lo + (hi - lo) * i / (n - 1))))) for i in range(n)]
    with open(path, "w") as f:
        f.write("#ifndef LLMOS_SIGMOID_TABLE_H\n#define LLMOS_SIGMOID_TABLE_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define SIGMOID_TABLE_N {n}u\n")
        f.write(f"#define FX_SIGMOID_LO {fx(lo)}\n")
        f.write(f"#define FX_SIGMOID_HI {fx(hi)}\n\n")
        emit_array(f, "sigmoid_table", vals)
        f.write("#endif\n")

def gen_rope_freq(path, head_dim, theta=10000.0):
    half = head_dim // 2
    freqs = [fx(theta ** (-2.0 * i / head_dim)) for i in range(half)]
    with open(path, "w") as f:
        f.write("#ifndef LLMOS_ROPE_FREQ_TABLE_H\n#define LLMOS_ROPE_FREQ_TABLE_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"/* generated for head_dim={head_dim} theta={theta} */\n")
        f.write(f"#define ROPE_FREQ_HEAD_DIM {head_dim}u\n\n")
        emit_array(f, "rope_freq_table", freqs)
        f.write("#endif\n")

if __name__ == "__main__":
    gen_sincos("include/llmos/sincos_table.h")
    gen_exp("include/llmos/exp_table.h")
    gen_sigmoid("include/llmos/sigmoid_table.h")
    gen_rope_freq("include/llmos/rope_freq_table.h", head_dim=8)
    print("generated fixed-point tables")
