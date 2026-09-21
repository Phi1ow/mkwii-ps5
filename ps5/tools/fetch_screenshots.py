"""Download the diagnostic screen captures of the latest PPSA99611 run and convert them to PNG.

Usage: python ps5/tools/fetch_screenshots.py <output folder> [console ip]

The game writes /data/PPSA99611/UserData/Logs/shot-<seconds>.bgra (u32 width,
u32 height, BGRA rows) while UserData/screenshot.txt exists
(ps5/gpu/gx_frame_renderer.cpp). The run log folder must be readable over FTP
(ps5/Collect-GameRun.ps1 or Build-LogAccessDiagnostic.ps1 open it).
"""
import ftplib
import io
import struct
import sys
from pathlib import Path

from PIL import Image

out = Path(sys.argv[1])
host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
out.mkdir(parents=True, exist_ok=True)
ftp = ftplib.FTP()
ftp.connect(host, 2120, timeout=60)
ftp.login()
root = "/data/PPSA99611/UserData/Logs"
names = sorted(n.rsplit("/", 1)[-1] for n in ftp.nlst(root) if n.endswith(".bgra"))
for name in names:
    buffer = bytearray()
    ftp.retrbinary(f"RETR {root}/{name}", buffer.extend)
    width, height = struct.unpack_from("<II", buffer, 0)
    if len(buffer) < 8 + width * height * 4:
        print(f"{name}: truncated ({len(buffer)} bytes)")
        continue
    image = Image.frombuffer("RGBA", (width, height), bytes(buffer[8:8 + width * height * 4]), "raw", "BGRA", 0, 1)
    target = out / (Path(name).stem + ".png")
    image.convert("RGB").save(target)
    print(f"{name} -> {target} ({width}x{height})")
ftp.quit()

