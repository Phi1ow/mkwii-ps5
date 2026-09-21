#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Aggregate full function-profiler tables (UserData/Logs/fprof-NNN.csv).

Rows named "host anchor+N" (out-of-line helpers emitted in translated objects)
are resolved with the linker's game.elf.functions.json of the profiled build.
Self times are corrected by the calibrated residual hook cost: the CSV header's
bias per call is removed once for each call of the function and once for each
call it makes (the caller-side and callee-side halves of every hook pair).

    python ps5/tools/analyze_fprof_csv.py <csv>... --functions <build>/game.elf.functions.json [--top 50]
"""
import argparse
import bisect
import collections
import csv
import json
import re


def load_window(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        header = handle.readline()
        meta = dict(re.findall(r"(\w+) (\S+)", header[1:]))
        rows = list(csv.DictReader(handle))
    return meta, rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="+")
    parser.add_argument("--functions", required=True)
    parser.add_argument("--top", type=int, default=50)
    args = parser.parse_args()
    functions = sorted(json.load(open(args.functions)), key=lambda f: f["address"])
    addresses = [f["address"] for f in functions]
    anchor = next(f["address"] for f in functions if f["name"] == "mkw_fprof_frame_end")

    def resolve(name):
        match = re.fullmatch(r"host anchor([+-]\d+)", name)
        if not match:
            return name
        target = anchor + int(match[1])
        index = bisect.bisect_right(addresses, target) - 1
        if index < 0:
            return name
        entry = functions[index]
        suffix = "" if target == entry["address"] else f"+0x{target - entry['address']:x}"
        return f"[host] {entry['name']}{suffix}"

    frames = wall = 0.0
    totals = collections.defaultdict(lambda: [0, 0.0, 0.0, 0.0, ""])
    translated = native = hooks_bias = 0.0
    calls_total = 0
    for path in args.csv:
        meta, rows = load_window(path)
        bias = float(meta["bias_ns_per_call"])
        frames += float(meta["frames"])
        wall += float(meta["wall_ns"])
        for row in rows:
            calls, child = int(row["calls"]), int(row["child_calls"])
            self_ns = float(row["self_ns"])
            adjusted = max(0.0, self_ns - bias * (calls + child))
            key = (resolve(row["name"]), row["guest"])
            entry = totals[key]
            entry[0] += calls; entry[1] += self_ns; entry[2] += adjusted; entry[3] += float(row["inclusive_ns"])
            if int(row["key"], 16) >> 63:
                native += adjusted
            else:
                translated += adjusted
            calls_total += calls
            hooks_bias += self_ns - adjusted
    per = lambda ns: ns / 1e6 / frames
    print(f"{len(args.csv)} windows, {frames:.0f} frames, wall {per(wall):.2f} ms/f; corrected self: translated "
          f"{per(translated):.2f} ms/f, native {per(native):.2f} ms/f; {calls_total / frames:.0f} calls/f; "
          f"residual hook cost removed {per(hooks_bias):.2f} ms/f")
    print("rank  adj-ms/f  raw-ms/f  %transl+native  incl-ms/f    calls/f  adj-ns/call  name (guest)")
    work = translated + native
    ranked = sorted(totals.items(), key=lambda item: -item[1][2])
    for rank, ((name, guest), (calls, raw, adjusted, inclusive, _)) in enumerate(ranked[:args.top], 1):
        print(f"{rank:4d} {per(adjusted):9.3f} {per(raw):9.3f} {100 * adjusted / work:14.2f} {per(inclusive):10.3f} "
              f"{calls / frames:10.1f} {adjusted / calls if calls else 0:12.1f}  {name} ({guest})")


if __name__ == "__main__":
    main()
