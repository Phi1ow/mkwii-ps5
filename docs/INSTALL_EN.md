# Kart PS5: Installation

Native port of **Mario Kart Wii (PAL version, RMCP01)** for a jailbroken PS5. The game is statically recompiled (WiiCompiled), and rendering goes directly through the PS5 GPU: this is **not an emulator**.

**Separate disc files are not provided. The recompiled executable retains embedded game-derived code/data.** You must use your own copy of Mario Kart Wii PAL.

## What you must supply locally

Downloads include the recompiled executable without separate disc images or extracted game files. Download the application package from the release, then supply the files below.

1. Use your own **PAL RMCP01** Mario Kart Wii disc. Follow the [official Dolphin ripping guide](https://dolphin-emu.org/docs/guides/ripping-games/), which documents CleanRip, to create an image from your disc. No game download links are provided.
2. Transfer that image to your PC and add it to Dolphin's game list. Follow step 1 below to extract its data partition.
3. Keep the image, `DATA/`, `main.dol` and `StaticR.rel` local. Do not attach them to GitHub issues or pull requests.
4. Read the [build notes](BUILDING.md). The `ps5/Prepare-Game.ps1 -DiscImage <path>` script validates the expected PAL revision hashes and requires the documented tools. Review its checks before using it with an existing extraction.
5. Supply your own signed, firmware-compatible Sony `libc.prx` at `PPSA99611/sce_module/libc.prx` in your personal package. This library is not distributed here.

The current build generates game code and data from the disc. Do not publish extracted disc files or generated data blobs separately. The complete clean-build process is not yet validated; these notes do not promise an immediately reproducible player package.

## Application package contents

| Item | Purpose |
|---|---|
| `PPSA99611/` | The “Kart PS5” application: executable, shaders, configuration, and save folder |
| `Outils/droits-kart-ps5.elf` | Payload that makes `eboot.bin` and `sce_module/libc.prx` executable inside `/data/PPSA99611` (and nothing else) |
| `docs/INSTALL_EN.md` | This guide |

## Requirements

- A jailbroken PS5 with an ELF loader (port 9021) and an FTP server.  
  Tested only on **PS5 Pro, firmware 9.40**. A standard PS5 has not been tested.
- **kstuff**, sent after a clean console boot. Tested version: EchoStretch kstuff v1.6.7.
- **ShadowMountPlus 1.6beta16**. Version 1.7alpha refused the title information during our tests.  
  ShadowMountPlus is also used to pause kstuff while the game is running (step 4, required for 60 FPS).
- A copy of **Mario Kart Wii PAL (RMCP01)**. NTSC-U (RMCE01), Japanese (RMCJ01), and Korean versions will not work: the executable specifically targets the PAL version.
- Around 3 GB of free space on the console.
- On PC: [Dolphin](https://dolphin-emu.org/) to extract the disc, and an FTP client (FileZilla, WinSCP, etc.).

## 1. Extract the game data with Dolphin

1. In Dolphin, right-click Mario Kart Wii, then select **Properties** and open the **Filesystem** tab.
2. Right-click the **Data Partition**, then choose the option to extract the entire partition.
3. Rename the extracted folder to `DATA`. It must directly contain `sys/` and `files/`:

```text
DATA/
├── sys/
│   ├── boot.bin
│   ├── fst.bin
│   └── main.dol
└── files/
    ├── rel/StaticR.rel
    ├── Race/
    ├── Scene/
    └── ...
```

To verify the extraction: there should be around 2,000 files and 2.7 GB of data. `DATA/sys/main.dol` and `DATA/files/rel/StaticR.rel` must exist.

## 2. Prepare the folder on PC

Place the `DATA` folder **inside** `PPSA99611/`:

```text
PPSA99611/
├── DATA/            <- your extracted game data
├── eboot.bin
├── sce_module/
├── sce_sys/
├── shaders/
├── runtime/
├── wii_bootstrap/
├── UserData/

```

The game cannot read files outside its own folder. `DATA` must therefore be located inside `PPSA99611`.

## 3. Copy to the PS5

1. Start the console, send **kstuff**, then **ShadowMountPlus**.
2. Using your FTP client, copy the entire `PPSA99611` folder into **`/data/`**. The result must look like `/data/PPSA99611/eboot.bin`, `/data/PPSA99611/DATA/sys/main.dol`, etc.  
   Transferring `DATA` may take some time.
3. Make the application executable by sending `Outils/droits-kart-ps5.elf` to the ELF loader (port 9021), just like any other payload.  
   This payload only runs `chmod 755` on `/data/PPSA99611/eboot.bin` and `/data/PPSA99611/sce_module/libc.prx`.  
   Without this step, the game will usually fail to launch with error **CE-107750-0**.  
   An FTP `SITE CHMOD 755` command may not be enough: some FTP servers respond with “OK” without actually applying the permission change.

## 4. Pause kstuff immediately after launch (important)

While kstuff is active, the game runs at around 40 FPS instead of 60 FPS and appears to run in slow motion.

ShadowMountPlus automatically pauses kstuff when a game is launched, then re-enables it when the game is closed.

By default, however, it waits 15 seconds. It can also automatically increase this delay (30, 60, 120 seconds, etc.) whenever it thinks the game crashed after kstuff was paused.

For Kart PS5, kstuff can be paused 5 seconds after launch:

1. Using FTP, open `/data/shadowmount/config.ini` and add this line at the end:

   ```ini
   kstuff_delay=PPSA99611:5
   ```

2. Make sure `kstuff_game_auto_toggle` is not set to `0`, and that PPSA99611 does not appear in any `kstuff_no_pause=` line.
3. ShadowMountPlus reloads its configuration while it is running. If necessary, the setting should take effect after restarting the PS5 and sending kstuff followed by ShadowMountPlus again.

If the game becomes slow again after a notification saying:

`Crash detected after KStuff pause: pause delay for PPSA99611 increased…`

ShadowMountPlus has increased the delay inside `/data/shadowmount/autotune.ini`.

Change the `kstuff_delay=PPSA99611:…` line in that file back to `5`.

To prevent this from happening, exit the game normally using the PS button, then close the game.

While the game is running, going to the home screen re-enables kstuff. ShadowMountPlus pauses it again when you return to the game.

## 5. Launch

ShadowMountPlus scans `/data` approximately every 15 seconds. An **“installed game PPSA99611”** notification will appear, followed by the **Kart PS5** icon on the home screen.

Simply launch it.

After restarting the PS5, send kstuff and then ShadowMountPlus again before launching the game.

## Controls (DualSense)

Default controls: the port uses Mario Kart Wii's GameCube controller mode. No Wii Remote motion gestures are needed.

| DualSense button | Action in Mario Kart Wii |
| --- | --- |
| Left stick | Steer; navigate menus |
| Cross (✕) | Accelerate; confirm menu selections |
| Circle (○) or R2 | Brake / reverse; hop and drift in manual mode. Circle also goes back in menus |
| Triangle (△) or L2 | Use an item; hold to trail items that support it |
| Square (□) or R1 | Look behind |
| D-pad, as you leave a jump | Perform a trick |
| D-pad up, while riding a bike | Start a wheelie |
| D-pad down, while riding a bike | End a wheelie |
| Options | Open the pause menu |

For manual drifting, hold Cross to accelerate, press R2 or Circle and steer with the left stick. Release the drift button once sparks have charged to trigger a mini-turbo. Automatic drift does not provide manual mini-turbos.

Mappings were checked against the port's input code and the [Nintendo Mario Kart Wii manual, GameCube controls and driving techniques](https://www.mariomayhem.com/downloads/mario_instruction_booklets/Mario_Kart_Wii-WII.pdf). This is a source/documentation check, not a new console test. Custom button bindings may change these controls.
## Current status

- **60 FPS** measured on PS5 Pro once kstuff is paused (step 4): menus, loading screens, intro, and races, including demanding sections.  
  The game is intentionally limited to 60 FPS because its game logic is tied to that frame rate.
- The first few seconds after launch, before kstuff is paused, run more slowly.
- Known bug: the 2D mini-map on the right side of the screen during races does not display correctly.
- Online play is not available.
- Save data is written to `/data/PPSA99611/UserData/NAND`. Do not delete this folder if you want to keep your progress.

## Troubleshooting

**The game is slow or appears to run in “slow motion”:** kstuff is not paused. See step 4, especially the delay configured in `autotune.ini`.

Each launch writes a log to `/data/PPSA99611/UserData/Logs/` in a folder named `base_<date>_pid<N>`.

The folder contains `console.log` and `stderr.log`, and sometimes `crash_exception.txt`.

To report a crash or bug, retrieve this folder through FTP and send it together with a description of the issue, including when it happened, the track being played, and a photo of the screen if possible.

## Updating and uninstalling

- **Update:** replace only `/data/PPSA99611/eboot.bin` through FTP, then send `Outils/droits-kart-ps5.elf` again.  
  Do not modify `DATA` or `UserData`.
- **Uninstall:** delete the `/data/PPSA99611` folder through FTP. This will also delete all save data.  
  Removal of the home-screen icon depends on ShadowMountPlus and has not been tested with this version.




