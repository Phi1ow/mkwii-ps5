#!/usr/bin/env python3
"""Independent integer-rational oracle and native GPU case input generation.

Expected values use Python unbounded integers/floor division, not C shifts.
Aurora source: shader.cpp tev_regular_i32, tev_op and tev_alpha_op.
"""
import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'artifacts/tev-shaders'
out.mkdir(parents=True, exist_ok=True)
clang = root / '.tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
subprocess.run([str(clang), '-O2', '-shared', str(root / 'ps5/tests/tev_integer.c'),
                '-o', str(out / 'tev_host.dll')], check=True)
lib = ctypes.CDLL(str(out / 'tev_host.dll'))
lib.tev_host.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint] * 3 + [ctypes.c_int] + [ctypes.c_uint] * 2
lib.tev_host.restype = ctypes.c_int


def oracle(a, b, c, d, ca, cb, op, bias, scale, clamp):
    a, b, c = a % 256, b % 256, c % 256
    if op in (0, 1):
        factor = (1, 2, 4, 1)[scale]
        weight = c + c // 128
        # Weighted endpoint sum; avoids the implementation's (b-a) form.
        mix = (a * (256 - weight) + b * weight) * factor
        rounding = 0 if scale == 3 else (128 if op == 0 else 127)
        delta = (mix + rounding) // 256
        value = (d + bias) * factor + (delta if op == 0 else -delta)
        if scale == 3:
            value //= 2
    else:
        if op < 14:
            modulus = 256 ** (1 + (op - 8) // 2)
            a, b = ca % modulus, cb % modulus
        take = a == b if op % 2 else a > b
        value = d + (c if take else 0)
    return max(0 if clamp else -1024, min(255 if clamp else 1023, value))


ops = (0, 1, 8, 9, 10, 11, 12, 13, 14, 15)
edges = [
    (0, 255, 127, 0, 0x123456, 0x123456),
    (255, 0, 128, -1, 0x010001, 0x000002),
    (257, -1, 255, -1024, 0x020000, 0x00ffff),
    (-1024, 1023, 0, 1023, 0x0000ff, 0x010000),
    (1, 2, 128, 127, 0x00ff01, 0x010001),
    (127, 128, 1, -129, 0x010000, 0x0000ff),
    (255, 255, 255, 255, 0xffffff, 0xffffff),
    (0, 0, 0, -1023, 0, 0),
]
native = [list(edge) + [op, bias, scale, clamp]
          for op, bias, scale, clamp in itertools.product(ops, (-128, 0, 128), range(4), range(2))
          for edge in edges]
rng = random.Random(0x544556)


def random_case():
    return [rng.randrange(-1024, 1024) for _ in range(4)] + [
        rng.randrange(1 << 24), rng.randrange(1 << 24), rng.choice(ops),
        rng.choice((-128, 0, 128)), rng.randrange(4), rng.randrange(2)]


while len(native) < 2048:
    native.append(random_case())
checks = 0
for case in itertools.chain(native, (random_case() for _ in range(250000))):
    expected = oracle(*case)
    actual = lib.tev_host(*case)
    if actual != expected:
        raise AssertionError((case, expected, actual))
    checks += 1
records = []
for i, case in enumerate(native):
    a, b, c, d, ca, cb, op, bias, scale, clamp = case
    flags = op | (scale << 4) | (clamp << 6) | ((bias // 128 + 1) << 8)
    tag = (i * 73 + 165) & 255
    encoded = oracle(*case) + 1024
    # The existing framebuffer uses BGRA byte order.
    pixel = 0xff000000 | ((encoded & 255) << 16) | ((encoded >> 8) << 8) | tag
    records.append(dict(words=[x & 0xffffffff for x in (a, b, c, d, ca, cb, flags, tag)], expected=pixel))
(out / 'cases.json').write_text(json.dumps(records) + '\n', encoding='utf-8')
report = dict(hostChecks=checks, nativeCases=len(records), nativeTested=False,
    casesSha256=hashlib.sha256((out / 'cases.json').read_bytes()).hexdigest(),
    arithmeticSha256=hashlib.sha256((root / 'ps5/gpu/tev_integer.h').read_bytes()).hexdigest(),
    oracle='Python unbounded integer rational arithmetic; Aurora TEV equations',
    operations=list(ops), bias=[-128, 0, 128], scale=[1, 2, 4, 0.5],
    clampRanges=[[0, 255], [-1024, 1023]], fullGxRenderer=False)
(out / 'host-result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
message = f'PASS TEV integer arithmetic: {checks} host checks, {len(records)} GPU cases prepared; no native execution claimed'
(out / 'host.log').write_text(message + '\n', encoding='utf-8')
print(message)
