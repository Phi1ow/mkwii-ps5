"""Summarise a WiiCompiled PS5 run: frame statistics, stutter and autopilot.

Usage: python ps5/tools/summarize_frame_stats.py <run folder with stderr.log>

Reads the `[mkw-frame-stats]` lines written every 120 frames by
ps5/gpu/gx_frame_renderer.cpp and prints one compact row per report, followed
by the autopilot presses and any runtime errors, so menu and race phases can be
compared without reading the raw log.
"""
import re
import sys
from pathlib import Path

run = Path(sys.argv[1])
text = (run / "stderr.log").read_text(errors="replace")

def field(line, name):
    m = re.search(re.escape(name) + r" ([0-9.]+)", line)
    return float(m.group(1)) if m else float("nan")

columns = [
    ("fps", "fps", 5.1), ("frame", "frame", 6.1), ("present", "present", 5.1),
    ("draws", "draws", 6.0), ("frontend", "frontend", 6.1), ("build", "build", 5.1),
    ("submit", "submit", 5.1), ("gpu-wait", "gpu-wait", 5.1), ("guest-idle", "idle", 6.1),
    ("guest-idle-sleep", "sleep", 6.1), ("interval-p95", "p95", 6.1), ("interval-max", "max", 6.1),
    ("frames>34ms", ">34", 4.0), ("frames>51ms", ">51", 4.0), ("tex-digests", "dig", 5.1),
]
stats = [l for l in text.splitlines() if "[mkw-frame-stats]" in l]
print(f"{len(stats)} reports (120 frames each)")
print(" ".join(f"{label:>{int(w)}}" for _, label, w in columns))
for line in stats:
    fps = re.search(r"frames ([0-9.]+) fps", line)
    values = []
    for name, _, width in columns:
        v = float(fps.group(1)) if name == "fps" and fps else field(line, name) if name != "fps" else float("nan")
        values.append(f"{v:>{int(width)}.{round((width % 1) * 10)}f}")
    print(" ".join(values))

presses = [l for l in text.splitlines() if "[mkw-autopilot]" in l]
if presses:
    print(f"\nautopilot: {len(presses)} lines; first {presses[0].strip()}; last {presses[-1].strip()}")
for marker in ("Runtime error", "fatal dialog", "[mkw-microbench]", "[mkw-profile]"):
    hits = [l for l in text.splitlines() if marker in l]
    if hits:
        print(f"\n{marker}:")
        for l in hits:
            print("  " + l.strip())
