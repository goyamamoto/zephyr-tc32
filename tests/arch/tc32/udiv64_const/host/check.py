#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the console of tests/arch/tc32/udiv64_const (run on the TC32 in
TC32-devtools' emulator) against the host's arithmetic: for each divisor line
of test_divisors, the value count and the hash of every quotient and
remainder, recomputed here with the same values (src/main.c) and Python's
integer division. Also requires 0 mismatches on every summary line.

Usage: check.py <console text>   (exit status 1 on any difference)
"""

import re
import sys

M64 = (1 << 64) - 1
RANDOM_VALUES = 200000
SPECIAL = [
    0,
    1,
    2,
    0xFFFFFFFF,
    0x100000000,
    0x100000001,
    0x7FFFFFFFFFFFFFFF,
    0x8000000000000000,
    0x8000000000000001,
    0xFFFFFFFFFFFFFFFE,
    M64,
]


class Rng:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        s = self.s
        s ^= (s << 13) & M64
        s ^= s >> 7
        s ^= (s << 17) & M64
        self.s = s
        return s


def ceil_log2(d):
    return (d - 1).bit_length()


def values(d):
    out = list(SPECIAL) + [d - 1, d, d + 1, 2 * d - 1, 2 * d, 2 * d + 1]
    for bound in (1 << 32, 1 << 63, M64):
        k = bound // d
        for j in (-1, 0, 1):
            kk = k + j
            if kk > M64 // d:
                continue
            base = kk * d
            for o in (-1, 0, 1):
                if (o < 0 and base == 0) or (o > 0 and base == M64):
                    continue
                out.append(base + o)
    log2d = ceil_log2(d)
    r = Rng(0x9E3779B97F4A7C15 ^ d)
    for v in range(RANDOM_VALUES):
        x = r.next()
        sel = v & 3
        if sel == 0:
            n = x
        elif sel == 1:
            n = x >> (r.next() & 63)
        else:
            q = r.next() >> log2d
            sh = (r.next() >> 32) % (65 - log2d)
            q = 0 if sh >= 64 - log2d else q >> sh
            if sel == 2:
                rr = 1 if x & 1 else 0
            else:
                rr = d - 1 if x & 1 else d - 2
            n = q * d + rr
        out.append(n)
    return out


def expected(d):
    h = 2166136261
    vals = values(d)
    for n in vals:
        q, rem = divmod(n, d)
        for w in (q & 0xFFFFFFFF, q >> 32, rem & 0xFFFFFFFF):
            h = ((h ^ w) * 16777619) & 0xFFFFFFFF
    return len(vals), h


def main():
    with open(sys.argv[1], errors="replace") as f:
        text = f.read()
    bad = 0
    lines = re.findall(r"divisor (\d+): (\d+) values, (\d+) mismatches, hash ([0-9a-f]{8})", text)
    if len(lines) != 8:
        print(f"expected 8 divisor lines, found {len(lines)}")
        bad += 1
    for d, nv, mm, h in lines:
        d, nv, mm, h = int(d), int(nv), int(mm), int(h, 16)
        env, eh = expected(d)
        ok = nv == env and eh == h and mm == 0
        print(
            f"{'ok' if ok else 'DIFF'} divisor {d}: {nv} values (host {env}), hash {h:08x} "
            f"(host {eh:08x}), {mm} mismatches"
        )
        bad += not ok
    for name, mm in re.findall(r"^(divisors|conversions):.* (\d+) mismatches$", text, re.M):
        print(f"{name}: {mm} mismatches")
        bad += int(mm) != 0
    print("PASSED" if not bad else f"FAILED ({bad})")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
