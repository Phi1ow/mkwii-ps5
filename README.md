# Mario Kart Wii for PS5

**English** · [Français](README_FR.md)

A native PlayStation 5 port of **Mario Kart Wii PAL (RMCP01)**, based on [WiiCompiled](https://github.com/patchzyy/Wiicompiled).

- Launches as **Kart PS5** from the PS5 home screen.
- Statically recompiles the Wii game and renders through the PS5 GPU using AGC.
- Uses DualSense controls mapped to a GameCube controller.

> Bring your own Mario Kart Wii PAL disc data. Other regions are not supported by this executable.
>
> The supplied release documentation reports testing on **PS5 Pro, firmware 9.40**. Standard PS5 hardware and other firmware versions are not confirmed.

---

## Features

| Feature | Status |
| --- | --- |
| Native GPU rendering | AGC backend |
| Frame rate | 60 FPS reported on PS5 Pro after kstuff is paused |
| Controller | DualSense, GameCube button mapping |
| Saves | Local NAND in `UserData/NAND` |
| Game region | PAL, RMCP01 |
| Title ID | `PPSA99611` |
| Online play | Unavailable |

The game is capped at 60 FPS because its logic depends on that timing. The first seconds after launch run more slowly while kstuff is still active.

### Known limitations

- The 2D mini-map during races does not display correctly.
- Firmware-specific packages are not a compatibility test. See [firmware notes](docs/FIRMWARE.md).

## Requirements

- A jailbroken PS5, an FTP server and an ELF loader.
- kstuff; the release documentation lists EchoStretch v1.6.7.
- ShadowMountPlus 1.6beta16; the tested 1.7alpha versions rejected the title information.
- Your own Mario Kart Wii PAL extraction, with `DATA/sys/main.dol` and `DATA/files/rel/StaticR.rel`.
- Approximately 3 GB of available console storage.

## Source-only distribution

**No ready-to-play binaries are currently offered.** The binary archives were withdrawn after confirming that the static recompilation embeds DOL/REL game data inside `eboot.bin`. Removing the disc image or `DATA/` folder alone does not remove those embedded bytes.

This repository provides port sources, dependency modifications and documentation. Supply your own Mario Kart Wii PAL (RMCP01) copy. See the [personal extraction guide](docs/INSTALL_EN.md) and [build notes](docs/BUILDING.md). A clean build is not yet validated.

Do not upload disc images, extracted game files, translated game sources, generated data blobs, game executables, Sony libraries or personal NANDs to this repository, releases, issues or pull requests. Project licensing does not grant rights to third-party game data or trademarks.
## Installing

Follow the [English installation guide](docs/INSTALL_EN.md) or [French installation guide](docs/INSTALL_FR.md).

1. Extract your game's data partition with Dolphin into a folder named `DATA`.
2. Place `DATA` inside the application folder `PPSA99611`.
3. Copy the application to `/data/PPSA99611` using FTP.
4. Send `Outils/droits-kart-ps5.elf` to the ELF loader to set executable permissions.
5. Configure ShadowMountPlus to pause kstuff after launch, then launch **Kart PS5** from the home screen.

The tested delay setting, in `/data/shadowmount/config.ini`, is:

```ini
kstuff_delay=PPSA99611:5
```

The full guide explains auto-toggle settings and recovery if ShadowMountPlus increases that delay after a crash.

## Controls

| DualSense | GameCube |
| --- | --- |
| Left stick | Control stick |
| D-pad | D-pad |
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| R1 | Z |
| L2 / R2 | L / R |
| Options | Start |

## Application layout

```text
PPSA99611/
├── eboot.bin
├── DATA/                  Your extracted PAL disc data
├── sce_module/libc.prx    Firmware-dependent runtime library
├── sce_sys/               Title metadata and icon
├── shaders/               AGC shaders
├── runtime/               Runtime assets
├── wii_bootstrap/         First-run Wii bootstrap
└── UserData/
    ├── Config.toml
    ├── NAND/              Saves; preserve when updating
    └── Logs/              Created at runtime
```

## Reporting issues

Include console model, firmware, package variant, track and steps to reproduce. Logs are under `/data/PPSA99611/UserData/Logs/base_<date>_pid<N>/`. Review logs before sharing them and do not attach disc data or your full NAND.

## Credits

- [WiiCompiled](https://github.com/patchzyy/Wiicompiled): static recompilation and game runtime.
- [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk): PS5 linking and AGC foundation.
- [SharpProspero](https://github.com/SvenGDK/SharpProspero): signing tools and shader containers.
- Dolphin contributors: disc extraction tooling and the referenced Wii bootstrap layout.
- kstuff, ShadowMountPlus and their contributors: console launch environment.

## Repository status

The PS5 source code and pinned dependency modifications are included. See [build notes](docs/BUILDING.md), [release notes](docs/RELEASE-NOTES.md) and [license notices](THIRD-PARTY-NOTICES.md). A clean build has not been verified.




