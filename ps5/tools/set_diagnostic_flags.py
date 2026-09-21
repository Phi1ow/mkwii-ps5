#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Enable or disable the PPSA99611 diagnostic flag files over FTP.

Each flag is a file in /data/PPSA99611/UserData (the game sees /app0/UserData).
A disabled flag keeps its content under the same name with a ".off" suffix, so
toggling never loses the autopilot script. Only these known names are touched.

    python ps5/tools/set_diagnostic_flags.py autopilot=on profile=off screenshot=off
    python ps5/tools/set_diagnostic_flags.py            # prints the current state
"""
import ftplib
import sys

KNOWN = ("autopilot", "profile", "microbench", "screenshot", "placement")
FOLDER = "/data/PPSA99611/UserData"


def state(ftp):
    names = {entry.rsplit("/", 1)[-1] for entry in ftp.nlst(FOLDER)}
    result = {}
    for flag in KNOWN:
        on, off = f"{flag}.txt" in names, f"{flag}.txt.off" in names
        result[flag] = "on" if on else "off" if off else "absent"
        if on and off:
            raise SystemExit(f"{flag}: both {flag}.txt and {flag}.txt.off exist; resolve by hand")
    return result


def main(argv):
    wanted = {}
    for item in argv:
        flag, _, value = item.partition("=")
        if flag not in KNOWN or value not in ("on", "off"):
            raise SystemExit(f"usage: flag=on|off with flag in {KNOWN}; got {item!r}")
        wanted[flag] = value
    ftp = ftplib.FTP()
    ftp.connect("127.0.0.1", 2120, timeout=60)
    ftp.login()
    ftp.encoding = "utf-8"
    current = state(ftp)
    for flag, value in wanted.items():
        if current[flag] == "absent":
            raise SystemExit(f"{flag}: no {flag}.txt or {flag}.txt.off to toggle")
        if current[flag] == value:
            continue
        on, off = f"{FOLDER}/{flag}.txt", f"{FOLDER}/{flag}.txt.off"
        ftp.rename(on, off) if value == "off" else ftp.rename(off, on)
    final = state(ftp)
    ftp.quit()
    print(" ".join(f"{flag}={value}" for flag, value in final.items()))
    for flag, value in wanted.items():
        if final[flag] != value:
            raise SystemExit(f"{flag} is {final[flag]}, expected {value}")


if __name__ == "__main__":
    main(sys.argv[1:])

