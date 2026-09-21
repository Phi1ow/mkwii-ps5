"""Place game stalls against periodic system activity in the kernel log.

Usage: python ps5/tools/correlate_stalls.py <run folder with stderr.log> <klog> [<klog> ...]

The game prints "[mkw-stall] N ms ... ending at process time T s". The kernel
log prints the launch as "EXEC /app0/eboot.bin ... <U msec>" (system uptime)
and ShellCore memory reports as "... when S [sec]" (system uptime). A stall
ends at system uptime U/1000 + T and started N ms earlier. For each stall the
script lists the ShellCore reports that fall inside or just before it.
"""
import re
import sys
from pathlib import Path

run = Path(sys.argv[1])
klogs = [Path(p) for p in sys.argv[2:]]
text = "\n".join(p.read_text(errors="replace") for p in klogs)

exec_ms = [int(m) for m in re.findall(r"EXEC /app0/eboot\.bin .*?<(\d+) msec>", text)]
markers = sorted({int(m) for m in re.findall(r"Libc Heap Status: .*?when (\d+) \[sec\]", text)})
print(f"EXEC timestamps (system uptime ms): {exec_ms}")
if len(markers) > 1:
    gaps = [b - a for a, b in zip(markers, markers[1:])]
    print(f"ShellCore heap reports: {len(markers)}, uptime {markers[0]}..{markers[-1]} s, gaps {sorted(set(gaps))}")

stderr = (run / "stderr.log").read_text(errors="replace")
stalls = [(float(ms), float(t)) for ms, t in
          re.findall(r"\[mkw-stall\] (\d+) ms .*?process time ([0-9.]+) s", stderr)]
print(f"{len(stalls)} stalls with process time")
if not exec_ms or not stalls:
    sys.exit(0)
start = exec_ms[-1] / 1000.0
for duration_ms, process_end in stalls:
    end = start + process_end
    begin = end - duration_ms / 1000.0
    near = [m for m in markers if begin - 2 <= m <= end + 1]
    print(f"stall {duration_ms / 1000:6.1f} s  system uptime {begin:9.1f} .. {end:9.1f}  ShellCore reports in window: {near}")
