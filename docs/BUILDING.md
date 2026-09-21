# Source code and build status

The PS5 port sources are in `ps5/`. Upstream dependencies are pinned in `sources.lock.json`; complete tracked modifications are in `patches/` and new dependency files are in `dependency-additions/`.

From a fresh checkout, use PowerShell 7:

```powershell
./tools/Get-Sources.ps1
```

This reconstructs the dependency source trees without bundling their entire history. Do **not** then run `ps5/Apply-SourcePatches.ps1`: its historical patch series overlaps the complete snapshot patches.

## Toolchain

The existing build scripts target Windows with PowerShell 7, Git, CMake/Ninja, Python, .NET 10 SDK plus the .NET 8 runtime, LLVM 18.1.8 for shaders, and llvm-mingw. Exact recorded versions are in `sources.lock.json` and `ps5/toolchain/`.

Scripts expect local tools beneath `.tools/` and default to a Visual Studio CMake/Ninja location. Supply the exposed `-CMake` and `-Ninja` arguments for another installation. A signed `libc.prx` supplied locally is expected at `.tools/console/libc.prx`.

## Build entry points

| Stage | Script |
| --- | --- |
| Prepare C++ sysroot | `ps5/Prepare-CxxToolchain.ps1` |
| Build libc++ | `ps5/Build-Libcxx.ps1` |
| Fetch runtime dependencies | `ps5/Prepare-GameDependencies.ps1` |
| Prepare shader tools | `ps5/Prepare-ShaderTools.ps1` |
| Extract and verify a personal PAL disc image | `ps5/Prepare-Game.ps1 -DiscImage <path>` |
| Translate game code after building Translator.Cli | `ps5/Translate-Game.ps1` |
| Compile translated code | `ps5/Compile-Game.ps1` |
| Compile native runtime | `ps5/Compile-Runtime.ps1` |
| Link and sign executable | `ps5/Build-GameExecutable.ps1` |
| Assemble local player package | `ps5/Build-ReleasePackage.ps1` |

These are existing entry points, **not a validated clean-build recipe**. The shader outputs, generated import stubs and supporting .NET tools also need preparation; the original development tree already contains those outputs. The local package builder includes the locally supplied runtime library, so its output must not be uploaded blindly.

The final supplied executable differs from the compared local builds. Its SHA-256 is recorded in `docs/RELEASE-NOTES.md`. Source-to-binary correspondence and a complete clean build remain to be established before publishing this as a reproducible release.

## Keep game-derived outputs local

`generate-data-init` embeds the original DOL/REL sections into generated source/blob files, then into the executable. Therefore, even a package without `DATA/` contains game-derived data. Do not upload `generated/`, translated game code, blob files or resulting game executables. The previously published binaries have been withdrawn.

A future binary distribution without these raw sections requires a build/runtime change to load and verify the user's own local game files. This is not implemented by merely removing the DATA folder. The source checkout itself excludes generated game files.
