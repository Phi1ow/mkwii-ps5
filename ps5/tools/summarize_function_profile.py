#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Summarize [mkw-fprof] windows from a collected run (stderr.log).

Prints one line per profiler window (frame time, translated self time, native
time, hook overhead) next to the [mkw-frame-stats] draws of the same period,
then aggregates the top self-time functions over the selected windows.

    python ps5/tools/summarize_function_profile.py artifacts/game-native/<run> [--from N] [--to M] [--top 40]
"""
import argparse
import collections
import pathlib
import re

HEADER = re.compile(
    r"\[mkw-fprof\] window (\d+): (\d+) frames, process-time ([\d.]+) s, wall ([\d.]+) ms/f; "
    r"translated self ([\d.]+) ms/f in (\d+) calls/f; native \(abi_bridge\) self ([\d.]+) ms/f in (\d+) calls/f; "
    r"outside guest calls ([\d.]+) ms/f; hooks ([\d.]+) ms/f excluded \((\d+) events/f\), residual estimate ([\d.]+) ms/f; "
    r"unmatched (\d+) unwound (\d+) overflow (\d+) fiber-switches ([\d.]+)/f; (\d+) functions")
ROW = re.compile(r"\[mkw-fprof\] +(\d+) +([\d.]+) +([\d.]+)% +([\d.]+) +([\d.]+) +([\d.]+) +([\d.]+) (.+) \(0x([0-9a-f]{8})\)$")
STATS = re.compile(r"\[mkw-frame-stats\] 120 frames ([\d.]+) fps; process-time ([\d.]+) s; .*?draws ([\d.]+)/f")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run")
    parser.add_argument("--from", dest="first", type=int, default=0)
    parser.add_argument("--to", dest="last", type=int, default=10**9)
    parser.add_argument("--top", type=int, default=40)
    args = parser.parse_args()
    lines = pathlib.Path(args.run, "stderr.log").read_text(errors="replace").splitlines()
    windows, stats = [], []
    current = None
    for line in lines:
        if m := STATS.search(line):
            stats.append((float(m[2]), float(m[1]), float(m[3])))
        if m := HEADER.search(line):
            current = {"index": int(m[1]), "frames": int(m[2]), "time": float(m[3]), "wall": float(m[4]),
                       "self": float(m[5]), "calls": int(m[6]), "native": float(m[7]), "ncalls": int(m[8]),
                       "outside": float(m[9]), "hook": float(m[10]), "events": int(m[11]), "residual": float(m[12]),
                       "unmatched": int(m[13]), "unwound": int(m[14]), "overflow": int(m[15]), "rows": []}
            windows.append(current)
        elif current is not None and (m := ROW.search(line)):
            current["rows"].append((m[8], m[9], float(m[2]), float(m[4]), float(m[5]), float(m[6])))
    print("win  time(s)  wall-ms/f  work-ms/f  transl-self  calls/f  native-self  ncalls/f  outside  hooks  residual  draws/f  unmatched")
    for w in windows:
        near = [s for s in stats if w["time"] - w["frames"] / 10 <= s[0] <= w["time"]]
        draws = sum(s[2] for s in near) / len(near) if near else float("nan")
        work = w["wall"] - w["hook"] - w["residual"]
        print(f"{w['index']:3d} {w['time']:8.1f} {w['wall']:10.2f} {work:10.2f} {w['self']:12.2f} {w['calls']:8d} "
              f"{w['native']:12.2f} {w['ncalls']:9d} {w['outside']:8.2f} {w['hook']:6.2f} {w['residual']:9.2f} "
              f"{draws:8.0f} {w['unmatched']:10d}")
    chosen = [w for w in windows if args.first <= w["index"] <= args.last]
    if not chosen:
        return
    total = collections.defaultdict(lambda: [0.0, 0.0, 0.0, 0.0, ""])
    for w in chosen:
        for name, guest, self_ms, adj_ms, incl_ms, calls in w["rows"]:
            entry = total[(name, guest)]
            entry[0] += self_ms / len(chosen); entry[1] += adj_ms / len(chosen)
            entry[2] += incl_ms / len(chosen); entry[3] += calls / len(chosen)
    work = sum(w["wall"] - w["hook"] - w["residual"] for w in chosen) / len(chosen)
    print(f"\nwindows {chosen[0]['index']}..{chosen[-1]['index']}: mean work {work:.2f} ms/f "
          "(only rows present in each window's top list are summed)")
    print("rank  self-ms/f  adj-ms/f  %work  incl-ms/f   calls/f  name (guest)")
    ranked = sorted(total.items(), key=lambda item: -item[1][0])
    for rank, ((name, guest), (self_ms, adj_ms, incl_ms, calls, _)) in enumerate(ranked[:args.top], 1):
        print(f"{rank:4d} {self_ms:10.3f} {adj_ms:9.3f} {100 * adj_ms / work:6.2f} {incl_ms:10.3f} {calls:9.1f}  {name} (0x{guest})")


if __name__ == "__main__":
    main()
