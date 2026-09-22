#!/usr/bin/env python3
"""Check the downloadable release archives before publishing them."""

import argparse
import hashlib
import json
import re
import sys
import zipfile


MAIN_ROOT = "Kart-PS5-candidate/"
APP_ROOT = MAIN_ROOT + "PPSA99611/"
REQUIRED_MAIN = (
    MAIN_ROOT + "README_EN.md",
    MAIN_ROOT + "README_FR.md",
    APP_ROOT + "portable.txt",
    APP_ROOT + "eboot.bin",
    APP_ROOT + "sce_sys/param.json",
    APP_ROOT + "sce_sys/icon0.png",
    APP_ROOT + "UserData/Config.toml",
    APP_ROOT + "shaders/gx_vertex.sb",
    APP_ROOT + "shaders/gx_pixel.sb",
    APP_ROOT + "shaders/copy_vertex.sb",
    APP_ROOT + "shaders/copy_pixel.sb",
    APP_ROOT + "runtime/assets/dsp/dsp_coef.bin",
)


def fail(message):
    raise ValueError(message)


def check_main(path):
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in REQUIRED_MAIN:
            if name not in names:
                fail(f"Main archive missing {name}")
        if APP_ROOT + "sce_module/libc.prx" in names:
            fail("Main archive must not distribute Sony libc.prx")
        data_files = [name for name in names if name.startswith(APP_ROOT + "DATA/") and not name.endswith("/")]
        if data_files != [APP_ROOT + "DATA/Mario_Kart_Wii_PAL_Dump_Files_Here.txt"]:
            fail("Main archive contains unexpected disc data")
        manifest_name = MAIN_ROOT + "SHA256SUMS.txt"
        if manifest_name not in names:
            fail("Main archive missing SHA256SUMS.txt")
        recorded = {}
        for line in archive.read(manifest_name).decode("utf-8").splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(r"([0-9a-fA-F]{64})  (.+)", line)
            if not match:
                fail(f"Invalid SHA256SUMS line: {line!r}")
            recorded[MAIN_ROOT + match.group(2)] = match.group(1).lower()
        files = {name for name in names if not name.endswith("/") and name != manifest_name}
        if set(recorded) != files:
            fail("SHA256SUMS.txt does not list exactly the archive files")
        for name in sorted(files):
            digest = hashlib.sha256()
            with archive.open(name) as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest() != recorded[name]:
                fail(f"SHA-256 mismatch: {name}")
        print(f"Main archive OK: {len(files)} files, portable.txt present")


def check_backport(path):
    root = "Backport-candidate/"
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        variants = sorted({match.group(1) for name in names if (match := re.fullmatch(
            r"Backport-candidate/FW_([0-9]+\.[0-9]{2})/PPSA99611/eboot\.bin", name
        ))})
        if not variants:
            fail("Backport archive has no firmware variants")
        for version in variants:
            prefix = f"{root}FW_{version}/PPSA99611/"
            eboot = prefix + "eboot.bin"
            param = prefix + "sce_sys/param.json"
            if param not in names:
                fail(f"FW_{version} missing param.json")
            with archive.open(eboot) as source:
                if source.read(4) != bytes.fromhex("5414f5ee"):
                    fail(f"FW_{version} eboot.bin is not a signed SELF")
            metadata = json.loads(archive.read(param))
            major, minor = map(int, version.split("."))
            expected = f"0x{major:02d}{minor:02d}000000000000"
            if metadata.get("titleId") != "PPSA99611" or metadata.get("requiredSystemSoftwareVersion") != expected:
                fail(f"FW_{version} param.json does not match its folder")
        print(f"Backport archive OK: {len(variants)} firmware variants")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("main_archive", help="Kart-PS5.zip")
    parser.add_argument("backport_archive", nargs="?", help="Backport.zip")
    args = parser.parse_args()
    check_main(args.main_archive)
    if args.backport_archive:
        check_backport(args.backport_archive)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"Release archive check failed: {error}", file=sys.stderr)
        sys.exit(1)
