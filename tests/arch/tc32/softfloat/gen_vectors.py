#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate src/vectors.h for tests/arch/tc32/softfloat: operands and the
expected result bits of every operation of the TC32 soft-float runtime
(arch/tc32/core/softfloat), computed on the host.

Operands: every pair of a list of special values (zeros, ones, infinities,
quiet and signalling NaNs, the smallest and largest subnormals and normals,
values at the integer conversion limits, rounding ties) and pseudo-random bit
patterns, half of the pairs sharing an exponent so that additions cancel.

Expected results are IEEE 754 arithmetic, correctly rounded (to nearest,
ties to even). Where C leaves the result undefined or it is a NaN, the
expected bits follow compiler-rt's rules, which the runtime implements: a NaN
operand comes back quieted (the first one; a - b is a + (-b), so a NaN b comes
back with its sign flipped), an invalid operation gives the default NaN,
float to integer saturates by sign past the integer's width, float to double
keeps a NaN's payload unquieted, double to float quiets it, negation flips the
sign bit only.

Usage: gen_vectors.py > src/vectors.h
"""

import math
import struct

RANDOM = 200
F_QUIET, D_QUIET = 0x00400000, 0x0008000000000000

F_SPECIAL = [
    0x00000000,
    0x80000000,
    0x3F800000,
    0xBF800000,
    0x7F800000,
    0xFF800000,
    0x7FC00000,
    0x7FA00001,
    0x00000001,
    0x007FFFFF,
    0x00800000,
    0x7F7FFFFF,
    0x3F000000,
    0x3FC00000,
    0x4B800000,
    0x4B7FFFFF,
    0x4F000000,
    0xCF000000,
    0x5F000000,
    0x3EAAAAAB,
    0x40490FDB,
    0x80000001,
    0x33800000,
    0x3F800001,
    0x33C00000,
    0x0C000000,
]
D_SPECIAL = [
    0x0000000000000000,
    0x8000000000000000,
    0x3FF0000000000000,
    0xBFF0000000000000,
    0x7FF0000000000000,
    0xFFF0000000000000,
    0x7FF8000000000000,
    0x7FF4000000000001,
    0x0000000000000001,
    0x000FFFFFFFFFFFFF,
    0x0010000000000000,
    0x7FEFFFFFFFFFFFFF,
    0x3FE0000000000000,
    0x3FF8000000000000,
    0x4340000000000000,
    0x433FFFFFFFFFFFFF,
    0x41E0000000000000,
    0xC1E0000000000000,
    0x43E0000000000000,
    0x3FD5555555555555,
    0x400921FB54442D18,
    0x47EFFFFFE0000000,
    0x36A0000000000000,
    0x3CA0000000000000,
    0x3FF0000000000001,
    0x3CA8000000000000,
    0x2000000000000000,
]
I32_SPECIAL = [
    0x00000000,
    0x00000001,
    0xFFFFFFFF,
    0x7FFFFFFF,
    0x80000000,
    0x01000001,
    0x01000003,
    0x00FFFFFF,
    0xFF000001,
]
I64_SPECIAL = [
    0x0000000000000000,
    0x0000000000000001,
    0xFFFFFFFFFFFFFFFF,
    0x7FFFFFFFFFFFFFFF,
    0x8000000000000000,
    0x0020000000000001,
    0x0020000000000003,
    0x001FFFFFFFFFFFFF,
    0x0000000001000001,
    0xFFFFFFFFFF000001,
]


class XorShift:
    def __init__(self):
        self.s = 2463534242

    def u32(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s

    def u64(self):
        return self.u32() << 32 | self.u32()


def f_val(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def d_val(u):
    return struct.unpack("<d", struct.pack("<Q", u))[0]


def d_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def f_bits_round(x):
    """x (a double) rounded to single precision, as bits."""
    try:
        return struct.unpack("<I", struct.pack("<f", x))[0]
    except OverflowError:
        return 0x7F800000 | (0x80000000 if x < 0 else 0)


def f_nan(u):
    return (u & 0x7F800000) == 0x7F800000 and u & 0x007FFFFF


def d_nan(u):
    return (u & 0x7FF0000000000000) == 0x7FF0000000000000 and u & 0x000FFFFFFFFFFFFF


def div(x, y):
    """IEEE division, including by zero (Python raises)."""
    if y == 0.0:
        if x == 0.0 or math.isnan(x):
            return math.nan
        return math.copysign(math.inf, math.copysign(1.0, x) * math.copysign(1.0, y))
    return x / y


def binary(nan, quiet, default_nan, a, b, value, to_bits):
    if nan(a):
        return a | quiet
    if nan(b):
        return b | quiet
    r = value()
    if math.isnan(r):
        return default_nan
    return to_bits(r)


def fop(name, a, b):
    if name == "sub":
        name, b = "add", b ^ 0x80000000
    x, y = f_val(a), f_val(b)
    # Float operands are exact in double, and one double operation rounded
    # again to float gives the correctly rounded float result (53 >= 2*24+2).
    ops = {"add": lambda: x + y, "mul": lambda: x * y, "div": lambda: div(x, y)}
    return binary(f_nan, F_QUIET, 0x7FC00000, a, b, ops[name], f_bits_round)


def dop(name, a, b):
    if name == "sub":
        name, b = "add", b ^ 0x8000000000000000
    x, y = d_val(a), d_val(b)
    ops = {"add": lambda: x + y, "mul": lambda: x * y, "div": lambda: div(x, y)}
    return binary(d_nan, D_QUIET, 0x7FF8000000000000, a, b, ops[name], d_bits)


def cmp_mask(x, y):
    un = math.isnan(x) or math.isnan(y)
    return (
        int(x < y)
        | int(x <= y) << 1
        | int(x == y) << 2
        | int(x >= y) << 3
        | int(x > y) << 4
        | int(un) << 5
    )


def fix(u, sig_bits, exp_bits, width, signed):
    """compiler-rt's __fixint/__fixuint on the bits of a float or double."""
    total = 1 + exp_bits + sig_bits
    bias = (1 << (exp_bits - 1)) - 1
    negative = u >> (total - 1)
    exponent = ((u >> sig_bits) & ((1 << exp_bits) - 1)) - bias
    significand = (u & ((1 << sig_bits) - 1)) | (1 << sig_bits)
    mask = (1 << width) - 1
    if not signed and negative:
        return 0
    if exponent < 0:
        return 0
    if exponent >= width:
        if not signed:
            return mask
        return (1 << (width - 1)) - 1 if not negative else 1 << (width - 1)
    mag = (
        significand >> (sig_bits - exponent)
        if exponent < sig_bits
        else significand << (exponent - sig_bits)
    )
    return (-mag if negative else mag) & mask


def int_to_f32(n):
    """n rounded to single precision (to nearest, ties to even), as bits."""
    if n == 0:
        return 0
    mag = abs(n)
    shift = max(mag.bit_length() - 24, 0)
    q, r = divmod(mag, 1 << shift)
    if shift and (r > 1 << (shift - 1) or (r == 1 << (shift - 1) and q & 1)):
        q += 1
    value = float(q << shift)
    return f_bits_round(-value if n < 0 else value)


def int_to_f64(n):
    """n rounded to double precision (Python's int to float rounds to nearest, ties to even)."""
    return d_bits(float(n))


def signed(v, width):
    return v - (1 << width) if v >> (width - 1) else v


def f2d(u):
    if f_nan(u) or (u & 0x7FFFFFFF) == 0x7F800000:
        return (u >> 31) << 63 | 0x7FF0000000000000 | (u & 0x007FFFFF) << 29
    return d_bits(f_val(u))


def d2f(u):
    if d_nan(u):
        payload = ((u & (D_QUIET - 1)) >> 29) & (F_QUIET - 1)
        return (u >> 63) << 31 | 0x7F800000 | F_QUIET | payload
    return f_bits_round(d_val(u))


def pairs(special, rnd, sign, mantissa):
    out = [(a, b) for a in special for b in special]
    for i in range(RANDOM):
        a = rnd()
        b = rnd() if i & 1 else a ^ (rnd() & (sign | mantissa))
        out.append((a, b))
    return out


def row(*fields):
    """Print one initializer, its fields wrapped at 100 columns (a tab counts 8)."""
    lines = []
    cur = "\t{"
    for i, f in enumerate(fields):
        item = f + ("}," if i == len(fields) - 1 else ",")
        if cur in ("\t{", "\t "):
            cur += item
        elif 8 + len(cur) - 1 + 1 + len(item) <= 100:
            cur += " " + item
        else:
            lines.append(cur)
            cur = "\t " + item
    lines.append(cur)
    print("\n".join(lines))


def main():
    r = XorShift()
    fp = pairs(F_SPECIAL, r.u32, 0x80000000, 0x007FFFFF)
    dp = pairs(D_SPECIAL, r.u64, 0x8000000000000000, 0x000FFFFFFFFFFFFF)
    # Conversion operands: the specials, then values within and around the integer ranges.
    fc = F_SPECIAL + [
        (r.u32() & 0x80000000) | ((0x3F000000 + r.u32() % 0x21000000) & 0x7FFFFFFF)
        for _ in range(RANDOM)
    ]
    dc = D_SPECIAL + [
        (r.u64() & 0x8000000000000000)
        | ((0x3FE0000000000000 + r.u64() % 0x0420000000000000) & 0x7FFFFFFFFFFFFFFF)
        for _ in range(RANDOM)
    ]
    ic = list(
        zip(I32_SPECIAL + [0] * (len(I64_SPECIAL) - len(I32_SPECIAL)), I64_SPECIAL, strict=True)
    )
    for _ in range(RANDOM):
        u = r.u32() >> (r.u32() % 32)
        v = r.u64() >> (r.u32() % 64)
        ic.append((u, v))

    p = print
    p("/*")
    p(" * SPDX-License-Identifier: Apache-2.0")
    p(" *")
    p(" * Generated by gen_vectors.py: operands and expected result bits.")
    p(" */")
    p()
    p("static const struct f_bin_vec f_bin_vecs[] = {")
    for a, b in fp:
        row(
            f"0x{a:08x}U",
            f"0x{b:08x}U",
            f"0x{fop('add', a, b):08x}U",
            f"0x{fop('sub', a, b):08x}U",
            f"0x{fop('mul', a, b):08x}U",
            f"0x{fop('div', a, b):08x}U",
            f"0x{cmp_mask(f_val(a), f_val(b)):02x}U",
        )
    p("};")
    p()
    p("static const struct d_bin_vec d_bin_vecs[] = {")
    for a, b in dp:
        row(
            f"0x{a:016x}ULL",
            f"0x{b:016x}ULL",
            f"0x{dop('add', a, b):016x}ULL",
            f"0x{dop('sub', a, b):016x}ULL",
            f"0x{dop('mul', a, b):016x}ULL",
            f"0x{dop('div', a, b):016x}ULL",
            f"0x{cmp_mask(d_val(a), d_val(b)):02x}U",
        )
    p("};")
    p()
    p("static const struct f_conv_vec f_conv_vecs[] = {")
    for a in fc:
        row(
            f"0x{a:08x}U",
            f"0x{fix(a, 23, 8, 32, True):08x}U",
            f"0x{fix(a, 23, 8, 32, False):08x}U",
            f"0x{fix(a, 23, 8, 64, True):016x}ULL",
            f"0x{fix(a, 23, 8, 64, False):016x}ULL",
            f"0x{f2d(a):016x}ULL",
            f"0x{a ^ 0x80000000:08x}U",
        )
    p("};")
    p()
    p("static const struct d_conv_vec d_conv_vecs[] = {")
    for a in dc:
        row(
            f"0x{a:016x}ULL",
            f"0x{fix(a, 52, 11, 32, True):08x}U",
            f"0x{fix(a, 52, 11, 32, False):08x}U",
            f"0x{fix(a, 52, 11, 64, True):016x}ULL",
            f"0x{fix(a, 52, 11, 64, False):016x}ULL",
            f"0x{d2f(a):08x}U",
            f"0x{a ^ 0x8000000000000000:016x}ULL",
        )
    p("};")
    p()
    p("static const struct i_conv_vec i_conv_vecs[] = {")
    for u, v in ic:
        row(
            f"0x{u:08x}U",
            f"0x{v:016x}ULL",
            f"0x{int_to_f32(signed(u, 32)):08x}U",
            f"0x{int_to_f32(u):08x}U",
            f"0x{int_to_f64(signed(u, 32)):016x}ULL",
            f"0x{int_to_f64(u):016x}ULL",
            f"0x{int_to_f32(signed(v, 64)):08x}U",
            f"0x{int_to_f32(v):08x}U",
            f"0x{int_to_f64(signed(v, 64)):016x}ULL",
            f"0x{int_to_f64(v):016x}ULL",
        )
    p("};")


if __name__ == "__main__":
    main()
