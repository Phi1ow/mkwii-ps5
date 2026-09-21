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

## Separate files and embedded data

The data initializer embeds original DOL/REL sections into the executable. The release retains the recompilation and its embedded code/data but excludes separate disc images, extracted files, generated data blobs, Sony libraries and personal saves. A binary without raw embedded game sections would require a separate runtime/build change, which has not been implemented.
