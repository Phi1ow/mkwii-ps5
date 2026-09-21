#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare [mkw-frame-stats] of two autopilot runs over the same race frames.

Both runs follow ps5/autopilot/menu-to-race.txt. The race start is the first
window after the course loading dip (fps below 75 % of the median of the next
--settle windows) from which draws/f stay above --race-draws, skipping the
pre-race intro (blits >= 2 ms/frame). The game is locked to its frames, so
windows are paired by index from there (same race frames up to the < 120-frame
report phase), summarized by the reference window's load, and, because item and
AI randomness make races diverge, also compared per run by own draw count.

    python ps5/tools/compare_frame_stats.py artifacts/game-native/<before> artifacts/game-native/<after> [--windows 40]
"""
import argparse
import pathlib
import re

LINE = re.compile(r"\[mkw-frame-stats\] (\d+) frames ([\d.]+) fps; process-time ([\d.]+) s; (.*)")
FIELD = re.compile(r"([a-z0-9>-]+) ([\d.]+)(ms|/f|)?;")


def load(run):
    windows = []
    for line in pathlib.Path(run, "stderr.log").read_text(errors="replace").splitlines():
        m = LINE.search(line)
        if not m:
            continue
        fields = {k: float(v) for k, v, _ in FIELD.findall(m[4] + ";")}
        windows.append({"fps": float(m[2]), "time": float(m[3]), **fields})
    return windows


def race_start(windows, draws, settle):
    # The course loading screen is a frame-rate dip (below 75 % of the median of
    # what follows); the race is the first window after such a dip from which
    # draws stay high and no further dip occurs.
    for i in range(1, len(windows) - settle):
        following = sorted(w["fps"] for w in windows[i:i + settle])
        floor = 0.75 * following[len(following) // 2]
        if windows[i - 1]["fps"] < floor and all(
                w.get("draws", 0) >= draws and w["fps"] >= floor for w in windows[i:i + settle]):
            # Skip the pre-race intro and countdown, whose screen effect copies
            # cost several ms of blits per frame in every build.
            while i < len(windows) and windows[i].get("blit", 0.0) >= 2.0:
                i += 1
            return i
    raise SystemExit("race start not found")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--windows", type=int, default=40)
    parser.add_argument("--race-draws", type=float, default=200)
    parser.add_argument("--settle", type=int, default=6)
    args = parser.parse_args()
    runs = [load(args.before), load(args.after)]
    starts = [race_start(w, args.race_draws, args.settle) for w in runs]
    count = min(args.windows, len(runs[0]) - starts[0], len(runs[1]) - starts[1])
    print(f"race starts: before window {starts[0]} (t={runs[0][starts[0]]['time']:.0f} s), "
          f"after window {starts[1]} (t={runs[1][starts[1]]['time']:.0f} s); {count} paired windows of 120 frames")
    keys = ["fps", "frame", "draws", "frontend", "blit", "gpu-wait", "present", "interval-p95", "interval-max"]
    print("  n | " + " | ".join(f"{k:>17}" for k in keys))
    pairs = []
    for n in range(count):
        a, b = runs[0][starts[0] + n], runs[1][starts[1] + n]
        pairs.append((a, b))
        print(f"{n:3d} | " + " | ".join(f"{a.get(k, float('nan')):8.2f}{b.get(k, float('nan')):9.2f}" for k in keys))

    def summarize(label, chosen):
        if not chosen:
            return
        mean = lambda i, k: sum(p[i].get(k, 0.0) for p in chosen) / len(chosen)
        fa, fb = mean(0, "frame"), mean(1, "frame")
        print(f"{label:28s} windows {len(chosen):3d}: frame {fa:6.2f} -> {fb:6.2f} ms ({fb - fa:+.2f} ms, "
              f"{100 * (fb - fa) / fa:+.1f} %), fps {mean(0, 'fps'):5.2f} -> {mean(1, 'fps'):5.2f}, "
              f"draws {mean(0, 'draws'):5.0f} / {mean(1, 'draws'):5.0f}, frontend {mean(0, 'frontend'):4.2f} -> {mean(1, 'frontend'):4.2f} ms")

    print()
    summarize("all paired race windows", pairs)
    summarize("heavy sections (>=380 draws)", [p for p in pairs if p[0]["draws"] >= 380])
    summarize("medium (300-380 draws)", [p for p in pairs if 300 <= p[0]["draws"] < 380])
    summarize("light sections (<300 draws)", [p for p in pairs if p[0]["draws"] < 300])

    # Pairing by index assumes both races unfold alike; also compare the race
    # windows of each run bucketed by their own draw count (same scene load).
    print()
    print("by own draw count (race windows of each run):")
    for low, high in ((0, 260), (260, 300), (300, 340), (340, 380), (380, 420), (420, 460), (460, 10**9)):
        sets = [[w for w in runs[i][starts[i]:starts[i] + count] if low <= w.get("draws", 0) < high] for i in (0, 1)]
        if not sets[0] or not sets[1]:
            continue
        mean = lambda ws, k: sum(w.get(k, 0.0) for w in ws) / len(ws)
        fa, fb = mean(sets[0], "frame"), mean(sets[1], "frame")
        print(f"  draws {low:3d}-{min(high, 999):3d}: before {len(sets[0]):3d} windows {fa:6.2f} ms {mean(sets[0], 'fps'):5.1f} fps | "
              f"after {len(sets[1]):3d} windows {fb:6.2f} ms {mean(sets[1], 'fps'):5.1f} fps | {fb - fa:+6.2f} ms ({100 * (fb - fa) / fa:+.1f} %)")


if __name__ == "__main__":
    main()
